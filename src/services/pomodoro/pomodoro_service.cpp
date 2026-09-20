#include "services/pomodoro/pomodoro_service.h"

#include <algorithm>

namespace cardputer_hub::services {
namespace {
constexpr std::chrono::milliseconds displayedRemaining(std::chrono::milliseconds remaining) {
    if (remaining.count() <= 0)
        return std::chrono::milliseconds(0);
    const auto extra = remaining.count() % 1000;
    if (extra == 0)
        return remaining;
    return remaining + std::chrono::milliseconds(1000 - extra);
}

std::chrono::milliseconds sanitized(std::chrono::milliseconds value) {
    return std::max(value, std::chrono::milliseconds(1));
}
} // namespace

PomodoroService::PomodoroService(PomodoroDurations durations)
    : durations_{sanitized(durations.work), sanitized(durations.shortBreak),
                 sanitized(durations.longBreak)} {
    snapshot_.duration = durations_.work;
    snapshot_.remaining = durations_.work;
}

void PomodoroService::bump() { ++snapshot_.generation; }

void PomodoroService::bumpIfDisplayedRemainingChanged(std::chrono::milliseconds previousRemaining) {
    if (displayedRemaining(previousRemaining) != displayedRemaining(snapshot_.remaining))
        bump();
}

std::chrono::milliseconds PomodoroService::durationFor(PomodoroPhase phase) const noexcept {
    switch (phase) {
    case PomodoroPhase::ShortBreak:
        return durations_.shortBreak;
    case PomodoroPhase::LongBreak:
        return durations_.longBreak;
    case PomodoroPhase::Work:
        break;
    }
    return durations_.work;
}

void PomodoroService::enter(PomodoroPhase phase) {
    snapshot_.phase = phase;
    snapshot_.duration = durationFor(phase);
    snapshot_.remaining = snapshot_.duration;
    snapshot_.runState = PomodoroRunState::Running;
    bump();
}

void PomodoroService::recordTransition(PomodoroPhase from, PomodoroPhase to) {
    if (queuedTransitionCount_ == maxQueuedTransitions) {
        for (std::size_t i = 1; i < maxQueuedTransitions; ++i)
            queuedTransitions_[i - 1] = queuedTransitions_[i];
        --queuedTransitionCount_;
    }
    queuedTransitions_[queuedTransitionCount_++] = {from, to};
    ++snapshot_.transitionGeneration;
}

void PomodoroService::clearTransitions() { queuedTransitionCount_ = 0; }

std::size_t PomodoroService::takeTransitions(PomodoroTransition* out, std::size_t capacity) {
    const auto count = std::min<std::size_t>(queuedTransitionCount_, capacity);
    if (out != nullptr) {
        for (std::size_t i = 0; i < count; ++i)
            out[i] = queuedTransitions_[i];
    }
    const auto remaining = static_cast<std::uint8_t>(queuedTransitionCount_ - count);
    for (std::size_t i = 0; i < remaining; ++i)
        queuedTransitions_[i] = queuedTransitions_[i + count];
    queuedTransitionCount_ = remaining;
    return count;
}

void PomodoroService::completeCurrentPhase() {
    const auto from = snapshot_.phase;
    if (snapshot_.phase == PomodoroPhase::Work) {
        if (snapshot_.completedWorkSessions < 3) {
            ++snapshot_.completedWorkSessions;
            enter(PomodoroPhase::ShortBreak);
        } else {
            snapshot_.completedWorkSessions = 3;
            enter(PomodoroPhase::LongBreak);
        }
    } else {
        if (snapshot_.phase == PomodoroPhase::LongBreak)
            snapshot_.completedWorkSessions = 0;
        enter(PomodoroPhase::Work);
    }
    recordTransition(from, snapshot_.phase);
}

void PomodoroService::consume(std::chrono::milliseconds elapsed) {
    elapsed = std::max(elapsed, std::chrono::milliseconds(0));
    while (elapsed.count() > 0 && snapshot_.runState == PomodoroRunState::Running) {
        const auto previousRemaining = snapshot_.remaining;
        if (snapshot_.remaining > elapsed) {
            snapshot_.remaining -= elapsed;
            bumpIfDisplayedRemainingChanged(previousRemaining);
            return;
        }
        elapsed -= snapshot_.remaining;
        snapshot_.remaining = std::chrono::milliseconds(0);
        completeCurrentPhase();
    }
}

void PomodoroService::update(std::chrono::milliseconds elapsed) {
    if (snapshot_.runState != PomodoroRunState::Running)
        return;
    consume(elapsed);
}

void PomodoroService::start() {
    if (snapshot_.runState != PomodoroRunState::Idle)
        return;
    snapshot_.runState = PomodoroRunState::Running;
    bump();
}

void PomodoroService::pause() {
    if (snapshot_.runState != PomodoroRunState::Running)
        return;
    snapshot_.runState = PomodoroRunState::Paused;
    bump();
}

void PomodoroService::resume() {
    if (snapshot_.runState != PomodoroRunState::Paused)
        return;
    snapshot_.runState = PomodoroRunState::Running;
    bump();
}

void PomodoroService::toggleRun() {
    switch (snapshot_.runState) {
    case PomodoroRunState::Idle:
        start();
        break;
    case PomodoroRunState::Running:
        pause();
        break;
    case PomodoroRunState::Paused:
        resume();
        break;
    }
}

void PomodoroService::reset() {
    snapshot_.runState = PomodoroRunState::Idle;
    snapshot_.phase = PomodoroPhase::Work;
    snapshot_.duration = durations_.work;
    snapshot_.remaining = durations_.work;
    snapshot_.completedWorkSessions = 0;
    clearTransitions();
    bump();
}

void PomodoroService::skip() { completeCurrentPhase(); }

} // namespace cardputer_hub::services
