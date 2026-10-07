#include "apps/service_status/service_status_app.h"

namespace cardputer_hub::apps {
namespace {
using core::InputEvent;
using core::InputEventType;

bool isRefresh(const InputEvent& event) {
    return event.type == InputEventType::PrintableCharacter &&
           (event.character == 'r' || event.character == 'R') && !event.modifiers.fn &&
           !event.modifiers.ctrl;
}
} // namespace

void ServiceStatusApp::onActivate() {
    drawn_.reset();
    status_.setActive(true);
}

void ServiceStatusApp::onDeactivate() {
    status_.setActive(false);
    drawn_.reset();
}

void ServiceStatusApp::update(const core::InputEvents& input, std::chrono::milliseconds) {
    for (const auto& event : input) {
        if (isRefresh(event))
            (void)actions_.dispatch(
                {services::serviceStatusRefreshActionId, serviceStatusAppId, {}});
    }
    // Building a frame allocates strings; skip it unless something it shows
    // can have changed: the snapshot, or the header's ten-second step.
    const auto& snapshot = status_.snapshot();
    const auto step =
        std::chrono::duration_cast<std::chrono::seconds>(snapshot.sinceRound).count() / 10;
    if (drawn_ && snapshot.revision == drawnRevision_ && step == drawnStep_)
        return;
    drawnRevision_ = snapshot.revision;
    drawnStep_ = step;
    auto next = serviceStatusFrame(snapshot);
    if (drawn_ && *drawn_ == next)
        return;
    drawServiceStatus(display_, next, drawn_ ? &*drawn_ : nullptr);
    drawn_ = std::move(next);
}

} // namespace cardputer_hub::apps
