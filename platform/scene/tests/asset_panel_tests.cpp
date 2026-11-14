#include "emberfall/area_assignment.hpp"

#include <algorithm>
#include <climits>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

using namespace emberfall::scene::panel;
namespace {
unsigned checks=0;
void check(bool condition,const char* detail) {
    ++checks;if (!condition) throw std::runtime_error(detail);
}
const Blit* find(const Output& out,std::string_view name) {
    const auto at=std::find_if(out.blits.begin(),out.blits.end(),[&](const Blit& b){return b.asset==name;});
    return at==out.blits.end()?nullptr:&*at;
}
AssignmentValues values() {
    AssignmentValues v;v.date={3,50,1,25};
    v.character_visible={true,true,false,true};v.position={1,2,0,3};v.area_visible.fill(true);
    for (unsigned id:{5U,8U,9U,10U,11U,12U,13U,15U}) {v.submission[id]=0;v.gain[id]=0;}
    v.character_state[5]=2;v.submission[5]=30;v.area_target[1]=5;
    return v;
}
const HitTest opaque=[](auto,int,int){return true;};
Input pointer(int x,int y,bool click=false,bool cancel=false) {
    Input in;in.pointer_x=x;in.pointer_y=y;in.click=click;in.cancel=cancel;return in;
}
bool same_draw(const Output& a,const Output& b) {
    if (a.mode!=b.mode || a.black_percent!=b.black_percent || a.finished!=b.finished || a.blits.size()!=b.blits.size()) return false;
    for (std::size_t i=0;i<a.blits.size();++i) {
        const auto& x=a.blits[i];const auto& y=b.blits[i];
        if (x.asset!=y.asset || x.x!=y.x || x.y!=y.y || x.source_x!=y.source_x || x.source_y!=y.source_y ||
            x.width!=y.width || x.height!=y.height || x.alpha!=y.alpha || x.opacity_percent!=y.opacity_percent ||
            x.add_red!=y.add_red || x.add_green!=y.add_green || x.add_blue!=y.add_blue || x.additive!=y.additive ||
            x.translucent!=y.translucent) return false;
    }
    return true;
}
void date_contract() {
    auto card=*DateCard::create({10,15,1,71});
    for (unsigned frame=0;frame<53;++frame) {
        card.advance({});const auto out=card.render();
        check(out.finished==(frame==52),"date completion is the original delayed callback");
        const int black=frame<9?90-static_cast<int>(frame)*10:frame<=40?0:std::min(100,(static_cast<int>(frame)-40)*10);
        check(out.black_percent==black,"date entrance and exit count exactly");
        if (frame==0) {
            check(out.blits.front().asset=="sys/day_bg02","full moon chooses original background");
            const auto* moon=find(out,"sys/ta_day_moon10");
            check(moon && moon->x==250 && moon->y==120 && moon->width==300 && moon->height==300,"original moon geometry");
            const auto* power=find(out,"sys/ta_bloodbar01");
            check(power && power->width==284 && power->x==204 && power->y==514,"original power bar geometry");
            check(out.blits[3].source_x==32 && out.blits[4].source_x==160 && out.blits[4].x==460,"date digits use original atlas spacing");
        }
    }
    auto batched=*DateCard::create({10,15,1,71});Input many;many.frames=53;batched.advance(many);
    check(same_draw(card.render(),batched.render()),"date batched frames preserve the clock");
    auto infinity=*DateCard::create({1,99,1,0});
    check(find(infinity.render(),"sys/ta_mugen02")!=nullptr,"99 uses infinity asset");
    auto normal=*DateCard::create({1,99,0,0});check(normal.render().blits.size()==2,"normal difficulty does not show easy HUD");
    Input click;click.click=true;normal.advance(click);Input exit;exit.frames=12;normal.advance(exit);
    check(normal.render().finished,"left click starts date exit");
    auto cancelled=*DateCard::create({1,1,0,0});Input cancel;cancel.cancel=true;cancelled.advance(cancel);
    check(cancelled.state().fade_out<0,"right click does not dismiss date");
    check(!DateCard::create({0,1,0,0}) && !DateCard::create({1,100,1,0}),"invalid source atlas indices rejected");
    check(DateCard::create({1,-1,0,-1}).has_value(),"unused normal-mode HUD fields are retained without invented defaults");
    auto bad=card.state();bad.pulse=16;check(!DateCard::restore(bad),"invalid date clock snapshot rejected");
}
void extension_contract() {
    auto v=values();AssignmentMemory original;
    original.previous_character_state[2]=-400;original.origin[2]=-999;original.destination[2]=10000;
    original.area_target[8]=-800;
    auto raw=export_assignment(original);
    for (std::size_t i=0;i<raw.size();++i) check(raw[i].size()==assignment_block_sizes[i],"exact PC EXT block size");
    auto imported=import_assignment(raw);
    check(imported && export_assignment(*imported)==raw,"unused state retained byte for byte");
    auto invalid=raw;invalid[3].pop_back();check(!import_assignment(invalid),"partial EXT rejected as a whole");
    invalid=raw;invalid[5][0]=2;check(!import_assignment(invalid),"invalid PC boolean rejected");
    invalid=raw;invalid[4][0]=10;check(!import_assignment(invalid),"out of range destination rejected");
    check(export_assignment(original)==raw,"failed candidate leaves source state intact");
    auto map=*AreaAssignment::enter(51,v,original);
    check(map.memory().previous_character_state[5]==2 && map.memory().previous_character_state[6]==1 &&
        map.memory().previous_character_state[2]==-400,"fresh map captures helper values and retains unused leading slots");
    check(map.memory().origin[0]==1 && map.memory().destination[1]==2 && map.memory().choosing,"fresh map captures destinations and chooses flag");
    auto saved=*import_assignment(export_assignment(map.memory()));
    check(saved.resume,"PC load derives resume flag from EXT105");
    v.position[0]=9;v.character_state[5]=3;v.area_state[1]=5;
    auto resumed=*AreaAssignment::enter(51,v,saved);
    check(resumed.memory().destination[0]==1 && resumed.memory().previous_character_state[5]==2 &&
        resumed.memory().previous_area_state[1]==1,"resuming preserves entry baselines instead of recapturing variables");
    check(resumed.render().black_percent==100,"PC load preserves baselines but clears the menu-only fade suppression");
    v.area_visible[8]=false;auto hidden=*AreaAssignment::enter(51,v,original);
    check(hidden.memory().area_target[8]==-800 && hidden.memory().previous_area_state[8]==0,"hidden target is retained as on PC");
    v.character_visible[3]=false;v.position[3]=-1;v.gain[8]=-99;v.submission[8]=-999;v.area_state[8]=9000;
    check(AreaAssignment::enter(51,v).has_value(),"hidden or unused values do not block the map");
}
void assignment_input() {
    auto map=*AreaAssignment::enter(51,values());
    check(map.can_open_system_menu() && map.can_save(),"idle map opens original system modes");
    map.advance(pointer(510,50),opaque);
    const auto picked=map.advance(pointer(510,50,true),opaque);
    check(map.state().selected==0 && picked.sounds==std::vector<int>{3} && !map.can_open_system_menu(),"pick character with original sound and menu gating");
    auto restored=AreaAssignment::restore(map.state());check(restored && same_draw(map.render(),restored->render()),"dragging snapshot retains exact presentation");
    auto bound=map.state();bound.characters[0].phase=16;
    check(!AreaAssignment::restore(bound),"eight-frame character rejects phase sixteen");
    bound.characters[0].phase=17;check(!AreaAssignment::restore(bound),"character snapshot cannot make negative atlas coordinates");
    bound=map.state();bound.action.phase=19;check(AreaAssignment::restore(bound).has_value(),"action owns its separate ten-frame animation");
    bound.action.phase=20;check(!AreaAssignment::restore(bound),"action animation upper bound rejected");
    bound=map.state();bound.areas[0].phase=1;check(!AreaAssignment::restore(bound),"static area button cannot own an animation counter");
    bound=map.state();bound.pointer_x=INT_MIN;bound.pointer_y=INT_MIN;
    auto outside=AreaAssignment::restore(bound);check(outside.has_value(),"extreme pointer snapshot remains safe to render");
    const auto outside_frame=outside->render();
    check(outside_frame.blits.back().x==INT_MIN && outside_frame.blits.back().y==INT_MIN,"drag origin arithmetic saturates without integer wrap");
    auto far=outside->advance(pointer(INT_MAX,INT_MAX),opaque);
    check(far.blits.back().x==INT_MAX-60 && far.blits.back().y==INT_MAX-94,"positive extreme pointer leaves finite rectangle edges");
    far=outside->advance(pointer(-1000,1000),opaque);
    check(far.blits.back().x== -1030 && far.blits.back().y==910,"normal off-canvas dragging is preserved");
    const auto cancelled=map.advance(pointer(600,400,false,true),opaque);
    check(map.state().selected<0 && map.memory().destination[0]==1 && cancelled.sounds==std::vector<int>{4},"right click cancels only the drag");
    map.advance(pointer(510,50,true),opaque);map.advance(pointer(200,185),opaque);
    const auto assigned=map.advance(pointer(200,185,true),opaque);
    check(map.memory().destination[0]==3 && assigned.sounds==std::vector<int>{2} && assigned.writes.empty(),"assignment remains private until action completes");
    map.advance(pointer(700,540),opaque);const auto started=map.advance(pointer(700,540,true),opaque);
    check(started.sounds==std::vector<int>{1} && !map.can_save(),"original action animation gates saves");
    Input animation=pointer(700,540);animation.frames=9;
    const auto done=map.advance(animation,opaque);
    check(done.writes.size()==6 && done.writes[0].value==0 && done.writes[3].value==3 && done.writes[4].value==2 && done.writes[5].value==3,
        "action clears destination variables then writes visible character assignments");
    check(!map.memory().choosing && !done.finished,"action clears EXT105 before delayed script return");
    animation.frames=12;check(map.advance(animation,opaque).finished,"map exit waits original twelve callbacks");
    auto mask=*AreaAssignment::enter(51,values());mask.advance(pointer(510,50,true),[](auto,int,int){return false;});
    check(mask.state().selected<0,"transparent original asset pixel cannot select a character");
    auto overlapping_values=values();overlapping_values.position={1,1,0,1};
    auto overlapping=*AreaAssignment::enter(51,overlapping_values);
    const auto overlap=overlapping.advance(pointer(535,50,true),opaque);
    check(overlapping.state().selected==3 && overlap.sounds==std::vector<int>({3,3,3}),"overlapping opaque icons retain original last-hit order");
    check(!find(overlap,"sys/ta_charaicon01") && !find(overlap,"sys/ta_charaicon02"),"other pressed animated icons retain their original pressed state");
    auto menu=*AreaAssignment::enter(51,values());menu.advance(pointer(710,25),opaque);
    menu.advance(pointer(710,25,true),opaque);Input delay;delay.frames=12;
    const auto request=menu.advance(delay,opaque);
    check(request.system_mode==13 && request.writes.empty() && !request.finished && menu.memory().resume && menu.can_save(),
        "system menu suspends map and preserves savable state");
    check(menu.advance(delay,opaque).system_mode==0,"system-mode request is consumed once");
    auto returned=*AreaAssignment::enter(51,values(),menu.memory());
    check(returned.render().black_percent==0 && returned.memory().destination==menu.memory().destination,"returning from original menu skips private entrance fade");
    for (int mode:{5,6,12,13}) {
        auto shortcut=*AreaAssignment::enter(51,values());Input in;in.system_mode=mode;
        shortcut.advance(in,opaque);check(shortcut.advance(delay,opaque).system_mode==mode,"original system shortcut keeps its mode number");
    }
}
void result_contract() {
    auto v=values();auto memory=AreaAssignment::enter(51,v)->memory();
    v.character_state[5]=3;v.submission[5]=32;v.gain[5]=2;v.area_state[1]=2;
    auto result=*AreaAssignment::enter(52,v,memory);
    check(!result.can_save() && !result.can_open_system_menu(),"result only displays script-computed values");
    for (unsigned frame=0;frame<153;++frame) {
        const auto out=result.advance({},opaque);
        check(out.writes.empty(),"result never recomputes or writes story variables");
        check(out.finished==(frame==152),"result callback timeline matches PC");
        if (frame==0) check(out.sounds==std::vector<int>{5} && find(out,"sys/ta_resultchara22"),"result starts old state portrait and original sound");
        if (frame==10) {
            const auto* p=find(out,"sys/ta_resultchara22");
            check(out.sounds==std::vector<int>{6} && p && p->add_red==25,"state change starts original color-add flash");
        }
        if (frame==30) check(find(out,"sys/ta_resultchara23")!=nullptr,"state transition ends at new original portrait");
        if (frame==80) {
            check(out.sounds==std::vector<int>{6} && find(out,"sys/ta_area_effect02"),"changed area starts original overlay");
            const auto* glow=find(out,"sys/area_effect02");check(glow && glow->additive,"area glow retains additive blend");
            auto restored=AreaAssignment::restore(result.state());check(restored && same_draw(out,restored->render()),"result snapshot restores the active area animation");
        }
    }
    check(result.memory().previous_area_state[1]==2,"result commits changed area baseline after display");
    auto unchanged=values();auto no_change=*AreaAssignment::enter(52,unchanged,memory);
    Input frames;frames.frames=52;check(!no_change.advance(frames,opaque).finished,"empty result still holds before returning");
    check(no_change.advance({},opaque).finished,"empty result uses original hold and exit time");
    auto multi_values=values();const auto multi_memory=AreaAssignment::enter(51,multi_values)->memory();
    multi_values.character_state[5]=3;multi_values.submission[5]=32;multi_values.gain[5]=2;
    multi_values.submission[8]=20;multi_values.gain[8]=1;
    auto multi=*AreaAssignment::enter(52,multi_values,multi_memory);
    unsigned opening_sounds=0;
    for (unsigned frame=0;frame<188;++frame) {
        const auto out=multi.advance({},opaque);
        opening_sounds+=static_cast<unsigned>(std::count(out.sounds.begin(),out.sounds.end(),5));
        check(out.finished==(frame==187),"multiple result rows retain PC transition timing");
        if (frame==80) check(find(out,"sys/ta_resultchara11")!=nullptr,"next result begins on the same frame as preceding hold ends");
    }
    check(opening_sounds==1,"original result-start sound occurs only at the initial count-zero update");
    auto bad=result.state();bad.result_count=INT32_MAX;check(!AreaAssignment::restore(bad),"malformed result counter rejected");
    multi_values.gain[5]=400000000;auto large=*AreaAssignment::enter(52,multi_values,multi_memory);
    bad=large.state();bad.result_hold=true;bad.result_count=1000000000;
    check(!AreaAssignment::restore(bad),"large gain cannot extend the fixed result hold phase");
    bad.result_hold=false;bad.result_flash=true;
    check(!AreaAssignment::restore(bad),"large gain cannot extend the fixed result flash phase");
}
void recognition_contract() {
    std::vector<int> sizes={116,36,36,16,16,1};
    const std::vector<std::string> commands={"selectplace","result","printdate"};
    const HasName has=[](auto){return true;};
    check(recognize_assignment_contract(commands,6,sizes,has,has),"complete capability combination recognized");
    check(!recognize_assignment_contract(commands,6,sizes,[](auto){return false;},has),"matching EXT sizes alone do not register a private capability");
    check(!recognize_assignment_contract(commands,6,sizes,has,[](auto){return false;}),"missing original asset contract rejected");
    check(!recognize_assignment_contract({"result","printdate"},6,sizes,has,has),"all private command registrations required");
    check(!recognize_assignment_contract({"selectplace","result","result","printdate"},6,sizes,has,has),"ambiguous command table rejected");
    sizes[0]=120;check(!recognize_assignment_contract(commands,6,sizes,has,has),"different EXE extension layout rejected");
}
}
int main() {
    try {date_contract();extension_contract();assignment_input();result_contract();recognition_contract();}
    catch (const std::exception& e) {std::cerr<<"asset panel check failed after "<<checks<<": "<<e.what()<<'\n';return EXIT_FAILURE;}
    std::cout<<"asset panel checks: "<<checks<<" passed\n";return EXIT_SUCCESS;
}
