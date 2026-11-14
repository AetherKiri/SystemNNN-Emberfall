#include "../src/legacy_chart.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>

namespace {
namespace legacy=emberfall::scene::legacy_chart;
namespace pc=emberfall::scene::pc;
namespace chart=nnn::chart;
using Bytes=std::vector<nnn::Byte>;
int checks=0;
void require(bool condition,const char* message) {
    if (!condition) throw std::runtime_error(message);
    ++checks;
}
template<class F> void rejected(F&& call,const char* message) {
    bool failed=false;
    try {call();} catch (const std::exception&) {failed=true;}
    require(failed,message);
}
struct Fixture {
    std::filesystem::path root=std::filesystem::temp_directory_path()/
        ("nnn-legacy-chart-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Fixture() {
        if (!std::filesystem::create_directory(root)) throw std::runtime_error("Temporary chart directory already exists");
        text("init/chart.xtx","chartListNumber 2\nbasicChartNumber 2\nslotSizeX 118\nslotSizeY 30\n");
        text("init/game.xtx","extSaveDataNumber 1\nextSaveDataSize1 11280\n");
        text("spt/command.xtx","chartmap,Chart\n");
        text("spt/function.xtx","genzaichi,Position\n");
        text("chart/chart1.xtx","1800 @1001 2 232 138 1002 1003 Opening @1002 1 250 287 1004 BranchA "
             "@1003 1 562 353 1004 BranchB @1004 1 409 399 2001 CrossPage\n");
        text("chart/chart2.xtx","2400 @2001 1 438 186 Final\n");
    }
    ~Fixture() {std::error_code ignored;std::filesystem::remove_all(root,ignored);}
    void text(const char* path,const char* contents) {
        const auto file=root/path;std::filesystem::create_directories(file.parent_path());
        std::ofstream stream(file,std::ios::binary);stream<<contents;
        if (!stream) throw std::runtime_error("Cannot write temporary chart configuration");
    }
};
void word(Bytes& bytes,std::uint32_t value) {
    for (unsigned i=0;i<4;++i) bytes.push_back(static_cast<nnn::Byte>(value>>(i*8U)));
}
bool has_write(const legacy::Action& action,const std::string& name,nnn::Value value) {
    return std::any_of(action.writes.begin(),action.writes.end(),[&](const auto& write) {
        return write.name==name && write.value==value;
    });
}
void behavior() {
    Fixture fixture;
    require(legacy::recognizes(fixture.root),"Configuration contract requires neither nine pages nor every unused function");
    chart::Graph graph(fixture.root,800,600,{},chart::Variant::LegacyChart);
    require(graph.pages().size()==2 && graph.slot_capacity()==10000 && graph.node(1001)->page==0,
        "Legacy page indices must preserve source slot numbers");
    require(graph.node(1001)->center.x==232 && graph.node(1001)->center.y==138,"Source node coordinates were rescaled");
    require(graph.page(0)->extent==1800 && graph.scroll_min(0)==-300 && graph.scroll_limit(0)==1500,
        "Legacy source extent and PC scrolling limits changed");
    const auto read=[&](std::string_view name)->std::optional<nnn::Value> {
        if (name==graph.setup().work_var) return 1001;
        if (name==graph.setup().display_page_var) return 1;
        return 0;
    };
    const auto visit=graph.begin_visit(1001,0,read);
    require(visit.page==0 && visit.scroll_y==-162,"Legacy initial node must be centered with its original 300-pixel offset");
    const auto screen=graph.screen_point(graph.node(1001)->center,visit.scroll_y);
    require(screen.x==232 && screen.y==300,"Legacy screen position differs from original chart coordinates");
    require(graph.background_y(0,-300)==0 && graph.background_y(0,1500)==-600 &&
        graph.center_y(0,600,600)==0,"Legacy original background/center formulas changed");
    const auto atlas=graph.atlas(1001,472,60);
    require(atlas && atlas->asset=="sys/ta_chart_button1_1" && atlas->source.x==118 && atlas->source.y==0,
        "Legacy atlas lookup used a zero-based page block");
    auto progress=graph.global_defaults();auto run=graph.run_defaults();
    require(progress.seen.size()==10004 && progress.traversed.size()==10000 && run.can_select.size()==320,
        "Legacy PC padding was discarded");
    auto action=legacy::apply(graph,legacy::Function::Enable,{1002,0},progress,run,read);
    require(action.run_changed && graph.access(progress,run,1001,1002,read)==chart::Access::Locked,
        "eraberu state zero must set a disabled bit");
    action=legacy::apply(graph,legacy::Function::Enabled,{1002},progress,run,read);
    require(action.writes.size()==1 && action.writes[0].value==0,"slotonoff must return enabled polarity");
    legacy::apply(graph,legacy::Function::Enable,{1002},progress,run,read);
    require(graph.access(progress,run,1001,1002,read)==chart::Access::Forward,"Default eraberu state must enable the node");
    legacy::apply(graph,legacy::Function::Mark,{1001,1},progress,run,read);
    action=legacy::apply(graph,legacy::Function::Complete,{1001},progress,run,read);
    require(action.writes[0].value==0,"A partially seen multi-route node is not complete");
    action=legacy::apply(graph,legacy::Function::Event,{1001},progress,run,read);
    require(action.writes[0].value==1,"slotevent must detect an observed route");
    legacy::apply(graph,legacy::Function::Mark,{1001,2},progress,run,read);
    require(graph.complete(progress,1001),"mita did not set the requested route");
    action=legacy::apply(graph,legacy::Function::Position,{1002},progress,run,read);
    require(action.save_node==1002 && run.move_road[1001]==1 && progress.traversed[1001]==1,
        "genzaichi must capture the incoming edge and request a node checkpoint");
    require(has_write(action,graph.setup().slot_var,1002) && has_write(action,graph.setup().display_page_var,1) &&
        std::none_of(action.writes.begin(),action.writes.end(),[&](const auto& w){return w.name==graph.setup().work_var;}),
        "genzaichi overwrote last-selected-node state or lost page/slot writes");
    run=graph.run_defaults();
    const auto zero=[](std::string_view)->std::optional<nnn::Value>{return 0;};
    legacy::apply(graph,legacy::Function::Position,{1002},progress,run,zero);
    require(run.move_road[0]==0,"Legacy slot zero must not alias the first node when recording an edge");
    std::string reserved="\x8c\xa9\x82\xbd\x97\x5c\x96\xf1";
    const auto pending=[&](std::string_view name)->std::optional<nnn::Value>{return name==reserved ? 1003 : 0;};
    action=legacy::apply(graph,legacy::Function::Refresh,{},progress,run,pending);
    require(graph.seen(progress,1003) && has_write(action,reserved,0) && action.flush_global,
        "checkallcomp must consume and clear the deferred seen node");
    run.can_select[0]=0;run.move_road[0]=9;graph.new_game(run);
    require(run.can_select[0]==UINT32_MAX && run.move_road[0]==0,"New legacy game must clear disabled bits and current route");
    const auto old=progress.seen;
    rejected([&]{auto invalid=run;invalid.can_select.pop_back();legacy::apply(graph,legacy::Function::Mark,{1001},progress,invalid,read);},
        "Invalid state extents must be rejected before function writes");
    require(progress.seen==old,"Rejected function altered global progress");
    nnn::RuntimeContext runtime(fixture.root);
    require(!runtime.set_chart_progress(progress),"Standard layout must not infer legacy semantics from buffer lengths");
    require(runtime.set_chart_progress(progress,chart::StorageLayout::LegacyChart),"Explicit legacy global extent was rejected");
    runtime.set_chart_selection(graph.slot_capacity(),run.can_select);
    require(!runtime.chart_selection(),"Standard selection setter accepted legacy padding implicitly");
    runtime.set_chart_selection(graph.slot_capacity(),run.can_select,chart::StorageLayout::LegacyChart);
    require(runtime.chart_selection() && runtime.chart_selection()->enabled.size()==320,"Explicit legacy selection state was lost");
    fixture.text("spt/function.xtx","unrelated,Other\n");
    require(!legacy::recognizes(fixture.root),"A same-size EXT alone must not activate the legacy chart contract");
}
void codecs() {
    Fixture fixture;chart::Graph graph(fixture.root,800,600,{},chart::Variant::LegacyChart);
    auto run=graph.run_defaults();graph.set_enabled(run,1001,false);
    run.can_select.back()=0x12345678U;run.move_road.back()=7;
    const auto payload=legacy::write_extension(run,graph);
    require(payload.size()==11280 && payload[1001/8]==2,"PC disabled bit layout is incorrect");
    const auto parsed=legacy::parse_extension(payload,graph);
    require(parsed.can_select==run.can_select && parsed.move_road==run.move_road,"PC extension did not retain padding/current routes");
    auto truncated=payload;truncated.pop_back();
    rejected([&]{legacy::parse_extension(truncated,graph);},"Truncated EXT was accepted");
    pc::Status status;status.game_mode=51;
    Bytes checkpoint=pc::write_status_block(status,pc::legacy_status_bytes);
    word(checkpoint,8);word(checkpoint,0);word(checkpoint,1000); // 1000 zero DWORD variables.
    word(checkpoint,80); // Original BYTE RLE: 39 runs of 255 followed by 55.
    for (int i=0;i<39;++i) {checkpoint.push_back(0);checkpoint.push_back(255);}
    checkpoint.push_back(0);checkpoint.push_back(55);
    word(checkpoint,8);word(checkpoint,0);word(checkpoint,320); // Disabled bits also use marker zero.
    const auto node=legacy::parse_node(checkpoint,graph);
    require(node.status.game_mode==51 && node.run.can_select==graph.run_defaults().can_select,
        "Original zero-marker checkpoint decoded with modern enabled-bit RLE semantics");
    require(legacy::write_node(node,graph)==checkpoint,"Original 5024-byte checkpoint changed during round trip");
    auto damaged=checkpoint;damaged.back()=0xff;
    rejected([&]{legacy::parse_node(damaged,graph);},"Oversized decoded run accepted");
    damaged=checkpoint;damaged.push_back(0);
    rejected([&]{legacy::parse_node(damaged,graph);},"Trailing checkpoint bytes accepted");
    auto saved=node;saved.vars[199]=5;saved.vars[200]=6;saved.vars[300]=7;saved.vars[999]=8;saved.run=run;
    const auto restored=legacy::parse_node(legacy::write_node(saved,graph),graph);
    require(restored.vars==saved.vars && restored.run.can_select==run.can_select && restored.run.move_road==run.move_road,
        "Nonzero variables and reserved words changed in legacy checkpoint");
    std::array<nnn::Value,1001> variables{};variables[200]=99;variables[1000]=77;
    chart::restore_variables(restored.vars,[&](std::size_t i,nnn::Value value){variables[i]=value;});
    require(variables[199]==5 && variables[200]==99 && variables[300]==7 && variables[1000]==77,
        "Node restore changed live system variables or absent high variables");
    require(!legacy::verifies_executable({}) && !legacy::verifies_executable(Bytes(64,0)),"Malformed executable was recognized");
}
void real_contract(const std::filesystem::path& root) {
    require(legacy::recognizes(root),"Installed game's configuration contract was not recognized");
    chart::Graph graph(root,800,600,{},chart::Variant::LegacyChart);
    require(graph.pages().size()==9 && graph.node(1001) && graph.node(9001),"Installed chart pages failed bounded parsing");
    for (const auto* name:{"mushiai.exe","mushiai_chs.exe"}) {
        std::ifstream in(root/name,std::ios::binary);
        Bytes bytes{std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()};
        require(legacy::verifies_executable(bytes),"Original/translated executable algorithm evidence was not recognized");
    }
}
}
int main(int argc,char** argv) {
    try {
        behavior();codecs();
        if (argc>1) real_contract(argv[1]);
        std::cout<<"LEGACY CHART RESULT "<<checks<<" checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr<<"LEGACY CHART FAILED after "<<checks<<" checks: "<<error.what()<<'\n';
        return 1;
    }
}
