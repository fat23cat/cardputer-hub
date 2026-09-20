#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>

namespace cardputer_hub::services {

enum class PomodoroRunState : std::uint8_t {
    Idle,
    Running,
    Paused,
};

enum class PomodoroPhase : std::uint8_t {
    Work,
    ShortBreak,
    LongBreak,
};

struct PomodoroDurations {
    std::chrono::milliseconds work{std::chrono::minutes(25)};
    std::chrono::milliseconds shortBreak{std::chrono::minutes(5)};
    std::chrono::milliseconds longBreak{std::chrono::minutes(15)};
};

struct PomodoroTransition {
    PomodoroPhase from = PomodoroPhase::Work;
    PomodoroPhase to = PomodoroPhase::Work;
};

struct PomodoroSnapshot {
    std::uint32_t generation = 0;
    std::uint32_t transitionGeneration = 0;
    PomodoroRunState runState = PomodoroRunState::Idle;
    PomodoroPhase phase = PomodoroPhase::Work;
    std::chrono::milliseconds duration{std::chrono::minutes(25)};
    std::chrono::milliseconds remaining{std::chrono::minutes(25)};
    std::uint8_t completedWorkSessions = 0;
};

inline constexpr PomodoroDurations productionPomodoroDurations{};

class PomodoroService {
  public:
    explicit PomodoroService(PomodoroDurations durations = productionPomodoroDurations);

    void update(std::chrono::milliseconds elapsed);
    void start();
    void pause();
    void resume();
    void toggleRun();
    void reset();
    void skip();

    [[nodiscard]] PomodoroSnapshot snapshot() const noexcept { return snapshot_; }
    [[nodiscard]] const PomodoroDurations& durations() const noexcept { return durations_; }
    std::size_t takeTransitions(PomodoroTransition* out, std::size_t capacity);

  private:
    static constexpr std::size_t maxQueuedTransitions = 8;

    void bump();
    void bumpIfDisplayedRemainingChanged(std::chrono::milliseconds previousRemaining);
    void enter(PomodoroPhase phase);
    void completeCurrentPhase();
    void consume(std::chrono::milliseconds elapsed);
    void recordTransition(PomodoroPhase from, PomodoroPhase to);
    void clearTransitions();
    [[nodiscard]] std::chrono::milliseconds durationFor(PomodoroPhase phase) const noexcept;

    PomodoroDurations durations_;
    PomodoroSnapshot snapshot_{};
    std::array<PomodoroTransition, maxQueuedTransitions> queuedTransitions_{};
    std::uint8_t queuedTransitionCount_ = 0;
};

} // namespace cardputer_hub::services
