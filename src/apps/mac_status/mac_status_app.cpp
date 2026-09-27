#include "apps/mac_status/mac_status_app.h"

#include "core/display/palette.h"

namespace cardputer_hub::apps {

void MacStatusApp::onActivate() {
    frame_.reset();
    service_.startMonitoring();
}

void MacStatusApp::onDeactivate() {
    service_.stopMonitoring();
    frame_.reset();
}

void MacStatusApp::update(const core::InputEvents&, std::chrono::milliseconds) {
    const auto next = formatMacStatus(service_.snapshot());
    if (!frame_)
        display_.clear(core::palette::bone);
    for (int index = 0; index < 8; ++index) {
        if (!frame_ || frame_->labels[index] != next.labels[index] ||
            (index < 4 && frame_->bars[index] != next.bars[index]))
            drawMacStatusMetric(display_, next, index);
    }
    frame_ = next;
}

} // namespace cardputer_hub::apps
