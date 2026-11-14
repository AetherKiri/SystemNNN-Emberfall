#include "emberfall/area_assignment.hpp"

#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstring>

namespace emberfall::scene::panel {
namespace {
constexpr std::array<unsigned,8> character_ids={5,8,9,10,11,12,13,15};
constexpr std::array<int,8> portrait_ids={2,1,3,4,6,5,7,8};
constexpr std::array<std::string_view,4> people={
    "\x82\xb5\x82\xb8\x82\xe9","\x8e\x6f\x96\x85","","\x97\x49\x8d\x81"};
constexpr std::array<std::string_view,9> places={
    "\x8d\xb2\x94\x8c\x93\x40",
    "\x90\xb9\x83\x86\x83\x8a\x83\x41\x83\x93\x8f\x97\x8a\x77\x89\x80",
    "\x8f\x5a\x91\xee\x8a\x58",
    "\x90\xe7\x8d\xbb\x91\x71\x92\x86\x89\x9b\x92\xca\x82\xe8",
    "\x90\xe7\x8d\xbb\x91\x71\x91\x8d\x8d\x87\x95\x61\x89\x40",
    "\x90\xe7\x8d\xbb\x91\x71\x83\x5e\x83\x8f\x81\x5b\x97\x5c\x92\xe8\x92\x6e",
    "\x82\x72\x82\x64\x82\x77\x82\x63\x82\x64\x82\x60\x82\x6c\x82\x6e\x82\x6d",
    "\x82\x76\x82\x68\x82\x6d\x82\x73\x82\x64\x82\x71\x82\x67\x82\x64\x82\x60\x82\x71\x82\x73\x82\x72",
    "\x9f\x4e\x96\xd8\x8c\xf6\x89\x80"};
constexpr std::array<std::string_view,8> characters={
    "\x93\x8c","\x82\xdc\x82\xe8\x82\xa9","\x93\xdf\x92\xc3\x94\xfc","\x97\xc1",
    "\x93\x63\x8f\xe3","\x8d\x5d\x97\x9c\x88\xdf","\x83\x41\x83\x69\x83\x78\x83\x89",
    "\x82\x63\x82\x95\x82\x93\x82\x94"};
constexpr std::string_view visible="\x95\x5c\x8e\xa6",position="\x88\xca\x92\x75",
    destination="\x88\xda\x93\xae\x90\xe6",target="\x8a\xd7\x97\x8e\x91\xce\x8f\xdb",
    status="\x8f\xf3\x91\xd4",submission="\x95\x9e\x8f\x5d\x93\x78",gain="\x83\x41\x83\x62\x83\x76\x93\x78";
std::string key(std::string_view prefix,std::string_view suffix) { return std::string(prefix)+std::string(suffix); }
std::string numbered(std::string_view prefix,int index) {
    char number[16]{};std::snprintf(number,sizeof(number),"%02d",index);
    return std::string(prefix)+number;
}
constexpr std::array<Rect,9> area_rects={{{468,109,158,46},{303,95,158,46},{150,163,158,46},
    {48,401,158,46},{519,312,158,46},{640,157,158,46},{2,230,158,46},{210,376,158,46},{352,265,158,46}}};
constexpr std::array<Rect,9> symbol_rects={{{481,148,48,39},{332,126,87,53},{150,200,111,54},
    {110,329,82,77},{537,257,74,59},{677,189,75,91},{91,261,36,44},{273,342,45,36},{379,202,78,61}}};
constexpr std::array<Rect,9> effect_rects={{{0,0,0,0},{314,60,122,130},{142,134,122,130},
    {93,281,122,130},{514,196,122,130},{0,0,0,0},{0,0,0,0},{0,0,0,0},{0,0,0,0}}};
constexpr std::array<Rect,10> origins={{{0,0,0,0},{485,26,0,0},{321,13,0,0},{167,80,0,0},
    {65,318,0,0},{536,228,0,0},{657,73,0,0},{20,148,0,0},{226,293,0,0},{370,184,0,0}}};
constexpr std::array<int,4> icon_width={60,78,44,44},icon_offset={0,46,41,41};
constexpr Rect menu_rect{699,0,100,50},action_rect{640,470,160,130};
std::string icon_asset(unsigned slot) { return numbered("sys/ta_charaicon",slot==3?3:static_cast<int>(slot)+1); }
bool valid_memory(const AssignmentMemory& m) {
    for (unsigned i:{0U,1U,3U})
        if (m.origin[i]<0 || m.origin[i]>9 || m.destination[i]<0 || m.destination[i]>9) return false;
    return true; // Slot 2 is retained verbatim and never indexed as a destination.
}
bool valid_values(const AssignmentValues& v) {
    if (!DateCard::create(v.date) || v.slow_pattern<=0 || v.slow_pattern>INT_MAX/32 || v.character_visible[2]) return false;
    for (std::size_t i=0;i<v.position.size();++i)
        if (v.character_visible[i] && (v.position[i]<0 || v.position[i]>9)) return false;
    for (const auto id:character_ids) {
        if (v.gain[id]>(INT_MAX-100)/5) return false;
    }
    return true;
}
template<std::size_t N> std::vector<std::uint8_t> encode(const std::array<std::int32_t,N>& data) {
    std::vector<std::uint8_t> bytes;bytes.reserve(N*4);
    for (const auto value:data) for (unsigned i=0;i<4;++i)
        bytes.push_back(static_cast<std::uint8_t>(static_cast<std::uint32_t>(value)>>(8*i)));
    return bytes;
}
template<std::size_t N> void decode(const std::vector<std::uint8_t>& bytes,std::array<std::int32_t,N>& data) {
    for (std::size_t i=0;i<N;++i) {
        std::uint32_t word=0;
        for (unsigned j=0;j<4;++j) word|=static_cast<std::uint32_t>(bytes[i*4+j])<<(8*j);
        std::memcpy(&data[i],&word,4);
    }
}
void blit(std::vector<Blit>& out,std::string asset,Rect rect,int sx=0,int sy=0,int opacity=100,int add=0,bool additive=false) {
    if (rect.width<=0 || rect.height<=0) return;
    out.push_back({std::move(asset),rect.x,rect.y,sx,sy,rect.width,rect.height,true,opacity,add,add,add,additive});
}
int pingpong(int phase,int frames) { return phase>=frames?frames*2-1-phase:phase; }
int coordinate(std::int64_t value,int extent) {
    // Existing paint_texture also forms origin+extent in its clipping path.
    return static_cast<int>(std::clamp<std::int64_t>(value,INT_MIN,static_cast<std::int64_t>(INT_MAX)-extent));
}
void append_area_effect(std::vector<Blit>& out,unsigned area,int value,int frame,int slow) {
    if (value<1 || value>6) return;
    const auto rect=effect_rects[area];
    const int image_frame=value<5?(std::max(0,frame)/slow)%8:(std::max(0,frame)/slow)/2;
    blit(out,numbered("sys/ta_area_effect",value),rect,rect.width*image_frame);
    blit(out,numbered("sys/area_effect",value),rect,rect.width*image_frame,0,100,0,true);
}
}

AssignmentBlocks export_assignment(const AssignmentMemory& m) {
    return {encode(m.previous_character_state),encode(m.previous_area_state),encode(m.area_target),
        encode(m.origin),encode(m.destination),{static_cast<std::uint8_t>(m.choosing?1:0)}};
}
std::optional<AssignmentMemory> import_assignment(const AssignmentBlocks& blocks) {
    for (std::size_t i=0;i<blocks.size();++i) if (blocks[i].size()!=assignment_block_sizes[i]) return std::nullopt;
    if (blocks[5][0]>1) return std::nullopt;
    AssignmentMemory candidate;
    decode(blocks[0],candidate.previous_character_state);decode(blocks[1],candidate.previous_area_state);
    decode(blocks[2],candidate.area_target);decode(blocks[3],candidate.origin);decode(blocks[4],candidate.destination);
    candidate.choosing=blocks[5][0]!=0;candidate.resume=candidate.choosing;
    return valid_memory(candidate)?std::optional<AssignmentMemory>(candidate):std::nullopt;
}
std::string_view assignment_destination_name(unsigned slot) noexcept {
    static const std::array<std::string,4> names={key(people[0],destination),key(people[1],destination),"",key(people[3],destination)};
    return slot<names.size()?std::string_view(names[slot]):std::string_view{};
}
const std::vector<std::string>& assignment_variable_names() {
    static const auto names=[] {
        std::vector<std::string> out;
        for (const auto name:date_variable_names()) out.emplace_back(name);
        for (const auto person:people) if (!person.empty())
            for (const auto suffix:{visible,position,destination}) out.push_back(key(person,suffix));
        for (std::size_t i=0;i<places.size();++i) {
            for (const auto suffix:{visible,target}) out.push_back(key(places[i],suffix));
            if (i>=1 && i<=4) out.push_back(key(places[i],status));
        }
        for (const auto person:characters) for (const auto suffix:{status,submission,gain}) out.push_back(key(person,suffix));
        return out;
    }();
    return names;
}
const std::vector<std::string>& assignment_assets() {
    static const auto assets=[] {
        std::vector<std::string> out={"sys/day_bg01","sys/day_bg02","sys/map_bg01","sys/map_bg02",
            "sys/ta_font_01","sys/ta_font_02","sys/ta_day_pt01","sys/ta_mugen01","sys/ta_mugen02",
            "sys/ta_bloodpower01","sys/ta_bloodbar01","sys/ta_bloodpowpt01","sys/ta_bloodpowpt02","sys/ta_bloodpowpt03",
            "sys/ta_moon_base01","sys/ta_moon_base02","sys/ta_moon_pt01","sys/ta_sysmenu1_base01","sys/ta_sysmenu1_bt01",
            "sys/ta_action_bt1","sys/ta_resultbar01"};
        for (int i=1;i<=10;++i) out.push_back(numbered("sys/ta_day_moon",i));
        for (int i=1;i<=9;++i) for (auto p:{"sys/ta_area_bt","sys/ta_area_sym"}) out.push_back(numbered(p,i));
        for (int i=1;i<=6;++i) for (auto p:{"sys/ta_area_effect","sys/area_effect"}) out.push_back(numbered(p,i));
        for (int i=1;i<=3;++i) out.push_back(numbered("sys/ta_charaicon",i));
        for (int i=1;i<=8;++i) for (int j=1;j<=3;++j) out.push_back("sys/ta_resultchara"+std::to_string(i)+std::to_string(j));
        return out;
    }();
    return assets;
}
bool recognize_assignment_contract(const std::vector<std::string>& commands,int extension_count,
    const std::vector<int>& sizes,const HasName& has_variable,const HasName& has_asset) {
    if (extension_count!=6 || sizes.size()!=6 || !has_variable || !has_asset) return false;
    for (std::size_t i=0;i<sizes.size();++i) if (sizes[i]!=static_cast<int>(assignment_block_sizes[i])) return false;
    for (const auto* name:{"printdate","selectplace","result"})
        if (std::count(commands.begin(),commands.end(),name)!=1) return false;
    for (const auto& name:assignment_variable_names()) if (!has_variable(name)) return false;
    for (const auto& name:assignment_assets()) if (!has_asset(name)) return false;
    return true;
}
std::optional<AssignmentValues> read_assignment_values(const ReadVariable& read,int slow_pattern) {
    const auto date=read_date_values(read);if (!date) return std::nullopt;
    AssignmentValues result;result.date=*date;result.slow_pattern=slow_pattern;
    result.character_state.fill(1);result.submission.fill(-1);result.gain.fill(-1);result.area_state.fill(1);
    const auto get=[&](std::string_view prefix,std::string_view suffix,int& value) {
        const auto found=read(key(prefix,suffix));if (!found) return false;value=*found;return true;
    };
    for (std::size_t i=0;i<people.size();++i) if (i!=2) {
        int show=0;
        if (!get(people[i],visible,show) || !get(people[i],position,result.position[i])) return std::nullopt;
        result.character_visible[i]=show==1;
    }
    for (std::size_t i=0;i<places.size();++i) {
        int show=0;
        if (!get(places[i],visible,show) || !get(places[i],target,result.area_target[i]) ||
            (i>=1 && i<=4 && !get(places[i],status,result.area_state[i]))) return std::nullopt;
        result.area_visible[i]=show==1;
    }
    for (std::size_t i=0;i<characters.size();++i) {
        const auto id=character_ids[i];
        if (!get(characters[i],status,result.character_state[id]) || !get(characters[i],submission,result.submission[id]) ||
            !get(characters[i],gain,result.gain[id])) return std::nullopt;
    }
    return valid_values(result)?std::optional<AssignmentValues>(result):std::nullopt;
}

std::optional<AreaAssignment> AreaAssignment::enter(int mode,const AssignmentValues& values,const AssignmentMemory& memory) {
    if ((mode!=selection_mode && mode!=result_mode) || !valid_values(values) || !valid_memory(memory)) return std::nullopt;
    AreaAssignment result;auto& s=result.state_;s.mode=mode;s.values=values;s.memory=memory;
    if (mode==selection_mode) s.memory.choosing=true;
    if (!memory.resume) {
        for (std::size_t i=0;i<4;++i) s.memory.origin[i]=s.memory.destination[i]=values.character_visible[i]?values.position[i]:0;
    }
    if (mode==selection_mode && !memory.resume) {
        for (std::size_t i=0;i<9;++i) {
            s.memory.previous_area_state[i]=0;
            if (!values.area_visible[i]) continue; // The PC retains hidden area's target slot.
            s.memory.area_target[i]=values.area_target[i];
            if (s.memory.area_target[i]!=0) s.memory.previous_area_state[i]=values.area_state[i];
        }
        for (std::size_t i=5;i<29;++i) s.memory.previous_character_state[i]=values.character_state[i];
    }
    if (mode==result_mode) {
        for (std::size_t i=5;i<29;++i) if (values.gain[i]>0) {
            if (values.submission[i]<0 || memory.previous_character_state[i]<1 || memory.previous_character_state[i]>3 ||
                values.character_state[i]<1 || values.character_state[i]>3) return std::nullopt;
            s.results.push_back({static_cast<int>(i),values.submission[i],values.gain[i]});
        }
        for (std::size_t i=0;i<9;++i)
            if (values.area_visible[i] && memory.area_target[i]!=0) s.result_area_state[i]=values.area_state[i];
    }
    for (std::size_t i=0;i<9;++i) if (values.area_visible[i] && s.memory.area_target[i]!=0) {
        const int value=mode==selection_mode?s.memory.previous_area_state[i]:s.result_area_state[i];
        if (value<0 || value>6) return std::nullopt;
    }
    s.suppress_fade=memory.suppress_entrance;s.memory.resume=false;s.memory.suppress_entrance=false;
    return result;
}
std::optional<AreaAssignment> AreaAssignment::restore(const State& s) {
    if (!valid_values(s.values) || !valid_memory(s.memory) || (s.mode!=selection_mode && s.mode!=result_mode) ||
        s.selected< -1 || s.selected>3 || s.selected==2 || s.pulse< -1 || s.pulse>15 ||
        s.area_frame< -1 || s.area_frame>=16*s.values.slow_pattern || s.result_pulse< -1 || s.result_pulse>15 ||
        s.result_count< -1 || s.fade_in<0 || s.fade_in>10 || s.fade_out< -1 || s.fade_out>10 ||
        s.exit_delay<0 || s.exit_delay>11 || s.results.size()>24 || s.result_index>s.results.size() ||
        (s.pending_system_mode!=0 && s.pending_system_mode!=5 && s.pending_system_mode!=6 && s.pending_system_mode!=12 && s.pending_system_mode!=13)) return std::nullopt;
    if ((s.mode==selection_mode && (!s.results.empty() || s.result_index!=0)) ||
        (s.mode==result_mode && (s.selected>=0 || s.pending_system_mode!=0)) ||
        (s.selected>=0 && !s.values.character_visible[static_cast<std::size_t>(s.selected)])) return std::nullopt;
    int previous=0;
    for (const auto& row:s.results) {
        if (row.character<=previous || row.character<5 || row.character>=29 ||
            std::find(character_ids.begin(),character_ids.end(),static_cast<unsigned>(row.character))==character_ids.end()) return std::nullopt;
        const auto id=static_cast<std::size_t>(row.character);
        if (row.base<0 || row.base!=s.values.submission[id] || row.gain!=s.values.gain[id] || row.gain<=0 ||
            s.memory.previous_character_state[id]<1 || s.memory.previous_character_state[id]>3 ||
            s.values.character_state[id]<1 || s.values.character_state[id]>3) return std::nullopt;
        previous=row.character;
    }
    const int count_limit=s.result_index<s.results.size()?std::max(60,s.results[s.result_index].gain*5):60;
    if (s.result_count>count_limit) return std::nullopt;
    for (const int value:s.result_area_state) if (value<0 || value>6) return std::nullopt;
    if (s.mode==selection_mode) for (std::size_t i=0;i<9;++i)
        if (s.values.area_visible[i] && s.memory.area_target[i]!=0 &&
            (s.memory.previous_area_state[i]<0 || s.memory.previous_area_state[i]>6)) return std::nullopt;
    const auto button_ok=[&](const Button& b,int frames,bool action=false) {
        if (b.state<0 || b.state>4 || b.phase<0 || (frames?b.phase>=frames*2*s.values.slow_pattern:b.phase!=0)) return false;
        if ((frames==8 && b.state==3) || (frames!=8 && b.state>2)) return false;
        if (!action) return !b.activated && !b.ready && b.activated_count==0;
        return b.activated_count>=0 && b.activated_count<=9*s.values.slow_pattern &&
            (!b.ready || (b.activated && b.activated_count==9*s.values.slow_pattern));
    };
    for (const auto& b:s.characters) if (!button_ok(b,8)) return std::nullopt;
    for (const auto& b:s.areas) if (!button_ok(b,0)) return std::nullopt;
    for (const auto& b:s.symbols) if (!button_ok(b,0)) return std::nullopt;
    if (!button_ok(s.action,10,true) || !button_ok(s.menu,0)) return std::nullopt;
    AreaAssignment result;result.state_=s;return result;
}
Rect AreaAssignment::character_rect(unsigned slot) const {
    const auto& origin=origins[static_cast<std::size_t>(state_.memory.destination[slot])];
    return {origin.x+icon_offset[slot],origin.y,icon_width[slot],94};
}
bool AreaAssignment::can_open_system_menu() const noexcept {
    return state_.mode==selection_mode && state_.selected<0 && !state_.action.activated &&
        state_.fade_out<0 && state_.pending_system_mode==0 && !state_.finished;
}
bool AreaAssignment::can_save() const noexcept {
    return state_.mode==selection_mode && state_.selected<0 && !state_.action.activated && !state_.finished &&
        (state_.fade_out<0 || state_.pending_system_mode!=0);
}
bool AreaAssignment::update_button(Button& b,const Rect& rect,std::string_view asset,int animation_frames,
    bool enabled,const Input& input,bool moved,const HitTest& hit) {
    if (animation_frames>0) {
        b.phase=(b.phase+1)%(animation_frames*2*state_.values.slow_pattern);
        if (b.activated) {
            b.activated_count=std::min(b.activated_count+1,9*state_.values.slow_pattern);
            b.ready=b.activated_count>=9*state_.values.slow_pattern;
        }
    }
    if (!enabled) return false;
    if (b.activated) return b.ready;
    const bool on=rect.contains(input.pointer_x,input.pointer_y) && hit &&
        hit(asset,input.pointer_x-rect.x+b.state*rect.width,input.pointer_y-rect.y);
    if (moved && b.state==0 && on) b.state=1;
    else if (moved && b.state==1 && !on) b.state=0;
    if (b.state==1 && input.click) {
        b.state=2;
        if (animation_frames==10) { b.activated=true;b.ready=false;b.activated_count=0; }
    }
    return on;
}
void AreaAssignment::begin_exit(int system_mode) {
    state_.exit_delay=0;state_.pending_system_mode=system_mode;
    if (system_mode) { state_.memory.resume=true;state_.memory.suppress_entrance=true;state_.suppress_fade=true; }
    else { state_.memory.choosing=false;state_.fade_in=0;state_.fade_out=0; }
}
void AreaAssignment::advance_selection(const Input& input,bool moved,const HitTest& hit,Output& events) {
    auto& s=state_;
    if (s.selected<0) {
        if (!s.action.activated) {
            const bool menu_on=update_button(s.menu,menu_rect,"sys/ta_sysmenu1_bt01",0,true,input,moved,hit);
            const bool menu_click=menu_on && input.click;
            const bool shortcut=input.system_mode==5 || input.system_mode==6 || input.system_mode==12 || input.system_mode==13;
            if (menu_click || shortcut) { events.sounds.push_back(0);begin_exit(menu_click?13:input.system_mode);return; }
            int picked=-1;
            for (unsigned i=0;i<4;++i) if (s.values.character_visible[i])
                if (update_button(s.characters[i],character_rect(i),icon_asset(i),8,true,input,moved,hit) && input.click) {
                    picked=static_cast<int>(i);events.sounds.push_back(3);
                }
            if (picked>=0) {
                s.selected=picked;s.characters[static_cast<std::size_t>(picked)]={};
                s.characters[static_cast<std::size_t>(picked)].state=4;
                s.areas={};s.symbols={};
            }
        }
        Input action_input=input;
        if (input.confirm && s.selected<0 && !s.action.activated) {
            // Keyboard activation is a host action for the original action button.
            s.action.state=2;s.action.activated=true;s.action.activated_count=0;
            events.sounds.push_back(1);
        } else {
            const bool was=s.action.activated;
            const bool on=update_button(s.action,action_rect,"sys/ta_action_bt1",10,true,action_input,moved,hit);
            if (!was && s.action.activated) events.sounds.push_back(1);
            if (on && s.action.activated && s.action.ready && s.selected<0) {
                for (unsigned i:{0U,1U,3U}) events.writes.push_back({std::string(assignment_destination_name(i)),0});
                for (unsigned i:{0U,1U,3U}) if (s.values.character_visible[i])
                    events.writes.push_back({std::string(assignment_destination_name(i)),s.memory.destination[i]});
                begin_exit();
            }
        }
        return;
    }
    Input no_click=input;no_click.click=false;no_click.confirm=false;
    for (unsigned i=0;i<4;++i) if (s.values.character_visible[i] && static_cast<int>(i)!=s.selected)
        update_button(s.characters[i],character_rect(i),icon_asset(i),8,true,no_click,moved,hit);
    update_button(s.action,action_rect,"sys/ta_action_bt1",10,true,no_click,moved,hit);
    int area=-1;
    for (unsigned i=0;i<9;++i) if (s.values.area_visible[i]) {
        const bool a=update_button(s.areas[i],area_rects[i],numbered("sys/ta_area_bt",static_cast<int>(i)+1),0,true,input,moved,hit);
        const bool b=update_button(s.symbols[i],symbol_rects[i],numbered("sys/ta_area_sym",static_cast<int>(i)+1),0,true,input,moved,hit);
        if ((a || b) && input.click) area=static_cast<int>(i);
    }
    if (area>=0 || input.cancel) {
        const auto selected=static_cast<std::size_t>(s.selected);
        if (area>=0) s.memory.destination[selected]=area+1;
        s.characters[selected]={};s.selected=-1;events.sounds.push_back(area>=0?2:4);
    }
}
void AreaAssignment::advance_result(bool click,Output& events) {
    auto& s=state_;++s.result_count;s.result_pulse=(s.result_pulse+1)%16;
    if (s.result_index<s.results.size()) {
        const auto row=s.results[s.result_index];const int duration=row.gain*5;
        if (!s.result_hold && !s.result_flash && s.result_count==0) events.sounds.push_back(5);
        if (!s.result_hold && !s.result_flash && (s.result_count>=duration || click)) {
            s.result_count=0;
            if (s.memory.previous_character_state[static_cast<std::size_t>(row.character)]!=s.values.character_state[static_cast<std::size_t>(row.character)]) {
                s.result_flash=true;events.sounds.push_back(6);
            } else s.result_hold=true;
        } else if (s.result_flash) {
            if (s.result_count>=20 || click) { s.result_count=0;s.result_flash=false;s.result_hold=true; }
        } else if (s.result_hold && (s.result_count>=50 || click)) {
            click=false;s.result_count=0;++s.result_index;
            if (s.result_index<s.results.size()) s.result_hold=false;
        }
    }
    if (s.result_index<s.results.size()) return;
    if (!s.area_hold) {
        if (!s.area_changed) {
            for (unsigned i=0;i<9;++i) if (s.values.area_visible[i] && s.memory.area_target[i]!=0 &&
                s.result_area_state[i]!=s.memory.previous_area_state[i]) {
                s.area_changed=true;s.area_frame=0;s.result_count=0;
                events.sounds.push_back(s.result_area_state[i]<5?6:7);break;
            }
        }
        if (s.area_changed && s.result_count<60 && click) s.result_count=60;
        if (!s.area_changed || s.result_count>=60) { s.area_hold=true;s.result_count=s.area_changed?40:0; }
    }
    if (s.area_hold && s.result_count<40 && click) s.result_count=40;
    if (s.area_hold && s.result_count>=40) {
        if (s.area_changed) for (unsigned i=0;i<9;++i)
            if (s.values.area_visible[i] && s.memory.area_target[i]!=0) s.memory.previous_area_state[i]=s.result_area_state[i];
        begin_exit();
    }
}
Output AreaAssignment::advance(const Input& input,const HitTest& hit) {
    Output events;
    for (unsigned frame=0;frame<input.frames && !state_.finished && !state_.system_dispatched;++frame) {
        auto& s=state_;s.pulse=(s.pulse+1)%16;
        if (s.fade_in>0) { --s.fade_in;if (s.fade_in==0) s.suppress_fade=false; }
        if (s.fade_out>=0 || s.pending_system_mode) {
            if (s.fade_out>=0) s.fade_out=std::min(10,s.fade_out+1);
            if (s.exit_delay>10) {
                if (s.pending_system_mode) { events.system_mode=s.pending_system_mode;s.system_dispatched=true; }
                else s.finished=true;
            } else ++s.exit_delay;
            continue;
        }
        s.area_frame=(s.area_frame+1)%(s.values.slow_pattern*16);
        Input current=input;if (frame) {current.click=false;current.cancel=false;current.confirm=false;current.system_mode=0;}
        const bool moved=current.pointer_x!=s.pointer_x || current.pointer_y!=s.pointer_y;
        s.pointer_x=current.pointer_x;s.pointer_y=current.pointer_y;
        if (s.mode==selection_mode) advance_selection(current,moved,hit,events);
        else advance_result(current.click || current.cancel || current.confirm,events);
    }
    auto out=render();out.sounds=std::move(events.sounds);out.writes=std::move(events.writes);out.system_mode=events.system_mode;
    return out;
}
Output AreaAssignment::render() const {
    const auto& s=state_;Output out;out.mode=s.mode;out.finished=s.finished;
    out.blits.push_back({s.values.date.moon==10?"sys/map_bg02":"sys/map_bg01",0,0,0,0,800,600,false});
    blit(out.blits,s.values.date.easy==1?"sys/ta_moon_base02":"sys/ta_moon_base01",{0,0,100,174});
    blit(out.blits,"sys/ta_moon_pt01",{10,5,50,50},(s.values.date.moon-1)*50);
    if (s.values.date.easy==1) append_remaining_days(out.blits,s.values.date.remaining_days,true);
    const auto draw_areas=[&](bool result) {
        for (unsigned i=0;i<9;++i) if (s.values.area_visible[i]) {
            blit(out.blits,numbered("sys/ta_area_sym",static_cast<int>(i)+1),symbol_rects[i],s.symbols[i].state*symbol_rects[i].width);
            blit(out.blits,numbered("sys/ta_area_bt",static_cast<int>(i)+1),area_rects[i],s.areas[i].state*area_rects[i].width);
            if (s.memory.area_target[i]!=0 && (!result || s.result_area_state[i]!=s.memory.previous_area_state[i]))
                append_area_effect(out.blits,i,result?s.result_area_state[i]:s.memory.previous_area_state[i],s.area_frame,s.values.slow_pattern);
        }
    };
    if (s.mode==selection_mode) {
        draw_areas(false);
        for (unsigned i=0;i<4;++i) if (s.values.character_visible[i] && static_cast<int>(i)!=s.selected) {
            const auto rect=character_rect(i);const auto& b=s.characters[i];
            if (b.state!=0 && b.state!=1) continue;
            const int atlas=b.state==1?8:pingpong(b.phase/s.values.slow_pattern,8);
            blit(out.blits,icon_asset(i),rect,atlas*rect.width);
        }
        blit(out.blits,"sys/ta_sysmenu1_base01",{699,0,100,65});
        blit(out.blits,"sys/ta_sysmenu1_bt01",menu_rect,s.menu.state*100);
        if (s.action.activated) {
            const int frame=s.action.ready?8:s.action.activated_count/s.values.slow_pattern;
            blit(out.blits,"sys/ta_action_bt1",action_rect,frame*160,130);
        } else if (s.action.state==1) blit(out.blits,"sys/ta_action_bt1",action_rect,0,130);
        else blit(out.blits,"sys/ta_action_bt1",action_rect,pingpong(s.action.phase/s.values.slow_pattern,10)*160);
        if (s.values.date.easy==1) append_power(out.blits,s.values.date.power,s.pulse);
        if (s.selected>=0) {
            const auto slot=static_cast<unsigned>(s.selected);
            blit(out.blits,icon_asset(slot),{coordinate(static_cast<std::int64_t>(s.pointer_x)-icon_width[slot]/2,icon_width[slot]),
                coordinate(static_cast<std::int64_t>(s.pointer_y)-90,94),icon_width[slot],94},8*icon_width[slot]);
        }
    } else if (s.result_index<s.results.size()) {
        const auto row=s.results[s.result_index];const auto id=static_cast<std::size_t>(row.character);
        const auto found=std::find(character_ids.begin(),character_ids.end(),static_cast<unsigned>(row.character));
        if (found!=character_ids.end()) {
            const auto portrait=portrait_ids[static_cast<std::size_t>(found-character_ids.begin())];
            const auto asset=[&](int state){return "sys/ta_resultchara"+std::to_string(portrait)+std::to_string(state);};
            const int old=s.memory.previous_character_state[id],now=s.values.character_state[id];
            const Rect character{240,97,319,342};
            const int alpha=s.result_hold && s.result_count>=40?(49-s.result_count)*10:100;
            const int add=s.result_flash && s.result_count<10?(s.result_count+1)*25:0;
            if (s.result_flash && s.result_count>=10) {
                blit(out.blits,asset(now),character,0,0,100,255);
                blit(out.blits,asset(now),character,0,0,(s.result_count-9)*10);
            } else blit(out.blits,asset(s.result_hold?now:old),character,0,0,alpha,add);
            const int pulse=pingpong(std::max(0,s.result_pulse),8);
            const bool growing=!s.result_hold && !s.result_flash;
            const auto units=std::min<std::int64_t>(100,static_cast<std::int64_t>(row.base)+(growing?s.result_count/5:row.gain));
            const int height=static_cast<int>(units)*3;
            const Rect bar{257,421-height,63,height};
            if (s.result_flash && s.result_count>=10) blit(out.blits,"sys/ta_resultbar01",bar,pulse*63,300-height,(s.result_count-9)*10);
            else blit(out.blits,"sys/ta_resultbar01",bar,pulse*63,300-height,alpha,add);
            if (growing) blit(out.blits,"sys/ta_resultbar01",{257,418-height,63,3},pulse*63,297-height);
        }
    } else if (s.area_changed) draw_areas(true);
    if (!s.suppress_fade) out.black_percent=s.fade_in>0?s.fade_in*10:std::max(0,s.fade_out)*10;
    return out;
}

} // namespace emberfall::scene::panel
