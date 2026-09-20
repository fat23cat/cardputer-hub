#include "apps/pomodoro/pomodoro_app.h"

namespace cardputer_hub::apps {
using namespace core;
using namespace services;

namespace {
bool isPlain(const InputEvent& event) {
    return !event.modifiers.ctrl && !event.modifiers.alt && !event.modifiers.option;
}

bool isSpace(const InputEvent& event) {
    return isPlain(event) && event.type == InputEventType::PrintableCharacter &&
           event.character == ' ';
}

bool isReset(const InputEvent& event) {
    return isPlain(event) && event.type == InputEventType::PrintableCharacter &&
           (event.character == 'r' || event.character == 'R');
}

bool isSkip(const InputEvent& event) {
    if (!isPlain(event))
        return false;
    if (event.type == InputEventType::NamedKey && event.namedKey == NamedKey::Right)
        return true;
    return event.type == InputEventType::PrintableCharacter &&
           (event.character == 's' || event.character == 'S' ||
            (event.character == '/' && !event.modifiers.shift));
}

std::int32_t displayedSeconds(const PomodoroSnapshot& snapshot) {
    if (snapshot.remaining.count() <= 0)
        return 0;
    return static_cast<std::int32_t>((snapshot.remaining.count() + 999) / 1000);
}
} // namespace

PomodoroApp::PomodoroApp(PomodoroService& pomodoro, IDisplayAdapter& display)
    : pomodoro_(pomodoro), display_(display) {}

void PomodoroApp::onActivate() { frame_.reset(); }

void PomodoroApp::onDeactivate() { frame_.reset(); }

void PomodoroApp::update(const InputEvents& input, std::chrono::milliseconds) {
    for (const auto& event : input)
        handle(event);
    render();
}

void PomodoroApp::handle(const InputEvent& event) {
    if (isSpace(event)) {
        pomodoro_.toggleRun();
        return;
    }
    if (isReset(event)) {
        pomodoro_.reset();
        return;
    }
    if (isSkip(event))
        pomodoro_.skip();
}

PomodoroApp::Frame PomodoroApp::capture() const {
    const auto snapshot = pomodoro_.snapshot();
    return {snapshot.runState, snapshot.phase, displayedSeconds(snapshot),
            pomodoroCycleDisplay(snapshot), pomodoroLcdFilledSegments(snapshot)};
}

void PomodoroApp::render() {
    const auto next = capture();
    if (frame_ && frame_->runState == next.runState && frame_->phase == next.phase &&
        frame_->remainingSeconds == next.remainingSeconds && frame_->cycle == next.cycle &&
        frame_->progress == next.progress)
        return;
    drawPomodoroScreen(display_, pomodoro_.snapshot());
    frame_ = next;
}

} // namespace cardputer_hub::apps
