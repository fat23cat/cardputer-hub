#include <unity.h>

#include <chrono>

#include "services/pomodoro/pomodoro_service.h"

using namespace cardputer_hub::services;
using namespace std::chrono_literals;

namespace {
PomodoroDurations shortDurations() { return {10s, 5s, 15s}; }

void assertState(const PomodoroSnapshot& snapshot, PomodoroRunState runState, PomodoroPhase phase,
                 std::chrono::milliseconds remaining, std::uint8_t completed) {
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(runState),
                            static_cast<unsigned>(snapshot.runState));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(phase), static_cast<unsigned>(snapshot.phase));
    TEST_ASSERT_EQUAL_INT64(remaining.count(), snapshot.remaining.count());
    TEST_ASSERT_EQUAL_UINT8(completed, snapshot.completedWorkSessions);
    TEST_ASSERT_TRUE(snapshot.remaining.count() >= 0);
}

void test_initial_state_is_idle_work_twenty_five_minutes() {
    PomodoroService pomodoro;
    assertState(pomodoro.snapshot(), PomodoroRunState::Idle, PomodoroPhase::Work, 25min, 0);
    TEST_ASSERT_EQUAL_INT64(std::chrono::milliseconds(25min).count(),
                            pomodoro.snapshot().duration.count());
}

void test_start_pause_resume_and_paused_elapsed_does_not_advance() {
    PomodoroService pomodoro(shortDurations());
    const auto generation = pomodoro.snapshot().generation;
    pomodoro.start();
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(PomodoroRunState::Running),
                            static_cast<unsigned>(pomodoro.snapshot().runState));
    TEST_ASSERT_TRUE(pomodoro.snapshot().generation > generation);
    pomodoro.update(3s);
    assertState(pomodoro.snapshot(), PomodoroRunState::Running, PomodoroPhase::Work, 7s, 0);
    pomodoro.pause();
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(PomodoroRunState::Paused),
                            static_cast<unsigned>(pomodoro.snapshot().runState));
    pomodoro.update(4s);
    assertState(pomodoro.snapshot(), PomodoroRunState::Paused, PomodoroPhase::Work, 7s, 0);
    pomodoro.resume();
    pomodoro.update(2s);
    assertState(pomodoro.snapshot(), PomodoroRunState::Running, PomodoroPhase::Work, 5s, 0);
}

void test_toggle_run_cycles_idle_running_paused() {
    PomodoroService pomodoro(shortDurations());
    pomodoro.toggleRun();
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(PomodoroRunState::Running),
                            static_cast<unsigned>(pomodoro.snapshot().runState));
    pomodoro.toggleRun();
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(PomodoroRunState::Paused),
                            static_cast<unsigned>(pomodoro.snapshot().runState));
    pomodoro.toggleRun();
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(PomodoroRunState::Running),
                            static_cast<unsigned>(pomodoro.snapshot().runState));
}

void test_reset_returns_complete_session_to_initial_state() {
    PomodoroService pomodoro(shortDurations());
    pomodoro.start();
    pomodoro.update(10s);
    pomodoro.skip();
    pomodoro.reset();
    assertState(pomodoro.snapshot(), PomodoroRunState::Idle, PomodoroPhase::Work, 10s, 0);
}

void test_natural_work_completion_selects_short_break() {
    PomodoroService pomodoro(shortDurations());
    pomodoro.start();
    pomodoro.update(10s);
    assertState(pomodoro.snapshot(), PomodoroRunState::Running, PomodoroPhase::ShortBreak, 5s, 1);
}

void test_fourth_work_selects_long_break() {
    PomodoroService pomodoro(shortDurations());
    pomodoro.start();
    for (int i = 0; i < 3; ++i) {
        pomodoro.skip();
        pomodoro.skip();
    }
    assertState(pomodoro.snapshot(), PomodoroRunState::Running, PomodoroPhase::Work, 10s, 3);
    pomodoro.update(10s);
    assertState(pomodoro.snapshot(), PomodoroRunState::Running, PomodoroPhase::LongBreak, 15s, 3);
}

void test_short_break_completion_selects_work() {
    PomodoroService pomodoro(shortDurations());
    pomodoro.start();
    pomodoro.skip();
    pomodoro.update(5s);
    assertState(pomodoro.snapshot(), PomodoroRunState::Running, PomodoroPhase::Work, 10s, 1);
}

void test_long_break_completion_resets_cycle_count() {
    PomodoroService pomodoro(shortDurations());
    pomodoro.start();
    for (int i = 0; i < 7; ++i)
        pomodoro.skip();
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(PomodoroPhase::LongBreak),
                            static_cast<unsigned>(pomodoro.snapshot().phase));
    pomodoro.update(15s);
    assertState(pomodoro.snapshot(), PomodoroRunState::Running, PomodoroPhase::Work, 10s, 0);
}

void test_skip_follows_completion_semantics() {
    PomodoroService pomodoro(shortDurations());
    pomodoro.skip();
    assertState(pomodoro.snapshot(), PomodoroRunState::Running, PomodoroPhase::ShortBreak, 5s, 1);
    pomodoro.skip();
    assertState(pomodoro.snapshot(), PomodoroRunState::Running, PomodoroPhase::Work, 10s, 1);
}

