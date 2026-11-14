#pragma once

#include "emberfall/asset_panel.hpp"
#include <array>

namespace emberfall::scene::panel {

struct DateValues {
    int moon=1,remaining_days=0,easy=0,power=0;
};
// Names use the game's CP932 byte encoding, as do GameSession's variable keys.
const std::array<std::string_view,4>& date_variable_names() noexcept;
std::optional<DateValues> read_date_values(const ReadVariable& read);

class DateCard {
public:
    static constexpr int mode=50;
    struct State {
        DateValues values;
        int count=-1,fade_in=10,fade_out=-1,exit_delay=0,pulse=-1;
        bool finished=false;
    };
    static std::optional<DateCard> create(DateValues values);
    static std::optional<DateCard> restore(const State& state);
    const State& state() const noexcept { return state_; }
    // Advances original game frames. Rendering may be repeated without changing
    // the clock; neither calls the script nor touches game variables.
    void advance(const Input& input);
    Output render() const;
private:
    State state_;
};

// Shared original HUD used by the date card and the assignment/result screens.
void append_remaining_days(std::vector<Blit>& output,int days,bool small);
void append_power(std::vector<Blit>& output,int power,int pulse);

} // namespace emberfall::scene::panel
