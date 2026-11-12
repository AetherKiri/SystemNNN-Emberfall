#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace emberfall::scene {

struct PortraitVisual {
    std::string image;
    int x=0,y=0,width=0,height=0;
    int animation_type=1,frames=1,speed=1,wait_min=0,wait_max=0;
};

struct CutinRowVisual {
    bool positioned=false;
    int x=0,y=0,font_delta=0;
    int window_type=0,window_x=0,window_y=0;
    int pad_x=0,pad_y=0,step_x=0,step_y=0,window_rows=0;
    std::uint32_t window_color=0;
    std::string window_image;
};

struct DialogueVisuals {
    int portrait_depth=0; // 1 below the window, 2 above it.
    bool cutin=false;
    std::vector<PortraitVisual> portraits;
    std::vector<CutinRowVisual> rows;
};

} // namespace emberfall::scene