void test_large_elapsed_crosses_one_and_multiple_boundaries() {
    PomodoroService one(shortDurations());
    one.start();
    one.update(12s);
    assertState(one.snapshot(), PomodoroRunState::Running, PomodoroPhase::ShortBreak, 3s, 1);
    TEST_ASSERT_EQUAL_UINT32(1, one.snapshot().transitionGeneration);
    PomodoroTransition oneTransitions[8]{};
    TEST_ASSERT_EQUAL_UINT(1, one.takeTransitions(oneTransitions, 8));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(PomodoroPhase::Work),
                            static_cast<unsigned>(oneTransitions[0].from));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(PomodoroPhase::ShortBreak),
                            static_cast<unsigned>(oneTransitions[0].to));

    PomodoroService many(shortDurations());
    many.start();
    many.update(17s);
    assertState(many.snapshot(), PomodoroRunState::Running, PomodoroPhase::Work, 8s, 1);
    TEST_ASSERT_EQUAL_UINT32(2, many.snapshot().transitionGeneration);
    PomodoroTransition manyTransitions[8]{};
    TEST_ASSERT_EQUAL_UINT(2, many.takeTransitions(manyTransitions, 8));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(PomodoroPhase::Work),
                            static_cast<unsigned>(manyTransitions[0].from));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(PomodoroPhase::ShortBreak),
                            static_cast<unsigned>(manyTransitions[0].to));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(PomodoroPhase::ShortBreak),
                            static_cast<unsigned>(manyTransitions[1].from));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(PomodoroPhase::Work),
                            static_cast<unsigned>(manyTransitions[1].to));
    TEST_ASSERT_EQUAL_UINT(0, many.takeTransitions(manyTransitions, 8));
}

void test_reset_discards_pending_transitions() {
    PomodoroService pomodoro(shortDurations());
    pomodoro.start();
    pomodoro.update(17s);
    TEST_ASSERT_EQUAL_UINT32(2, pomodoro.snapshot().transitionGeneration);
    pomodoro.reset();
    PomodoroTransition transitions[8]{};
    TEST_ASSERT_EQUAL_UINT(0, pomodoro.takeTransitions(transitions, 8));
    TEST_ASSERT_EQUAL_UINT32(2, pomodoro.snapshot().transitionGeneration);
}

void test_take_transitions_keeps_remainder_when_capacity_is_smaller() {
    PomodoroService pomodoro(shortDurations());
    pomodoro.skip();
    pomodoro.skip();
    pomodoro.skip();

    PomodoroTransition first{};
    TEST_ASSERT_EQUAL_UINT(1, pomodoro.takeTransitions(&first, 1));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(PomodoroPhase::Work),
                            static_cast<unsigned>(first.from));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(PomodoroPhase::ShortBreak),
                            static_cast<unsigned>(first.to));

    PomodoroTransition remaining[2]{};
    TEST_ASSERT_EQUAL_UINT(2, pomodoro.takeTransitions(remaining, 2));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(PomodoroPhase::ShortBreak),
                            static_cast<unsigned>(remaining[0].from));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(PomodoroPhase::Work),
                            static_cast<unsigned>(remaining[0].to));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(PomodoroPhase::Work),
                            static_cast<unsigned>(remaining[1].from));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(PomodoroPhase::ShortBreak),
                            static_cast<unsigned>(remaining[1].to));
    TEST_ASSERT_EQUAL_UINT(0, pomodoro.takeTransitions(remaining, 2));
}

void test_remaining_never_becomes_negative() {
    PomodoroService pomodoro(shortDurations());
    pomodoro.start();
    pomodoro.update(1000s);
    TEST_ASSERT_TRUE(pomodoro.snapshot().remaining.count() >= 0);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(PomodoroRunState::Running),
                            static_cast<unsigned>(pomodoro.snapshot().runState));
}

void test_update_cadence_does_not_change_total_elapsed_behavior() {
    PomodoroService coarse(shortDurations());
    PomodoroService fine(shortDurations());
    coarse.start();
    fine.start();
    coarse.update(10s);
    for (int i = 0; i < 10; ++i)
        fine.update(1s);
    TEST_ASSERT_EQUAL_INT64(coarse.snapshot().remaining.count(), fine.snapshot().remaining.count());
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(coarse.snapshot().phase),
                            static_cast<unsigned>(fine.snapshot().phase));
    TEST_ASSERT_EQUAL_UINT8(coarse.snapshot().completedWorkSessions,
                            fine.snapshot().completedWorkSessions);
}

void test_no_phase_transition_while_paused() {
    PomodoroService pomodoro(shortDurations());
    pomodoro.start();
    pomodoro.update(9s);
    pomodoro.pause();
    pomodoro.update(5s);
    assertState(pomodoro.snapshot(), PomodoroRunState::Paused, PomodoroPhase::Work, 1s, 0);
}
} // namespace

void setUp() {}
void tearDown() {}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_initial_state_is_idle_work_twenty_five_minutes);
    RUN_TEST(test_start_pause_resume_and_paused_elapsed_does_not_advance);
    RUN_TEST(test_toggle_run_cycles_idle_running_paused);
    RUN_TEST(test_reset_returns_complete_session_to_initial_state);
    RUN_TEST(test_natural_work_completion_selects_short_break);
    RUN_TEST(test_fourth_work_selects_long_break);
    RUN_TEST(test_short_break_completion_selects_work);
    RUN_TEST(test_long_break_completion_resets_cycle_count);
    RUN_TEST(test_skip_follows_completion_semantics);
    RUN_TEST(test_large_elapsed_crosses_one_and_multiple_boundaries);
    RUN_TEST(test_reset_discards_pending_transitions);
    RUN_TEST(test_take_transitions_keeps_remainder_when_capacity_is_smaller);
    RUN_TEST(test_remaining_never_becomes_negative);
    RUN_TEST(test_update_cadence_does_not_change_total_elapsed_behavior);
    RUN_TEST(test_no_phase_transition_while_paused);
    return UNITY_END();
}
