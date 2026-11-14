#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace emberfall::scene::panel {

// A panel describes the game's assets in its original logical canvas. The host
// owns image loading, presentation, input scaling and the script suspension.
struct Rect {
    int x=0,y=0,width=0,height=0;
    bool contains(int px,int py) const noexcept {
        return px>=x && py>=y && static_cast<std::int64_t>(px)<static_cast<std::int64_t>(x)+width &&
            static_cast<std::int64_t>(py)<static_cast<std::int64_t>(y)+height;
    }
};
struct Blit {
    std::string asset;
    int x=0,y=0,source_x=0,source_y=0,width=0,height=0;
    bool alpha=true;
    int opacity_percent=100;
    int add_red=0,add_green=0,add_blue=0;
    bool additive=false; // CPicture::AddBlt, after any preceding masked image.
};
struct Input {
    unsigned frames=1;
    int pointer_x=-1,pointer_y=-1;
    bool click=false; // One PC CheckClick(0) event, consumed on the first frame.
    bool cancel=false; // One PC CheckClick(1) event.
    bool confirm=false;
    int system_mode=0; // A host-mapped L/S/C/M/O shortcut; zero means none.
};
using ReadVariable=std::function<std::optional<std::int32_t>(std::string_view)>;
using HitTest=std::function<bool(std::string_view,int,int)>; // True for an opaque asset pixel.
struct VariableWrite { std::string name; std::int32_t value=0; };
struct Output {
    int mode=0;
    std::vector<Blit> blits;
    int black_percent=0;
    std::vector<int> sounds;
    std::vector<VariableWrite> writes;
    bool finished=false;
    int system_mode=0;
};

} // namespace emberfall::scene::panel
