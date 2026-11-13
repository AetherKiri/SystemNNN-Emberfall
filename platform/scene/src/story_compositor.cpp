#include "emberfall/story_compositor.hpp"
#include <cstring>
#include <stdexcept>
#include <utility>

namespace emberfall::scene {

std::string format_plane_key(const PlaneKey& key) {
    return std::to_string(key[0])+","+std::to_string(key[1])+","+std::to_string(key[2])+","+
           std::to_string(key[3])+","+std::to_string(key[4])+","+std::to_string(key[5]);
}

void PlaneMailbox::set_attached(bool attached) {
    std::lock_guard<std::mutex> lock(mutex_);
    attached_=attached;
    if (!attached) {
        front_.reset();
        spare_.reset();
        key_={};
        has_plane_=false;
    }
}

bool PlaneMailbox::attached() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return attached_;
}

void PlaneMailbox::push(const render::Byte* rgba,std::size_t stride,unsigned width,unsigned height,
                        const PlaneKey& key,bool show_backdrop) {
    if (rgba) {
        if (width==0 || height==0 || width>render::max_dimension || height>render::max_dimension ||
            static_cast<std::uint64_t>(width)*height>render::max_pixels || stride<std::size_t{width}*4)
            throw std::invalid_argument("Invalid message plane buffer");
    }
    std::shared_ptr<render::MessagePlane> buffer;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!attached_) return;
        buffer=std::move(spare_);
    }
    if (!buffer) buffer=std::make_shared<render::MessagePlane>();
    buffer->show_backdrop=show_backdrop;
    if (rgba) {
        const std::size_t row_bytes=std::size_t{width}*4;
        const std::size_t total_bytes=row_bytes*height;
        buffer->width=width;
        buffer->height=height;
        buffer->stride=row_bytes;
        buffer->rgba.resize(total_bytes);
        if (stride==row_bytes) {
            std::memcpy(buffer->rgba.data(),rgba,total_bytes);
        } else {
            for (unsigned y=0;y<height;++y)
                std::memcpy(buffer->rgba.data()+std::size_t{y}*row_bytes,rgba+std::size_t{y}*stride,row_bytes);
        }
    } else {
        buffer->width=0;
        buffer->height=0;
        buffer->stride=0;
        buffer->rgba.clear();
    }
    std::shared_ptr<render::MessagePlane> discarded;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!attached_) {
            discarded=std::move(buffer);
            return;
        }
        if (front_ && front_.use_count()==1) spare_=std::move(front_);
        else discarded=std::move(front_);
        front_=std::move(buffer);
        key_=key;
        has_plane_=true;
        ++revision_;
    }
}

PlaneMailbox::Snapshot PlaneMailbox::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return Snapshot{front_,key_,revision_,has_plane_,attached_};
}

StoryCompositor::StoryCompositor(std::shared_ptr<PlaneMailbox> mailbox,WarningLogger logger)
    : mailbox_(mailbox ? std::move(mailbox) : std::make_shared<PlaneMailbox>()),
      logger_(std::move(logger)) {}

void StoryCompositor::reset_present_state() noexcept {
    presented_key_={};
    last_expected_key_={};
    presented_revision_=0;
    skip_ticks_=0;
    has_presented_=false;
    presented_with_plane_=false;
    has_expected_key_=false;
    fallback_logged_=false;
    pending_retry_=false;
}

StoryCompositor::PresentOutcome StoryCompositor::present(GameSession& game,bool force_repaint,bool story_mode,
                                                         const SubmitCallback& submit,
                                                         const render::Surface* override_surface) {
    const auto snap=mailbox_->snapshot();
    const bool use_mailbox=story_mode && snap.attached && !override_surface;
    const PlaneKey expected=(story_mode && !override_surface) ? game.expected_plane_key() : PlaneKey{};
    const bool needs_plane=use_mailbox && game.needs_message_plane();
    const bool revision_changed=needs_plane && snap.has_plane && snap.revision!=presented_revision_;
    const bool plane_state_changed=
        needs_plane!=presented_with_plane_ ||
        (needs_plane && !scene_plane_key_matches(expected,presented_key_));

    if (!force_repaint && has_presented_ && !game.needs_redraw() && !revision_changed &&
        !plane_state_changed && !pending_retry_) {
        if (story_mode && !override_surface && !needs_plane) presented_key_=expected;
        return PresentOutcome::Idle;
    }

    if (needs_plane) {
        const bool matching=snap.has_plane && scene_plane_key_matches(snap.key,expected);
        if (!matching) {
            if (!has_expected_key_ || !scene_plane_key_matches(expected,last_expected_key_)) {
                has_expected_key_=true;
                last_expected_key_=expected;
                skip_ticks_=0;
                fallback_logged_=false;
            }
            if (skip_ticks_<max_skip_ticks) {
                ++skip_ticks_;
                pending_retry_=true;
                return PresentOutcome::SkippedPending;
            }
            if (!fallback_logged_) {
                fallback_logged_=true;
                if (logger_) {
                    logger_("StoryCompositor fallback: UI plane key ["+format_plane_key(expected)+
                            "] not ready after "+std::to_string(max_skip_ticks)+
                            " skipped ticks; retaining the last complete frame");
                }
            }
            // Audio and the event loop keep running. Never publish a new scene
            // without its UI, nor acknowledge an image that was not displayed.
            pending_retry_=true;
            return PresentOutcome::SkippedPending;
        }
        has_expected_key_=true;
        last_expected_key_=expected;
        skip_ticks_=0;
        fallback_logged_=false;
        game.set_message_plane(snap.plane);
        const auto& frame=game.surface();
        if (!submit(frame)) {
            pending_retry_=true;
            return PresentOutcome::SubmitFailed;
        }
        game.after_present();
        has_presented_=true;
        presented_with_plane_=true;
        pending_retry_=false;
        presented_key_=snap.key;
        presented_revision_=snap.revision;
        return PresentOutcome::Presented;
    }

    game.clear_message_plane();
    skip_ticks_=0;
    fallback_logged_=false;
    has_expected_key_=false;
    const auto& frame=override_surface ? *override_surface : game.surface();
    if (!submit(frame)) {
        pending_retry_=true;
        return PresentOutcome::SubmitFailed;
    }
    if (!override_surface) game.after_present();
    has_presented_=true;
    presented_with_plane_=false;
    pending_retry_=false;
    presented_key_=(story_mode && !override_surface) ? expected : PlaneKey{};
    if (use_mailbox) presented_revision_=snap.revision;
    return PresentOutcome::Presented;
}

} // namespace emberfall::scene
