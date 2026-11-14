#pragma once

#include "emberfall/date_card.hpp"
#include <array>

namespace emberfall::scene::panel {

// The six PC EXT payloads have value semantics. They contain no native pointers.
// Zero-initialization here is the verified PE zero-filled data tail before any
// map has run; enter() subsequently captures the original variable baselines.
struct AssignmentMemory {
    std::array<std::int32_t,29> previous_character_state{}; // Game EXT block 1, 116 bytes
    std::array<std::int32_t,9> previous_area_state{};       // Game EXT block 2, 36 bytes
    std::array<std::int32_t,9> area_target{};               // Game EXT block 3, 36 bytes
    std::array<std::int32_t,4> origin{};                    // Game EXT block 4, 16 bytes
    std::array<std::int32_t,4> destination{};               // Game EXT block 5, 16 bytes
    bool choosing=false;                                  // Game EXT block 6, 1 byte
    bool resume=false; // Derived from block 6 on PC load; not another PC block.
    bool suppress_entrance=false; // Menu return only; PC load explicitly clears it.
};
using AssignmentBlocks=std::array<std::vector<std::uint8_t>,6>;
inline constexpr std::array<std::size_t,6> assignment_block_sizes={116,36,36,16,16,1};
AssignmentBlocks export_assignment(const AssignmentMemory& memory);
std::optional<AssignmentMemory> import_assignment(const AssignmentBlocks& blocks);

struct AssignmentValues {
    DateValues date;
    int slow_pattern=1;
    std::array<bool,4> character_visible{}; // 0=しずる, 1=姉妹, 2 unused, 3=悠香
    std::array<int,4> position{};
    std::array<bool,9> area_visible{};
    std::array<int,9> area_target{},area_state{};
    // Original character numbers: 東5, まりか8, 那津美9, 涼10, 田上11,
    // 江梨衣12, アナベラ13, Dust15. Other slots retain the PC helper defaults.
    std::array<int,29> character_state{},submission{},gain{};
    AssignmentValues() {
        character_state.fill(1);submission.fill(-1);gain.fill(-1);area_state.fill(1);
    }
};
std::optional<AssignmentValues> read_assignment_values(const ReadVariable& read,int slow_pattern=1);
std::string_view assignment_destination_name(unsigned slot) noexcept;
const std::vector<std::string>& assignment_variable_names();
const std::vector<std::string>& assignment_assets();
using HasName=std::function<bool(std::string_view)>;
// Pure capability check. The caller owns cached configuration/resource lookup.
bool recognize_assignment_contract(const std::vector<std::string>& commands,int extension_count,
    const std::vector<int>& extension_sizes,const HasName& has_variable,const HasName& has_asset);

class AreaAssignment {
public:
    static constexpr int selection_mode=51,result_mode=52;
    struct Button {
        int state=0,phase=0,activated_count=0;
        bool activated=false,ready=false;
    };
    struct ResultRow { int character=0,base=0,gain=0; };
    struct State {
        int mode=selection_mode;
        AssignmentMemory memory;
        AssignmentValues values;
        std::array<Button,4> characters{};
        std::array<Button,9> areas{},symbols{};
        Button action,menu;
        int selected=-1,pointer_x=-1,pointer_y=-1;
        int pulse=-1,area_frame=-1,result_pulse=-1,result_count=-1;
        int fade_in=10,fade_out=-1,exit_delay=0,pending_system_mode=0;
        bool suppress_fade=false,finished=false,system_dispatched=false;
        std::vector<ResultRow> results;
        unsigned result_index=0;
        bool result_hold=false,result_flash=false,area_changed=false,area_hold=false;
        std::array<int,9> result_area_state{};
    };
    static std::optional<AreaAssignment> enter(int mode,const AssignmentValues& values,
                                              const AssignmentMemory& memory={});
    static std::optional<AreaAssignment> restore(const State& state);
    const State& state() const noexcept { return state_; }
    const AssignmentMemory& memory() const noexcept { return state_.memory; }
    bool can_open_system_menu() const noexcept;
    bool can_save() const noexcept;
    // Returns one-time sounds/writes/system-mode requests. render() is pure.
    Output advance(const Input& input,const HitTest& hit);
    Output render() const;
private:
    State state_;
    Rect character_rect(unsigned slot) const;
    bool update_button(Button& button,const Rect& rect,std::string_view asset,int animation_frames,
                       bool enabled,const Input& input,bool moved,const HitTest& hit);
    void advance_selection(const Input& input,bool moved,const HitTest& hit,Output& events);
    void advance_result(bool click,Output& events);
    void begin_exit(int system_mode=0);
};

} // namespace emberfall::scene::panel
