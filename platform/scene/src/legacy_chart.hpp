#pragma once

#include "chart_node_save.hpp"

namespace emberfall::scene::legacy_chart {

// The old chart callback stores these fixed buffers, independently of how
// many chart pages or script functions the game exposes.
inline constexpr std::size_t slot_capacity=nnn::chart::legacy_slot_capacity,
    selection_words=nnn::chart::legacy_selection_words,seen_bytes=nnn::chart::legacy_seen_bytes;
inline constexpr std::size_t extension_bytes=selection_words*sizeof(nnn::Word)+slot_capacity;

inline constexpr auto current_var="\x8c\xbb\x8d\xdd\x92\x6e"; // 現在地
inline constexpr auto reserved_var="\x8c\xa9\x82\xbd\x97\x5c\x96\xf1"; // 見た予約

enum class Function { Mark, Enable, Complete, Enabled, Event, Position, Refresh };
std::optional<Function> function(std::string_view name);

// Configuration-only recognition: does not open any executable or save.
bool recognizes(const std::filesystem::path& root,const nnn::resource::FilenameEncoder& encode={});

struct Write { std::string name; nnn::Value value; };
struct Action {
    bool handled=false,progress_changed=false,run_changed=false,flush_global=false;
    std::vector<Write> writes;
    // Capture AFTER applying writes/progress/run, with the original chart mode.
    // Position does not replace the last-selected-node variable.
    std::optional<nnn::Value> save_node;
};

Action apply(const nnn::chart::Graph& graph,Function command,const std::vector<nnn::Value>& arguments,
             nnn::chart::Progress& progress,nnn::chart::RunState& run,const nnn::chart::ReadVariable& read);

// Existing Graph and ChartViewModel use enabled bits. Only the PC boundary
// complements them to the old callback's disabled-bit representation.
nnn::chart::RunState parse_extension(const std::vector<nnn::Byte>& bytes,const nnn::chart::Graph& graph);
std::vector<nnn::Byte> write_extension(const nnn::chart::RunState& run,const nnn::chart::Graph& graph);

// Original mushiex-style node files have no outer story-save header. The
// status block carries its own layout; all three following RLE markers are 0.
ChartNodeSave parse_node(const std::vector<nnn::Byte>& bytes,const nnn::chart::Graph& graph);
std::vector<nnn::Byte> write_node(const ChartNodeSave& node,const nnn::chart::Graph& graph);

} // namespace emberfall::scene::legacy_chart
