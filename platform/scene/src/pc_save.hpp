#pragma once

#include "nnn/script.hpp"

#include <array>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace emberfall::scene::pc {

inline constexpr std::size_t max_save_bytes=16U*1024U*1024U;
inline constexpr std::size_t header_size=32;
inline constexpr std::size_t effect_size=1024, filename_size=256;
inline constexpr std::size_t classic_layer_count=16;
inline constexpr std::size_t layer_count=32;
inline constexpr std::size_t vm_count=4;
inline constexpr std::size_t message_limit=64;
inline constexpr std::size_t legacy_message_width=128;
inline constexpr std::size_t classic_message_width=256;
inline constexpr std::size_t expanded_message_width=1024;
inline constexpr std::size_t log_limit=256;
inline constexpr std::uint32_t first_extension_code=100;
inline constexpr std::size_t max_extensions=256;
inline constexpr std::size_t classic_status_bytes=7088, preview_status_bytes=8112;
inline constexpr std::size_t okikae_count=100, okikae_width=64;
using CutinWords=std::array<nnn::Value,message_limit*4>;
inline constexpr std::size_t omake_parent_count=100, omake_slots=32;
using OmakeWords=std::array<nnn::Value,omake_parent_count*omake_slots>;

constexpr bool supported_status_size(std::size_t size) {
    return size==classic_status_bytes || size==preview_status_bytes;
}

constexpr std::optional<std::size_t> message_width_from_block_size(std::size_t size) {
    switch (size) {
    case header_size+message_limit*legacy_message_width: return legacy_message_width;
    case header_size+message_limit*classic_message_width: return classic_message_width;
    case header_size+message_limit*expanded_message_width: return expanded_message_width;
    default: return std::nullopt;
    }
}

struct ExecutableSaveLayout {
    std::optional<unsigned> layer_records;
    std::optional<std::size_t> message_width;
    std::optional<std::size_t> status_bytes;
    std::optional<bool> registers_okikae;
    std::optional<int> system_save_version;
};

// Recognizes SystemNNN write headers in PE32 x86 code sections.
ExecutableSaveLayout executable_save_layout(const std::filesystem::path& game_root);

struct FileSaveLayout {
    unsigned layer_records=layer_count;
    std::size_t message_width=expanded_message_width, status_bytes=classic_status_bytes;
    bool registers_okikae=true;
    int system_save_version=1;
    bool trial=false;
};

enum class SystemSaveSource { Pending=0, Executable, Template, GameFiles };

struct SystemSaveFormat {
    int version=0;
    bool trial=false;
    SystemSaveSource source=SystemSaveSource::Pending;
};

struct DirectorySaveLayout {
    FileSaveLayout files;
    ExecutableSaveLayout executable;
    SystemSaveFormat system;

    unsigned layers() const { return executable.layer_records.value_or(files.layer_records); }
    std::size_t message_width() const { return executable.message_width.value_or(files.message_width); }
    std::size_t status_bytes() const { return executable.status_bytes.value_or(files.status_bytes); }
    bool registers_okikae() const { return executable.registers_okikae.value_or(files.registers_okikae); }
};

struct SaveLayoutConfig {
    DirectorySaveLayout layout;
    std::filesystem::path file;
};

DirectorySaveLayout directory_save_layout(const std::filesystem::path& game_root,bool analyze_executable,
                                          const std::function<FileSaveLayout()>& file_defaults);
std::optional<SaveLayoutConfig> read_save_layout(const std::filesystem::path& game_root);
std::optional<SaveLayoutConfig> regenerate_save_layout(const std::filesystem::path& game_root,
                                                       const std::function<FileSaveLayout()>& file_defaults);

struct Header {
    std::uint32_t size=0, code=0, dummy1=0, dummy2=0;
    std::string name;
};

struct Info {
    int data_count=0, version=0, extension_count=0;
    bool has_omake=false, has_cutin=false, has_log=false;
};

struct DateTime {
    int year=0, month=0, day=0, weekday=0;
    int hour=0, minute=0, second=0, millisecond=0;
};

struct VmSlot {
    int script_number=0, pc=0, stack_pointer=0;
    // Positions 253 and 254 carry the SCRIPT and COMMAND call-table indices.
    std::array<std::int32_t,255> call_stack{};
};

