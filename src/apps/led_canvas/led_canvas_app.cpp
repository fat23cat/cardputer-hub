#include "apps/led_canvas/led_canvas_app.h"
#include "core/display/contextual_footer.h"
#include "core/display/palette.h"
#include "core/display/screen_header.h"
#include "core/display/text_layout.h"
#include "core/display/web_join_code.h"
#include <algorithm>
#include <string>
namespace cardputer_hub::apps {
void LedCanvasApp::onActivate() {
    canvas_.setActive(true);
    (void)canvas_.takeEnded();
    engine_ = std::make_unique<LedCanvasEngine>();
    clearGeneration_ = canvas_.status().clearGeneration;
    claim_ = indicator_.acquire(ledCanvasAppId, services::IndicatorPriority::ForegroundApplication);
    claim_.setFrame({});
    outputElapsed_ = {};
    showingQr_ = false;
    wasLive_ = false;
    drawn_.clear();
}
void LedCanvasApp::onDeactivate() {
    canvas_.setActive(false);
    claim_.release();
    engine_.reset();
    drawn_.clear();
    showingQr_ = false;
}
bool LedCanvasApp::handleBack() {
    if (!showingQr_)
        return false;
    showingQr_ = false;
    drawn_.clear();
    return true;
}
void LedCanvasApp::update(const core::InputEvents& input, std::chrono::milliseconds elapsed) {
    if (!engine_)
        return;
    using core::InputEventType;
    using core::NamedKey;
    using services::WebAccessState;
    for (const auto& e : input) {
        if (!core::isPlainInput(e) || e.modifiers.fn)
            continue;
        const bool running = web_.status().state == WebAccessState::Running;
        if (e.type == InputEventType::PrintableCharacter &&
            (e.character == 'q' || e.character == 'Q') && running) {
            showingQr_ = !showingQr_;
            drawn_.clear();
            continue;
        }
        if (showingQr_)
            continue;
        if (e.type == InputEventType::NamedKey && e.namedKey == NamedKey::Enter && !running)
            (void)actions_.dispatch(
                {services::webAccessSetEnabledActionId, ledCanvasAppId, {{"enabled", true}}});
        else if (e.type == InputEventType::PrintableCharacter && e.character == ' ')
            (void)actions_.dispatch({services::ledCanvasHoldActionId,
                                     ledCanvasAppId,
                                     {{"hold", !canvas_.status().settings.hold}}});
        else if (e.type == InputEventType::NamedKey &&
                 (e.namedKey == NamedKey::Backspace || e.namedKey == NamedKey::Delete))
            (void)actions_.dispatch({services::ledCanvasClearActionId, ledCanvasAppId, {}});
    }
    if (web_.status().state != WebAccessState::Running) {
        canvas_.endPointers();
        showingQr_ = false;
    }
    const auto status = canvas_.status();
    const bool cleared = status.clearGeneration != clearGeneration_;
    if (cleared) {
        engine_->clear();
        clearGeneration_ = status.clearGeneration;
    }
    engine_->setSettings(status.settings);
    engine_->endPointers(canvas_.takeEnded());
    engine_->advance(elapsed);
    auto due = canvas_.takeDue(elapsed);
    bool changed = cleared;
    while (due) {
        engine_->point(*due);
        changed = true;
        due = canvas_.takeDue(std::chrono::milliseconds{0});
    }
    engine_->endPointers(canvas_.takeEnded());
    const bool live = engine_->live();
    const auto interval = std::chrono::milliseconds{33};
    if (elapsed.count() > 0)
        outputElapsed_ += std::min(elapsed, interval);
    if (cleared || (outputElapsed_ >= interval && (live || wasLive_ || changed))) {
        claim_.setFrame({engine_->frame().pixels});
        outputElapsed_ %= interval;
        wasLive_ = live;
    }
    draw();
}
void LedCanvasApp::draw() {
    using namespace core;
    const auto& w = web_.status();
    const auto s = canvas_.status();
    const bool running = w.state == services::WebAccessState::Running;
    const bool live = running && (s.fingers || s.recent);
    const std::string signature = std::to_string(static_cast<unsigned>(w.state)) + ":" + w.ssid +
                                  ":" + w.password + ":" + std::to_string(w.clients) + ":" +
                                  std::to_string(live) + ":" + std::to_string(s.fingers) + ":" +
                                  std::to_string(s.revision) + ":" + std::to_string(showingQr_);
    if (signature == drawn_)
        return;
    // Traffic should not redraw the full-screen join code.
    if (showingQr_ && drawn_ == "qr:" + w.ssid + ":" + w.password)
        return;
    display_.clear(palette::bone);
    if (showingQr_) {
        drawWebJoinCode(display_, w.ssid, w.password);
        drawn_ = "qr:" + w.ssid + ":" + w.password;
        return;
    }
    drawn_ = signature;
    const TextStyle ink{palette::ink, palette::bone, systemTextScale};
    const TextStyle quiet{palette::ordinal, palette::bone, systemTextScale};
    const auto centered = [&](int y, const std::string& text, float scale = systemTextScale) {
        display_.drawText({centeredTextX(text.c_str(), 0, 240, scale), y}, text.c_str(),
                          {palette::ink, palette::bone, scale});
    };
    if (!running) {
        const bool error = w.state == services::WebAccessState::Error;
        drawScreenHeader(display_, "LED CANVAS", error ? "WEB ERROR" : "WEB OFF",
                         error ? palette::vermilion : palette::ordinal);
        centered(40, error ? "WEB ACCESS ERROR" : "WEB ACCESS OFF", 2);
        display_.drawText({centeredTextX("THE PHONE DRAWS OVER WI-FI", 0, 240), 68},
                          "THE PHONE DRAWS OVER WI-FI", quiet);
        drawContextualFooter(display_, "", error ? "ENTER RETRY" : "ENTER START");
        return;
    }
    const auto clients = w.clients
                             ? std::to_string(w.clients) + (w.clients == 1 ? " CLIENT" : " CLIENTS")
                             : "NO CLIENTS";
    drawScreenHeader(display_, "LED CANVAS", clients);
    if (live) {
        const auto fingers =
            "LIVE  " + std::to_string(s.fingers) + (s.fingers == 1 ? " FINGER" : " FINGERS");
        const auto left = centeredTextX(fingers.c_str(), 0, 240) - 11;
        display_.fillRectangle({left, 29}, 6, 6, palette::leaf);
        centered(28, fingers);
        centered(46, ledCanvasModeNames[static_cast<unsigned>(s.settings.mode)], 2);
        std::string fade = "FADE ";
        const std::array<const char*, 3> names{{"SHORT", "MEDIUM", "LONG"}};
        fade += names[static_cast<unsigned>(s.settings.fade)];
        if (s.settings.hold)
            fade += "  HOLD";
        display_.drawText({centeredTextX(fade.c_str(), 0, 240), 74}, fade.c_str(), quiet);
        drawContextualFooter(display_, s.settings.hold ? "SPACE RESUME" : "SPACE HOLD",
                             "DEL CLEAR");
    } else {
        const auto row = [&](int y, const char* label, const std::string& value) {
            display_.drawText({6, y}, label, quiet);
            display_.drawText({rightAlignedTextX(value.c_str()), y}, value.c_str(), ink);
        };
        row(28, "NETWORK", w.ssid);
        row(44, "OPEN", services::webAccessAddress);
        row(60, "TAB", "LED");
        const auto left = centeredTextX("WAITING FOR A TOUCH", 0, 240) - 11;
        display_.fillRectangle({left, 87}, 6, 6, palette::blue);
        centered(86, "WAITING FOR A TOUCH");
        drawContextualFooter(display_, "Q JOIN CODE", "");
    }
}
} // namespace cardputer_hub::apps
