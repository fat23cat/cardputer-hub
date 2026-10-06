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

void PomodoroApp::render() {
    const auto next = pomodoro_.snapshot();
    drawPomodoroScreen(display_, next, frame_ ? &*frame_ : nullptr);
    frame_ = next;
}

} // namespace cardputer_hub::apps
