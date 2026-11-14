#include "emberfall/date_card.hpp"

#include <algorithm>
#include <cstdio>

namespace emberfall::scene::panel {
namespace {
bool valid(DateValues values) {
    return values.moon>=1 && values.moon<=10 && (values.easy!=1 ||
        (values.remaining_days>=0 && values.remaining_days<=99 && values.power>=0));
}
void blit(std::vector<Blit>& out,std::string asset,int x,int y,int sx,int sy,int w,int h) {
    out.push_back({std::move(asset),x,y,sx,sy,w,h,true});
}
}

const std::array<std::string_view,4>& date_variable_names() noexcept {
    // bloodv7 callback bindings at 419a06, 419a3e, 419a92 and 419aae.
    static const std::array<std::string_view,4> names={
        "\x8c\x8e\x97\xee", // 月齢
        "\x8e\x63\x82\xe8\x93\xfa\x90\x94", // 残り日数
        "\x83\x43\x81\x5b\x83\x57\x81\x5b\x83\x82\x81\x5b\x83\x68", // イージーモード
        "\x82\xb5\x82\xb8\x82\xe9\x8e\x78\x94\x7a\x93\x78" // しずる支配度
    };
    return names;
}
std::optional<DateValues> read_date_values(const ReadVariable& read) {
    if (!read) return std::nullopt;
    std::array<int,4> values{};
    const auto& names=date_variable_names();
    for (std::size_t i=0;i<values.size();++i) {
        const auto value=read(names[i]);
        if (!value) return std::nullopt;
        values[i]=*value;
    }
    const DateValues result{values[0],values[1],values[2],values[3]};
    return valid(result) ? std::optional<DateValues>(result) : std::nullopt;
}

std::optional<DateCard> DateCard::create(DateValues values) {
    if (!valid(values)) return std::nullopt;
    DateCard result;result.state_.values=values;return result;
}
std::optional<DateCard> DateCard::restore(const State& state) {
    if (!valid(state.values) || state.count< -1 || state.count>40 || state.fade_in<0 ||
        state.fade_in>10 || state.fade_out< -1 || state.fade_out>10 || state.exit_delay<0 ||
        state.exit_delay>11 || state.pulse< -1 || state.pulse>15 ||
        (state.fade_out>=0 && state.fade_in!=0) ||
        (state.finished && (state.fade_out!=10 || state.exit_delay!=11))) return std::nullopt;
    DateCard result;result.state_=state;return result;
}
void DateCard::advance(const Input& input) {
    // 418f30 calls the common clock before its own hold counter. The ten-frame
    // entrance is included in the hold. Exit completion uses >10, not >=10.
    for (unsigned frame=0;frame<input.frames && !state_.finished;++frame) {
        state_.pulse=(state_.pulse+1)%16;
        if (state_.fade_in>0) --state_.fade_in;
        if (state_.fade_out>=0) {
            state_.fade_out=std::min(10,state_.fade_out+1);
            if (state_.exit_delay>10) state_.finished=true;
            else ++state_.exit_delay;
            continue;
        }
        ++state_.count;
        if (state_.count>=40 || (frame==0 && (input.click || input.confirm))) {
            state_.fade_in=0;state_.fade_out=0;state_.exit_delay=0;
        }
    }
}

void append_remaining_days(std::vector<Blit>& output,int days,bool small) {
    // 418690: 99 is the game's infinity symbol, not a pair of numeric digits.
    if (days<0 || days>99) return;
    if (days==99) {
        if (small) blit(output,"sys/ta_mugen01",59,70,0,0,23,24);
        else blit(output,"sys/ta_mugen02",442,233,0,0,40,70);
        return;
    }
    const int tens=days/10,ones=days%10;
    const int x=small?59:432,y=small?70:231,w=small?12:32,h=small?24:62,gap=small?11:28;
    const std::string name=small?"sys/ta_font_01":"sys/ta_font_02";
    if (tens) blit(output,name,x,y,w*tens,0,w,h);
    blit(output,name,x+(tens?gap:0),y,w*ones,0,w,h);
}
void append_power(std::vector<Blit>& output,int power,int pulse) {
    // Original 800x600 layout records 58..60; eight rows pulse 0..7..0.
    if (power<0) return;
    const auto portrait=power>=71?"03":power>=36?"02":"01";
    blit(output,std::string("sys/ta_bloodpowpt")+portrait,0,448,0,0,223,152);
    blit(output,"sys/ta_bloodpower01",164,490,0,0,467,103);
    const int phase=std::max(0,pulse)%16;
    const int row=phase>=8?15-phase:phase;
    blit(output,"sys/ta_bloodbar01",204,514,0,row*56,std::min(power,100)*4,56);
}
Output DateCard::render() const {
    Output out;out.mode=mode;out.finished=state_.finished;
    out.blits.push_back({state_.values.moon==10?"sys/day_bg02":"sys/day_bg01",0,0,0,0,800,600,false});
    char moon[40]{};std::snprintf(moon,sizeof(moon),"sys/ta_day_moon%02d",state_.values.moon);
    blit(out.blits,moon,250,120,0,0,300,300);
    if (state_.values.easy==1) {
        blit(out.blits,"sys/ta_day_pt01",298,233,0,0,140,70);
        append_remaining_days(out.blits,state_.values.remaining_days,false);
        append_power(out.blits,state_.values.power,state_.pulse);
    }
    out.black_percent=state_.fade_in>0?state_.fade_in*10:std::max(0,state_.fade_out)*10;
    return out;
}

} // namespace emberfall::scene::panel
