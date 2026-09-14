#include "apps/shell/ui_scheduler.h"

#include <chrono>
#include <unity.h>

using namespace std::chrono_literals;

namespace {

void test_idle_updates_are_rate_limited_and_elapsed_is_accumulated() {
    cardputer_hub::apps::UiScheduler scheduler;

    TEST_ASSERT_FALSE(scheduler.elapsedForUpdate(7ms, false).has_value());
    TEST_ASSERT_FALSE(scheduler.elapsedForUpdate(12ms, false).has_value());

    const auto elapsed = scheduler.elapsedForUpdate(1ms, false);
    TEST_ASSERT_TRUE(elapsed.has_value());
    TEST_ASSERT_EQUAL_INT64(20, elapsed->count());
}

void test_input_triggers_an_immediate_update_with_accumulated_elapsed() {
    cardputer_hub::apps::UiScheduler scheduler;

    TEST_ASSERT_FALSE(scheduler.elapsedForUpdate(4ms, false).has_value());
    const auto elapsed = scheduler.elapsedForUpdate(3ms, true);

    TEST_ASSERT_TRUE(elapsed.has_value());
    TEST_ASSERT_EQUAL_INT64(7, elapsed->count());
    TEST_ASSERT_FALSE(scheduler.elapsedForUpdate(19ms, false).has_value());
}

void test_delayed_updates_do_not_schedule_catch_up_frames() {
    cardputer_hub::apps::UiScheduler scheduler;

    const auto delayed = scheduler.elapsedForUpdate(95ms, false);
    TEST_ASSERT_TRUE(delayed.has_value());
    TEST_ASSERT_EQUAL_INT64(95, delayed->count());

    TEST_ASSERT_FALSE(scheduler.elapsedForUpdate(0ms, false).has_value());
    TEST_ASSERT_FALSE(scheduler.elapsedForUpdate(19ms, false).has_value());
    const auto next = scheduler.elapsedForUpdate(1ms, false);
    TEST_ASSERT_TRUE(next.has_value());
    TEST_ASSERT_EQUAL_INT64(20, next->count());
}

} // namespace

void setUp() {}
void tearDown() {}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_idle_updates_are_rate_limited_and_elapsed_is_accumulated);
    RUN_TEST(test_input_triggers_an_immediate_update_with_accumulated_elapsed);
    RUN_TEST(test_delayed_updates_do_not_schedule_catch_up_frames);
    return UNITY_END();
}
