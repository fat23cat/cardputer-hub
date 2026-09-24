#include "apps/launcher/launcher.h"

#include "apps/launcher/launcher_graphics.h"
#include "core/display/palette.h"
#include "core/display/text_layout.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace cardputer_hub::apps {
using namespace core;

namespace {
constexpr int overlayHiddenY = -launcherHeaderHeight;

bool isPlain(const InputEvent& event) {
    return !event.modifiers.ctrl && !event.modifiers.alt && !event.modifiers.option;
}

bool isUp(const InputEvent& event) {
    return (event.type == InputEventType::NamedKey && event.namedKey == NamedKey::Up) ||
           (event.type == InputEventType::PrintableCharacter && event.character == ';' &&
            !event.modifiers.shift);
}

bool isDown(const InputEvent& event) {
    return (event.type == InputEventType::NamedKey && event.namedKey == NamedKey::Down) ||
           (event.type == InputEventType::PrintableCharacter && event.character == '.' &&
            !event.modifiers.shift);
}

std::string ordinal(std::size_t index) {
    char text[3] = {};
    const auto value = static_cast<unsigned>(std::min<std::size_t>(index + 1, 99));
    std::snprintf(text, sizeof(text), "%02u", value);
    return text;
}

std::string fitName(const std::string& name) {
    constexpr std::size_t maximum = 22;
    if (name.size() <= maximum)
        return name;
    return name.substr(0, maximum - 3) + "...";
}

std::string fitOverlayReason(std::string text) {
    constexpr std::int32_t overlayTextX = 6;
    const auto maximum = static_cast<std::size_t>((240 - overlayTextX) / core::systemGlyphWidth);
    if (text.size() <= maximum)
        return text;
    if (maximum <= 3)
        return text.substr(0, maximum);
    return text.substr(0, maximum - 3) + "...";
}
} // namespace

Launcher::Launcher(const AppRegistry& apps, MiniAppRuntime& runtime, ActionBus& actions,
                   IDisplayAdapter& display)
    : apps_(apps), runtime_(runtime), actions_(actions), display_(display) {}

void Launcher::activate() {
    clearOverlay();
    if (!apps_.apps().empty()) {
        selected_ = std::min(selected_, apps_.apps().size() - 1);
        const auto lastStart =
            apps_.apps().size() > visibleRows ? apps_.apps().size() - visibleRows : 0;
        windowStart_ = std::min(windowStart_, lastStart);
        if (selected_ < windowStart_)
            windowStart_ = selected_;
        else if (selected_ >= windowStart_ + visibleRows)
            windowStart_ = selected_ + 1 - visibleRows;
    } else {
        selected_ = 0;
        windowStart_ = 0;
    }
    frame_.reset();
}

void Launcher::deactivate() {
    clearOverlay();
    frame_.reset();
}

bool Launcher::animating() const { return overlay_ != OverlayPhase::Hidden; }

void Launcher::showUnavailableOverlay() { startOverlay(overlayReasonFor(selectedAvailability())); }

void Launcher::update(const InputEvents& input, std::chrono::milliseconds elapsed) {
    for (const auto& event : input) {
        handle(event);
        if (runtime_.hasActiveApp())
            return;
    }
    advanceOverlay(elapsed);
    render();
}

void Launcher::handle(const InputEvent& event) {
    if (!isPlain(event))
        return;
    if (isUp(event)) {
        moveSelection(-1);
        return;
    }
    if (isDown(event)) {
        moveSelection(1);
        return;
    }
    if (isPlainEscape(event)) {
        (void)actions_.dispatch({"ui.back", "launcher", {}});
        return;
    }
    if (event.type == InputEventType::NamedKey && event.namedKey == NamedKey::Enter)
        launchSelected();
}

void Launcher::moveSelection(int delta) {
    if (apps_.apps().empty())
        return;
    const auto last = apps_.apps().size() - 1;
    const auto next = delta > 0 ? std::min(selected_ + static_cast<std::size_t>(delta), last)
                      : selected_ >= static_cast<std::size_t>(-delta)
                          ? selected_ - static_cast<std::size_t>(-delta)
                          : 0;
    if (next == selected_)
        return;
    selected_ = next;
    if (selected_ < windowStart_)
        windowStart_ = selected_;
    else if (selected_ >= windowStart_ + visibleRows)
        windowStart_ = selected_ + 1 - visibleRows;
}

void Launcher::launchSelected() {
    if (apps_.apps().empty())
        return;
    const auto availability = selectedAvailability();
    if (availability.eligibility != MiniAppEligibility::Eligible) {
        startOverlay(overlayReasonFor(availability));
        return;
    }
    (void)actions_.dispatch({"app.open", "launcher", {{"appId", apps_.apps()[selected_].id}}});
}

void Launcher::clearOverlay() {
    overlay_ = OverlayPhase::Hidden;
    overlayY_ = static_cast<float>(overlayHiddenY);
    holdElapsed_ = {};
    overlayReason_.clear();
}

void Launcher::startOverlay(std::string reason) {
    overlayReason_ = std::move(reason);
    holdElapsed_ = {};
    if (overlay_ == OverlayPhase::Visible || overlay_ == OverlayPhase::Entering) {
        overlay_ = OverlayPhase::Visible;
        overlayY_ = 0;
        return;
    }
    overlay_ = OverlayPhase::Entering;
}

