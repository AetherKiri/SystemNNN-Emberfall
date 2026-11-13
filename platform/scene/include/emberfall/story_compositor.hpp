#pragma once

#include "emberfall/game_session.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

namespace emberfall::scene {

constexpr bool scene_plane_key_matches(const PlaneKey& candidate,const PlaneKey& expected) noexcept {
    return candidate[0]==expected[0] &&
           candidate[1]==expected[1] &&
           candidate[2]==expected[2] &&
           candidate[3]==expected[3] &&
           candidate[4]==expected[4];
}

std::string format_plane_key(const PlaneKey& key);

// Detached thread-safe UI plane mailbox shared via std::shared_ptr<PlaneMailbox>.
// The UI thread writes premultiplied RGBA8 logical-resolution planes and PlaneKey
// under a short mutex without ever touching Player*, GameSession, or Layers.
class PlaneMailbox {
public:
    struct Snapshot {
        std::shared_ptr<const render::MessagePlane> plane;
        PlaneKey key{};
        std::uint64_t revision=0;
        bool has_plane=false;
        bool attached=false;
    };

    void set_attached(bool attached);
    bool attached() const;

    void push(const render::Byte* rgba,std::size_t stride,unsigned width,unsigned height,
              const PlaneKey& key,bool show_backdrop=true);

    Snapshot snapshot() const;

private:
    mutable std::mutex mutex_;
    std::shared_ptr<render::MessagePlane> front_;
    std::shared_ptr<render::MessagePlane> spare_;
    PlaneKey key_{};
    std::uint64_t revision_=0;
    bool has_plane_=false;
    bool attached_=false;
};

// Non-blocking Story presenter. A late UI plane retains the last complete frame;
// the warning threshold never licenses a partial frame or a presentation ack.
class StoryCompositor {
public:
    using WarningLogger=std::function<void(const std::string&)>;
    using SubmitCallback=std::function<bool(const render::Surface&)>;

    static constexpr unsigned max_skip_ticks=2;

    explicit StoryCompositor(std::shared_ptr<PlaneMailbox> mailbox=std::make_shared<PlaneMailbox>(),
                             WarningLogger logger={});

    const std::shared_ptr<PlaneMailbox>& mailbox() const noexcept { return mailbox_; }
    void set_warning_logger(WarningLogger logger) { logger_=std::move(logger); }

    PlaneKey presented_plane_key() const noexcept { return presented_key_; }
    std::uint64_t presented_revision() const noexcept { return presented_revision_; }
    unsigned skipped_ticks() const noexcept { return skip_ticks_; }

    void reset_present_state() noexcept;

    enum class PresentOutcome {
        Idle,
        SkippedPending,
        Presented,
        SubmitFailed
    };

    PresentOutcome present(GameSession& game,bool force_repaint,bool story_mode,
                           const SubmitCallback& submit,const render::Surface* override_surface=nullptr);

private:
    std::shared_ptr<PlaneMailbox> mailbox_;
    WarningLogger logger_;
    PlaneKey presented_key_{};
    PlaneKey last_expected_key_{};
    std::uint64_t presented_revision_=0;
    unsigned skip_ticks_=0;
    bool has_presented_=false;
    bool presented_with_plane_=false;
    bool has_expected_key_=false;
    bool fallback_logged_=false;
    bool pending_retry_=false;
};

} // namespace emberfall::scene
