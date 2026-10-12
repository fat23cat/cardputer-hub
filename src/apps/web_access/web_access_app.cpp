#include "apps/web_access/web_access_app.h"

#include "core/display/contextual_footer.h"
#include "core/display/palette.h"
#include "core/display/qr_code.h"
#include "core/display/screen_header.h"
#include "core/display/text_layout.h"

#include <algorithm>

namespace cardputer_hub::apps {
namespace {
using core::InputEventType;
using core::NamedKey;
using services::WebAccessState;

constexpr float passwordTextScale = 2.0f;
// Two modules of light border at least, inside the 135-pixel height.
constexpr std::int32_t qrMaxModulePixels = 4;

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

std::int32_t webAccessQrModulePixels(int modules) {
    return std::clamp<std::int32_t>(135 / (modules + 4), 1, qrMaxModulePixels);
}

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
    const core::TextStyle quiet{core::palette::ordinal, core::palette::bone, core::systemTextScale};
    const auto code = core::QrCode::encodeText(core::wifiQrPayload(frame.ssid, frame.password));
    if (!code)
        return;
    const auto modulePixels = webAccessQrModulePixels(code->size());
    const auto extent = code->size() * modulePixels;
    const auto left = (240 - extent) / 2;
    const auto top = (135 - extent) / 2;
    // One rectangle per run of dark modules in a row.
    for (int y = 0; y < code->size(); ++y) {
        for (int x = 0; x < code->size();) {
            if (!code->module(x, y)) {
                ++x;
                continue;
            }
            int end = x;
            while (end < code->size() && code->module(end, y))
                ++end;
            display_.fillRectangle({left + x * modulePixels, top + y * modulePixels},
                                   (end - x) * modulePixels, modulePixels, core::palette::ink);
            x = end;
        }
    }
    // The side labels keep the code's two-module light border clear.
    const auto border = 2 * modulePixels;
    const auto side = [&](std::int32_t y, const char* text, bool rightSide) {
        const auto columnLeft = rightSide ? left + extent + border : 0;
        display_.drawText({core::centeredTextX(text, columnLeft, left - border), y}, text, quiet);
    };
    side(54, "SCAN", false);
    side(70, "TO JOIN", false);
    side(62, "Q CLOSE", true);
}

} // namespace cardputer_hub::apps
