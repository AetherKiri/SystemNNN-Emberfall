#include "dialogue_features.hpp"

#include "nnn/game_name_list.hpp"
#include "nnn/snapshot.hpp"
#include <algorithm>
#include <stdexcept>

namespace emberfall::scene {
namespace {
constexpr int coordinate_limit=16384;
int coordinate(int value) { return std::clamp(value,-coordinate_limit,coordinate_limit); }
std::string system_art(std::string name) {
    return nnn::resource::normalize_cp932_path("sys/"+name);
}
} // namespace

void Faces::configure(int depth,const std::filesystem::path& root,const nnn::resource::FilenameEncoder& encode) {
    depth_=depth;
    const auto setup_text=nnn::game_config::read(root,"init","face.xtx",encode);
    const nnn::game_config::NameListSetup setup(setup_text);
    const auto table=nnn::game_config::read(root,"spt","facelist.xtx",encode);
    const std::string tag(setup.field("filenameTag").value_or("ta_sys_face"));
    const bool custom=setup.integer("customFaceNameFlag",0)!=0;
    count_=std::clamp(setup.integer("facePrintNumber",1),1,2);
    x_[0]=coordinate(setup.integer("printX",10));x_[1]=coordinate(setup.integer("printX2",x_[0]));
    y_[0]=coordinate(setup.integer("printY",400));y_[1]=coordinate(setup.integer("printY2",y_[0]));
    width_=std::clamp(setup.integer("sizeX",160),1,4096);
    height_=std::clamp(setup.integer("sizeY",160),1,4096);
    std::size_t cursor=0;
    std::array<std::string_view,2> pair{};
    int types=1;
    while (characters_.size()<second_slot && nnn::game_config::read_name_row(table,cursor,pair)) {
        const auto id=characters_.size();
        auto& character=characters_.emplace_back();
        if (!id) continue;
        const auto suffix=std::to_string(id);
        character.name=nnn::resource::fold_cp932(std::string(pair[1]));
        const auto specific=custom ? setup.field("filenameFace"+suffix) : std::nullopt;
        character.image=system_art(specific ? std::string(*specific) : tag+suffix);
        types=std::clamp(setup.integer("face"+suffix+"TypeNumber",setup.integer("faceType"+suffix,types)),1,256);
        Animation animation;
        for (int type=0;type<=types;++type) {
            const auto key="face"+suffix+"Type"+std::to_string(type);
            animation.frames=std::clamp(setup.integer(key+"animePattern",animation.frames),1,256);
            if (animation.frames>1) {
                animation.type=std::clamp(setup.integer(key+"animeType",animation.type),1,8);
                animation.speed=std::clamp(setup.integer(key+"animeSpeed",animation.speed),1,1000);
                animation.wait_min=std::clamp(setup.integer(key+"WaitMin",animation.wait_min),0,100000);
                animation.wait_max=std::clamp(setup.integer(key+"WaitMax",animation.wait_max),0,100000);
            }
            character.animations.push_back(animation);
        }
    }
    save_words_=static_cast<std::size_t>(std::clamp(setup.integer("maxSave",256),
        std::max(1,static_cast<int>(characters_.size())),4096));
    reset();
}

void Faces::reset() {
    state_={};
    for (std::size_t i=1;i<characters_.size();++i) state_.words[i]=1;
    state_.words[first_slot]=state_.words[second_slot]=-1;
    hide();
}

bool Faces::valid(int face) const {
    return face>0 && static_cast<std::size_t>(face)<characters_.size();
}

void Faces::expression(int face,int type) {
    if (valid(face)) state_.words[static_cast<std::size_t>(face)]=std::max(0,type);
}

void Faces::force(nnn::Value argument) {
    const auto bits=static_cast<nnn::Word>(argument);
    state_.words[0]=static_cast<nnn::Value>(bits&0xffffU);
    if (argument!=0) {
        const auto high=bits>>16;
        state_.words[pin_side]=static_cast<nnn::Value>(high)-(high>=0x8000U ? 65536 : 0);
        if (state_.words[pin_side]!=0) state_.pin_next=true;
    }
}

void Faces::select(std::string_view name) {
    hide();
    if (!enabled()) return;
    auto& words=state_.words;
    int face=words[0];
    if (face==0 && !name.empty()) {
        const auto key=nnn::resource::fold_cp932(std::string(name));
        for (std::size_t i=1;i<characters_.size();++i)
            if (characters_[i].name==key) { face=static_cast<int>(i);break; }
    }
    if (!valid(face) || words[static_cast<std::size_t>(face)]<1) return;
    if (state_.pin_next) {
        words[pinned_face]=face;
        state_.pin_next=false;
    }
    const auto pinned=words[pinned_face];
    const auto rule=words[pin_side];
    const bool right=count_==2 && (face==pinned
        ? rule==2 || (rule==3 && words[first_slot]!=face && words[second_slot]==face)
        : (rule==1 || rule==3) && pinned!=-1 && words[first_slot]==pinned);
    const auto current=right?second_slot:first_slot,other=right?first_slot:second_slot;
    const auto displaced=words[current];
    if (displaced!=-1 && displaced!=face && (words[other]!=pinned || words[other]==-1))
        words[other]=displaced;
    words[current]=face;
    state_.speaker_side=right?1:0;
}

void Faces::add_portrait(DialogueVisuals& result,int side,bool animate) const {
    const int face=state_.words[side==0?first_slot:second_slot];
    if (!valid(face)) return;
    const auto index=static_cast<std::size_t>(face);
    const auto& character=characters_[index];
    if (character.image.empty()) return;
    const auto expression=state_.words[index];
    auto& visual=result.portraits.emplace_back();
    visual.image=character.image;
    if (expression>1) visual.image+="__"+std::to_string(expression);
    visual.x=x_[static_cast<std::size_t>(side)];visual.y=y_[static_cast<std::size_t>(side)];
    visual.width=width_;visual.height=height_;
    if (animate && expression>0 && static_cast<std::size_t>(expression)<character.animations.size()) {
        const auto& animation=character.animations[static_cast<std::size_t>(expression)];
        visual.animation_type=animation.type;visual.frames=animation.frames;visual.speed=animation.speed;
        visual.wait_min=animation.wait_min;visual.wait_max=animation.wait_max;
    }
}

void Faces::visuals(DialogueVisuals& result) const {
    const auto side=state_.speaker_side;
    if (side<0 || (depth_!=1 && depth_!=2)) return;
    result.portrait_depth=depth_;
    add_portrait(result,side,true);
    if (count_==2) add_portrait(result,1-side,false);
}

void Faces::restore(FaceSaveState state) {
    state_=std::move(state);
}

std::vector<nnn::Byte> Faces::pc_payload() const {
    nnn::snapshot::Writer out;
    for (std::size_t i=0;i<save_words_;++i) out(i<state_.words.size()?state_.words[i]:0);
    return std::move(out.bytes);
}

void Faces::restore_pc(const std::vector<nnn::Byte>& payload) {
    if (payload.size()!=save_words_*4) throw std::runtime_error("PC face extension has a different layout");
    reset();
    nnn::snapshot::Reader in(payload);
    for (std::size_t i=0;i<std::min(save_words_,state_.words.size());++i) in(state_.words[i]);
}

void Cutins::configure(const std::filesystem::path& root,const nnn::resource::FilenameEncoder& encode) {
    enabled_=true;
    const auto settings=nnn::game_config::read(root,"init","printmessage.xtx",encode);
    const nnn::game_config::NameListSetup setup(settings);
    print_name_=setup.integer("cutinNamePrintFlag",0)!=0;
    window_tag_=setup.field("filenameCutin").value_or("ta_cutinwindow");
    const auto table=nnn::game_config::read(root,"spt","cutinChara.xtx",encode);
    std::size_t cursor=0;
    std::array<std::string_view,16> fields{};
    while (styles_.size()<4096 && nnn::game_config::read_name_row(table,cursor,fields)) {
        const auto value=[&](std::size_t index){return nnn::game_config::name_number(fields[index]);};
        const auto rgb=[&](std::size_t index){return static_cast<std::uint32_t>(std::clamp(value(index),0,255));};
        const auto alpha=static_cast<std::uint32_t>(std::clamp(value(12),0,100))*255U/100U;
        styles_.push_back({std::string(fields[0]),coordinate(value(2)),coordinate(value(3)),
            coordinate(value(4)),coordinate(value(5)),coordinate(value(6)),coordinate(value(7)),
            coordinate(value(8)),std::clamp(value(13),-256,256),alpha<<24U|rgb(9)<<16U|rgb(10)<<8U|rgb(11)});
    }
}

const Cutins::Style* Cutins::style(int type) const {
    return type>0 && static_cast<std::size_t>(type)<styles_.size() ? &styles_[static_cast<std::size_t>(type)] : nullptr;
}

void Cutins::begin_message(bool append) {
    if (enabled_ && !append) std::fill_n(words.begin(),pc::message_limit*2,0);
}

void Cutins::strip_name(std::vector<std::string>& rows,int type) const {
    if (!active() || words.back()<0 || type==0 || print_name_ || rows.empty()) return;
    if (const auto* info=style(type);info && (rows.front()==info->name ||
        (!info->name.empty() && info->name.front()=='*'))) rows.erase(rows.begin());
}

void Cutins::tag(std::size_t existing,std::size_t added,int type) {
    if (!enabled_ || words.back()<=0 || type==0 || existing>=pc::message_limit) return;
    added=std::min(added,pc::message_limit-existing);
    for (std::size_t i=0;i<added;++i) {
        words[(existing+i)*2]=type;
        words[(existing+i)*2+1]=static_cast<int>(added-i);
    }
}

void Cutins::discard_first_row() {
    std::move(words.begin()+2,words.begin()+static_cast<std::ptrdiff_t>(pc::message_limit*2),words.begin());
    words[pc::message_limit*2-2]=words[pc::message_limit*2-1]=0;
}

void Cutins::visuals(DialogueVisuals& result,std::size_t row_count) const {
    if (!active()) return;
    result.cutin=true;
    int previous=0;
    row_count=std::min(row_count,pc::message_limit);
    for (std::size_t i=0;i<row_count;++i) {
        auto& row=result.rows.emplace_back();
        const auto* info=style(words[i*2]);
        if (!info) continue;
        row.positioned=true;
        row.x=coordinate(info->x+info->step_x*static_cast<int>(i));
        row.y=coordinate(info->y+info->step_y*static_cast<int>(i));
        row.font_delta=info->font_delta;
        const auto count=std::clamp(words[i*2+1],0,static_cast<int>(row_count-i));
        if (count>previous) {
            row.window_type=info->window_type;row.window_rows=count;
            row.window_x=info->x-info->pad_x;row.window_y=info->y-info->pad_y;
            row.pad_x=info->pad_x;row.pad_y=info->pad_y;
            row.step_x=info->step_x;row.step_y=info->step_y;row.window_color=info->color;
            if (info->window_type<0) row.window_image=system_art(window_tag_+std::to_string(-info->window_type));
        }
        previous=count;
    }
}

} // namespace emberfall::scene
