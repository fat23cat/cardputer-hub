#include "apps/mac_status/mac_status_app.h"

#include "core/display/palette.h"

namespace cardputer_hub::apps {
namespace {
using core::InputEvent;

bool isPlain(const InputEvent& event) {
    return !event.modifiers.ctrl && !event.modifiers.alt && !event.modifiers.option;
}
} // namespace

void MacStatusApp::onActivate() {
    view_ = std::make_unique<View>();
    service_.startMonitoring();
}

void MacStatusApp::onDeactivate() {
    service_.stopMonitoring();
    view_.reset();
}

void MacStatusApp::showPage(MacStatusPage page) {
    view_->page = page;
    view_->drawn = false;
    view_->frame.reset();
    view_->regionKeys.clear();
    service_.setDetailGroup(macStatusDetailGroup(page));
}

void MacStatusApp::turnPage(int delta) {
    const auto count = static_cast<int>(macStatusPageCount);
    const auto next = (static_cast<int>(view_->page) + delta + count) % count;
    display_.beginTransition(delta > 0 ? core::SlideDirection::Forward
                                       : core::SlideDirection::Backward);
    showPage(static_cast<MacStatusPage>(next));
}

void MacStatusApp::update(const core::InputEvents& input, std::chrono::milliseconds) {
    const auto* history = service_.history();
    const auto* details = service_.details();
    if (!view_ || history == nullptr || details == nullptr)
        return;
    const bool pages = service_.detailsSupported();
    if (!pages && view_->page != MacStatusPage::Overview)
        showPage(MacStatusPage::Overview);
    if (pages != view_->pagesVisible) {
        view_->pagesVisible = pages;
        view_->drawn = false;
    }
    for (const auto& event : input) {
        if (!pages || !isPlain(event))
            continue;
        if (core::isPageLeft(event))
            turnPage(-1);
        else if (core::isPageRight(event))
            turnPage(1);
    }
    if (view_->page == MacStatusPage::Overview)
        renderOverview(*history);
    else
        renderDetailPage(*history, *details);
}

void MacStatusApp::renderOverview(const services::MacStatusHistory& history) {
    auto& view = *view_;
    const auto next = formatMacStatus(service_.snapshot());
    if (!view.drawn) {
        display_.clear(core::palette::bone);
        view.frame.reset();
        if (view.pagesVisible)
            drawMacStatusPageDots(display_, view.page);
        view.drawn = true;
    }
    for (int index = 0; index < 8; ++index) {
        const bool changed =
            !view.frame || view.frame->labels[index] != next.labels[index] ||
            (index == 0 && view.historyGeneration != history.generation()) ||
            (index > 0 && index < 4 && view.frame->bars[index] != next.bars[index]) ||
            (index == 3 && view.frame->charging != next.charging);
        if (changed)
            drawMacStatusMetric(display_, next, index, history);
    }
    view.historyGeneration = history.generation();
    view.frame = next;
}

void MacStatusApp::renderDetailPage(const services::MacStatusHistory& history,
                                    const services::MacStatusDetails& details) {
    auto& view = *view_;
    const auto snapshot = service_.snapshot();
    const std::array<std::uint32_t, 3> inputs{snapshot.generation, details.generation,
                                              history.generation()};
    if (view.drawn && inputs == view.detailInputs)
        return;
    view.detailInputs = inputs;
    const auto regions = layoutMacStatusPage(view.page, snapshot, details, history);
    if (!view.drawn) {
        display_.clear(core::palette::bone);
        view.regionKeys.clear();
        view.drawn = true;
    }
    view.regionKeys.resize(regions.size());
    for (std::size_t index = 0; index < regions.size(); ++index) {
        const auto& region = regions[index];
        if (view.regionKeys[index] == region.key && !region.key.empty())
            continue;
        display_.fillRectangle({region.x, region.y}, region.width, region.height,
                               core::palette::bone);
        region.draw(display_);
        view.regionKeys[index] = region.key;
    }
}

} // namespace cardputer_hub::apps
