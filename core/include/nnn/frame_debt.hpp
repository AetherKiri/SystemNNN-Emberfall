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
    void begin_frame(std::uint32_t elapsed_ms,bool no_skip_frame=false) noexcept {
        debt_+=elapsed_ms;
        if (debt_<frame_) planned_=0;
        else {
            const std::int64_t span=std::int64_t{window_}*frame_;
            if (planned_*span<debt_+span) ++planned_;
            if (planned_>1 && (planned_-1)*span>debt_+span) --planned_;
            // A scheduled Android update can only present the final step.
            // Repay whole frames already due so an on-time tick never skips a frame.
            const int limit=no_skip_frame ? 1 : max_;
            planned_=static_cast<int>(std::min<std::int64_t>({planned_,limit,debt_/frame_}));
        }
    }
    int repay_frames(int wanted) noexcept {
        const int frames=std::min(planned_,std::max(0,wanted));
        debt_=std::max<std::int64_t>(debt_-std::int64_t{frames}*frame_,-std::int64_t{frame_});
        return frames;
    }
    int repay(std::uint32_t elapsed_ms,int wanted,bool no_skip_frame=false) noexcept {
        begin_frame(elapsed_ms,no_skip_frame);
        return repay_frames(wanted);
    }

private:
    int frame_=20, max_=2, window_=10, planned_=0;
    std::int64_t debt_=0;
};

} // namespace nnn
