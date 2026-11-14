#include "emberfall/game_session.hpp"
#include "emberfall/story_compositor.hpp"
#include "frame_pixels.hpp"
#include "resource_names_jni.hpp"
#include "surface_binding.hpp"
#include "native_window_buffer.hpp"
#include "nnn/runtime_context.hpp"
#include <android/native_window_jni.h>
#include <android/log.h>
#include <jni.h>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace {
using emberfall::scene::GameSession;
using emberfall::android::SurfaceBinding;
using SurfaceRef=std::shared_ptr<SurfaceBinding>;
struct StoryScreenLayer {
    StoryScreenLayer(std::shared_ptr<emberfall::render::ScreenLayerFrame> source,std::array<jint,3> stamp)
        :frame(std::move(source)),state(stamp) {}
    std::shared_ptr<emberfall::render::ScreenLayerFrame> frame;
    std::array<jint,3> state;
    std::mutex mutex;
    std::condition_variable ready;
    emberfall::render::Surface sample;
    bool committed=false;
};
using PublishedScreenLayer=std::shared_ptr<StoryScreenLayer>;
using PlaneMailboxRef=std::shared_ptr<emberfall::scene::PlaneMailbox>;
struct Player {
    std::deque<std::string> script_debug_messages;
    std::unique_ptr<GameSession> game;
    emberfall::scene::StoryCompositor compositor;
    SurfaceRef surface;
    unsigned width=0,height=0;
    bool repaint=true,window_geometry_pending=true;
    bool overlap_endpoint=false;
    std::uint64_t remainder=0;
    bool title=false,loop_title=false;
    std::shared_ptr<emberfall::render::ScreenLayerFrame> screen_layer;
    PublishedScreenLayer published_screen_layer;
    std::array<jint,3> screen_layer_state{};
    std::uint64_t screen_layer_revision=0;
    std::string back_script;
    emberfall::render::Surface static_title;
};
Player& player(jlong handle) { return *reinterpret_cast<Player*>(handle); }
bool has_surface(const Player& p) { return p.surface && p.surface->available(); }
void fail(JNIEnv* env,const char* message) {
    if (!env->ExceptionCheck()) env->ThrowNew(env->FindClass("java/lang/IllegalStateException"),message);
}
jbyteArray bytes(JNIEnv* env,std::string_view value) {
    auto size=static_cast<jsize>(value.size());
    auto out=env->NewByteArray(size);
    if (out && size) env->SetByteArrayRegion(out,0,size,reinterpret_cast<const jbyte*>(value.data()));
    return out;
}
void array_text(JNIEnv* env,jobjectArray result,jsize index,const std::string& value) {
    auto encoded=bytes(env,value);
    if (encoded) { env->SetObjectArrayElement(result,index,encoded); env->DeleteLocalRef(encoded); }
}
jobjectArray status_overlay_rows(JNIEnv* env,const emberfall::scene::ScriptModal& overlay,bool include_mode) {
    auto out=env->NewObjectArray(static_cast<jsize>(1+2*overlay.rows.size()),env->FindClass("[B"),nullptr);
    if (!out) return nullptr;
    nnn::snapshot::Writer info;
    if (include_mode) info(static_cast<nnn::Value>(overlay.mode));
    info(overlay.group,overlay.selection);
    for (const auto& row:overlay.rows) info(row.id,row.flags);
    array_text(env,out,0,std::string(info.bytes.begin(),info.bytes.end()));
    jsize index=1;
    for (const auto& row:overlay.rows) {
        if (env->ExceptionCheck()) return nullptr;
        array_text(env,out,index++,row.label);
        array_text(env,out,index++,row.detail);
    }
    return out;
}
jintArray int_array(JNIEnv* env,const std::vector<int>& values) {
    const auto size=static_cast<jsize>(values.size());
    auto array=env->NewIntArray(size);
    if (array && size) env->SetIntArrayRegion(array,0,size,values.data());
    return array;
}
template <typename Int,std::size_t N>
jintArray fixed_int_array(JNIEnv* env,const std::array<Int,N>& values) {
    std::array<jint,N> copy{};
    for (std::size_t i=0;i<N;++i) copy[i]=static_cast<jint>(values[i]);
    const auto size=static_cast<jsize>(copy.size());
    auto array=env->NewIntArray(size);
    if (array && size) env->SetIntArrayRegion(array,0,size,copy.data());
    return array;
}
std::string directory_path(JNIEnv* env,jbyteArray raw) {
    if (!raw) return {};
    const auto n=env->GetArrayLength(raw);
    if (n<=0 || n>32768) throw std::invalid_argument("Invalid game directory");
    std::string value(static_cast<std::size_t>(n),'\0');
    env->GetByteArrayRegion(raw,0,n,reinterpret_cast<jbyte*>(value.data()));
    return value;
}
std::vector<std::string> read_text_fields(JNIEnv* env,jobjectArray array,jsize max_fields,jsize max_bytes) {
    if (!array || env->GetArrayLength(array)>max_fields) throw std::invalid_argument("Invalid text field count");
    const auto count=env->GetArrayLength(array);
    std::vector<std::string> values;
    values.reserve(static_cast<std::size_t>(count));
    for (jsize i=0;i<count;++i) {
        auto item=static_cast<jbyteArray>(env->GetObjectArrayElement(array,i));
        if (!item) throw std::invalid_argument("Missing text field");
        const auto size=env->GetArrayLength(item);
        if (size>max_bytes) {
            env->DeleteLocalRef(item);
            throw std::invalid_argument("Text field exceeds byte limit");
        }
        std::string value(static_cast<std::size_t>(size),'\0');
        if (size) env->GetByteArrayRegion(item,0,size,reinterpret_cast<jbyte*>(value.data()));
        env->DeleteLocalRef(item);
        if (env->ExceptionCheck()) throw std::runtime_error("Unable to read text field");
        values.push_back(std::move(value));
    }
    return values;
}
std::string dialogue_body(const std::string& text,const std::string& speaker) {
    if (speaker.empty() || text.compare(0,speaker.size(),speaker)!=0) return text;
    auto start=speaker.size();
    if (start<text.size() && text[start]=='\r') ++start;
    if (start<text.size() && text[start]=='\n') ++start;
    return text.substr(start);
}
std::string resource_name(JNIEnv* env,jbyteArray raw) {
    if (!raw) return {};
    const auto n=env->GetArrayLength(raw);
    if (n>255) throw std::runtime_error("UI resource name too long");
    std::string value(static_cast<std::size_t>(n),'\0');
    if (n) env->GetByteArrayRegion(raw,0,n,reinterpret_cast<jbyte*>(value.data()));
    return value;
}
bool draws_story_ui(const Player& p) {
    return !p.title && p.game->story_draws_screen_layer();
}
std::array<jint,3> story_stamp(const Player& p) {
    const auto& state=p.game->state();
    return {static_cast<jint>(state.wait),static_cast<jint>(state.word),
        static_cast<jint>(state.dialogue.sequence)};
}
void flush_screen_capture(Player& p) {
    const auto& screen=p.published_screen_layer;
    if (!screen || !screen->frame->captures_screen() || !draws_story_ui(p) ||
        screen->state!=story_stamp(p) || p.screen_layer_revision!=p.game->screen_layer_revision()) return;
    std::unique_lock lock(screen->mutex);
    // A script may consume GET on the next tick. The UI only writes this
    // detached frame, so it can finish the draw while the game thread waits.
    if (has_surface(p) && !screen->committed)
        screen->ready.wait_for(lock,std::chrono::milliseconds(45),[&] { return !screen->sample.rgb.empty(); });
    if (screen->sample.rgb.empty() && screen->committed) return;
    auto sample=std::exchange(screen->sample,{});
    screen->committed=true;
    lock.unlock();
    if (sample.rgb.empty()) sample=screen->frame->capture_without_ui();
    p.game->commit_screen_capture(std::move(sample));
}
void copy_scene_rgba(const emberfall::render::Surface& src,std::uint8_t* row,std::size_t stride) {
    const auto* rgb=src.rgb.data();
    for (unsigned y=0;y<src.height;++y,row+=stride) {
        auto* pixel=row;
        for (unsigned x=0;x<src.width;++x,pixel+=4,rgb+=3) {
            pixel[0]=rgb[0]; pixel[1]=rgb[1]; pixel[2]=rgb[2]; pixel[3]=255;
        }
    }
}
bool sync_story_plane(Player& p) {
    if (p.title || !p.compositor.mailbox()->attached()) return true;
    if (!p.game->needs_message_plane()) {
        p.game->clear_message_plane();
        return true;
    }
    const auto snap=p.compositor.mailbox()->snapshot();
    if (snap.has_plane && emberfall::scene::scene_plane_key_matches(snap.key,p.game->expected_plane_key())) {
        p.game->set_message_plane(snap.plane);
        return true;
    }
    return false;
}
jlong present(Player& p) {
    const bool visible=has_surface(p);
    if (!p.compositor.mailbox()->attached()) {
        p.game->capture_screen_layer(visible && draws_story_ui(p));
        if (!visible || (!p.repaint && !p.game->needs_redraw())) return 0;
        const auto& src=p.static_title.rgb.empty() ? p.game->surface() : p.static_title;
        jlong queued_at=0;
        p.surface->submit([&](ANativeWindow* window) {
            if (p.window_geometry_pending) {
                if (ANativeWindow_setBuffersGeometry(window,static_cast<int>(p.width),static_cast<int>(p.height),WINDOW_FORMAT_RGBA_8888)!=0)
                    return;
                p.window_geometry_pending=false;
            }
            ANativeWindow_Buffer dst{};
            if (ANativeWindow_lock(window,&dst,nullptr)!=0) { p.repaint=true; return; }
            if (emberfall::android::rgba_window_buffer(dst,static_cast<int>(src.width),static_cast<int>(src.height))) {
                copy_scene_rgba(src,static_cast<std::uint8_t*>(dst.bits),static_cast<std::size_t>(dst.stride)*4);
                p.repaint=false;
            } else {
                emberfall::android::clear_window_buffer(dst);
                p.repaint=true;
                p.window_geometry_pending=true;
            }
            // The SurfaceTexture receives the native producer's monotonic timestamp.
            const auto now=std::chrono::steady_clock::now().time_since_epoch();
            if (ANativeWindow_unlockAndPost(window)!=0) p.repaint=true;
            if (!p.repaint) queued_at=std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
        });
        return queued_at;
    }
    p.game->capture_screen_layer(false);
    if (!visible) return 0;
    jlong queued_at=0;
    const auto* override_surface=p.static_title.rgb.empty() ? nullptr : &p.static_title;
    const auto outcome=p.compositor.present(*p.game,p.repaint,!p.title,
        [&](const emberfall::render::Surface& src) {
            bool posted=false;
            p.surface->submit([&](ANativeWindow* window) {
                if (p.window_geometry_pending) {
                    if (ANativeWindow_setBuffersGeometry(window,static_cast<int>(p.width),static_cast<int>(p.height),WINDOW_FORMAT_RGBA_8888)!=0)
                        return;
                    p.window_geometry_pending=false;
                }
                ANativeWindow_Buffer dst{};
                if (ANativeWindow_lock(window,&dst,nullptr)!=0) return;
                const bool copied=emberfall::android::rgba_window_buffer(dst,static_cast<int>(src.width),static_cast<int>(src.height));
                if (copied)
                    copy_scene_rgba(src,static_cast<std::uint8_t*>(dst.bits),static_cast<std::size_t>(dst.stride)*4);
                else {
                    emberfall::android::clear_window_buffer(dst);
                    p.window_geometry_pending=true;
                }
                const auto now=std::chrono::steady_clock::now().time_since_epoch();
                if (ANativeWindow_unlockAndPost(window)==0 && copied) {
                    posted=true;
                    queued_at=std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
                }
            });
            return posted;
        },override_surface);
    using Outcome=emberfall::scene::StoryCompositor::PresentOutcome;
    if (outcome==Outcome::Presented) p.repaint=false;
    else if (outcome==Outcome::SubmitFailed) p.repaint=true;
    return queued_at;
}
void frame_meta(JNIEnv* env,Player& p,jintArray meta) {
    const auto& f=p.game->state();
    const jint values[]={static_cast<jint>(f.wait),static_cast<jint>(f.word),static_cast<jint>(f.dialogue.sequence),
        static_cast<jint>(p.width),static_cast<jint>(p.height),static_cast<jint>(p.game->frame_ms()),
        static_cast<jint>(f.wait==emberfall::scene::Wait::Choice?f.choice_remaining_ms:f.remaining_frames)};
    env->SetIntArrayRegion(meta,0,7,values);
    if (env->GetArrayLength(meta)>=10) {
        const jint cursor=f.script_cursor;
        env->SetIntArrayRegion(meta,9,1,&cursor);
    }
    if (env->GetArrayLength(meta)>=11) {
        const jint message=static_cast<jint>(p.game->draw_message());
        env->SetIntArrayRegion(meta,10,1,&message);
    }
    if (env->GetArrayLength(meta)>=14) {
        const jint film_skip=p.game->film_skip_available()?1:0;
        env->SetIntArrayRegion(meta,13,1,&film_skip);
    }
    if (env->GetArrayLength(meta)>=15) {
        const jint mouse=f.script_mouse;
        env->SetIntArrayRegion(meta,14,1,&mouse);
    }
}
emberfall::scene::Wait step_title(Player& p,std::optional<unsigned> elapsed_ms={}) {
    emberfall::scene::InputState input;
    input.mouse_x=input.mouse_y=-1;
    const auto& state=elapsed_ms ? p.game->tick_ui_background(input,p.loop_title,*elapsed_ms)
                                 : p.game->step_ui_background(input,p.loop_title);
    if(state.wait==emberfall::scene::Wait::Error)throw std::runtime_error(state.error);
    return state.wait;
}
}
#define API extern "C" JNIEXPORT
#define FN(name) Java_com_tyranor_runtime_systemnnn_NativeGame_##name

