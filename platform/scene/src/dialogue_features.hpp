#pragma once

#include "emberfall/dialogue_visuals.hpp"
#include "nnn/game_config.hpp"
#include "pc_save.hpp"

namespace emberfall::scene {

struct FaceSaveState {
    std::array<nnn::Value,256> words{};
    bool pin_next=false;
    int speaker_side=-1;
};

class Faces {
public:
    void configure(int depth,const std::filesystem::path& root,const nnn::resource::FilenameEncoder& encode);
    bool enabled() const { return depth_!=0; }
    void reset();
    void expression(int face,int type);
    void force(nnn::Value argument);
    void select(std::string_view name);
    void hide() { state_.speaker_side=-1; }
    void visuals(DialogueVisuals& result) const;
    const FaceSaveState& state() const { return state_; }
    void restore(FaceSaveState state);
    std::vector<nnn::Byte> pc_payload() const;
    void restore_pc(const std::vector<nnn::Byte>& payload);
private:
    struct Animation { int type=1,frames=1,speed=1,wait_min=0,wait_max=0; };
    struct Character {
        std::string name,image;
        std::vector<Animation> animations;
    };
    bool valid(int face) const;
    void add_portrait(DialogueVisuals& result,int side,bool animate) const;
    static constexpr std::size_t first_slot=253, second_slot=252, pinned_face=254, pin_side=255;
    FaceSaveState state_;
    std::vector<Character> characters_;
    int depth_=0,count_=1;
    int width_=160,height_=160;
    std::array<int,2> x_{10,10},y_{400,400};
    std::size_t save_words_=256;
};

class Cutins {
public:
    void configure(const std::filesystem::path& root,const nnn::resource::FilenameEncoder& encode);
    bool enabled() const { return enabled_; }
    bool active() const { return enabled_ && words.back()!=0; }
    void mode(nnn::Value value) { if (enabled_) words.back()=value; }
    void begin_message(bool append);
    void strip_name(std::vector<std::string>& rows,int type) const;
    void tag(std::size_t existing,std::size_t added,int type);
    void discard_first_row();
    void visuals(DialogueVisuals& result,std::size_t row_count) const;
    pc::CutinWords words{};
    bool wait_click=false;
private:
    struct Style {
        std::string name;
        int x=0,y=0,step_x=0,step_y=0,window_type=0,pad_x=0,pad_y=0,font_delta=0;
        std::uint32_t color=0;
    };
    const Style* style(int type) const;
    std::vector<Style> styles_;
    std::string window_tag_="ta_cutinwindow";
    bool enabled_=false,print_name_=false;
};

} // namespace emberfall::scene
