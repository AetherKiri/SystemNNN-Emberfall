#pragma once

#include "emberfall/dialogue_visuals.hpp"

#include "emberfall/chart_view.hpp"

#include "emberfall/layers.hpp"
#include "nnn/script.hpp"
#include "nnn/game_audio.hpp"
#include "nnn/resource_path.hpp"
#include "nnn/runtime_context.hpp"
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <string_view>

namespace emberfall::scene {
namespace pc { struct DirectorySaveLayout; }

enum class Wait { Running, Draw, Overlap, Dialogue, Choice, Ended, Error, Movie, Place, Modal };
enum class EndReason { None, End, Next, Exit, ReplayComplete };
enum class DrawMessage { None, Window, RetainedMessage, LongBackground };
struct ScriptClockSample {
    int date=0; // Local YYYYMMDD.
    int milliseconds=0; // Since local midnight.
};
struct InputState {
    int mouse_x=0, mouse_y=0;
    bool advance=false, held=false;
    unsigned choice=0; // 1-based; zero leaves the selection pending.
    // An explicit platform command; absent leaves the Core's actual mode alone.
    std::optional<bool> skip;
    bool play_movies=false; // Interactive callers pause at a movie. Input replay continues.
    bool expire_choices=true; // Replay supplies the recorded timeout explicitly.
    bool legacy_calculations=false; // Old Android input journal, before these PC built-ins existed.
    bool recorded_clock=false;
    std::optional<ScriptClockSample> clock_sample;
};
struct FrameTiming {
    std::uint32_t elapsed_ms=0;
    std::uint32_t animation_ms=0;
};
inline constexpr unsigned timeout_choice=UINT32_MAX;
struct DialogueState {
    std::string script, speaker, raw_bytes, display_bytes; // Unmodified game encoding.
    std::uint32_t message_id=0, table_index=0, command=0;
    std::uint32_t layout_command=0;
    std::uint64_t sequence=0;
    int window_type=0, font_type=0, effect_type=0;
    bool window_visible=false;
};
struct BacklogEntry {
    std::string speaker, text;
    std::array<std::string,4> voices;
    int color=-1; // Optional RGB override for selection entries.
    bool configuration_text=false;
    std::uint64_t jump_id=0; // Live session only, never written to an ordinary save.
};
struct BacklogJump {
    unsigned journal_cursor=0;
    unsigned keep_backlog=0; // Nonzero when replay must discard rows already evicted in the live log.
    std::uint32_t random_state=0;
    int mouse_x=0,mouse_y=0;
};
struct AudioEvent {
    std::string script;
    std::uint32_t word=0;
    nnn::SystemEvent command; // VOICE includes its optional raw name. Order is retained across frames.
};
struct ScriptModalRow {
    enum Flag { Enabled=1, New=2, Selected=4, Locked=8, SoldOut=16, PurchaseEnabled=32 };
    int id=0,flags=0;
    std::string label,asset,detail; // Original game bytes.
};
struct ScriptModal {
    nnn::RuntimeMode mode=nnn::RuntimeMode::Nothing;
    std::optional<nnn::RuntimeMode> return_mode; // Empty resumes the suspended script.
    int request=0;
    std::vector<nnn::Value> arguments;
    std::vector<std::string> text; // Original game bytes.
    std::vector<int> byte_limits;
    int page=0,pages=1,group=0,selection=0; // Pages are zero-based.
    int balance=0; // SHOP's current point balance.
    std::vector<ScriptModalRow> rows;
    std::shared_ptr<const ChartView> chart;
};
struct FrameState {
    Wait wait=Wait::Running;
    EndReason end_reason=EndReason::None;
    std::string script, error;
    std::uint32_t word=0;
    DialogueState dialogue;
    std::uint32_t remaining_frames=0;
    std::uint32_t choice_limit_ms=0,choice_remaining_ms=0;
    bool choice_timed_out=false;
    int config_mask=0,demo_mode=0;
    int script_cursor=0,script_mouse=0; // Message cursor and mouse pointer are separate.
    std::string film_name,story_name;
    unsigned movie=0; // One-based position in spt/movielist.xtx.
    std::optional<nnn::ScriptSelection> selection;
    std::optional<ScriptModal> modal;
};
struct SessionOptions {
    int width=0, height=0; // Use exesetup/mainControl configuration, then the PC default 800x600.
    unsigned frame_ms=0; // Use the game's frameTime.
    std::size_t cache_bytes=96U*1024U*1024U;
    std::string entry="sys.spt";
    bool standalone_script=false; // Inspection of an individual script can stop at its END.
    nnn::audio::Settings audio;
    bool queue_audio_events=true; // Disable when only the PCM device consumes audio.
    std::shared_ptr<nnn::RuntimeContext> runtime; // Shared by the story and decorative UI sessions.
    std::vector<nnn::Word> replay_films; // Zero-based film labels of entry, in playback order.
    std::uint32_t replay_hidden_layers=0;
    std::optional<std::uint32_t> random_seed; // Omitted: PC initRandomSeed policy.
    std::function<ScriptClockSample()> local_clock;
    nnn::resource::FilenameEncoder filename_encoder;
    bool analyze_executable=true; // Probe the PC save layout if this game has no valid cached layout.
    std::function<void(std::string_view)> script_debug_output;
    std::shared_ptr<const pc::DirectorySaveLayout> save_layout; // Reused by sessions in the same player run.
};
struct SessionStats {
    std::size_t cache_hits=0, decodes=0, evictions=0, cached_bytes=0, packed_reads=0, loose_reads=0;
    std::size_t draws=0, overlaps=0, choices=0, file_transfers=0, audio_events=0, cg_unlocks=0;
};
struct ScriptRestrictions { int auto_message=0, cannot_click=0, cannot_skip=0, option_off=0; };
using PlaneKey=std::array<std::int32_t,6>;

class GameSession {
public:
    using PlaneKey=emberfall::scene::PlaneKey;
    struct RuntimeRef {
        std::shared_ptr<nnn::RuntimeContext> runtime;
        std::shared_ptr<const pc::DirectorySaveLayout> save_layout;
    };
    explicit GameSession(const std::filesystem::path& game_root, SessionOptions options={});
    // Decorative peers share an asset cache. Their caller must serialize access to it.
    static std::unique_ptr<GameSession> create_ui_preview(const std::filesystem::path& game_root,
        SessionOptions options={},const GameSession* asset_peer=nullptr);
    ~GameSession();
    GameSession(const GameSession&)=delete;
    GameSession& operator=(const GameSession&)=delete;
    const FrameState& step_frame(std::uint32_t dt_ms,const InputState& input={});
    const FrameState& step_frame(FrameTiming timing,const InputState& input={},
                                 const render::Layers::OverlapCapture& capture={});
    // Convert elapsed wall time to the animation budget for this update.
    std::uint32_t pace(std::uint32_t elapsed_ms);
    bool film_skip_available() const;
    bool request_film_skip();
    bool complete_minigame(unsigned number);
    const FrameState& state() const;
    bool complete_modal(int request,bool accepted,unsigned elapsed_frames,const std::vector<std::string>& values);
    // Back, choose, page, hover, confirm, group, resume, chart scroll and chart commit use codes 0 through 8.
    // Returns -1 for rejection, 0 for an update, 1 for another mode, 2 for exit,
    // 3 to abort the request, 4 to animate before committing a chart exit, or 5 to enter story from an external chart.
    int act_modal(int request,int action,int value);
    void advance_modal(int request,unsigned frames);
    bool modal_savable() const;
    bool open_modal(nnn::RuntimeMode mode,nnn::RuntimeMode back_mode,bool from_title=false);
    bool menu_extension_enabled(nnn::RuntimeMode mode) const;
    std::optional<ScriptModal> status_overlay(nnn::RuntimeMode type,nnn::RuntimeMode mode,bool prefer_story=false);
    std::vector<ScriptModal> status_overlays(nnn::RuntimeMode mode,bool prefer_story=false);
    DialogueVisuals dialogue_visuals() const;
    void set_chart_storage(const std::filesystem::path& private_root);
    // A preparation failure returns -1 for a stale request, 0 to resume the script, or its external caller mode.
    int abort_modal(int request,const std::string& reason,unsigned elapsed_frames=0);
    void sync_runtime_names(); // Apply global names at initial startup, before any slot is restored.
    std::vector<int> places() const; // 0 hidden, 1 disabled, 2 selectable.
    std::vector<int> place_characters() const; // Matches places(); zero means no character art.
    nnn::RuntimeMode place_mode() const; // Nothing when no place selection is pending.
    const std::deque<BacklogEntry>& backlog() const;
    void seed_legacy_replay_variables();
    void capture_backlog_jump(unsigned journal_cursor);
    BacklogJump backlog_jump(std::uint64_t id) const;
    bool restore_backlog_jump(std::uint64_t id,unsigned journal_cursor);
    void trim_replayed_backlog(unsigned keep);
    const render::Surface& surface();
    const render::Surface& scene_surface();
    void set_message_plane(std::shared_ptr<const render::MessagePlane> plane);
    void clear_message_plane() noexcept;
    bool needs_message_plane() const noexcept;
    PlaneKey expected_plane_key() const noexcept;
    void after_present();
    const render::Surface& presented_surface() const noexcept;
    bool screen_layer_active() const;
    bool story_draws_screen_layer() const;
    std::uint64_t screen_layer_revision() const;
    void capture_screen_layer(bool enable);
    std::unique_ptr<render::ScreenLayerFrame> take_screen_layer();
    void commit_screen_capture(render::Surface frame);
    // Worker-only access to SELECTCHART's split preview composition.
    render::Layers& chart_preview_layers();
    bool needs_redraw() const;
    int overlap_type() const noexcept;
    const std::array<render::Layer,32>& layers() const;
    std::shared_ptr<const render::Texture> ui_texture(const std::string& name);
    void reset_ui_background(const std::string& entry,int mode,std::shared_ptr<nnn::RuntimeContext> runtime={});
    bool claim_ui_background(std::shared_ptr<nnn::RuntimeContext> runtime,const nnn::RuntimeContext::PreviewState& before);
    void start_ui_background(const std::string& entry,int mode,bool fresh=false);
    const FrameState& step_ui_background(const InputState& input,bool loop,unsigned frames=1);
    const FrameState& tick_ui_background(const InputState& input,bool loop,std::uint32_t elapsed_ms);
    void reset_frame_pacing();
    void set_ui_saved_screen(render::Surface frame,bool background=true);
    void set_ui_variable(const std::string& name,nnn::Value value);
    void set_ui_place_preview(unsigned place);
    nnn::Value named_variable(const std::string& name,bool prefer_story=false,nnn::Value fallback=0) const;
    // The title's InitNewGame(setVar,setData): call on a new story before its first frame.
    bool set_story_variable(const std::string& name,nnn::Value value);
    const std::vector<std::string>& warnings() const;
    std::vector<AudioEvent> take_audio_events();
    bool take_system_save_request();
    unsigned take_notice();
    // Pull stereo PCM16 on the playback clock. Submitting script events happens inside step_frame.
    nnn::audio::GameAudio& audio();
    const nnn::audio::GameAudio& audio() const;
    SessionStats stats() const;
    nnn::Value variable(unsigned index) const;
    std::uint32_t script_random_state() const;
    void set_script_random_state(std::uint32_t state);
    std::optional<ScriptClockSample> script_clock_sample() const;
    unsigned frame_ms() const;
    nnn::Value mode() const;
    std::shared_ptr<nnn::RuntimeContext> runtime() const;
    nnn::Value get_system_parameter(nnn::Word id) const;
    bool set_system_parameter(nnn::Word id,nnn::Value value);
    bool update_system_parameters(const std::vector<std::pair<nnn::Word,nnn::Value>>& values);
    nnn::RuntimeParameters snapshot_parameters() const;
    void apply_runtime_parameters();
    // These use the same context lock as settings; no UI-owned timing model.
    void begin_player_line(bool has_voice,std::uint32_t full_voice_ms,int script_speed=0);
    std::uint32_t text_characters(std::uint32_t elapsed_ms);
    int text_delay_ms() const;
    std::uint32_t settings_text_rate(int index,bool automatic) const noexcept;
    int settings_auto_speed() const;
    std::string_view append_prefix() const noexcept;
    void set_text_length(std::uint32_t length,std::optional<std::uint32_t> visible={});
    std::uint32_t visible_text() const;
    unsigned text_fraction() const;
    std::uint32_t text_length() const;
    bool text_complete() const;
    bool request_advance();
    bool auto_ready(std::uint32_t elapsed_ms,bool text_complete);
    std::int64_t auto_remaining_ms() const;
    bool continue_voice() const;
    bool auto_continue() const;
    bool auto_skip() const;
    bool skip_read_only() const;
    void set_skip_active(bool enabled);
    void set_ctrl_skip(bool enabled);
    bool skip_active() const;
    bool skip_effective() const;
    ScriptRestrictions script_restrictions() const;
    bool options_enabled() const;
    DrawMessage draw_message() const;
    nnn::snapshot::Bytes snapshot() const;
    // Parse the full save without loading game assets or running a script.
    static void inspect_snapshot(nnn::snapshot::Reader& in,const std::function<bool(const FrameState&)>& verify={});
    // Import uses a staged owner and commits only a fully validated PC slot.
    void restore_pc_save(const std::vector<nnn::Byte>& bytes);
    // A host that already constructed a private load candidate can use it
    // directly. Discard that candidate if loading throws.
    void load_pc_save_candidate(const std::vector<nnn::Byte>& bytes);
    struct PcSaveLayout {
        enum class Source { GameFiles=0, Executable=2 }; // Values used by Android debug metadata.
        unsigned render_layers=0, layer_limit=0, layer_records=0;
        std::size_t message_width=0, loaded_message_width=0; // The loaded width is diagnostic only.
        Source source=Source::GameFiles;
    };
    PcSaveLayout pc_save_layout() const;
    static int detect_pc_system_save_version(const std::filesystem::path& root);
    // Game-file default and optional EXE version, in that order.
    static std::array<int,2> pc_system_save_versions(const std::filesystem::path& root,bool analyze_executable);
    // Resolved archive version and the trial-edition flag.
    static std::array<int,2> pc_system_save_format(const std::filesystem::path& root,bool analyze_executable);
    std::array<int,2> pc_system_save_format() const;
    RuntimeRef runtime_ref() const;
    struct PcSaveConfiguration {
        std::filesystem::path file;
        unsigned layer_records=0;
        std::size_t message_width=0, status_bytes=0;
        int system_save_version=0, system_save_source=0;
        bool executable_layout=false, okikae=false;
        bool executable_message_width=false, executable_status_bytes=false, executable_okikae=false;
        int extension_start_code=-1; // -1 unresolved, 0 none, otherwise the actual EXE operand.
        int extension_enabled=-1; // -1 older configuration without enough information.
        std::vector<int> extension_sizes; // Payload bytes in saved order. -1 means unknown.
    };
    // Inspection is read-only. Regeneration always analyzes the EXE before applying fallbacks.
    static std::optional<PcSaveConfiguration> pc_save_configuration(const std::filesystem::path& root,bool regenerate);
    struct PcSavePreview {
        std::array<int,7> time{};
        int artwork=0, width=0, height=0;
        std::string summary;
        std::vector<std::uint32_t> pixels;
        std::vector<nnn::Value> vars;
    };
    static PcSavePreview inspect_pc_save(const std::vector<nnn::Byte>& bytes, std::size_t picture_budget);
    struct PcSaveGameDate {
        bool valid=false;
        int year=2000, date=0, month=0, day=0, weekday=0;
    };
    static int game_weekday(int year,int month,int day) noexcept;
    static std::array<int,2> pc_save_date_variables(const std::filesystem::path& root);
    static PcSaveGameDate resolve_game_date(const std::vector<nnn::Value>& vars,int year_var,int date_var) noexcept;
    static PcSaveGameDate inspect_pc_save_game_date(const std::vector<nnn::Byte>& bytes,int year_var,int date_var);
    struct SharedConfigSnapshot {
        static constexpr std::size_t size=28;
        int screen_width=800;
        int screen_height=600;
        int default_frame_ms=20;
        int code_byte=2;
        int trial_flag=0;
        int system_save_version=2;
        int layer_limit=16;
        int render_layer_count=32;
        int save_layer_records=32;
        int registers_okikae=1;
        int window_message_number=4;
        int full_message_number=4;
        int message_font_size=24;
        int print_x=100;
        int print_y=100;
        int next_x=0;
        int next_y=30;
        int lprint_x=100;
        int lprint_y=100;
        int lnext_x=0;
        int lnext_y=30;
        int append_skip_name=1;
        // NNNPARAM_WINDOWPERCENT (106): raw transparency percent (0=opaque default, 100=transparent).
        int window_percent_raw=0;
        // Effective window opacity percent (100 - clamp(window_percent_raw, 0, 100)).
        int window_opacity_percent=100;
        int chara_voice_count=0;
        int title_bgm_number=1;
        int year_var_index=-1;
        int date_var_index=-1;
        std::array<int,size> to_array() const noexcept {
            return {{screen_width,screen_height,default_frame_ms,code_byte,trial_flag,system_save_version,
                layer_limit,render_layer_count,save_layer_records,registers_okikae,
                window_message_number,full_message_number,message_font_size,
                print_x,print_y,next_x,next_y,lprint_x,lprint_y,lnext_x,lnext_y,append_skip_name,
                window_percent_raw,window_opacity_percent,chara_voice_count,title_bgm_number,
                year_var_index,date_var_index}};
        }
    };
    SharedConfigSnapshot shared_config_snapshot() const;
    std::size_t full_message_number() const noexcept;
    int window_opacity_percent() const noexcept;
    int year_variable_index() const noexcept;
    int date_variable_index() const noexcept;
    std::vector<nnn::Byte> export_pc_save(const std::vector<std::uint32_t>& pixels={}, int width=0, int height=0);
    std::vector<nnn::Byte> export_presented_pc_save();
    nnn::Value save_slot_art() const;
    void restore_snapshot(const nnn::snapshot::Bytes& bytes,const std::function<bool(const FrameState&)>& verify={},
                          bool keep_music_position=false,bool migrate_legacy_system_variables=false,bool restart_music=false);
private:
    const FrameState& advance_ui_background(const InputState& input,bool loop,unsigned frames,
                                           std::optional<std::uint32_t> elapsed_ms);
    struct Impl;
    explicit GameSession(std::unique_ptr<Impl> data);
    std::unique_ptr<Impl> data_;
};
} // namespace emberfall::scene