namespace {
using RuntimeRef=GameSession::RuntimeRef;
void apply_runtime(emberfall::scene::SessionOptions& options,jlong token) {
    const auto& ref=*reinterpret_cast<const RuntimeRef*>(token);
    options.runtime=ref.runtime;
    options.save_layout=ref.save_layout;
}
jlong open_game(JNIEnv* env,jbyteArray path,jint rate,emberfall::scene::SessionOptions options={},bool debug_mode=false) {
        const auto root=directory_path(env,path);
        if (env->ExceptionCheck()) return 0;
        if (root.empty() || root.find('\0')!=std::string::npos) throw std::invalid_argument("Invalid game directory");
        options.audio.sample_rate=static_cast<unsigned>(rate);
        options.queue_audio_events=false;
        options.filename_encoder=emberfall::android::filename_encoder(env);
        auto p=std::make_unique<Player>();
        p->compositor.set_warning_logger([](const std::string& warning) {
            __android_log_print(ANDROID_LOG_WARN,"SystemNNN","%s",warning.c_str());
        });
        if (debug_mode) options.script_debug_output=[owner=p.get()](std::string_view message) {
            auto& messages=owner->script_debug_messages;
            if (messages.size()==20) messages.pop_front();
            messages.emplace_back(message.substr(0,254));
        };
        p->game=std::make_unique<GameSession>(std::filesystem::u8path(root),options);
        const auto& surface=p->game->surface();
        p->width=surface.width; p->height=surface.height;
        return reinterpret_cast<jlong>(p.release());
}
}
API jlong JNICALL FN(open)(JNIEnv* env,jclass,jbyteArray path,jint rate) {
    try { return open_game(env,path,rate); }
    catch (const std::exception& e) { fail(env,e.what()); return 0; }
}
API jlong JNICALL FN(openWithRuntime)(JNIEnv* env,jclass,jbyteArray path,jint rate,jlong token) {
    try {
        if (!token) throw std::invalid_argument("Missing runtime context");
        emberfall::scene::SessionOptions options;
        apply_runtime(options,token);
        return open_game(env,path,rate,std::move(options));
    } catch (const std::exception& e) { fail(env,e.what()); return 0; }
}
API jlong JNICALL FN(openWithOptions)(JNIEnv* env,jclass,jbyteArray path,jint rate,jlong token,
                                     jboolean analyze_executable,jboolean debug_mode) {
    try {
        emberfall::scene::SessionOptions options;
        if (token) apply_runtime(options,token);
        options.analyze_executable=analyze_executable==JNI_TRUE;
        return open_game(env,path,rate,std::move(options),debug_mode==JNI_TRUE);
    } catch (const std::exception& e) { fail(env,e.what()); return 0; }
}
API jlong JNICALL FN(openReplay)(JNIEnv* env,jclass,jbyteArray path,jint rate,jlong token,
                               jbyteArray script,jintArray films,jint hidden_layers) {
    try {
        if (!token || !films) throw std::invalid_argument("Missing replay context or film list");
        const auto count=env->GetArrayLength(films);
        if (count<=0 || count>4096) throw std::invalid_argument("Invalid replay film count");
        std::vector<jint> raw(static_cast<std::size_t>(count));
        env->GetIntArrayRegion(films,0,count,raw.data());
        if (env->ExceptionCheck()) return 0;
        emberfall::scene::SessionOptions options;
        options.entry=resource_name(env,script);
        if (!nnn::resource::valid_component(options.entry))
            throw std::invalid_argument("Invalid replay script name");
        apply_runtime(options,token);
        options.replay_hidden_layers=static_cast<std::uint32_t>(hidden_layers);
        for (jint film:raw) {
            if (film<0) throw std::invalid_argument("Invalid replay film number");
            options.replay_films.push_back(static_cast<nnn::Word>(film));
        }
        return open_game(env,path,rate,std::move(options));
    } catch (const std::exception& e) { fail(env,e.what()); return 0; }
}
API jlong JNICALL FN(retainContext)(JNIEnv* env,jclass,jlong handle) {
    try { return reinterpret_cast<jlong>(new RuntimeRef(player(handle).game->runtime_ref())); }
    catch (const std::exception& e) { fail(env,e.what()); return 0; }
}
API jlong JNICALL FN(cloneContext)(JNIEnv* env,jclass,jlong token) {
    try {
        if (!token) throw std::invalid_argument("Missing runtime context");
        return reinterpret_cast<jlong>(new RuntimeRef(*reinterpret_cast<const RuntimeRef*>(token)));
    } catch (const std::exception& e) { fail(env,e.what()); return 0; }
}
API jlong JNICALL FN(forkContext)(JNIEnv* env,jclass,jlong token) {
    try {
        if (!token) throw std::invalid_argument("Missing runtime context");
        const auto& ref=*reinterpret_cast<const RuntimeRef*>(token);
        return reinterpret_cast<jlong>(new RuntimeRef{ref.runtime->fork(),ref.save_layout});
    } catch (const std::exception& e) { fail(env,e.what()); return 0; }
}
API void JNICALL FN(releaseContext)(JNIEnv*,jclass,jlong token) {
    delete reinterpret_cast<RuntimeRef*>(token);
}
API void JNICALL FN(close)(JNIEnv*,jclass,jlong handle) {
    std::unique_ptr<Player> p(reinterpret_cast<Player*>(handle));
    if (!p) return;
    p->compositor.mailbox()->set_attached(false);
    const auto scene=p->game->stats();
    const auto audio=p->game->audio().stats();
    __android_log_print(ANDROID_LOG_INFO,"SystemNNN","closed: images=%zu audioEvents=%llu decodedClips=%llu missingAudio=%llu audioErrors=%llu",
        scene.decodes,static_cast<unsigned long long>(audio.events),static_cast<unsigned long long>(audio.decoded),
        static_cast<unsigned long long>(audio.missing),static_cast<unsigned long long>(audio.errors));
}
API jlong JNICALL FN(createSurface)(JNIEnv* env,jclass,jobject surface) {
    try {
        std::unique_ptr<ANativeWindow,decltype(&ANativeWindow_release)>
            window(ANativeWindow_fromSurface(env,surface),ANativeWindow_release);
        if (!window) return 0;
        auto binding=std::make_shared<SurfaceBinding>(window.get(),ANativeWindow_release);
        window.release();
        return reinterpret_cast<jlong>(new SurfaceRef(std::move(binding)));
    } catch (const std::exception& e) { fail(env,e.what()); return 0; }
}
API void JNICALL FN(releaseSurface)(JNIEnv*,jclass,jlong token) {
    std::unique_ptr<SurfaceRef> binding(reinterpret_cast<SurfaceRef*>(token));
    if (binding) (*binding)->invalidate();
}
API void JNICALL FN(deactivateSurface)(JNIEnv*,jclass,jlong token) {
    if (token) (*reinterpret_cast<const SurfaceRef*>(token))->deactivate();
}
API void JNICALL FN(bindSurface)(JNIEnv*,jclass,jlong handle,jlong token) {
    auto& p=player(handle);
    p.surface=token ? *reinterpret_cast<const SurfaceRef*>(token) : SurfaceRef{};
    p.repaint=true;
    p.window_geometry_pending=true;
}
API void JNICALL FN(presentSurface)(JNIEnv* env,jclass,jlong handle) {
    try { present(player(handle)); }
    catch (const std::exception& e) { fail(env,e.what()); }
}
API jboolean JNICALL FN(configureMenuBackground)(JNIEnv* env,jclass,jlong handle,jint mode,jbyteArray script,
                                                jbyteArray background,jboolean repeat) {
    try {
        auto& p=player(handle);
        p.title=true; p.loop_title=repeat; p.back_script=resource_name(env,script);
        p.static_title={};
        p.game->clear_message_plane();
        p.compositor.reset_present_state();
        p.repaint=true;
        if (!p.back_script.empty()) {
            try { p.game->start_ui_background(p.back_script,mode);step_title(p); }
            catch (const std::exception& e) { p.back_script.clear(); __android_log_print(ANDROID_LOG_WARN,"SystemNNN","menu script: %s",e.what()); }
        }
        if (p.back_script.empty()) p.game->start_ui_background({},mode);
        const auto image=resource_name(env,background);
        if (p.back_script.empty() && !image.empty()) {
            try {
                auto texture=p.game->ui_texture(image);
                p.static_title={p.width,p.height,std::vector<emberfall::render::Byte>(std::size_t{p.width}*p.height*3)};
                emberfall::render::blit(texture->pixels,p.static_title,
                    {0,0,texture->content.width,texture->content.height},{texture->content.x,texture->content.y},
                    emberfall::render::BlendProfile::Source64Cpp);
            } catch (const std::exception& e) { __android_log_print(ANDROID_LOG_WARN,"SystemNNN","menu image: %s",e.what()); }
        }
        return !p.back_script.empty() || !p.static_title.rgb.empty();
    } catch (const std::exception& e) { fail(env,e.what()); return JNI_FALSE; }
}
API jint JNICALL FN(advanceOpening)(JNIEnv* env,jclass,jlong handle,jint elapsed_ms) {
    try {
        if (elapsed_ms<0) throw std::invalid_argument("Invalid opening elapsed time");
        auto& p=player(handle);
        const auto period=p.game->frame_ms();
        if (p.back_script.empty()) return static_cast<jint>(period);
        using emberfall::scene::Wait;
        const auto wait=elapsed_ms ? step_title(p,static_cast<unsigned>(elapsed_ms)) : p.game->state().wait;
        if (wait!=Wait::Ended && wait!=Wait::Running && wait!=Wait::Draw && wait!=Wait::Overlap)
            throw std::runtime_error("Opening background waits for unsupported input");
        present(p);
        return wait==Wait::Ended?0:static_cast<jint>(p.game->frame_ms());
    } catch (const std::exception& e) { fail(env,e.what()); return 0; }
}
API void JNICALL FN(resetFramePacing)(JNIEnv* env,jclass,jlong handle) {
    try { player(handle).game->reset_frame_pacing(); }
    catch (const std::exception& e) { fail(env,e.what()); }
}
API void JNICALL FN(restartTitle)(JNIEnv* env,jclass,jlong handle) {
    try {
        auto& p=player(handle);
        p.game->clear_message_plane();
        p.compositor.reset_present_state();
        if(!p.back_script.empty()){
            p.game->start_ui_background(p.back_script,4,true);
            step_title(p);
        }
        p.repaint=true;
        present(p);
    } catch(const std::exception& e) { fail(env,e.what()); }
}
static jintArray ui_sprite(JNIEnv* env,jlong handle,jbyteArray raw,int max_pixels,bool use_color_key=false) {
    try {
        auto t=player(handle).game->ui_texture(resource_name(env,raw));
        const auto w=t->canvas_width,h=t->canvas_height;
        if (w<=0 || h<=0 || static_cast<std::int64_t>(w)*h>max_pixels) return nullptr;
        std::vector<jint> pixels(2+static_cast<std::size_t>(w)*h);
        pixels[0]=w; pixels[1]=h;
        const auto rgb=[](const unsigned char* src) {
            return (std::uint32_t(src[0])<<16)|(std::uint32_t(src[1])<<8)|src[2];
        };
        const bool keyed=use_color_key && !t->has_mask;
        const auto key=keyed && t->content.x==0 && t->content.y==0 && !t->pixels.rgba.empty() ? rgb(t->pixels.rgba.data()) : 0U;
        for (int y=0;y<t->content.height;y++) {
            auto row=pixels.data()+2+(t->content.y+y)*w+t->content.x;
            const auto* src=t->pixels.rgba.data()+static_cast<std::size_t>(y)*t->pixels.width*4;
            for(int x=0;x<t->content.width;++x,src+=4){
                const auto color=rgb(src);
                row[x]=static_cast<jint>((std::uint32_t(keyed && color==key ? 0 : src[3])<<24)|color);
            }
        }
        auto result=env->NewIntArray(static_cast<jsize>(pixels.size()));
        if (result) env->SetIntArrayRegion(result,0,static_cast<jsize>(pixels.size()),pixels.data());
        return result;
    } catch (const std::exception& e) { __android_log_print(ANDROID_LOG_WARN,"SystemNNN","title button: %s",e.what()); return nullptr; }
}
API jintArray JNICALL FN(titleSprite)(JNIEnv* env,jclass,jlong handle,jbyteArray raw) {
    return ui_sprite(env,handle,raw,2*1024*1024);
}
API jintArray JNICALL FN(gallerySprite)(JNIEnv* env,jclass,jlong handle,jbyteArray raw) {
    return ui_sprite(env,handle,raw,6*1024*1024);
}
API jintArray JNICALL FN(keyedSprite)(JNIEnv* env,jclass,jlong handle,jbyteArray raw) {
    return ui_sprite(env,handle,raw,6*1024*1024,true);
}
namespace {
bool capture_overlap_ui(JNIEnv* env,jobject host,emberfall::render::Surface& scene) {
    const auto host_type=env->GetObjectClass(host);
    if (!host_type) return false;
    const auto callback=env->GetMethodID(host_type,"captureOverlapSource","(II)Landroid/graphics/Bitmap;");
    env->DeleteLocalRef(host_type);
    if (!callback) return false;
    const auto bitmap=env->CallObjectMethod(host,callback,static_cast<jint>(scene.width),static_cast<jint>(scene.height));
    if (!bitmap || env->ExceptionCheck()) return false;
    bool drawn=false;
    try {
        emberfall::android::LockedBitmap overlay(env,bitmap);
        if (overlay.info.width==scene.width && overlay.info.height==scene.height) {
            auto* pixel=scene.rgb.data();
            for (unsigned y=0;y<scene.height;++y) {
                const auto* src=overlay.row(y);
                for (unsigned x=0;x<scene.width;++x,src+=4,pixel+=3) {
                    if (!src[3]) continue;
                    const unsigned remaining=255-src[3];
                    for (unsigned c=0;c<3;++c)
                        pixel[c]=static_cast<std::uint8_t>(src[c]+(pixel[c]*remaining+127)/255);
                }
            }
            drawn=true;
        }
    } catch (const std::exception& e) {
        __android_log_print(ANDROID_LOG_WARN,"SystemNNN","story transition: %s",e.what());
    }
    env->DeleteLocalRef(bitmap);
    return drawn;
}
void tick(JNIEnv* env,jlong handle,jint ms,jint x,jint y,jboolean advance,jint choice,jboolean replay,jintArray meta,jobject host,
          bool* stage_story=nullptr) {
    try {
        const auto length=meta ? env->GetArrayLength(meta) : 0;
        if (length<7 || ms<0) throw std::invalid_argument("Invalid frame input");
        if (length>=16) {
            const jint executed=0;
            env->SetIntArrayRegion(meta,15,1,&executed);
        }
        const bool with_clock=length>=9;
        jint clock[2]{};
        if (replay && with_clock) {
            env->GetIntArrayRegion(meta,7,2,clock);
            if (env->ExceptionCheck()) return;
            if (clock[0]< -1) throw std::invalid_argument("Invalid journal clock mode");
        }
        auto& p=player(handle);
        if (p.title) {
            if (ms>0 && !p.back_script.empty()) {
                try {
                    step_title(p,static_cast<unsigned>(ms));
                }
                catch(const std::exception& e) {
                    __android_log_print(ANDROID_LOG_WARN,"SystemNNN","title background: %s",e.what());
                    p.back_script.clear();
                }
            }
            present(p);
            const jint info[]={6,0,0,static_cast<jint>(p.width),static_cast<jint>(p.height),static_cast<jint>(p.game->frame_ms()),0};
            env->SetIntArrayRegion(meta,0,7,info);
            if (with_clock) {const jint empty[]={0,0};env->SetIntArrayRegion(meta,7,2,empty);}
            if (length>=10) {const jint cursor=0;env->SetIntArrayRegion(meta,9,1,&cursor);}
            if (length>=12) {
                const jint ended=!p.loop_title && !p.back_script.empty() &&
                    p.game->state().wait==emberfall::scene::Wait::Ended;
                env->SetIntArrayRegion(meta,11,1,&ended);
            }
            if (length>=15) {
                const jint mouse=p.game->state().script_mouse;
                env->SetIntArrayRegion(meta,14,1,&mouse);
            }
            return;
        }
        if (!replay && has_surface(p) && p.compositor.waiting_for_draw(*p.game)) {
            present(p);
            p.game->reset_frame_pacing();
            frame_meta(env,p,meta);
            if (with_clock) {const jint clock_values[]={0,0};env->SetIntArrayRegion(meta,7,2,clock_values);}
            if (length>=13) {const jint paced=0;env->SetIntArrayRegion(meta,12,1,&paced);}
            return;
        }
        if (replay) {
            const auto samples=static_cast<unsigned>(ms)*std::uint64_t{p.game->audio().sample_rate()}+p.remainder;
            p.game->audio().skip_frames(samples/1000);
            p.remainder=samples%1000;
        }
        emberfall::scene::InputState input;
        input.mouse_x=x; input.mouse_y=y; input.advance=advance; input.choice=static_cast<unsigned>(choice);
        input.play_movies=replay!=JNI_TRUE;
        input.expire_choices=replay!=JNI_TRUE;
        input.legacy_calculations=replay==JNI_TRUE && clock[0]==0;
        input.recorded_clock=replay==JNI_TRUE && clock[0]!=0;
        if (input.recorded_clock && clock[0]>0)
            input.clock_sample=emberfall::scene::ScriptClockSample{clock[0],clock[1]};
        const bool mailbox_attached=p.compositor.mailbox()->attached();
        if (!replay && !mailbox_attached) flush_screen_capture(p);
        // Legacy journals supply a single interval. Live updates keep actual time
        // for the choice and speech clocks and apply CShakin to animation.
        const auto elapsed_ms=static_cast<unsigned>(ms);
        const auto step_ms=replay || ms==0 ? elapsed_ms : p.game->pace(elapsed_ms);
        emberfall::render::Layers::OverlapCapture capture;
        if (host && has_surface(p) && !mailbox_attached)
            capture=[env,host](auto& source) { return capture_overlap_ui(env,host,source); };
        using emberfall::scene::Wait;
        const auto large_dialogue=[](const emberfall::scene::FrameState& frame) {
            return frame.wait==Wait::Dialogue && frame.dialogue.layout_command==0x22220002U;
        };
        const auto& previous=p.game->state();
        const auto before=previous.wait;
        const bool before_large=large_dialogue(previous);
        const bool before_overlap=before==Wait::Overlap || p.overlap_endpoint;
        const auto& f=p.game->step_frame({elapsed_ms,step_ms},input,capture);
        if (length>=16) {
            const jint executed=1;
            env->SetIntArrayRegion(meta,15,1,&executed);
        }
        if (f.wait==emberfall::scene::Wait::Error) throw std::runtime_error(f.script+" @"+std::to_string(f.word)+" "+f.error);
        const auto sampled=p.game->script_clock_sample();
        if (input.clock_sample && !sampled) throw std::runtime_error("Unused journal clock sample");
        p.overlap_endpoint=before==Wait::Overlap && f.wait==Wait::Running;
        const bool in_overlap=f.wait==Wait::Overlap || p.overlap_endpoint;
        const bool story=f.wait==Wait::Overlap || f.wait==Wait::Dialogue || f.wait==Wait::Draw || f.wait==Wait::Choice;
        // DRAW can carry the same artwork that LPRINT will draw over the next scene.
        // Publish the replacement artwork with the new base so neither appears alone.
        const bool replace_large=before_large!=large_dialogue(f) && p.game->needs_redraw();
        const bool stage=!mailbox_attached && stage_story && has_surface(p) && story && (before_overlap!=in_overlap || replace_large);
        if (stage_story) *stage_story=stage;
        if (!replay && !stage) present(p);
        frame_meta(env,p,meta);
        if (with_clock) {
            const jint values[]={sampled ? sampled->date : 0,sampled ? sampled->milliseconds : 0};
            env->SetIntArrayRegion(meta,7,2,values);
        }
        if (length>=13) {
            const jint paced=static_cast<jint>(step_ms);
            env->SetIntArrayRegion(meta,12,1,&paced);
        }
    } catch (const std::exception& e) { fail(env,e.what()); }
}
}
API void JNICALL FN(tick)(JNIEnv* env,jclass,jlong handle,jint ms,jint x,jint y,jboolean advance,jint choice,jboolean replay,jintArray meta) {
    tick(env,handle,ms,x,y,advance,choice,replay,meta,nullptr);
}
API jboolean JNICALL FN(tickStory)(JNIEnv* env,jclass,jlong handle,jint ms,jint x,jint y,jboolean advance,jint choice,jintArray meta,jobject host) {
    bool stage=false;
    tick(env,handle,ms,x,y,advance,choice,JNI_FALSE,meta,host,&stage);
    return stage ? JNI_TRUE : JNI_FALSE;
}
API void JNICALL FN(copyStoryFrame)(JNIEnv* env,jclass,jlong handle,jobject bitmap) {
    try {
        auto& p=player(handle);
        const bool ready=sync_story_plane(p);
        emberfall::android::LockedBitmap out(env,bitmap);
        const auto& source=ready ? p.game->surface() : p.game->presented_surface();
        if (source.rgb.empty()) throw std::runtime_error("Story frame is not ready");
        if (out.info.width!=source.width || out.info.height!=source.height)
            throw std::invalid_argument("Story frame size changed");
        copy_scene_rgba(source,out.row(0),out.info.stride);
    } catch (const std::exception& e) { fail(env,e.what()); }
}
API jlong JNICALL FN(presentStoryFrame)(JNIEnv* env,jclass,jlong handle) {
    try {
        auto& p=player(handle);
        p.repaint=true;
        return present(p);
    } catch (const std::exception& e) { fail(env,e.what()); return 0; }
}
API jobjectArray JNICALL FN(content)(JNIEnv* env,jclass,jlong handle) {
    const auto& f=player(handle).game->state();
    const auto count=f.selection ? f.selection->options.size() : 0;
    auto result=env->NewObjectArray(static_cast<jsize>(2+count),env->FindClass("[B"),nullptr);
    if (!result) return nullptr;
    auto set=[&](int i,const std::string& value) {
        auto b=bytes(env,value);
        if (b) { env->SetObjectArrayElement(result,i,b); env->DeleteLocalRef(b); }
    };
    set(0,f.script); set(1,f.dialogue.display_bytes);
    for (std::size_t i=0;i<count && !env->ExceptionCheck();++i) set(static_cast<int>(i+2),f.selection->options[i]);
    return result;
}
API jobjectArray JNICALL FN(dialogueVisuals)(JNIEnv* env,jclass,jlong handle) {
    try {
        const auto visuals=player(handle).game->dialogue_visuals();
        if (!visuals.cutin && visuals.portraits.empty()) return nullptr;
        const auto portrait_count=static_cast<jsize>(visuals.portraits.size());
        const auto row_count=static_cast<jsize>(visuals.rows.size());
        auto out=env->NewObjectArray(1+portrait_count+row_count,env->FindClass("[B"),nullptr);
        if (!out) return nullptr;
        nnn::snapshot::Writer info;
        info(visuals.portrait_depth,visuals.cutin?1:0,portrait_count,row_count);
        jsize index=1;
        for (const auto& sprite:visuals.portraits) {
            info(sprite.x,sprite.y,sprite.width,sprite.height,sprite.animation_type,sprite.frames,
                 sprite.speed,sprite.wait_min,sprite.wait_max);
            array_text(env,out,index++,sprite.image);
            if (env->ExceptionCheck()) return nullptr;
        }
        for (const auto& row:visuals.rows) {
            info(row.positioned?1:0,row.x,row.y,row.font_delta,row.window_type,row.window_x,row.window_y,
                 row.pad_x,row.pad_y,row.step_x,row.step_y,row.window_rows,row.window_color);
            array_text(env,out,index++,row.window_image);
            if (env->ExceptionCheck()) return nullptr;
        }
        array_text(env,out,0,std::string(info.bytes.begin(),info.bytes.end()));
        return out;
    } catch (const std::exception& e) { fail(env,e.what()); return nullptr; }
}
API void JNICALL FN(frameInfo)(JNIEnv* env,jclass,jlong handle,jintArray meta) {
    if (!meta || env->GetArrayLength(meta)<7) {fail(env,"Missing frame metadata");return;}
    frame_meta(env,player(handle),meta);
}
API jint JNICALL FN(runtimeMode)(JNIEnv*,jclass,jlong handle) {
    return player(handle).game->mode();
}
API jobjectArray JNICALL FN(debugSnapshot)(JNIEnv* env,jclass,jlong handle) {
    try {
        auto& p=player(handle);
        const auto& game=*p.game;
        const auto& state=game.state();
        const auto layout=game.pc_save_layout();
        const auto playback=game.audio().state();
        const auto active=[](const auto& tracks) {
            return static_cast<int>(std::count_if(tracks.begin(),tracks.end(),[](const auto& track){return track.active;}));
        };
        const auto& layers=game.layers();
        const auto active_layers=std::count_if(layers.begin(),layers.end(),[](const auto& layer){return layer.enabled;});
        auto result=env->NewObjectArray(4,env->FindClass("java/lang/Object"),nullptr);
        if (!result) return nullptr;
        auto values=int_array(env,{static_cast<int>(state.wait),static_cast<int>(state.word),game.mode(),
            static_cast<int>(p.width),static_cast<int>(p.height),static_cast<int>(active_layers),
            static_cast<int>(layout.render_layers),static_cast<int>(layout.layer_limit),
            static_cast<int>(layout.layer_records),static_cast<int>(layout.source),static_cast<int>(layout.message_width),
            static_cast<int>(layout.loaded_message_width),active(playback.voices),active(playback.sounds),
            static_cast<int>(game.warnings().size()),static_cast<int>(game.frame_ms())});
        if (!values) return nullptr;
        env->SetObjectArrayElement(result,0,values);
        array_text(env,result,1,state.script);
        if (!env->ExceptionCheck()) array_text(env,result,2,playback.bgm.active ? playback.bgm.name : std::string{});
        if (!p.script_debug_messages.empty() && !env->ExceptionCheck()) {
            const auto count=static_cast<jsize>(p.script_debug_messages.size());
            auto messages=env->NewObjectArray(count,env->FindClass("[B"),nullptr);
            if (!messages) return nullptr;
            for (jsize i=0;i<count && !env->ExceptionCheck();++i)
                array_text(env,messages,i,p.script_debug_messages[static_cast<std::size_t>(i)]);
            if (env->ExceptionCheck()) return nullptr;
            env->SetObjectArrayElement(result,3,messages);
            if (!env->ExceptionCheck()) p.script_debug_messages.clear();
        }
        return result;
    } catch (const std::exception& e) { fail(env,e.what()); return nullptr; }
}
API jlong JNICALL FN(systemMenuModes)(JNIEnv* env,jclass,jlong handle,jlong requested) {
    try {
        auto& p=player(handle);
        auto mask=static_cast<std::uint64_t>(requested);
        for (unsigned mode=1;mode<=49;++mode) {
            const auto bit=std::uint64_t{1}<<mode;
            if ((mask&bit) && !p.game->menu_extension_enabled(static_cast<nnn::RuntimeMode>(mode))) mask&=~bit;
        }
        return static_cast<jlong>(mask);
    }catch(const std::exception& e){fail(env,e.what());return 0;}
}
API jintArray JNICALL FN(modalInfo)(JNIEnv* env,jclass,jlong handle) {
    const auto& modal=player(handle).game->state().modal;
    if (!modal) return nullptr;
    std::vector<int> fields{modal->request,static_cast<int>(modal->mode),
        modal->return_mode ? static_cast<int>(*modal->return_mode) : 0};
    fields.insert(fields.end(),modal->arguments.begin(),modal->arguments.end());
    return int_array(env,fields);
}
API jintArray JNICALL FN(modalLimits)(JNIEnv* env,jclass,jlong handle) {
    const auto& modal=player(handle).game->state().modal;
    return modal ? int_array(env,modal->byte_limits) : nullptr;
}
API jobjectArray JNICALL FN(modalText)(JNIEnv* env,jclass,jlong handle) {
    const auto& modal=player(handle).game->state().modal;
    if (!modal) return nullptr;
    auto out=env->NewObjectArray(static_cast<jsize>(modal->text.size()),env->FindClass("[B"),nullptr);
    if (!out) return nullptr;
    for (std::size_t i=0;i<modal->text.size() && !env->ExceptionCheck();++i)
        array_text(env,out,static_cast<jsize>(i),modal->text[i]);
    return out;
}
API jintArray JNICALL FN(modalView)(JNIEnv* env,jclass,jlong handle) {
    const auto& modal=player(handle).game->state().modal;
    if (!modal) return nullptr;
    std::vector<int> fields{modal->page,modal->pages,modal->group,modal->selection,modal->balance};
    fields.reserve(5+2*modal->rows.size());
    for (const auto& row:modal->rows) {
        fields.push_back(row.id);
        fields.push_back(row.flags);
    }
    return int_array(env,fields);
}
API jobjectArray JNICALL FN(modalRows)(JNIEnv* env,jclass,jlong handle) {
    const auto& modal=player(handle).game->state().modal;
    if (!modal) return nullptr;
    auto out=env->NewObjectArray(static_cast<jsize>(3*modal->rows.size()),env->FindClass("[B"),nullptr);
    if (!out) return nullptr;
    jsize index=0;
    for (const auto& row:modal->rows) {
        if (env->ExceptionCheck()) break;
        array_text(env,out,index++,row.label);
        array_text(env,out,index++,row.asset);
        array_text(env,out,index++,row.detail);
    }
    return out;
}
API jintArray JNICALL FN(chartGeometry)(JNIEnv* env,jclass,jlong handle) {
    try {
        const auto& modal=player(handle).game->state().modal;
        if (!modal || !modal->chart) return nullptr;
        const auto& v=*modal->chart;
        std::vector<int> fields{v.scroll,v.limit,v.extent,v.center_y,
            v.slot_x,v.slot_y,v.slot_area_width,v.slot_area_height,v.slot_width,v.slot_height,
            v.mini_x,v.mini_y,v.mini_width,v.mini_height,v.mini_base_x,v.mini_base_y,
            v.pointer_y,v.pending,v.page_percent,v.total_percent,
            static_cast<int>(v.page_states.size()),static_cast<int>(v.tiles.size()),static_cast<int>(v.nodes.size()),
            static_cast<int>(v.roads.size()),static_cast<int>(v.description.size()),v.back_extent};
        fields.reserve(fields.size()+v.page_states.size()+v.tiles.size()+11*v.nodes.size()+5*v.roads.size());
        fields.insert(fields.end(),v.page_states.begin(),v.page_states.end());
        for (const auto& tile:v.tiles) fields.push_back(tile.y);
        for (const auto& node:v.nodes)
            fields.insert(fields.end(),{node.id,node.x,node.y,node.style,node.access,node.sx,node.sy,node.sw,node.sh,node.routes,node.seen});
        for (const auto& road:v.roads) fields.insert(fields.end(),{road.x1,road.y1,road.x2,road.y2,road.style});
        return int_array(env,fields);
    } catch (const std::exception& e) { fail(env,e.what());return nullptr; }
}
API jobjectArray JNICALL FN(chartText)(JNIEnv* env,jclass,jlong handle) {
    const auto& modal=player(handle).game->state().modal;
    if (!modal || !modal->chart) return nullptr;
    const auto& v=*modal->chart;
    auto out=env->NewObjectArray(static_cast<jsize>(5+v.tiles.size()+v.nodes.size()+v.description.size()),
        env->FindClass("[B"),nullptr);
    if (!out) return nullptr;
    jsize index=0;
    const auto append=[&](const std::string& value) {
        if (!env->ExceptionCheck()) array_text(env,out,index++,value);
    };
    append(v.center);append(v.minimap);append(v.unseen);append(v.current);append(v.complete);
    for (const auto& tile:v.tiles) append(tile.asset);
    for (const auto& node:v.nodes) append(node.asset);
    for (const auto& line:v.description) append(line);
    return out;
}
API void JNICALL FN(setChartStorage)(JNIEnv* env,jclass,jlong handle,jbyteArray path) {
    try {
        const auto directory=directory_path(env,path);
        if (env->ExceptionCheck()) return;
        if (directory.empty() || directory.find('\0')!=std::string::npos)
            throw std::invalid_argument("Invalid chart directory");
        player(handle).game->set_chart_storage(std::filesystem::u8path(directory));
    } catch (const std::exception& e) { fail(env,e.what()); }
}
API jobjectArray JNICALL FN(statusOverlays)(JNIEnv* env,jclass,jlong handle,jint mode) {
    if (mode<1 || mode>49) return nullptr;
    try {
        auto& p=player(handle);
        const auto overlays=p.game->status_overlays(static_cast<nnn::RuntimeMode>(mode),p.title);
        auto out=env->NewObjectArray(static_cast<jsize>(overlays.size()),env->FindClass("[[B"),nullptr);
        if (!out) return nullptr;
        jsize layer=0;
        for (const auto& overlay:overlays) {
            auto data=status_overlay_rows(env,overlay,true);
            if (!data) return nullptr;
            env->SetObjectArrayElement(out,layer++,data);
            env->DeleteLocalRef(data);
            if (env->ExceptionCheck()) return nullptr;
        }
        return out;
    } catch (const std::exception& e) { fail(env,e.what());return nullptr; }
}
API jobjectArray JNICALL FN(statusOverlay)(JNIEnv* env,jclass,jlong handle,jint type,jint mode) {
    const auto kind=static_cast<nnn::RuntimeMode>(type);
    if ((kind!=nnn::RuntimeMode::PrintStatus && kind!=nnn::RuntimeMode::PrintParty) || mode<1 || mode>49)
        return nullptr;
    try {
        auto& p=player(handle);
        const auto overlay=p.game->status_overlay(kind,static_cast<nnn::RuntimeMode>(mode),p.title);
        return overlay ? status_overlay_rows(env,*overlay,false) : nullptr;
    } catch (const std::exception& e) { fail(env,e.what());return nullptr; }
}
API jint JNICALL FN(actModal)(JNIEnv* env,jclass,jlong handle,jint request,jint action,jint value) {
    if (request<=0) return -1;
    try {
        auto& p=player(handle);
        const auto result=p.game->act_modal(request,action,value);
        if (result==5 && p.title) {
            p.title=false;p.back_script.clear();p.static_title={};p.repaint=true;
            p.compositor.reset_present_state();
        }
        return result;
    }
    catch (const std::exception& e) { fail(env,e.what());return -1; }
}
API jboolean JNICALL FN(openModal)(JNIEnv* env,jclass,jlong handle,jint mode,jint caller) {
    if (mode<=0 || mode>49 || caller<=0 || caller>49) return JNI_FALSE;
    try {
        auto& p=player(handle);
        if (!p.title && !p.game->menu_extension_enabled(static_cast<nnn::RuntimeMode>(mode))) return JNI_FALSE;
        return p.game->open_modal(static_cast<nnn::RuntimeMode>(mode),static_cast<nnn::RuntimeMode>(caller),p.title) ? JNI_TRUE : JNI_FALSE;
    } catch (const std::exception& e) { fail(env,e.what());return JNI_FALSE; }
}
API jint JNICALL FN(abortModal)(JNIEnv* env,jclass,jlong handle,jint request,jstring reason) {
    if (request<=0 || !reason) return -1;
    const char* text=env->GetStringUTFChars(reason,nullptr);
    if (!text) return -1;
    try {
        const auto result=player(handle).game->abort_modal(request,text);
        env->ReleaseStringUTFChars(reason,text);
        return result;
    } catch (const std::exception& e) {
        env->ReleaseStringUTFChars(reason,text);fail(env,e.what());return -1;
    }
}
API void JNICALL FN(advanceModal)(JNIEnv* env,jclass,jlong handle,jint request,jint frames) {
    if (request<=0 || frames<=0) return;
    try { player(handle).game->advance_modal(request,static_cast<unsigned>(std::min(frames,64))); }
    catch (const std::exception& e) { fail(env,e.what()); }
}
API jboolean JNICALL FN(modalSavable)(JNIEnv*,jclass,jlong handle) {
    return player(handle).game->modal_savable() ? JNI_TRUE : JNI_FALSE;
}
API jboolean JNICALL FN(completeModal)(JNIEnv* env,jclass,jlong handle,jint request,jboolean accepted,jint elapsed,jobjectArray fields) {
    if (request<=0 || elapsed<0) return JNI_FALSE;
    try {
        const auto values=read_text_fields(env,fields,16,255);
        return player(handle).game->complete_modal(request,accepted,static_cast<unsigned>(elapsed),values) ? JNI_TRUE : JNI_FALSE;
    } catch (const std::exception& e) { fail(env,e.what()); return JNI_FALSE; }
}
API jboolean JNICALL FN(storyExited)(JNIEnv*,jclass,jlong handle) {
    return player(handle).game->state().end_reason==emberfall::scene::EndReason::Exit;
}
API jbyteArray JNICALL FN(snapshot)(JNIEnv* env,jclass,jlong handle) {
    try {
        auto& p=player(handle);
        if (p.compositor.mailbox()->attached()) {
            if (sync_story_plane(p)) (void)p.game->surface();
        } else {
            flush_screen_capture(p);
        }
        const auto data=p.game->snapshot();
        p.repaint=true;
        auto out=env->NewByteArray(static_cast<jsize>(data.size()));
        if (out) env->SetByteArrayRegion(out,0,static_cast<jsize>(data.size()),reinterpret_cast<const jbyte*>(data.data()));
        return out;
    } catch (const std::exception& e) {
        __android_log_print(ANDROID_LOG_WARN,"SystemNNN","Save journal only: %s",e.what());return nullptr;
    }
}
API void JNICALL FN(restorePcSave)(JNIEnv* env,jclass,jlong handle,jbyteArray raw,jintArray meta,jlong previous) {
    try {
        if (!raw || !meta || env->GetArrayLength(meta)<7) throw std::invalid_argument("Missing PC save data");
        const auto size=env->GetArrayLength(raw);
        if (size<=0 || size>16*1024*1024) throw std::invalid_argument("Invalid PC save length");
        std::vector<nnn::Byte> data(static_cast<std::size_t>(size));
        env->GetByteArrayRegion(raw,0,size,reinterpret_cast<jbyte*>(data.data()));
        if (env->ExceptionCheck()) return;
        auto& p=player(handle);
        // The Java runner commits this player only after the slot loads.
        p.game->load_pc_save_candidate(data,previous ? player(previous).game.get() : nullptr);
        p.published_screen_layer.reset();
        p.compositor.reset_present_state();
        frame_meta(env,p,meta);
        p.repaint=true;
        present(p);
    } catch (const std::exception& e) { fail(env,e.what()); }
}
API jint JNICALL FN(detectPcSystemSaveVersion)(JNIEnv* env,jclass,jbyteArray path) {
    try {
        const auto root=directory_path(env,path);
        if (env->ExceptionCheck() || root.empty() || root.find('\0')!=std::string::npos) return 0;
        return GameSession::detect_pc_system_save_version(std::filesystem::u8path(root));
    } catch (const std::exception&) { return 0; }
}
API jintArray JNICALL FN(pcSystemSaveVersions)(JNIEnv* env,jclass,jbyteArray path,jboolean analyze_executable) {
    try {
        const auto root=directory_path(env,path);
        if (env->ExceptionCheck() || root.empty() || root.find('\0')!=std::string::npos) return nullptr;
        const auto versions=GameSession::pc_system_save_versions(std::filesystem::u8path(root),analyze_executable==JNI_TRUE);
        return int_array(env,{versions[0],versions[1]});
    } catch (const std::exception&) { return nullptr; }
}
API jintArray JNICALL FN(pcSystemSaveFormat)(JNIEnv* env,jclass,jbyteArray path,jboolean analyze_executable) {
    try {
        const auto root=directory_path(env,path);
        if (env->ExceptionCheck() || root.empty() || root.find('\0')!=std::string::npos) return nullptr;
        const auto format=GameSession::pc_system_save_format(std::filesystem::u8path(root),analyze_executable==JNI_TRUE);
        return int_array(env,{format[0],format[1]});
    } catch (const std::exception& e) { fail(env,e.what());return nullptr; }
}
API jintArray JNICALL FN(currentPcSystemSaveFormat)(JNIEnv* env,jclass,jlong handle) {
    try {
        if (!handle) return nullptr;
        const auto format=player(handle).game->pc_system_save_format();
        return int_array(env,{format[0],format[1]});
    } catch (const std::exception& e) { fail(env,e.what());return nullptr; }
}
API jobjectArray JNICALL FN(saveLayoutConfig)(JNIEnv* env,jclass,jbyteArray path,jboolean regenerate) {
    try {
        const auto root=directory_path(env,path);
        if (env->ExceptionCheck() || root.empty() || root.find('\0')!=std::string::npos) return nullptr;
        const auto config=GameSession::pc_save_configuration(std::filesystem::u8path(root),regenerate==JNI_TRUE);
        if (!config) return nullptr;
        const auto type=env->FindClass("java/lang/Object");
        if (!type) return nullptr;
        auto result=env->NewObjectArray(2,type,nullptr);
        if (!result) return nullptr;
        array_text(env,result,0,config->file.u8string());
        if (env->ExceptionCheck()) return nullptr;
        std::vector<int> values{static_cast<int>(config->layer_records),static_cast<int>(config->message_width),
            static_cast<int>(config->status_bytes),config->system_save_version,config->executable_layout,config->okikae,
            config->system_save_source,config->executable_message_width,config->executable_status_bytes,
            config->executable_okikae,config->extension_start_code,config->extension_enabled};
        values.insert(values.end(),config->extension_sizes.begin(),config->extension_sizes.end());
        auto fields=int_array(env,values);
        if (fields) env->SetObjectArrayElement(result,1,fields);
        return result;
    } catch (const std::exception& e) { fail(env,e.what());return nullptr; }
}
API jbyteArray JNICALL FN(exportPcSave)(JNIEnv* env,jclass,jlong handle,jintArray picture,jint width,jint height) {
    try {
        auto& p=player(handle);
        std::vector<std::uint32_t> pixels;
        if (picture) {
            if (width<=0 || width>512 || height<=0 || height>512 || env->GetArrayLength(picture)!=width*height)
                throw std::invalid_argument("Invalid PC save thumbnail");
            pixels.resize(static_cast<std::size_t>(width)*static_cast<std::size_t>(height));
            env->GetIntArrayRegion(picture,0,width*height,reinterpret_cast<jint*>(pixels.data()));
            if (env->ExceptionCheck()) return nullptr;
        }
        const auto data=picture ? p.game->export_pc_save(pixels,width,height) : p.game->export_presented_pc_save();
        const auto size=static_cast<jsize>(data.size());
        auto result=env->NewByteArray(size);
        if (result) env->SetByteArrayRegion(result,0,size,reinterpret_cast<const jbyte*>(data.data()));
        return result;
    } catch (const std::exception& e) { fail(env,e.what());return nullptr; }
}
API jobjectArray JNICALL FN(inspectPcSave)(JNIEnv* env,jclass,jbyteArray raw,jint picture_budget) {
    try {
        const auto size=raw?env->GetArrayLength(raw):0;
        if (size<=0 || size>16*1024*1024) throw std::invalid_argument("Invalid PC save length");
        std::vector<nnn::Byte> data(static_cast<std::size_t>(size));
        env->GetByteArrayRegion(raw,0,size,reinterpret_cast<jbyte*>(data.data()));
        if (env->ExceptionCheck()) return nullptr;
        auto preview=GameSession::inspect_pc_save(data,static_cast<std::size_t>(std::max(0,picture_budget)));
        const auto type=env->FindClass("java/lang/Object");
        if (!type) return nullptr;
        auto result=env->NewObjectArray(4,type,nullptr);
        if (!result) return nullptr;
        std::vector<int> fields(preview.time.begin(),preview.time.end());
        fields.insert(fields.end(),{preview.artwork,preview.width,preview.height});
        auto metadata=int_array(env,fields);
        if (!metadata) return nullptr;
        env->SetObjectArrayElement(result,0,metadata);
        array_text(env,result,1,preview.summary);
        if (!preview.pixels.empty() && !env->ExceptionCheck()) {
            const auto count=static_cast<jsize>(preview.pixels.size());
            auto image=env->NewIntArray(count);
            if (!image) return nullptr;
            env->SetIntArrayRegion(image,0,count,reinterpret_cast<const jint*>(preview.pixels.data()));
            if (!env->ExceptionCheck()) env->SetObjectArrayElement(result,2,image);
        }
        if (!env->ExceptionCheck()) {
            auto vars=int_array(env,preview.vars);
            if (!vars) return nullptr;
            env->SetObjectArrayElement(result,3,vars);
        }
        return result;
    } catch (const std::exception& e) { fail(env,e.what());return nullptr; }
}
API jintArray JNICALL FN(pcSaveDateVariables)(JNIEnv* env,jclass,jbyteArray path) {
    try {
        const auto root=directory_path(env,path);
        if (env->ExceptionCheck() || root.empty() || root.find('\0')!=std::string::npos) return nullptr;
        return fixed_int_array(env,GameSession::pc_save_date_variables(std::filesystem::u8path(root)));
    } catch (const std::exception& e) { fail(env,e.what());return nullptr; }
}
API jintArray JNICALL FN(inspectPcSaveGameDate)(JNIEnv* env,jclass,jbyteArray raw,jint year_var,jint date_var) {
    try {
        const auto size=raw?env->GetArrayLength(raw):0;
        if (size<=0 || size>16*1024*1024) throw std::invalid_argument("Invalid PC save length");
        std::vector<nnn::Byte> data(static_cast<std::size_t>(size));
        env->GetByteArrayRegion(raw,0,size,reinterpret_cast<jbyte*>(data.data()));
        if (env->ExceptionCheck()) return nullptr;
        const auto info=GameSession::inspect_pc_save_game_date(data,year_var,date_var);
        if (!info.valid) return nullptr;
        return fixed_int_array(env,std::array<int,5>{{info.year,info.date,info.month,info.day,info.weekday}});
    } catch (const std::exception& e) { fail(env,e.what());return nullptr; }
}
API jint JNICALL FN(computeGameWeekday)(JNIEnv*,jclass,jint year,jint month,jint day) {
    return GameSession::game_weekday(year,month,day);
}
API jintArray JNICALL FN(sharedConfigSnapshot)(JNIEnv* env,jclass,jlong handle) {
    try {
        return fixed_int_array(env,player(handle).game->shared_config_snapshot().to_array());
    } catch (const std::exception& e) { fail(env,e.what());return nullptr; }
}
API jboolean JNICALL FN(inspectSaveSnapshot)(JNIEnv* env,jclass,jobject raw,jint length,jint wait,jint word,jint line,
                                            jobjectArray content) {
    try {
        if (!raw || !content || length<=0 || env->GetDirectBufferCapacity(raw)<length) return JNI_FALSE;
        const auto* bytes=static_cast<const std::uint8_t*>(env->GetDirectBufferAddress(raw));
        if (!bytes) return JNI_FALSE;
        const auto expected=read_text_fields(env,content,1024,1024*1024);
        if (env->ExceptionCheck() || expected.size()<2) return JNI_FALSE;
        nnn::snapshot::Reader in(bytes,static_cast<std::size_t>(length));
        GameSession::inspect_snapshot(in,[&](const emberfall::scene::FrameState& frame){
            const auto choice=frame.wait==emberfall::scene::Wait::Choice;
            return static_cast<jint>(frame.wait)==wait && static_cast<jint>(frame.word)==word &&
                static_cast<jint>(frame.dialogue.sequence)==line && frame.script==expected[0] &&
                frame.dialogue.display_bytes==expected[1] && (choice ? expected.size()>2 : expected.size()==2);
        });
        return JNI_TRUE;
    } catch (const std::exception&) { return JNI_FALSE; }
}
API jboolean JNICALL FN(restoreSnapshot)(JNIEnv* env,jclass,jlong handle,jbyteArray raw,jint wait,jint word,jint line,
                                        jobjectArray content,jintArray meta,jboolean keep_music_position,jboolean migrate_legacy,
                                        jboolean restart_music) {
    try {
        if (!raw || !content || !meta || env->GetArrayLength(meta)<7) return JNI_FALSE;
        const jsize length=env->GetArrayLength(raw),fields=env->GetArrayLength(content);
        if (length<=0 || static_cast<std::size_t>(length)>nnn::snapshot::max_bytes || fields<2 || fields>1024) return JNI_FALSE;
        nnn::snapshot::Bytes data(static_cast<std::size_t>(length));
        env->GetByteArrayRegion(raw,0,length,reinterpret_cast<jbyte*>(data.data()));
        if (env->ExceptionCheck()) return JNI_FALSE;
        std::vector<std::string> expected;
        jsize remaining=1024*1024;
        for (jsize i=0;i<fields;++i) {
            auto item=static_cast<jbyteArray>(env->GetObjectArrayElement(content,i));
            if (!item) return JNI_FALSE;
            const jsize n=env->GetArrayLength(item);
            if (n>remaining) { env->DeleteLocalRef(item);return JNI_FALSE; }
            remaining-=n;
            std::string value(static_cast<std::size_t>(n),'\0');
            if (n) env->GetByteArrayRegion(item,0,n,reinterpret_cast<jbyte*>(value.data()));
            env->DeleteLocalRef(item);
            if (env->ExceptionCheck()) return JNI_FALSE;
            expected.push_back(std::move(value));
        }
        auto& p=player(handle);
        p.game->restore_snapshot(data,[&](const emberfall::scene::FrameState& f){
            if (static_cast<jint>(f.wait)!=wait || static_cast<jint>(f.word)!=word || static_cast<jint>(f.dialogue.sequence)!=line ||
                f.script!=expected[0] || f.dialogue.display_bytes!=expected[1]) return false;
            if (!f.selection) return expected.size()==2;
            const auto& options=f.selection->options;
            return options.size()==expected.size()-2 && std::equal(options.begin(),options.end(),expected.begin()+2);
        },keep_music_position==JNI_TRUE,migrate_legacy==JNI_TRUE,restart_music==JNI_TRUE);
        p.published_screen_layer.reset();
        p.compositor.reset_present_state();
        p.repaint=true;
        frame_meta(env,p,meta);
        present(p);
        __android_log_print(ANDROID_LOG_INFO,"SystemNNN","Native snapshot restored line=%d word=%d",static_cast<int>(line),static_cast<int>(word));
        return JNI_TRUE;
    } catch (const std::exception& e) {
        __android_log_print(ANDROID_LOG_WARN,"SystemNNN","Native snapshot fallback: %s",e.what());return JNI_FALSE;
    }
}
API jobjectArray JNICALL FN(dialogue)(JNIEnv* env,jclass,jlong handle) {
    try {
        const auto& line=player(handle).game->state().dialogue;
        auto result=env->NewObjectArray(2,env->FindClass("[B"),nullptr);
        if (!result) return nullptr;
        array_text(env,result,0,line.speaker);
        if (!env->ExceptionCheck()) array_text(env,result,1,dialogue_body(line.display_bytes,line.speaker));
        return result;
    } catch (const std::exception& e) { fail(env,e.what()); return nullptr; }
}
API jobjectArray JNICALL FN(backlog)(JNIEnv* env,jclass,jlong handle) {
    try {
        const auto& history=player(handle).game->backlog();
        auto out=env->NewObjectArray(static_cast<jsize>(history.size()*7),env->FindClass("[B"),nullptr);
        if (!out) return nullptr;
        jsize index=0;
        for (const auto& line:history) {
            array_text(env,out,index++,line.speaker);
            if (env->ExceptionCheck()) return nullptr;
            array_text(env,out,index++,dialogue_body(line.text,line.speaker));
            for (const auto& voice:line.voices) {
                if (env->ExceptionCheck()) return nullptr;
                array_text(env,out,index++,voice);
            }
            if (env->ExceptionCheck()) return nullptr;
            std::string style;
            if (line.color>=0) {
                style.push_back(static_cast<char>(line.color>>16));
                style.push_back(static_cast<char>(line.color>>8));
                style.push_back(static_cast<char>(line.color));
                if (line.configuration_text) style.push_back(1);
            }
            array_text(env,out,index++,style);
        }
        return out;
    } catch (const std::exception& e) { fail(env,e.what()); return nullptr; }
}
API jlongArray JNICALL FN(backlogJumpIds)(JNIEnv* env,jclass,jlong handle) {
    const auto& history=player(handle).game->backlog();
    std::vector<jlong> ids;
    ids.reserve(history.size());
    for (const auto& line:history) ids.push_back(static_cast<jlong>(line.jump_id));
    const auto count=static_cast<jsize>(ids.size());
    auto out=env->NewLongArray(count);
    if (out && count) env->SetLongArrayRegion(out,0,count,ids.data());
    return out;
}
API void JNICALL FN(captureBacklogJump)(JNIEnv*,jclass,jlong handle,jint cursor) {
    try { player(handle).game->capture_backlog_jump(static_cast<unsigned>(std::max(0,cursor))); }
    catch (const std::exception& e) {
        __android_log_print(ANDROID_LOG_WARN,"SystemNNN","Backlog checkpoint skipped: %s",e.what());
    }
}
API jintArray JNICALL FN(backlogJump)(JNIEnv* env,jclass,jlong handle,jlong id) {
    const auto plan=player(handle).game->backlog_jump(static_cast<std::uint64_t>(id));
    if (!plan.journal_cursor) return nullptr;
    const jint fields[]={static_cast<jint>(plan.journal_cursor),static_cast<jint>(plan.keep_backlog),
        static_cast<jint>(plan.random_state),plan.mouse_x,plan.mouse_y};
    auto out=env->NewIntArray(5);
    if (out) env->SetIntArrayRegion(out,0,5,fields);
    return out;
}
API jboolean JNICALL FN(restoreBacklogJump)(JNIEnv* env,jclass,jlong handle,jlong id,jint cursor,jintArray meta) {
    if (!meta || env->GetArrayLength(meta)<7 || cursor<=0) return JNI_FALSE;
    auto& p=player(handle);
    try {
        if (!p.game->restore_backlog_jump(static_cast<std::uint64_t>(id),static_cast<unsigned>(cursor))) return JNI_FALSE;
        p.published_screen_layer.reset();
        p.compositor.reset_present_state();
    } catch (const std::exception& e) {
        __android_log_print(ANDROID_LOG_WARN,"SystemNNN","Backlog jump rejected: %s",e.what());
        return JNI_FALSE;
    }
    p.repaint=true;
    frame_meta(env,p,meta);
    present(p);
    return JNI_TRUE;
}
API void JNICALL FN(trimReplayedBacklog)(JNIEnv* env,jclass,jlong handle,jint keep) {
    try { player(handle).game->trim_replayed_backlog(static_cast<unsigned>(keep)); }
    catch (const std::exception& e) { fail(env,e.what()); }
}
API jintArray JNICALL FN(systemVariables)(JNIEnv* env,jclass,jlong handle) {
    const auto values=player(handle).game->runtime()->system_variables();
    auto out=env->NewIntArray(static_cast<jsize>(values.size()));
    if (out) env->SetIntArrayRegion(out,0,static_cast<jsize>(values.size()),values.data());
    return out;
}
API void JNICALL FN(seedLegacyReplayVariables)(JNIEnv*,jclass,jlong handle) {
    player(handle).game->seed_legacy_replay_variables();
}
API void JNICALL FN(setSystemVariables)(JNIEnv* env,jclass,jlong handle,jintArray input) {
    nnn::SystemVariables values{};
    if (!input || env->GetArrayLength(input)!=static_cast<jsize>(values.size())) {
        fail(env,"Invalid system variable count");return;
    }
    env->GetIntArrayRegion(input,0,static_cast<jsize>(values.size()),values.data());
    if (!env->ExceptionCheck()) player(handle).game->runtime()->set_system_variables(values);
}
API jint JNICALL FN(scriptRandomState)(JNIEnv*,jclass,jlong handle) {
    return static_cast<jint>(player(handle).game->script_random_state());
}
API void JNICALL FN(setScriptRandomState)(JNIEnv*,jclass,jlong handle,jint state) {
    player(handle).game->set_script_random_state(static_cast<std::uint32_t>(state));
}
API jintArray JNICALL FN(scriptProgress)(JNIEnv* env,jclass,jlong handle) {
    const auto values=player(handle).game->runtime()->script_progress();
    auto out=env->NewIntArray(static_cast<jsize>(values.size()));
    if (out) env->SetIntArrayRegion(out,0,static_cast<jsize>(values.size()),values.data());
    return out;
}
API void JNICALL FN(setScriptProgress)(JNIEnv* env,jclass,jlong handle,jintArray input) {
    nnn::ScriptProgress values{};
    if (!input || env->GetArrayLength(input)!=static_cast<jsize>(values.size())) {
        fail(env,"Invalid script progress count");return;
    }
    env->GetIntArrayRegion(input,0,static_cast<jsize>(values.size()),values.data());
    if (!env->ExceptionCheck()) player(handle).game->runtime()->set_script_progress(values);
}
API jobjectArray JNICALL FN(runtimeText)(JNIEnv* env,jclass,jlong handle) {
    try {
        const auto text=player(handle).game->runtime()->runtime_text();
        auto out=env->NewObjectArray(static_cast<jsize>(text.system_replacements.size()+2),env->FindClass("[B"),nullptr);
        if (!out) return nullptr;
        array_text(env,out,0,text.surname);
        if (!env->ExceptionCheck()) array_text(env,out,1,text.given_name);
        for (std::size_t i=0;i<text.system_replacements.size() && !env->ExceptionCheck();++i)
            array_text(env,out,static_cast<jsize>(i+2),text.system_replacements[i]);
        return out;
    } catch (const std::exception& e) { fail(env,e.what()); return nullptr; }
}
API jboolean JNICALL FN(restoreRuntimeText)(JNIEnv* env,jclass,jlong handle,jobjectArray fields) {
    try {
        nnn::RuntimeTextState text;
        auto values=read_text_fields(env,fields,static_cast<jsize>(text.system_replacements.size()+2),32);
        if (values.size()!=text.system_replacements.size()+2) return JNI_FALSE;
        text.surname=std::move(values[0]);
        text.given_name=std::move(values[1]);
        for (std::size_t i=0;i<text.system_replacements.size();++i)
            text.system_replacements[i]=std::move(values[i+2]);
        auto& game=*player(handle).game;
        if (!game.runtime()->restore_runtime_text(std::move(text),true)) return JNI_FALSE;
        game.sync_runtime_names();
        return JNI_TRUE;
    } catch (const std::exception& e) { fail(env,e.what()); return JNI_FALSE; }
}
API jintArray JNICALL FN(optionalProgress)(JNIEnv* env,jclass,jlong handle) {
    try {
        const auto state=player(handle).game->runtime()->optional_progress_view();
        const auto role_offset=state->terms.size()+state->voice_words.size();
        std::vector<jint> words(role_offset+state->voice_role_words.size());
        std::memcpy(words.data(),state->terms.data(),sizeof(state->terms));
        std::memcpy(words.data()+state->terms.size(),state->voice_words.data(),sizeof(state->voice_words));
        std::memcpy(words.data()+role_offset,state->voice_role_words.data(),sizeof(state->voice_role_words));
        return int_array(env,words);
    } catch (const std::exception& e) { fail(env,e.what()); return nullptr; }
}
API void JNICALL FN(restoreOptionalProgress)(JNIEnv* env,jclass,jlong handle,jintArray words) {
    try {
        nnn::RuntimeOptionalProgress state;
        const auto role_offset=state.terms.size()+state.voice_words.size();
        const auto legacy_count=static_cast<jsize>(role_offset);
        const auto count=words ? env->GetArrayLength(words) : 0;
        if (count!=legacy_count && count!=static_cast<jsize>(role_offset+state.voice_role_words.size()))
            throw std::invalid_argument("Invalid optional progress count");
        std::vector<jint> raw(static_cast<std::size_t>(count));
        env->GetIntArrayRegion(words,0,count,raw.data());
        if (env->ExceptionCheck()) return;
        std::memcpy(state.terms.data(),raw.data(),sizeof(state.terms));
        std::memcpy(state.voice_words.data(),raw.data()+state.terms.size(),sizeof(state.voice_words));
        const auto runtime=player(handle).game->runtime();
        state.voice_role_words=runtime->optional_progress_view()->voice_role_words;
        if (count!=legacy_count)
            for (std::size_t i=0;i<state.voice_role_words.size();++i)
                state.voice_role_words[i]|=static_cast<nnn::Word>(raw[role_offset+i]);
        runtime->restore_optional_progress(state);
    } catch (const std::exception& e) { fail(env,e.what()); }
}
API jboolean JNICALL FN(voiceRoleHeard)(JNIEnv* env,jclass,jlong handle,jint character) {
    try { return player(handle).game->runtime()->voice_role_heard(character) ? JNI_TRUE : JNI_FALSE; }
    catch (const std::exception& e) { fail(env,e.what()); return JNI_FALSE; }
}
API jbyteArray JNICALL FN(chartProgress)(JNIEnv* env,jclass,jlong handle) {
    try {
        const auto flags=player(handle).game->runtime()->chart_progress();
        if (!flags || flags->traversed.empty()) return env->NewByteArray(0);
        nnn::snapshot::Writer out;
        out(static_cast<std::uint32_t>(flags->seen.size()),static_cast<std::uint32_t>(flags->traversed.size()));
        out.raw(flags->seen.data(),flags->seen.size());out.raw(flags->traversed.data(),flags->traversed.size());
        const auto count=static_cast<jsize>(out.bytes.size());
        auto result=env->NewByteArray(count);
        if (result) env->SetByteArrayRegion(result,0,count,reinterpret_cast<const jbyte*>(out.bytes.data()));
        return result;
    } catch (const std::exception& e) { fail(env,e.what());return nullptr; }
}
API void JNICALL FN(restoreChartProgress)(JNIEnv* env,jclass,jlong handle,jbyteArray raw) {
    try {
        if (!raw) throw std::invalid_argument("Missing chart progress");
        const auto size=env->GetArrayLength(raw);
        if (size<0 || static_cast<std::size_t>(size)>2U*nnn::chart::max_progress_slots+8U)
            throw std::invalid_argument("Invalid chart progress length");
        if (size==0) return;
        nnn::snapshot::Bytes bytes(static_cast<std::size_t>(size));
        env->GetByteArrayRegion(raw,0,size,reinterpret_cast<jbyte*>(bytes.data()));
        if (env->ExceptionCheck()) return;
        nnn::snapshot::Reader in(bytes);
        const auto seen=in.size(nnn::chart::max_progress_slots),traversed=in.size(nnn::chart::max_progress_slots);
        if (seen+traversed!=in.remaining()) throw std::invalid_argument("Invalid chart progress blocks");
        nnn::chart::Progress flags;
        const auto* seen_data=in.raw(seen);
        const auto* traversed_data=in.raw(traversed);
        flags.seen.assign(seen_data,seen_data+seen);
        flags.traversed.assign(traversed_data,traversed_data+traversed);
        if (!player(handle).game->runtime()->set_chart_progress(std::move(flags)))
            throw std::invalid_argument("Chart progress was rejected");
    } catch (const std::exception& e) { fail(env,e.what()); }
}
API jint JNICALL FN(configMask)(JNIEnv*,jclass,jlong handle) {
    return player(handle).game->state().config_mask;
}
API jlong JNICALL FN(takeScriptUiEvents)(JNIEnv*,jclass,jlong handle) {
    auto& game=*player(handle).game;
    return (static_cast<jlong>(game.take_system_save_request())<<32U)|game.take_notice();
}
API jintArray JNICALL FN(placeStates)(JNIEnv* env,jclass,jlong handle) {
    try {
        const auto values=player(handle).game->places();
        const auto count=static_cast<jsize>(values.size());
        auto out=env->NewIntArray(count);
        if (out && count) env->SetIntArrayRegion(out,0,count,values.data());
        return out;
    } catch (const std::exception& e) { fail(env,e.what());return nullptr; }
}
API jint JNICALL FN(placeMode)(JNIEnv*,jclass,jlong handle) {
    return static_cast<jint>(player(handle).game->place_mode());
}
API jintArray JNICALL FN(placeCharacters)(JNIEnv* env,jclass,jlong handle) {
    return int_array(env,player(handle).game->place_characters());
}
API void JNICALL FN(replayBacklogVoice)(JNIEnv* env,jclass,jlong handle,jbyteArray name) {
    try {
        const auto voice=resource_name(env,name);
        const auto error=player(handle).game->audio().replay_backlog_voice(voice);
        if (!error.empty()) throw std::runtime_error(error);
        __android_log_print(ANDROID_LOG_INFO,"SystemNNN","Backlog voice replayed: %s",voice.c_str());
    } catch (const std::exception& e) { fail(env,e.what()); }
}
API void JNICALL FN(suspendScriptAudio)(JNIEnv* env,jclass,jlong handle) {
    try { player(handle).game->audio().suspend_script_audio(); }
    catch (const std::exception& e) { fail(env,e.what()); }
}
API void JNICALL FN(restartLoopScriptAudio)(JNIEnv* env,jclass,jlong handle) {
    try { player(handle).game->audio().restart_loop_script_audio(); }
    catch (const std::exception& e) { fail(env,e.what()); }
}
API void JNICALL FN(stopScriptAudio)(JNIEnv* env,jclass,jlong handle) {
    try { player(handle).game->audio().stop_script_audio(); }
    catch (const std::exception& e) { fail(env,e.what()); }
}
API jintArray JNICALL FN(saveThumbnail)(JNIEnv* env,jclass,jlong handle,jint width,jint height) {
    try {
        auto& p=player(handle);
        const bool ready=p.title || sync_story_plane(p);
        const auto& source=p.title && !p.static_title.rgb.empty()?p.static_title:
            ready?p.game->surface():p.game->presented_surface();
        if (source.rgb.empty()) return nullptr;
        p.repaint=true;
        return emberfall::android::frame_pixels(env,source,width,height);
    } catch (const std::exception& e) { fail(env,e.what()); return nullptr; }
}
API jintArray JNICALL FN(saveSceneThumbnail)(JNIEnv* env,jclass,jlong handle,jint width,jint height) {
    try {
        auto& p=player(handle);
        const auto& source=p.title && !p.static_title.rgb.empty()?p.static_title:p.game->scene_surface();
        p.repaint=true;
        return emberfall::android::frame_pixels(env,source,width,height);
    } catch (const std::exception& e) { fail(env,e.what()); return nullptr; }
}
API jint JNICALL FN(saveSlotArt)(JNIEnv* env,jclass,jlong handle) {
    try { return player(handle).game->save_slot_art(); }
    catch (const std::exception& e) { fail(env,e.what()); return 0; }
}
API jlong JNICALL FN(takeScreenLayer)(JNIEnv* env,jclass,jlong handle) {
    try {
        auto& p=player(handle);
        if (p.compositor.mailbox()->attached() || !has_surface(p) || !draws_story_ui(p) || !p.game->screen_layer_active()) {
            const bool clear=static_cast<bool>(p.screen_layer);
            p.screen_layer.reset();
            p.published_screen_layer.reset();
            return clear ? -1 : 0;
        }
        p.game->capture_screen_layer(true);
        auto frame=p.game->take_screen_layer();
        const auto revision=p.game->screen_layer_revision();
        if (!frame && p.screen_layer && (!p.published_screen_layer || p.screen_layer_revision!=revision)) {
            p.screen_layer.reset();
            p.published_screen_layer.reset();
            return -1;
        }
        const auto stamp=story_stamp(p);
        if (!frame && (!p.screen_layer || p.screen_layer_state==stamp)) return 0;
        if (frame) p.screen_layer=std::move(frame);
        p.screen_layer_revision=revision;
        p.screen_layer_state=stamp;
        p.published_screen_layer=std::make_shared<StoryScreenLayer>(p.screen_layer,stamp);
        auto result=std::make_unique<PublishedScreenLayer>(p.published_screen_layer);
        return reinterpret_cast<jlong>(result.release());
    } catch (const std::exception& e) { fail(env,e.what()); return 0; }
}
API jboolean JNICALL FN(compositeScreenLayer)(JNIEnv* env,jclass,jlong handle,jobject bitmap,jint wait,jint word,jint line,
                                             jboolean show_backdrop,jboolean record) {
    try {
        if (!handle) return false;
        const auto& screen=*reinterpret_cast<const PublishedScreenLayer*>(handle);
        if (screen->state!=std::array<jint,3>{wait,word,line}) return false;
        emberfall::android::LockedBitmap image(env,bitmap);
        if (image.info.width!=screen->frame->width() || image.info.height!=screen->frame->height()) return false;
        screen->frame->composite(image.row(0),image.info.stride,show_backdrop);
        if (record && screen->frame->captures_screen()) {
            auto sample=screen->frame->composed();
            {
                std::lock_guard lock(screen->mutex);
                screen->sample=std::move(sample);
            }
            screen->ready.notify_one();
        }
        return true;
    } catch (const std::exception& e) { fail(env,e.what()); return false; }
}
API void JNICALL FN(releaseScreenLayer)(JNIEnv*,jclass,jlong handle) {
    delete reinterpret_cast<PublishedScreenLayer*>(handle);
}
API jlong JNICALL FN(messagePlaneToken)(JNIEnv* env,jclass,jlong handle) {
    try {
        auto& p=player(handle);
        auto mailbox=p.compositor.mailbox();
        mailbox->set_attached(true);
        p.screen_layer.reset();
        p.published_screen_layer.reset();
        p.repaint=true;
        return reinterpret_cast<jlong>(new PlaneMailboxRef(std::move(mailbox)));
    } catch (const std::exception& e) { fail(env,e.what()); return 0; }
}
API void JNICALL FN(pushStoryPlanes)(JNIEnv* env,jclass,jlong token,jobject bitmap,jobject floating,jintArray key,jboolean show_backdrop) {
    try {
        if (!token) return;
        const auto& mailbox=*reinterpret_cast<const PlaneMailboxRef*>(token);
        if (!mailbox || !mailbox->attached()) return;
        if (!key) throw std::invalid_argument("Missing message plane key");
        const auto key_len=env->GetArrayLength(key);
        if (key_len<5 || key_len>6) throw std::invalid_argument("Invalid message plane key length");
        std::array<jint,6> raw_key{};
        env->GetIntArrayRegion(key,0,key_len,raw_key.data());
        if (env->ExceptionCheck()) return;
        emberfall::scene::PlaneKey plane_key{};
        for (std::size_t i=0;i<plane_key.size();++i)
            plane_key[i]=static_cast<std::int32_t>(raw_key[i]);
        if (bitmap) {
            emberfall::android::LockedBitmap image(env,bitmap);
            if (floating) {
                emberfall::android::LockedBitmap decorations(env,floating);
                if (decorations.info.width!=image.info.width || decorations.info.height!=image.info.height)
                    throw std::invalid_argument("Floating plane dimensions differ from message plane");
                mailbox->push(image.row(0),static_cast<std::size_t>(image.info.stride),
                    image.info.width,image.info.height,plane_key,show_backdrop==JNI_TRUE,
                    decorations.row(0),static_cast<std::size_t>(decorations.info.stride));
            } else {
                mailbox->push(image.row(0),static_cast<std::size_t>(image.info.stride),
                    image.info.width,image.info.height,plane_key,show_backdrop==JNI_TRUE);
            }
        } else {
            mailbox->push(nullptr,0,0,0,plane_key,show_backdrop==JNI_TRUE);
        }
    } catch (const std::exception& e) { fail(env,e.what()); }
}
API void JNICALL FN(pushMessagePlane)(JNIEnv* env,jclass type,jlong token,jobject bitmap,jintArray key,jboolean show_backdrop) {
    FN(pushStoryPlanes)(env,type,token,bitmap,nullptr,key,show_backdrop);
}
API void JNICALL FN(releaseMessagePlane)(JNIEnv*,jclass,jlong token) {
    std::unique_ptr<PlaneMailboxRef> mailbox(reinterpret_cast<PlaneMailboxRef*>(token));
    if (mailbox && *mailbox) (*mailbox)->set_attached(false);
}
API jintArray JNICALL FN(expectedPlaneKey)(JNIEnv* env,jclass,jlong handle) {
    try {
        auto& p=player(handle);
        if (p.title) return fixed_int_array(env,emberfall::scene::PlaneKey{});
        return fixed_int_array(env,p.game->expected_plane_key());
    } catch (const std::exception& e) { fail(env,e.what()); return nullptr; }
}
API jintArray JNICALL FN(presentedPlaneKey)(JNIEnv* env,jclass,jlong handle) {
    try {
        return fixed_int_array(env,player(handle).compositor.presented_plane_key());
    } catch (const std::exception& e) { fail(env,e.what()); return nullptr; }
}
API void JNICALL FN(setVolumes)(JNIEnv* env,jclass,jlong handle,jint music,jint sound,jint voice,jint effect_voice) {
    try {
        auto& game=*player(handle).game;
        game.update_system_parameters({{nnn::NNNPARAM_MUSICVOLUME,music},{nnn::NNNPARAM_SOUNDVOLUME,sound},
            {nnn::NNNPARAM_SCRIPTSEVOLUME,sound},{nnn::NNNPARAM_VOICEVOLUME,voice},{nnn::NNNPARAM_SOUNDVOICEVOLUME,effect_voice}});
        auto& audio=game.audio();
        const auto state=audio.state();
        __android_log_print(ANDROID_LOG_INFO,"SystemNNN",
            "volumes: input music=%d sound=%d voice=%d effectVoice=%d; bgm=%d/%d voices=%d/%d,%d/%d,%d/%d,%d/%d (volume/active)",
            static_cast<int>(music),static_cast<int>(sound),static_cast<int>(voice),static_cast<int>(effect_voice),
            state.bgm.volume,static_cast<int>(state.bgm.active),
            state.voices[0].volume,static_cast<int>(state.voices[0].active),
            state.voices[1].volume,static_cast<int>(state.voices[1].active),
            state.voices[2].volume,static_cast<int>(state.voices[2].active),
            state.voices[3].volume,static_cast<int>(state.voices[3].active));
    }
    catch (const std::exception& e) { fail(env,e.what()); }
}
API jintArray JNICALL FN(dialogueProperties)(JNIEnv* env,jclass,jlong handle) {
    try {
        const auto& game=*player(handle).game;
        const auto& line=game.state().dialogue;
        const auto id=line.message_id;
        const std::array<jint,8> values{{id<=static_cast<nnn::Word>(INT32_MAX) ? static_cast<jint>(id) : -1-static_cast<jint>(~id),
            line.window_type,line.font_type,line.window_visible ? 1 : 0,game.options_enabled() ? 1 : 0,
            static_cast<jint>(line.command),static_cast<jint>(line.layout_command),static_cast<jint>(game.draw_message())}};
        auto result=env->NewIntArray(static_cast<jsize>(values.size()));
        if (result) env->SetIntArrayRegion(result,0,static_cast<jsize>(values.size()),values.data());
        return result;
    } catch (const std::exception& e) { fail(env,e.what()); return nullptr; }
}
API void JNICALL FN(setParameters)(JNIEnv* env,jclass,jlong handle,jintArray pairs) {
    try {
        if (!pairs) throw std::invalid_argument("Missing runtime parameters");
        const auto count=env->GetArrayLength(pairs);
        if (count%2!=0 || count>1144) throw std::invalid_argument("Invalid runtime parameter batch");
        std::vector<jint> input(static_cast<std::size_t>(count));
        if (count) env->GetIntArrayRegion(pairs,0,count,input.data());
        if (env->ExceptionCheck()) return;
        std::vector<std::pair<nnn::Word,nnn::Value>> values;
        values.reserve(input.size()/2);
        for (std::size_t i=0;i<input.size();i+=2) {
            if (input[i]<0) throw std::invalid_argument("Invalid runtime parameter identity");
            values.emplace_back(static_cast<nnn::Word>(input[i]),input[i+1]);
        }
        if (!player(handle).game->update_system_parameters(values)) throw std::invalid_argument("Unsupported runtime parameter identity");
    } catch (const std::exception& e) { fail(env,e.what()); }
}
API jintArray JNICALL FN(parameters)(JNIEnv* env,jclass,jlong handle) {
    try {
        const auto state=player(handle).game->snapshot_parameters();
        std::vector<jint> values;
        for (nnn::Word id=0;id<nnn::NNNPARAM_CHARAVOICEVOLUME+nnn::runtime_character_volume_count;++id) {
            nnn::Value value=0;
            if (state.get(id,value)) { values.push_back(static_cast<jint>(id)); values.push_back(value); }
        }
        auto result=env->NewIntArray(static_cast<jsize>(values.size()));
        if (result) env->SetIntArrayRegion(result,0,static_cast<jsize>(values.size()),values.data());
        return result;
    } catch (const std::exception& e) { fail(env,e.what()); return nullptr; }
}
API jintArray JNICALL FN(displayParameters)(JNIEnv* env,jclass,jlong handle) {
    try {
        const auto& game=*player(handle).game;
        std::vector<jint> values;
        for (nnn::Word id=0;id<nnn::NNNPARAM_CHARAVOICEVOLUME+nnn::runtime_character_volume_count;++id) {
            if (!nnn::RuntimeParameters::contains(id)) continue;
            values.push_back(static_cast<jint>(id));
            values.push_back(id==nnn::NNNPARAM_AUTOSPEEDSLIDER ? game.settings_auto_speed() : game.get_system_parameter(id));
        }
        auto result=env->NewIntArray(static_cast<jsize>(values.size()));
        if (result) env->SetIntArrayRegion(result,0,static_cast<jsize>(values.size()),values.data());
        return result;
    } catch (const std::exception& e) { fail(env,e.what()); return nullptr; }
}
API jintArray JNICALL FN(auxiliaryState)(JNIEnv* env,jclass,jlong handle) {
    try {
        const auto context=player(handle).game->runtime();
        std::array<jint,36> values{};
        values[0]=context->system_voice_selection(); values[1]=context->message_window();
        const auto flags=context->voice_off();
        for (std::size_t i=0;i<flags.size();++i)
            values[i+2]=flags[i]<=static_cast<nnn::Word>(INT32_MAX) ? static_cast<jint>(flags[i]) : -1-static_cast<jint>(~flags[i]);
        const auto cursors=context->cursor_variants();
        values[34]=cursors[0];values[35]=cursors[1];
        auto result=env->NewIntArray(static_cast<jsize>(values.size()));
        if (result) env->SetIntArrayRegion(result,0,static_cast<jsize>(values.size()),values.data());
        return result;
    } catch (const std::exception& e) { fail(env,e.what()); return nullptr; }
}
API void JNICALL FN(setPcDisplayOptions)(JNIEnv* env,jclass,jlong handle,jboolean no_skip_frame,jint mouse) {
    try { player(handle).game->runtime()->set_pc_display_options(no_skip_frame==JNI_TRUE,mouse); }
    catch (const std::exception& e) { fail(env,e.what()); }
}
API jboolean JNICALL FN(legacyVolumeDefaults)(JNIEnv* env,jclass,jlong handle) {
    try { return player(handle).game->runtime()->legacy_volume_defaults() ? JNI_TRUE : JNI_FALSE; }
    catch (const std::exception& e) { fail(env,e.what()); return JNI_FALSE; }
}
API jboolean JNICALL FN(usesXAudio2)(JNIEnv* env,jclass,jlong handle) {
    try { return player(handle).game->runtime()->routing().use_xaudio2 ? JNI_TRUE : JNI_FALSE; }
    catch (const std::exception& e) { fail(env,e.what()); return JNI_FALSE; }
}
API void JNICALL FN(setAuxiliaryState)(JNIEnv* env,jclass,jlong handle,jintArray pairs) {
    try {
        if (!pairs) throw std::invalid_argument("Missing auxiliary state");
        const auto count=env->GetArrayLength(pairs);
        if (count%2!=0 || count>68) throw std::invalid_argument("Invalid auxiliary state batch");
        std::vector<jint> input(static_cast<std::size_t>(count));
        if (count) env->GetIntArrayRegion(pairs,0,count,input.data());
        if (env->ExceptionCheck()) return;
        auto& game=*player(handle).game; const auto context=game.runtime();
        auto theme=context->system_voice_selection(),window=context->message_window(); auto flags=context->voice_off();
        bool theme_changed=false,window_changed=false,flags_changed=false;
        for (std::size_t i=0;i<input.size();i+=2) {
            const auto index=input[i],value=input[i+1];
            if (index<0 || index>=34) throw std::invalid_argument("Invalid auxiliary state index");
            if (index==0) {
                if (value<0 || static_cast<unsigned>(value)>=game.audio().system_voice_selection_count())
                    throw std::invalid_argument("System voice theme unavailable");
                theme=value; theme_changed=true;
            } else if (index==1) { window=value; window_changed=true; }
            else { flags[static_cast<std::size_t>(index-2)]=static_cast<nnn::Word>(value); flags_changed=true; }
        }
        // No owner is changed until every supplied DTO index/value is checked.
        if (theme_changed) context->set_system_voice_selection(theme);
        if (window_changed) context->set_message_window(window);
        if (flags_changed) context->set_voice_off(flags);
        game.apply_runtime_parameters();
    } catch (const std::exception& e) { fail(env,e.what()); }
}
API jbyteArray JNICALL FN(appendPrefix)(JNIEnv* env,jclass,jlong handle) {
    return bytes(env,player(handle).game->append_prefix());
}
API void JNICALL FN(setTextLength)(JNIEnv* env,jclass,jlong handle,jint length,jint visible) {
    try {
        if (length<0) throw std::invalid_argument("Invalid logical text length");
        const auto prefix=visible<0 ? std::nullopt : std::optional{static_cast<std::uint32_t>(visible)};
        player(handle).game->set_text_length(static_cast<std::uint32_t>(length),prefix);
    } catch (const std::exception& e) { fail(env,e.what()); }
}
API jintArray JNICALL FN(textState)(JNIEnv* env,jclass,jlong handle) {
    try {
        const auto& game=*player(handle).game;
        const std::array<jint,5> values{{static_cast<jint>(std::min<std::uint32_t>(game.visible_text(),INT32_MAX)),
            static_cast<jint>(std::min<std::uint32_t>(game.text_length(),INT32_MAX)),game.text_complete() ? 1 : 0,
            game.state().dialogue.effect_type,static_cast<jint>(game.text_fraction())}};
        auto result=env->NewIntArray(5);
        if (result) env->SetIntArrayRegion(result,0,5,values.data());
        return result;
    } catch (const std::exception& e) { fail(env,e.what()); return nullptr; }
}
API jintArray JNICALL FN(settingsTextRates)(JNIEnv* env,jclass,jlong handle) {
    try {
        const auto& game=*player(handle).game;
        std::array<jint,10> rates{};
        for (int i=0;i<5;++i) {
            rates[i]=static_cast<jint>(game.settings_text_rate(i,false));
            rates[i+5]=static_cast<jint>(game.settings_text_rate(i,true));
        }
        auto result=env->NewIntArray(static_cast<jsize>(rates.size()));
        if (result) env->SetIntArrayRegion(result,0,static_cast<jsize>(rates.size()),rates.data());
        return result;
    } catch (const std::exception& e) { fail(env,e.what()); return nullptr; }
}
API jboolean JNICALL FN(requestAdvance)(JNIEnv* env,jclass,jlong handle) {
    try { return player(handle).game->request_advance() ? JNI_TRUE : JNI_FALSE; }
    catch (const std::exception& e) { fail(env,e.what()); return JNI_FALSE; }
}
API jboolean JNICALL FN(requestFilmSkip)(JNIEnv* env,jclass,jlong handle) {
    try { return player(handle).game->request_film_skip() ? JNI_TRUE : JNI_FALSE; }
    catch (const std::exception& e) { fail(env,e.what()); return JNI_FALSE; }
}
API jboolean JNICALL FN(completeMiniGame)(JNIEnv* env,jclass,jlong handle,jint number) {
    try { return number>=0 && number<64 && player(handle).game->complete_minigame(static_cast<unsigned>(number)); }
    catch (const std::exception& e) { fail(env,e.what()); return JNI_FALSE; }
}
API jint JNICALL FN(textCharacters)(JNIEnv* env,jclass,jlong handle,jint elapsed) {
    try {
        if (elapsed<0) throw std::invalid_argument("Invalid text elapsed time");
        const auto count=player(handle).game->text_characters(static_cast<std::uint32_t>(elapsed));
        return static_cast<jint>(std::min<std::uint32_t>(count,INT32_MAX));
    } catch (const std::exception& e) { fail(env,e.what()); return 0; }
}
API jboolean JNICALL FN(autoReady)(JNIEnv* env,jclass,jlong handle,jint elapsed,jboolean complete) {
    try {
        if (elapsed<0) throw std::invalid_argument("Invalid Auto elapsed time");
        return player(handle).game->auto_ready(static_cast<std::uint32_t>(elapsed),complete==JNI_TRUE) ? JNI_TRUE : JNI_FALSE;
    } catch (const std::exception& e) { fail(env,e.what()); return JNI_FALSE; }
}
API void JNICALL FN(playSystemSe)(JNIEnv* env,jclass,jlong handle,jint id) {
    try {
        if (id<0) throw std::invalid_argument("Invalid system SE number");
        const auto error=player(handle).game->audio().play_system_se(static_cast<unsigned>(id));
        if (error.empty()) __android_log_print(ANDROID_LOG_INFO,"SystemNNN","UISE id=%d",static_cast<int>(id));
        if (!error.empty()) __android_log_print(ANDROID_LOG_WARN,"SystemNNN","system SE: %s",error.c_str());
    } catch (const std::exception& e) { fail(env,e.what()); }
}
API void JNICALL FN(playButtonVoice)(JNIEnv* env,jclass,jlong handle,jint id) {
    try {
        if (id<0) throw std::invalid_argument("Invalid button voice number");
        const auto error=player(handle).game->audio().play_button_voice(static_cast<unsigned>(id));
        if (!error.empty()) __android_log_print(ANDROID_LOG_WARN,"SystemNNN","button voice: %s",error.c_str());
    } catch (const std::exception& e) { fail(env,e.what()); }
}
API void JNICALL FN(playSettingsClick)(JNIEnv* env,jclass,jlong handle,jint se,jint voice,jint volume_type,
                                      jboolean ignore_voice_switch,jboolean ignore_character_mute) {
    try {
        auto& audio=player(handle).game->audio();
        if (se>=0) {
            const auto error=audio.play_system_se(static_cast<unsigned>(se),0,volume_type!=0);
            if (!error.empty()) __android_log_print(ANDROID_LOG_WARN,"SystemNNN","Settings SE: %s",error.c_str());
        }
        if (voice>=0) {
            const auto error=audio.play_button_voice(static_cast<unsigned>(voice),ignore_voice_switch==JNI_TRUE,
                ignore_character_mute==JNI_TRUE);
            if (!error.empty()) __android_log_print(ANDROID_LOG_WARN,"SystemNNN","Settings voice: %s",error.c_str());
        }
    } catch (const std::exception& e) { fail(env,e.what()); }
}
API void JNICALL FN(playConfigFeedback)(JNIEnv* env,jclass,jlong handle,jint slider,jint position,jint steps) {
    try {
        const auto error=player(handle).game->audio().play_config_feedback(static_cast<unsigned>(slider),position,steps);
        if (!error.empty()) __android_log_print(ANDROID_LOG_WARN,"SystemNNN","Settings slider: %s",error.c_str());
    } catch (const std::exception& e) { fail(env,e.what()); }
}
API void JNICALL FN(setSkip)(JNIEnv* env,jclass,jlong handle,jboolean skip) {
    try {
        auto& game=*player(handle).game; const bool enabled=skip==JNI_TRUE;
        if (game.skip_active()!=enabled) __android_log_print(ANDROID_LOG_INFO,"SystemNNN","skip: %s",enabled ? "on" : "off");
        game.set_skip_active(enabled);
    } catch (const std::exception& e) { fail(env,e.what()); }
}
API jboolean JNICALL FN(skipActive)(JNIEnv* env,jclass,jlong handle) {
    try { return player(handle).game->skip_active() ? JNI_TRUE : JNI_FALSE; }
    catch (const std::exception& e) { fail(env,e.what()); return JNI_FALSE; }
}
API jint JNICALL FN(storyModes)(JNIEnv* env,jclass,jlong handle) {
    try {
        const auto& game=*player(handle).game;
        return (game.get_system_parameter(nnn::NNNPARAM_AUTOMODE)!=0 ? 1 : 0) | (game.skip_active() ? 2 : 0);
    } catch (const std::exception& e) { fail(env,e.what()); return 0; }
}
API void JNICALL FN(setCtrlSkip)(JNIEnv* env,jclass,jlong handle,jboolean enabled) {
    try { player(handle).game->set_ctrl_skip(enabled==JNI_TRUE); }
    catch (const std::exception& e) { fail(env,e.what()); }
}
API jboolean JNICALL FN(skipEffective)(JNIEnv* env,jclass,jlong handle) {
    try { return player(handle).game->skip_effective() ? JNI_TRUE : JNI_FALSE; }
    catch (const std::exception& e) { fail(env,e.what()); return JNI_FALSE; }
}
API jintArray JNICALL FN(readState)(JNIEnv* env,jclass,jlong handle) {
    try {
        const auto ids=player(handle).game->runtime()->snapshot_read();
        std::vector<jint> values; values.reserve(ids.size());
        for (const auto id:ids) values.push_back(id<=static_cast<nnn::Word>(INT32_MAX) ? static_cast<jint>(id) : -1-static_cast<jint>(~id));
        auto result=env->NewIntArray(static_cast<jsize>(values.size()));
        if (result && !values.empty()) env->SetIntArrayRegion(result,0,static_cast<jsize>(values.size()),values.data());
        return result;
    } catch (const std::exception& e) { fail(env,e.what()); return nullptr; }
}
API void JNICALL FN(restoreReadState)(JNIEnv* env,jclass,jlong handle,jintArray raw) {
    try {
        if (!raw) throw std::invalid_argument("Missing read state");
        const auto count=env->GetArrayLength(raw);
        if (static_cast<std::size_t>(count)>nnn::RuntimeContext::max_read_messages)
            throw std::invalid_argument("Read state exceeds limit");
        std::vector<jint> values(static_cast<std::size_t>(count));
        if (count) env->GetIntArrayRegion(raw,0,count,values.data());
        if (env->ExceptionCheck()) return;
        std::vector<nnn::Word> ids; ids.reserve(values.size());
        for (const auto value:values) ids.push_back(static_cast<nnn::Word>(value));
        if (!player(handle).game->runtime()->restore_read(ids)) throw std::invalid_argument("Invalid read state");
    } catch (const std::exception& e) { fail(env,e.what()); }
}
API jboolean JNICALL FN(markRead)(JNIEnv* env,jclass,jlong handle,jint id) {
    try { return player(handle).game->runtime()->mark_read(static_cast<nnn::Word>(id)) ? JNI_TRUE : JNI_FALSE; }
    catch (const std::exception& e) { fail(env,e.what()); return JNI_FALSE; }
}
API jboolean JNICALL FN(isRead)(JNIEnv* env,jclass,jlong handle,jint id) {
    try { return player(handle).game->runtime()->is_read(static_cast<nnn::Word>(id)) ? JNI_TRUE : JNI_FALSE; }
    catch (const std::exception& e) { fail(env,e.what()); return JNI_FALSE; }
}
API jlong JNICALL FN(galleryRevision)(JNIEnv* env,jclass,jlong handle) {
    try { return static_cast<jlong>(player(handle).game->runtime()->gallery_revision()); }
    catch (const std::exception& e) { fail(env,e.what()); return 0; }
}
API jint JNICALL FN(movieNumber)(JNIEnv*,jclass,jlong handle) {
    return static_cast<jint>(player(handle).game->state().movie);
}
API jint JNICALL FN(movieVolume)(JNIEnv* env,jclass,jlong handle) {
    try {
        auto& game=*player(handle).game;
        return game.get_system_parameter(nnn::NNNPARAM_MOVIESWITCH)
            ? game.get_system_parameter(nnn::NNNPARAM_MOVIEVOLUME) : 0;
    } catch (const std::exception& e) { fail(env,e.what()); return 0; }
}
API jfloat JNICALL FN(movieGain)(JNIEnv* env,jclass,jlong handle) {
    try { return player(handle).game->audio().movie_gain(); }
    catch (const std::exception& e) { fail(env,e.what()); return 0; }
}
API jintArray JNICALL FN(choiceInfo)(JNIEnv* env,jclass,jlong handle) {
    const auto& game=*player(handle).game;
    const auto& choice=game.state().selection;
    if (!choice) return nullptr;
    const jint values[]={static_cast<jint>(game.state().choice_limit_ms),choice->auto_select,choice->special,
        static_cast<jint>(game.runtime()->selected_choices(choice->serial)),static_cast<jint>(game.state().choice_remaining_ms)};
    auto out=env->NewIntArray(5);
    if (out) env->SetIntArrayRegion(out,0,5,values);
    return out;
}
API jboolean JNICALL FN(choiceTimedOut)(JNIEnv*,jclass,jlong handle) {
    return player(handle).game->state().choice_timed_out?JNI_TRUE:JNI_FALSE;
}
API jobjectArray JNICALL FN(choiceMessages)(JNIEnv* env,jclass,jlong handle) {
    const auto& choice=player(handle).game->state().selection;
    const auto count=choice?choice->messages.size():0;
    auto out=env->NewObjectArray(static_cast<jsize>(count),env->FindClass("[B"),nullptr);
    if (!out) return nullptr;
    for (std::size_t i=0;i<count && !env->ExceptionCheck();++i)
        array_text(env,out,static_cast<jsize>(i),choice->messages[i]);
    return out;
}
API jintArray JNICALL FN(galleryState)(JNIEnv* env,jclass,jlong handle) {
    try {
        const auto ids=player(handle).game->runtime()->snapshot_gallery();
        const std::vector<jint> values(ids.begin(),ids.end());
        auto out=env->NewIntArray(static_cast<jsize>(values.size()));
        if (out && !values.empty()) env->SetIntArrayRegion(out,0,static_cast<jsize>(values.size()),values.data());
        return out;
    } catch (const std::exception& e) { fail(env,e.what()); return nullptr; }
}
static void update_gallery(JNIEnv* env,jlong handle,jintArray keys,bool replace) {
    try {
        if (!keys) throw std::invalid_argument("Missing gallery progress");
        const auto count=env->GetArrayLength(keys);
        if (static_cast<std::size_t>(count)>nnn::RuntimeContext::max_gallery_unlocks)
            throw std::invalid_argument("Gallery progress exceeds limit");
        std::vector<jint> input(static_cast<std::size_t>(count));
        if (count) env->GetIntArrayRegion(keys,0,count,input.data());
        if (env->ExceptionCheck()) return;
        const std::vector<nnn::Word> values(input.begin(),input.end());
        const auto context=player(handle).game->runtime();
        if (!(replace ? context->replace_gallery(values) : context->merge_gallery(values)))
            throw std::invalid_argument("Invalid gallery progress");
    } catch (const std::exception& e) { fail(env,e.what()); }
}
API void JNICALL FN(mergeGallery)(JNIEnv* env,jclass,jlong handle,jintArray keys) { update_gallery(env,handle,keys,false); }
API void JNICALL FN(replaceGallery)(JNIEnv* env,jclass,jlong handle,jintArray keys) { update_gallery(env,handle,keys,true); }
API jint JNICALL FN(musicNumber)(JNIEnv* env,jclass,jlong handle,jbyteArray name) {
    try { return player(handle).game->audio().music_number(resource_name(env,name)); }
    catch (const std::exception& e) { fail(env,e.what()); return 0; }
}
API jint JNICALL FN(galleryVariable)(JNIEnv* env,jclass,jlong handle,jbyteArray name) {
    try { return player(handle).game->named_variable(resource_name(env,name)); }
    catch (const std::exception& e) { fail(env,e.what()); return 0; }
}
API jint JNICALL FN(menuVariable)(JNIEnv* env,jclass,jlong handle,jbyteArray name) {
    try {auto& p=player(handle);return p.game->named_variable(resource_name(env,name),p.title);}
    catch (const std::exception& e) {fail(env,e.what());return 0;}
}
API jint JNICALL FN(openingVariable)(JNIEnv* env,jclass,jlong handle,jbyteArray name) {
    try {return player(handle).game->named_variable(resource_name(env,name),false,-1);}
    catch (const std::exception& e) {fail(env,e.what());return -1;}
}
API jboolean JNICALL FN(setStoryVariable)(JNIEnv* env,jclass,jlong handle,jbyteArray name,jint value) {
    try {return player(handle).game->set_story_variable(resource_name(env,name),value) ? JNI_TRUE : JNI_FALSE;}
    catch (const std::exception& e) {fail(env,e.what());return JNI_FALSE;}
}
API void JNICALL FN(inheritMenuMusic)(JNIEnv* env,jclass,jlong handle,jlong previous) {
    try {
        auto& target=*player(handle).game;
        const auto& source=*player(previous).game;
        if (target.runtime()->root()!=source.runtime()->root())
            throw std::invalid_argument("Cannot inherit music from another game directory");
        target.audio().inherit_music(source.audio());
    } catch (const std::exception& e) { fail(env,e.what()); }
}
API void JNICALL FN(menuMusic)(JNIEnv* env,jclass,jlong handle,jint number,jint plays,jint volume,jint fade,jboolean restart) {
    try {
        const auto error=player(handle).game->audio().play_menu_music(number,plays,volume,fade,restart==JNI_TRUE);
        if (!error.empty()) throw std::runtime_error(error);
    } catch (const std::exception& e) { fail(env,e.what()); }
}
API void JNICALL FN(playGalleryMusic)(JNIEnv* env,jclass,jlong handle,jbyteArray name,jint plays,jint volume) {
    try { player(handle).game->audio().play_music_preview(resource_name(env,name),plays,volume); }
    catch (const std::exception& e) { fail(env,e.what()); }
}
API void JNICALL FN(stopGalleryMusic)(JNIEnv* env,jclass,jlong handle) {
    try { player(handle).game->audio().stop_music_preview(); }
    catch (const std::exception& e) { fail(env,e.what()); }
}
API void JNICALL FN(pauseGalleryMusic)(JNIEnv* env,jclass,jlong handle,jboolean paused) {
    try { player(handle).game->audio().pause_music_preview(paused==JNI_TRUE); }
    catch (const std::exception& e) { fail(env,e.what()); }
}
API void JNICALL FN(galleryMusicVolume)(JNIEnv* env,jclass,jlong handle,jint volume) {
    try { player(handle).game->audio().set_music_preview_volume(volume); }
    catch (const std::exception& e) { fail(env,e.what()); }
}
API jboolean JNICALL FN(musicWave)(JNIEnv* env,jclass,jlong handle,jfloatArray wave) {
    try {
        const jsize count=wave?env->GetArrayLength(wave):0;
        if (count<64 || count>1024) throw std::invalid_argument("Invalid spectrum window");
        std::array<float,1024> samples{};
        if (!player(handle).game->audio().music_wave(samples.data(),static_cast<std::size_t>(count))) return JNI_FALSE;
        env->SetFloatArrayRegion(wave,0,count,samples.data());
        return JNI_TRUE;
    } catch (const std::exception& e) { fail(env,e.what()); return JNI_FALSE; }
}
API void JNICALL FN(audio)(JNIEnv* env,jclass,jlong handle,jobject output,jint frames) {
    auto* data=static_cast<std::int16_t*>(env->GetDirectBufferAddress(output));
    if (!data || frames<0 || std::int64_t{frames}*4>env->GetDirectBufferCapacity(output)) { fail(env,"Invalid audio buffer"); return; }
    try { player(handle).game->audio().pull(data,static_cast<std::size_t>(frames)); }
    catch (const std::exception& e) { fail(env,e.what()); }
}
