#include "apps/ai/ai_app.h"

#include "core/display/palette.h"
#include "core/display/text_layout.h"

#include <algorithm>

namespace cardputer_hub::apps {
namespace {
constexpr int headerHeight = 16;
constexpr int screenHeight = 135;
constexpr int screenWidth = 240;

bool horizontal(const core::InputEvent& event) {
    if (event.modifiers.ctrl || event.modifiers.alt || event.modifiers.option ||
        event.modifiers.shift)
        return false;
    if (event.type == core::InputEventType::PrintableCharacter && event.modifiers.fn)
        return false;
    return core::isPageLeft(event) || core::isPageRight(event);
}
} // namespace

AiApp::AiApp(services::AiUsageService& usage, services::AiUsageIndicatorController& usageIndicator,
             services::AiAgentStatusService& status,
             services::AiAgentStatusIndicatorController& statusIndicator,
             core::IDisplayAdapter& display)
    : status_(status), display_(display), contentDisplay_(*this),
      usageView_(usage, usageIndicator, contentDisplay_),
      statusView_(status, statusIndicator, contentDisplay_, false) {}

int AiApp::ContentDisplay::mapY(int y) const {
    return owner_.hasHeader() ? headerHeight + y * (screenHeight - headerHeight) / screenHeight : y;
}

void AiApp::ContentDisplay::clear(core::RgbColor color) {
    if (!owner_.hasHeader())
        owner_.headerDrawn_ = false;
    const int top = mapY(0);
    owner_.display_.fillRectangle({0, top}, screenWidth, screenHeight - top, color);
}

void AiApp::ContentDisplay::fillRectangle(core::PixelPosition position, std::int32_t width,
                                          std::int32_t height, core::RgbColor color) {
    if (width <= 0 || height <= 0)
        return;
    const int top = mapY(position.y);
    const int mappedHeight = std::max(1, mapY(position.y + height) - top);
    owner_.display_.fillRectangle({position.x, top}, width, mappedHeight, color);
}

void AiApp::ContentDisplay::beginTransition(core::SlideDirection direction) {
    owner_.display_.beginTransition(direction);
}

void AiApp::ContentDisplay::drawText(core::PixelPosition position, const char* text,
                                     core::TextStyle style) {
    const core::PixelPosition mapped{position.x, mapY(position.y)};
    // Glyphs keep their size; translate their clip by the same offset.
    if (style.clip)
        style.clip->origin.y += mapped.y - position.y;
    owner_.display_.drawText(mapped, text, style);
}

bool AiApp::hasHeader() const { return page_ != Page::Usage || !usageView_.showingDetails(); }

bool AiApp::needsAttention() const {
    const auto& snapshot = status_.snapshot();
    if (snapshot.freshness != services::AgentStatusFreshness::Fresh)
        return false;
    for (const auto provider : connectivity::agentStatusOrder)
        if (snapshot.status.installed(provider) &&
            snapshot.status.state(provider) == connectivity::AgentState::NeedsYou)
            return true;
    return false;
}

void AiApp::drawHeader() {
    if (!hasHeader())
        return;
    const bool attention = page_ == Page::Usage && needsAttention();
    if (headerDrawn_ && attention == attentionDrawn_)
        return;
    using namespace core;
    const TextStyle normal{palette::ink, palette::bone, systemTextScale};
    const TextStyle quiet{palette::ordinal, palette::bone, systemTextScale};
    const TextStyle selected{palette::bone, palette::ink, systemTextScale};
    display_.fillRectangle({0, 0}, screenWidth, headerHeight, palette::bone);
    display_.fillRectangle({page_ == Page::Status ? 34 : 115, 0}, page_ == Page::Status ? 73 : 69,
                           headerHeight - 1, palette::ink);
    display_.drawText({6, 2}, "AI", normal);
    display_.drawText({40, 2}, "STATUS", page_ == Page::Status ? selected : quiet);
    display_.drawText({120, 2}, "USAGE", page_ == Page::Usage ? selected : quiet);
    display_.drawText({208, 2}, "< >", quiet);
    if (attention)
        display_.drawText({98, 2}, "!", {palette::vermilion, palette::bone, systemTextScale});
    display_.fillRectangle({0, headerHeight - 1}, screenWidth, 1, palette::ink);
    headerDrawn_ = true;
    attentionDrawn_ = attention;
}

IMiniApp& AiApp::currentView() {
    if (page_ == Page::Usage)
        return usageView_;
    return statusView_;
}

void AiApp::onActivate() {
    active_ = true;
    headerDrawn_ = false;
    status_.startMonitoring();
    currentView().onActivate();
    // The shell requests its opening transition after activation. Render on
    // its scheduled update so that activation leaves the source frame intact.
}

void AiApp::onDeactivate() {
    if (!active_)
        return;
    currentView().onDeactivate();
    status_.stopMonitoring();
    active_ = false;
    headerDrawn_ = false;
}

void AiApp::update(const core::InputEvents& input, std::chrono::milliseconds elapsed) {
    if (!active_)
        return;
    for (const auto& event : input) {
        if (horizontal(event) && hasHeader()) {
            display_.beginTransition(core::isPageLeft(event) ? core::SlideDirection::Backward
                                                             : core::SlideDirection::Forward);
            currentView().onDeactivate();
            page_ = page_ == Page::Status ? Page::Usage : Page::Status;
            headerDrawn_ = false;
            currentView().onActivate();
        } else {
            currentView().update({event}, {});
        }
    }
    currentView().update({}, elapsed);
    drawHeader();
}

bool AiApp::handleBack() {
    if (!active_ || page_ != Page::Usage || !usageView_.handleBack())
        return false;
    update({}, {});
    return true;
}

} // namespace cardputer_hub::apps