void Launcher::advanceOverlay(std::chrono::milliseconds elapsed) {
    const auto dt = static_cast<float>(std::max<std::int64_t>(0, elapsed.count()));
    if (overlay_ == OverlayPhase::Hidden)
        return;
    if (overlay_ == OverlayPhase::Entering) {
        overlayY_ += static_cast<float>(launcherHeaderHeight) * dt /
                     static_cast<float>(overlayEnterDuration.count());
        if (overlayY_ >= 0) {
            overlayY_ = 0;
            overlay_ = OverlayPhase::Visible;
            holdElapsed_ = {};
        }
        return;
    }
    if (overlay_ == OverlayPhase::Visible) {
        holdElapsed_ += elapsed;
        if (holdElapsed_ >= overlayHoldDuration)
            overlay_ = OverlayPhase::Exiting;
        return;
    }
    overlayY_ -= static_cast<float>(launcherHeaderHeight) * dt /
                 static_cast<float>(overlayExitDuration.count());
    if (overlayY_ <= static_cast<float>(overlayHiddenY))
        clearOverlay();
}

MiniAppAvailability Launcher::selectedAvailability() const {
    if (apps_.apps().empty())
        return {};
    return runtime_.availability(apps_.apps()[selected_].id);
}

std::vector<MiniAppEligibility> Launcher::currentEligibility() const {
    std::vector<MiniAppEligibility> values;
    values.reserve(apps_.apps().size());
    for (const auto& app : apps_.apps())
        values.push_back(runtime_.eligibility(app.id));
    return values;
}

std::string Launcher::overlayReasonFor(const MiniAppAvailability& availability) const {
    switch (availability.eligibility) {
    case MiniAppEligibility::MissingInstance:
        return "APP NOT READY";
    case MiniAppEligibility::UnknownApp:
        return "APP NOT FOUND";
    case MiniAppEligibility::MissingCapability:
        if (availability.missingCapability)
            return fitOverlayReason("REQUIRES " + *availability.missingCapability);
        return "REQUIRES CAPABILITY";
    case MiniAppEligibility::Eligible:
        break;
    }
    return {};
}

void Launcher::render() {
    const auto& registered = apps_.apps();
    Frame next;
    next.count = registered.size();
    next.selected = selected_;
    next.windowStart = windowStart_;
    next.eligibility = currentEligibility();
    next.overlay = overlay_;
    next.overlayY = static_cast<int>(std::lround(overlayY_));
    next.overlayReason = overlayReason_;
    next.plateY = launcherRowTop + static_cast<int>((selected_ - windowStart_) * launcherRowHeight);
    next.empty = registered.empty();

    const bool full = !frame_;
    const bool listChanged = full || frame_->count != next.count ||
                             frame_->windowStart != next.windowStart ||
                             frame_->eligibility != next.eligibility || frame_->empty != next.empty;
    const bool plateChanged =
        full || frame_->plateY != next.plateY || frame_->selected != next.selected;
    const bool overlayChanged = full || frame_->overlay != next.overlay ||
                                frame_->overlayY != next.overlayY ||
                                frame_->overlayReason != next.overlayReason;
    const bool headerChanged =
        overlayChanged || full || frame_->selected != next.selected || frame_->count != next.count;
    if (!listChanged && !plateChanged && !headerChanged)
        return;

    const TextStyle normal{palette::ink, palette::bone, 1};
    const TextStyle selected{palette::bone, palette::ink, 1};
    const TextStyle quiet{palette::ordinal, palette::bone, 1};
    if (full) {
        display_.clear(palette::bone);
        display_.fillRectangle({6, 20}, 228, 1, palette::ink);
    }

    if (headerChanged) {
        display_.fillRectangle({0, 0}, 240, 20, palette::bone);
        if (overlay_ == OverlayPhase::Hidden || next.overlayY < 0) {
            display_.drawText({6, 6}, "APPS", normal);
            if (!next.empty) {
                const auto counter =
                    std::to_string(selected_ + 1) + "/" + std::to_string(next.count);
                display_.drawText({rightAlignedTextX(counter.c_str()), 6}, counter.c_str(), quiet);
            }
        }
        if (overlay_ != OverlayPhase::Hidden) {
            const int y = next.overlayY;
            if (y < 20) {
                const int destY = std::max(y, 0);
                const int height = std::min(20 - destY, 20 + std::min(y, 0));
                if (height > 0)
                    display_.fillRectangle({0, destY}, 240, height, palette::ink);
            }
            if (y >= 0 && !overlayReason_.empty())
                display_.drawText({6, 6}, overlayReason_.c_str(), selected);
        }
        display_.fillRectangle({6, 20}, 228, 1, palette::ink);
    }

    if (listChanged || plateChanged) {
        display_.fillRectangle({0, launcherRowTop}, 240, 135 - launcherRowTop, palette::bone);
        if (next.empty) {
            display_.drawText({99, 64}, "NO APPS", quiet);
        } else {
            display_.fillRectangle({launcherRowLeft, next.plateY}, launcherRowWidth,
                                   launcherRowHeight, palette::ink);
            const auto visible = std::min(visibleRows, next.count - windowStart_);
            for (std::size_t slot = 0; slot < visible; ++slot) {
                const auto index = windowStart_ + slot;
                const auto& app = registered[index];
                const int rowY = launcherRowTop + static_cast<int>(slot) * launcherRowHeight;
                const bool inverted = index == selected_;
                const auto style = inverted ? selected : normal;
                const int contentY = rowY + 14;
                display_.drawText({10, contentY}, ordinal(index).c_str(), style);
                drawAppIcon(display_, {launcherIconX, rowY + 11}, app.iconId.c_str(),
                            inverted ? palette::bone : palette::ink);
                display_.drawText({launcherNameX, contentY}, fitName(app.displayName).c_str(),
                                  style);
                drawAvailabilityDot(display_, {launcherDotX, rowY + 16},
                                    next.eligibility[index] == MiniAppEligibility::Eligible);
            }
        }
    }
    frame_ = std::move(next);
}

} // namespace cardputer_hub::apps
