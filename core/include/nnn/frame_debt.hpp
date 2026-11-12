#pragma once

#include <algorithm>
#include <cstdint>

namespace nnn {

// The PC's CShakin. Elapsed time accumulates as debt, and each frame repays a
// planned number of frames that grows by one per late frame up to the limit.
class FrameDebt {
public:
    void configure(int frame_ms,int max_frames,int window) noexcept {
        set_frame(frame_ms);
        max_=std::max(1,max_frames);
        window_=std::max(1,window);
    }
    void set_frame(int frame_ms) noexcept { frame_=std::max(1,frame_ms); }
    void clear() noexcept { debt_=0; planned_=0; }
    int max_frames() const noexcept { return max_; }
    // Frames to run now, at most wanted.
    int repay(std::uint32_t elapsed_ms,int wanted) noexcept {
        debt_+=elapsed_ms;
        if (debt_<frame_) planned_=0;
        else {
            const std::int64_t span=std::int64_t{window_}*frame_;
            if (planned_*span<debt_+span) ++planned_;
            if (planned_>1 && (planned_-1)*span>debt_+span) --planned_;
            planned_=std::min(planned_,max_);
        }
        const int frames=std::min(planned_,std::max(0,wanted));
        debt_=std::max<std::int64_t>(debt_-std::int64_t{frames}*frame_,-std::int64_t{frame_});
        return frames;
    }

private:
    int frame_=20, max_=2, window_=10, planned_=0;
    std::int64_t debt_=0;
};

} // namespace nnn