struct Status {
    DateTime saved_at;
    int game_month=0, game_day=0, game_hour=0, game_weekday=0;
    int game_mode=0, save_count=0, adjust_version=0, run_mode=0;
    std::array<VmSlot,vm_count> vm{};
    int last_message_id=0, message_count=0, message_sub_mode=0, message_serial=0;
    int last_select_id=0, select_count=0, select_add_count=0, select_serial=0;
    int select_time_limit=0, auto_select=0, select_time_count=0, special_flag=0;
    int bgm_number=-1, bgm_plays=0, bgm_volume=0; // PC BGM number is zero-based.
    std::array<std::int32_t,4> bgm_params{};
    std::array<std::uint8_t,4> voice_exist_count{};
    int window_number=0, cursor_number=0, mouse_number=0, font_type=0;
    bool window_visible=false;
    int frame_ms=0, remaining=0, last_heroine=0, rename_layer=0;
    std::uint32_t layer_off=0;
    // The legacy 16-layer layout uses signed shorts. In a 32-layer game,
    // low and high signed bytes address layers 0..15 and 16..31 respectively.
    std::array<std::uint16_t,16> layer_variables{};
    int config_mask=0, skip_film=0, auto_message=0, cannot_click=0, cannot_skip=0;
    std::array<std::array<std::int32_t,16>,8> loop_sound{}, loop_voice{};
    std::array<std::string,8> loop_voice_names;
    std::string mini_comment, surname, given_name, script_filename;
    std::array<nnn::Byte,16> guid{};
    int large_comment_flag=0;
    std::array<std::string,4> large_comments, log_messages;
};

// Original Windows RECT storage. Effects normally treat right/bottom as size.
struct Rect { int left=0, top=0, right=0, bottom=0; };

struct Effect {
    int flag=0, picture=0, command=0;
    Rect src,dst,src_from,dst_from,src_to,dst_to;
    int count=0, count_max=0;
    std::array<std::int32_t,224> parameters{};
    bool not_transparent=false;
    int previous_effect=0;
    int padding=0;
};

struct Thumbnail {
    int width=0, height=0;
    std::vector<std::uint32_t> pixels; // Raw PC 32-bit screen colors.
};

struct LogEntry {
    std::string text;
    std::uint32_t color=0;
    std::array<std::string,8> voices;
};

enum class LogLayout { Legacy, MultiVoice };

struct Log {
    LogLayout layout=LogLayout::Legacy;
    int next=0, count=0, voice_count=1, tail=0;
    std::vector<LogEntry> entries; // Chronological, excluding unused ring slots.
};

struct UnsupportedBlock {
    Header header;
    std::vector<nnn::Byte> payload; // Does not include the common 32-byte header.
};

struct Save {
    Header header;
    Info info;
    Status status;
    Thumbnail thumbnail;
    std::vector<std::int32_t> vars;
    std::size_t layer_records=layer_count;
    std::size_t message_width=expanded_message_width;
    std::size_t status_bytes=classic_status_bytes;
    std::array<Effect,layer_count> effects;
    std::array<std::string,layer_count> filenames;
    std::array<std::string,message_limit> messages;
    std::optional<Log> log;
    std::optional<CutinWords> cutin;
    std::optional<OmakeWords> omake;
    // Slot order is the PC registration order. A slot number carries no type.
    std::vector<std::vector<nnn::Byte>> extensions;
    std::vector<UnsupportedBlock> unsupported_blocks;
};

// Throws std::runtime_error for malformed or unrecognized layouts. Strings
// retain the original game encoding. The session resolves EXT owners.
Save parse_save(const std::vector<nnn::Byte>& bytes,std::string_view expected_header="SAVEDATA");
std::vector<nnn::Byte> write_save(const Save& save);
std::vector<std::string> read_okikae(const std::vector<nnn::Byte>& payload);
std::vector<nnn::Byte> write_okikae(const std::vector<std::string>& replacements);

// A chart checkpoint starts with this standalone code 2 block. Read both
// original widths and write the current 8112-byte layout.
Status parse_status_block(const nnn::Byte* bytes,std::size_t size);
std::vector<nnn::Byte> write_status_block(const Status& status);

} // namespace emberfall::scene::pc
