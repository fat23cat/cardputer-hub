#include "apps/web_access/web_access_app.h"

#include "core/display/contextual_footer.h"
#include "core/display/palette.h"
#include "core/display/screen_header.h"
#include "core/display/text_layout.h"
#include "core/display/web_join_code.h"

#include <algorithm>

namespace cardputer_hub::apps {
namespace {
using core::InputEventType;
using core::NamedKey;
using services::WebAccessState;

constexpr float passwordTextScale = 2.0f;

bool isEnter(const core::InputEvent& event) {
    return event.type == InputEventType::NamedKey && event.namedKey == NamedKey::Enter &&
           core::isPlainInput(event);
}

bool isQ(const core::InputEvent& event) {
    return event.type == InputEventType::PrintableCharacter &&
           (event.character == 'q' || event.character == 'Q') && core::isPlainInput(event) &&
           !event.modifiers.fn;
}

std::string clientsText(std::size_t clients) {
    if (clients == 0)
        return "NO CLIENTS";
    return std::to_string(clients) + (clients == 1 ? " CLIENT" : " CLIENTS");
}

// Groups of four for reading; the spaces are not part of the password.
std::string groupedPassword(const std::string& password) {
    std::string result;
    for (std::size_t index = 0; index < password.size(); ++index) {
        if (index != 0 && index % 4 == 0)
            result.push_back(' ');
        result.push_back(password[index]);
    }
    return result;
}
} // namespace

std::int32_t webAccessQrModulePixels(int modules) { return core::webJoinQrModulePixels(modules); }

bool WebAccessApp::handleBack() {
    if (!showingQr_)
        return false;
    showingQr_ = false;
    drawn_.reset();
    return true;
}

void WebAccessApp::update(const core::InputEvents& input, std::chrono::milliseconds) {
    for (const auto& event : input) {
        const bool running = web_.status().state == WebAccessState::Running;
        if (isQ(event) && running) {
            showingQr_ = !showingQr_;
            drawn_.reset();
            continue;
        }
        // Enter never stops the service from behind the join code.
        if (!isEnter(event) || showingQr_)
            continue;
        (void)actions_.dispatch(
            {services::webAccessSetEnabledActionId, webAccessAppId, {{"enabled", !running}}});
    }
    const auto& status = web_.status();
    if (status.state != WebAccessState::Running && showingQr_) {
        showingQr_ = false;
        drawn_.reset();
    }
    Frame next{status.state, status.ssid, status.password, status.clients};
    if (drawn_ && drawn_->state == next.state && drawn_->ssid == next.ssid &&
        drawn_->password == next.password) {
        if (drawn_->clients != next.clients && !showingQr_) {
            display_.fillRectangle({0, 0}, 240, core::screenHeaderRuleY, core::palette::bone);
            drawHeader(next);
        }
        drawn_ = std::move(next);
        return;
    }
    display_.clear(core::palette::bone);
    if (showingQr_) {
        drawQr(next);
    } else {
        drawHeader(next);
        drawBody(next);
    }
    drawn_ = std::move(next);
}

void WebAccessApp::drawHeader(const Frame& frame) {
    switch (frame.state) {
    case WebAccessState::Off:
        core::drawScreenHeader(display_, "WEB ACCESS", "OFF");
        return;
    case WebAccessState::Error:
        core::drawScreenHeader(display_, "WEB ACCESS", "ERROR", core::palette::vermilion);
        return;
    case WebAccessState::Running:
        core::drawScreenHeader(display_, "WEB ACCESS", clientsText(frame.clients));
        return;
    }
}

void WebAccessApp::drawBody(const Frame& frame) {
    const core::TextStyle normal{core::palette::ink, core::palette::bone, core::systemTextScale};
    const core::TextStyle quiet{core::palette::ordinal, core::palette::bone, core::systemTextScale};
    const auto centered = [&](std::int32_t y, const char* text, core::TextStyle style) {
        display_.drawText({core::centeredTextX(text, 0, 240, style.scale), y}, text, style);
    };
    switch (frame.state) {
    case WebAccessState::Off:
        centered(42, "STARTS A WI-FI NETWORK", normal);
        centered(58, "FOR A PHONE OR COMPUTER", quiet);
        // The access point and BLE share the radio (WebAccessBluetoothPause).
        centered(86, "BLUETOOTH PAUSES WHILE ON", quiet);
        core::drawContextualFooter(display_, "ESC BACK", "ENTER START");
        return;
    case WebAccessState::Error:
        centered(58, "WI-FI DID NOT START", normal);
        core::drawContextualFooter(display_, "ESC BACK", "ENTER RETRY");
        return;
    case WebAccessState::Running:
        break;
    }
    const auto row = [&](std::int32_t y, const char* ordinal, const char* label,
                         const std::string& value) {
        display_.drawText({10, y}, ordinal, quiet);
        display_.drawText({30, y}, label, normal);
        if (!value.empty())
            display_.drawText({core::rightAlignedTextX(value.c_str()), y}, value.c_str(), normal);
    };
    row(26, "01", "NETWORK", frame.ssid);
    row(44, "02", "PASSWORD", {});
    // The password is read off the screen and typed: twice the size, in groups.
    centered(60, groupedPassword(frame.password).c_str(),
             {core::palette::ink, core::palette::bone, passwordTextScale});
    row(84, "03", "OPEN", services::webAccessAddress);
    row(102, "04", "LOGIN", services::webAccessLogin);
    core::drawContextualFooter(display_, "ESC BACKGROUND", "ENTER STOP");
    display_.drawText({120, 123}, "Q QR", quiet);
}

void WebAccessApp::drawQr(const Frame& frame) {
    core::drawWebJoinCode(display_, frame.ssid, frame.password);
}

} // namespace cardputer_hub::apps
