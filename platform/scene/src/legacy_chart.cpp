#include "legacy_chart.hpp"

namespace emberfall::scene::legacy_chart {
namespace {
using nnn::Byte;
using nnn::Value;
using nnn::Word;
constexpr const char* current_var="\x8c\xbb\x8d\xdd\x92\x6e"; // 現在地
constexpr const char* reserved_var="\x8c\xa9\x82\xbd\x97\x5c\x96\xf1"; // 見た予約
constexpr const char* enabled_var="\x83\x58\x83\x8d\x83\x62\x83\x67\x81\x45\x82\x6e\x82\x6d\x82\x6e\x82\x65\x82\x65";
constexpr const char* complete_var="\x83\x58\x83\x8d\x83\x62\x83\x67\x81\x45\x83\x52\x83\x93\x83\x76\x83\x8a\x81\x5b\x83\x67";
constexpr const char* event_var="\x83\x58\x83\x8d\x83\x62\x83\x67\x81\x45\x82\x64\x82\x75\x82\x64\x82\x6d\x82\x73";
constexpr const char* percent_prefix="\x83\x52\x83\x93\x83\x76\x81\x45\x82\x6c\x82\x60\x82\x6f";
constexpr const char* total_var="\x83\x52\x83\x93\x83\x76\x81\x45\x82\x6c\x82\x60\x82\x6f\x82\x73\x82\x6e\x82\x73\x82\x60\x82\x6b";

void require_graph(const nnn::chart::Graph& graph) {
    if (!graph.configured() || graph.variant()!=nnn::chart::Variant::LegacyChart || graph.slot_capacity()!=slot_capacity)
        throw std::runtime_error("Legacy chart requires its explicit graph contract");
}
bool valid_slot(Value slot) { return slot>=0 && static_cast<std::size_t>(slot)<slot_capacity; }
bool enabled(const nnn::chart::RunState& run,Value slot) {
    if (!valid_slot(slot)) return false;
    const auto index=static_cast<std::size_t>(slot);
    return (run.can_select[index/32U]&(Word{1}<<(index%32U)))!=0;
}
void write(Action& action,const nnn::chart::ReadVariable& read,std::string name,Value value) {
    if (read && read(name)) action.writes.push_back({std::move(name),value});
}
void percentages(Action& action,const nnn::chart::Graph& graph,const nnn::chart::Progress& progress,
        const nnn::chart::ReadVariable& read,std::optional<Value> page) {
    const auto append=[&](Value id) {
        std::string name=percent_prefix;
        name.push_back(static_cast<char>(0x82));name.push_back(static_cast<char>(0x50+id));
        write(action,read,std::move(name),graph.percent(progress,id+1));
    };
    if (page) append(*page);
    else for (Value id=0;id<std::min(Value{8},static_cast<Value>(graph.pages().size()));++id) append(id);
    write(action,read,total_var,graph.percent(progress,0));
}
bool named(const std::filesystem::path& root,const char* list,std::string_view name,
           const nnn::resource::FilenameEncoder& encode) {
    const auto path=nnn::game_config::find(root,"spt",list,encode);
    if (path.empty()) return false;
    const auto text=nnn::game_config::read_text(path);
    std::size_t cursor=0;
    std::array<std::string_view,2> fields;
    while (nnn::game_config::read_name_row(text,cursor,fields))
        if (nnn::game_config::ascii_lower(std::string(fields[0]))==name) return true;
    return false;
}
} // namespace

std::optional<Function> function(std::string_view name) {
    const auto folded=nnn::game_config::ascii_lower(std::string(name));
    if (folded=="mita") return Function::Mark;
    if (folded=="eraberu") return Function::Enable;
    if (folded=="slotcomplete") return Function::Complete;
    if (folded=="slotonoff") return Function::Enabled;
    if (folded=="slotevent") return Function::Event;
    if (folded=="genzaichi") return Function::Position;
    if (folded=="checkallcomp") return Function::Refresh;
    return {};
}

bool recognizes(const std::filesystem::path& root,const nnn::resource::FilenameEncoder& encode) {
    if (nnn::game_config::find(root,"init","chart.xtx",encode).empty() ||
        !named(root,"command.xtx","chartmap",encode) ||
        (!named(root,"function.xtx","eraberu",encode) && !named(root,"function.xtx","genzaichi",encode))) return false;
    const nnn::game_config::NameListSetup game(nnn::game_config::read(root,"init","game.xtx",encode));
    if (game.integer("extSaveDataNumber",0)<1 || game.integer("extSaveDataSize1",0)!=static_cast<Value>(extension_bytes)) return false;
    // A successfully bounded graph corroborates the old page/block contract.
    // No inference is made from a PC save, title, or numeric extension code.
    const nnn::chart::Graph graph(root,800,600,encode,nnn::chart::Variant::LegacyChart);
    return graph.configured();
}


Action apply(const nnn::chart::Graph& graph,Function command,const std::vector<Value>& arguments,
             nnn::chart::Progress& progress,nnn::chart::RunState& run,const nnn::chart::ReadVariable& read) {
    require_graph(graph);
    if (!graph.valid(progress) || !graph.valid(run)) throw std::runtime_error("Invalid legacy chart state extent");
    auto next_progress=progress;
    auto next_run=run;
    Action result;result.handled=true;
    const auto slot=arguments.empty() ? 0 : arguments[0];
    const auto second=arguments.size()>1 ? arguments[1] : 1;
    const auto* node=graph.node(slot);
    switch (command) {
    case Function::Enable:
        result.run_changed=graph.set_enabled(next_run,slot,second!=0);
        break;
    case Function::Enabled:
        write(result,read,enabled_var,enabled(next_run,slot) ? 1 : 0);
        break;
    case Function::Complete:
        write(result,read,complete_var,graph.complete(next_progress,slot) ? 1 : 0);
        break;
    case Function::Event:
        write(result,read,event_var,node && (next_progress.seen[static_cast<std::size_t>(slot)]&
            ((1U<<static_cast<unsigned>(node->routes))-1U))!=0 ? 1 : 0);
        break;
    case Function::Mark:
        if (node) {
            result.progress_changed=graph.mark_route(next_progress,slot,second);
            percentages(result,graph,next_progress,read,node->page);
        }
        break;
    case Function::Position:
        if (node) {
            write(result,read,current_var,slot);
            write(result,read,graph.setup().slot_var,slot);
            write(result,read,graph.setup().display_page_var,node->page+1);
            result.progress_changed=graph.mark_route(next_progress,slot,1);
            percentages(result,graph,next_progress,read,node->page);
            const auto previous=read ? read(graph.setup().work_var).value_or(0) : 0;
            if (const auto edge=previous>0 && valid_slot(previous) ? graph.edge_index(previous,slot) : std::nullopt) {
                result.run_changed=graph.record_edge(next_run,{previous,*edge});
                if (*edge<8U) {
                    auto& bits=next_progress.traversed[static_cast<std::size_t>(previous)];
                    const auto before=bits;bits=static_cast<Byte>(bits|(1U<<*edge));
                    result.progress_changed|=before!=bits;
                }
            }
            result.save_node=slot;
        }
        break;
    case Function::Refresh:
        if (const auto reserved=read ? read(reserved_var) : std::nullopt;reserved && *reserved!=0) {
            result.progress_changed=graph.mark_route(next_progress,*reserved,1);
            write(result,read,reserved_var,0);
        }
        percentages(result,graph,next_progress,read,std::nullopt);
        break;
    }
    result.flush_global=result.progress_changed;
    progress=std::move(next_progress);run=std::move(next_run);
    return result;
}

nnn::chart::RunState parse_extension(const std::vector<Byte>& bytes,const nnn::chart::Graph& graph) {
    require_graph(graph);
    if (bytes.size()!=extension_bytes) throw std::runtime_error("Invalid legacy chart extension size");
    auto run=graph.run_defaults();
    nnn::snapshot::Reader in(bytes);
    for (auto& word:run.can_select) { in(word);word=~word; }
    std::copy_n(in.raw(run.move_road.size()),run.move_road.size(),run.move_road.begin());
    return run;
}
std::vector<Byte> write_extension(const nnn::chart::RunState& run,const nnn::chart::Graph& graph) {
    require_graph(graph);
    if (!graph.valid(run)) throw std::runtime_error("Invalid legacy chart state extent");
    nnn::snapshot::Writer out;
    for (const auto word:run.can_select) out(~word);
    out.raw(run.move_road.data(),run.move_road.size());
    return std::move(out.bytes);
}
ChartNodeSave parse_node(const std::vector<Byte>& bytes,const nnn::chart::Graph& graph) {
    require_graph(graph);
    nnn::snapshot::Reader in(bytes);
    std::uint32_t length=0;in(length);
    if (length<sizeof(length) || length>bytes.size()) chart_node_detail::invalid("invalid status extent");
    ChartNodeSave node;
    node.status=pc::parse_status_block(bytes.data(),length);in.raw(length-sizeof(length));
    node.run=graph.run_defaults();
    chart_node_detail::read_section(in,node.vars,Value{0});
    chart_node_detail::read_section(in,node.run.move_road,Byte{0});
    chart_node_detail::read_section(in,node.run.can_select,Word{0});
    for (auto& word:node.run.can_select) word=~word;
    if (in.remaining()!=0) chart_node_detail::invalid("trailing bytes");
    return node;
}
std::vector<Byte> write_node(const ChartNodeSave& node,const nnn::chart::Graph& graph) {
    require_graph(graph);
    if (!graph.valid(node.run)) chart_node_detail::invalid("invalid legacy state extent");
    nnn::snapshot::Writer out;
    out.bytes=pc::write_status_block(node.status,pc::legacy_status_bytes);
    chart_node_detail::write_section(out,node.vars,Value{0});
    chart_node_detail::write_section(out,node.run.move_road,Byte{0});
    auto disabled=node.run.can_select;
    for (auto& word:disabled) word=~word;
    chart_node_detail::write_section(out,disabled,Word{0});
    return std::move(out.bytes);
}
} // namespace emberfall::scene::legacy_chart
