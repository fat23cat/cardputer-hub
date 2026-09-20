#include <unity.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <vector>

#include "core/power/display_power_controller.h"

namespace {

using cardputer_hub::core::DisplayPowerController;
using cardputer_hub::core::DisplayPowerState;
using cardputer_hub::core::IBacklightAdapter;

constexpr std::uint8_t existingBrightness = 128;
constexpr std::uint8_t expectedDimLevel = 13;

std::chrono::milliseconds ms(std::int64_t value) { return std::chrono::milliseconds(value); }

class FakeBacklightAdapter final : public IBacklightAdapter {
  public:
    explicit FakeBacklightAdapter(std::uint8_t initial) : level_(initial) {}

    std::uint8_t level() const override { return level_; }
    void setLevel(std::uint8_t level) override {
        level_ = level;
        writes.push_back(level);
    }

    std::vector<std::uint8_t> writes;

  private:
    std::uint8_t level_ = 0;
};

struct Boundary {
    DisplayPowerState state;
    std::chrono::milliseconds duration;
};

struct Fixture {
    explicit Fixture(std::uint8_t initial = existingBrightness) : backlight(initial) {
        controller.captureNormalLevel();
        backlight.writes.clear();
    }

    void idleUntil(DisplayPowerState state) {
        const Boundary boundaries[] = {
            {DisplayPowerState::Dimming, DisplayPowerController::idleThreshold},
            {DisplayPowerState::Dimmed, DisplayPowerController::dimRampDuration},
            {DisplayPowerState::TurningOff, DisplayPowerController::dimHoldDuration},
            {DisplayPowerState::Off, DisplayPowerController::offRampDuration},
        };
        for (const auto& boundary : boundaries) {
            (void)controller.update(boundary.duration, false);
            assertState(boundary.state);
            if (boundary.state == state)
                return;
        }
        TEST_FAIL_MESSAGE("unreachable idle state requested");
    }

    void assertState(DisplayPowerState expected) const {
        TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(expected),
                                static_cast<unsigned>(controller.state()));
    }

    bool writesAreMonotonic() const {
        return std::is_sorted(backlight.writes.begin(), backlight.writes.end()) ||
               std::is_sorted(backlight.writes.rbegin(), backlight.writes.rend());
    }

    FakeBacklightAdapter backlight;
    DisplayPowerController controller{backlight};
};

} // namespace

void setUp() {}

void tearDown() {}

void test_normal_level_is_captured_from_existing_firmware_brightness() {
    FakeBacklightAdapter backlight(existingBrightness);
    DisplayPowerController controller(backlight);

    controller.captureNormalLevel();

    TEST_ASSERT_EQUAL_UINT8(existingBrightness, controller.normalLevel());
    TEST_ASSERT_EQUAL_UINT8(expectedDimLevel, controller.dimLevel());
    TEST_ASSERT_EQUAL_UINT8(existingBrightness, backlight.level());
    TEST_ASSERT_EQUAL_UINT(0, backlight.writes.size());

    FakeBacklightAdapter dark(0);
    DisplayPowerController darkController(dark);
    darkController.captureNormalLevel();

    TEST_ASSERT_EQUAL_UINT8(DisplayPowerController::maximumLevel, darkController.normalLevel());
    TEST_ASSERT_TRUE(darkController.dimLevel() > 0);
}

void test_display_starts_dimming_at_idle_threshold() {
    Fixture f;

    TEST_ASSERT_FALSE(f.controller.update(ms(14999), false));

    f.assertState(DisplayPowerState::Awake);
    TEST_ASSERT_EQUAL_UINT8(existingBrightness, f.backlight.level());
    TEST_ASSERT_EQUAL_UINT(0, f.backlight.writes.size());

    TEST_ASSERT_FALSE(f.controller.update(ms(1), false));

    f.assertState(DisplayPowerState::Dimming);
    TEST_ASSERT_EQUAL_UINT8(existingBrightness, f.backlight.level());
}

void test_dim_hold_starts_only_after_dim_ramp_completes() {
    Fixture f;
    f.idleUntil(DisplayPowerState::Dimming);

    (void)f.controller.update(ms(299), false);

    f.assertState(DisplayPowerState::Dimming);
    TEST_ASSERT_TRUE(f.backlight.level() < existingBrightness);
    TEST_ASSERT_TRUE(f.backlight.level() > expectedDimLevel);
    TEST_ASSERT_TRUE(f.writesAreMonotonic());

    (void)f.controller.update(ms(1), false);

    f.assertState(DisplayPowerState::Dimmed);
    TEST_ASSERT_EQUAL_UINT8(expectedDimLevel, f.backlight.level());
}

void test_display_stays_dimmed_for_full_hold_duration() {
    Fixture f;
    f.idleUntil(DisplayPowerState::Dimmed);
    f.backlight.writes.clear();

    (void)f.controller.update(ms(119999), false);

    f.assertState(DisplayPowerState::Dimmed);
    TEST_ASSERT_EQUAL_UINT8(expectedDimLevel, f.backlight.level());
    TEST_ASSERT_EQUAL_UINT(0, f.backlight.writes.size());

    (void)f.controller.update(ms(1), false);

    f.assertState(DisplayPowerState::TurningOff);
    TEST_ASSERT_EQUAL_UINT8(expectedDimLevel, f.backlight.level());
}

void test_display_reaches_off_after_off_ramp() {
    Fixture f;
    f.idleUntil(DisplayPowerState::TurningOff);
    f.backlight.writes.clear();

    (void)f.controller.update(ms(399), false);

    f.assertState(DisplayPowerState::TurningOff);
    TEST_ASSERT_TRUE(f.backlight.level() > 0);
    TEST_ASSERT_TRUE(f.backlight.level() < expectedDimLevel);

    (void)f.controller.update(ms(1), false);

    f.assertState(DisplayPowerState::Off);
    TEST_ASSERT_EQUAL_UINT8(0, f.backlight.level());
    TEST_ASSERT_TRUE(f.writesAreMonotonic());
}

void test_awake_physical_press_resets_idle_without_consumption() {
    Fixture f;
    TEST_ASSERT_FALSE(f.controller.update(ms(10000), false));

    TEST_ASSERT_FALSE(f.controller.update(ms(0), true));

    f.assertState(DisplayPowerState::Awake);
    TEST_ASSERT_FALSE(f.controller.update(ms(14999), false));
    f.assertState(DisplayPowerState::Awake);
    TEST_ASSERT_EQUAL_UINT8(existingBrightness, f.backlight.level());
    TEST_ASSERT_EQUAL_UINT(0, f.backlight.writes.size());

    TEST_ASSERT_FALSE(f.controller.update(ms(1), false));
    f.assertState(DisplayPowerState::Dimming);
}

void test_physical_press_after_delayed_update_resets_idle_to_zero() {
    Fixture f;

    // A delayed loop reports the whole interval at once, but it also carries a
    // press, so that interval cannot count as uninterrupted idle time.
    TEST_ASSERT_FALSE(f.controller.update(DisplayPowerController::idleThreshold, true));

    f.assertState(DisplayPowerState::Awake);
    TEST_ASSERT_EQUAL_UINT8(existingBrightness, f.backlight.level());
    TEST_ASSERT_EQUAL_UINT(0, f.backlight.writes.size());

    TEST_ASSERT_FALSE(f.controller.update(ms(14999), false));
    f.assertState(DisplayPowerState::Awake);

    TEST_ASSERT_FALSE(f.controller.update(ms(1), false));
    f.assertState(DisplayPowerState::Dimming);
}

void test_dimmed_input_wakes_and_is_consumed() {
    Fixture f;
    f.idleUntil(DisplayPowerState::Dimmed);

    TEST_ASSERT_TRUE(f.controller.update(ms(0), true));

    f.assertState(DisplayPowerState::Waking);
    TEST_ASSERT_EQUAL_UINT8(expectedDimLevel, f.backlight.level());

    TEST_ASSERT_TRUE(f.controller.update(ms(199), false));
    f.assertState(DisplayPowerState::Waking);
    TEST_ASSERT_TRUE(f.backlight.level() > expectedDimLevel);

    TEST_ASSERT_TRUE(f.controller.update(ms(1), false));
    f.assertState(DisplayPowerState::Awake);
    TEST_ASSERT_EQUAL_UINT8(existingBrightness, f.backlight.level());

    TEST_ASSERT_FALSE(f.controller.update(ms(0), true));
}

void test_off_input_wakes_and_is_consumed() {
    Fixture f;
    f.idleUntil(DisplayPowerState::Off);

    TEST_ASSERT_TRUE(f.controller.update(ms(0), true));

    f.assertState(DisplayPowerState::Waking);
    TEST_ASSERT_EQUAL_UINT8(0, f.backlight.level());

    TEST_ASSERT_TRUE(f.controller.update(DisplayPowerController::wakeRampDuration, false));
    f.assertState(DisplayPowerState::Awake);
    TEST_ASSERT_EQUAL_UINT8(existingBrightness, f.backlight.level());
}

void test_delayed_wake_press_starts_ramp_without_consuming_prior_elapsed() {
    Fixture f;
    f.idleUntil(DisplayPowerState::Off);

    // The reported interval passed before the press was sampled, so it cannot
    // complete a ramp that only exists because of that press.
    TEST_ASSERT_TRUE(f.controller.update(DisplayPowerController::wakeRampDuration, true));

    f.assertState(DisplayPowerState::Waking);
    TEST_ASSERT_EQUAL_UINT8(0, f.backlight.level());

    TEST_ASSERT_TRUE(f.controller.update(ms(199), false));
    f.assertState(DisplayPowerState::Waking);

    TEST_ASSERT_TRUE(f.controller.update(ms(1), false));
    f.assertState(DisplayPowerState::Awake);
    TEST_ASSERT_EQUAL_UINT8(existingBrightness, f.backlight.level());
}

void test_wake_reverses_active_fade_from_current_level() {
    Fixture f;
    f.idleUntil(DisplayPowerState::Dimming);
    (void)f.controller.update(ms(150), false);
    const auto midFade = f.backlight.level();
    TEST_ASSERT_TRUE(midFade < existingBrightness);
    TEST_ASSERT_TRUE(midFade > expectedDimLevel);
    f.backlight.writes.clear();

    TEST_ASSERT_TRUE(f.controller.update(ms(0), true));
    f.assertState(DisplayPowerState::Waking);

    for (int step = 0; step < 10; ++step)
        TEST_ASSERT_TRUE(f.controller.update(ms(20), false));

    f.assertState(DisplayPowerState::Awake);
    TEST_ASSERT_EQUAL_UINT8(existingBrightness, f.backlight.level());
    // No jump to the cancelled fade's target.
    TEST_ASSERT_TRUE(f.backlight.writes.size() > 0);
    for (const auto write : f.backlight.writes)
        TEST_ASSERT_TRUE(write >= midFade);
    TEST_ASSERT_TRUE(f.writesAreMonotonic());
}

void test_wake_reverses_turning_off_fade_from_current_level() {
    Fixture f;
    f.idleUntil(DisplayPowerState::TurningOff);
    (void)f.controller.update(ms(200), false);
    const auto midFade = f.backlight.level();
    TEST_ASSERT_TRUE(midFade > 0);
    TEST_ASSERT_TRUE(midFade < expectedDimLevel);
    f.backlight.writes.clear();

    TEST_ASSERT_TRUE(f.controller.update(ms(0), true));
    f.assertState(DisplayPowerState::Waking);

    for (int step = 0; step < 10; ++step)
        TEST_ASSERT_TRUE(f.controller.update(ms(20), false));

    f.assertState(DisplayPowerState::Awake);
    TEST_ASSERT_EQUAL_UINT8(existingBrightness, f.backlight.level());
    // Neither zero nor the dim level is shown on the way back up.
    TEST_ASSERT_TRUE(f.backlight.writes.size() > 0);
    for (const auto write : f.backlight.writes)
        TEST_ASSERT_TRUE(write >= midFade);
    TEST_ASSERT_TRUE(f.writesAreMonotonic());
}

void test_input_during_waking_is_consumed_without_restarting_ramp() {
    Fixture f;
    f.idleUntil(DisplayPowerState::Off);
    TEST_ASSERT_TRUE(f.controller.update(ms(0), true));

    TEST_ASSERT_TRUE(f.controller.update(ms(100), true));
    f.assertState(DisplayPowerState::Waking);
    const auto halfway = f.backlight.level();

    TEST_ASSERT_TRUE(f.controller.update(ms(100), true));

    f.assertState(DisplayPowerState::Awake);
    TEST_ASSERT_EQUAL_UINT8(existingBrightness, f.backlight.level());
    TEST_ASSERT_TRUE(halfway > 0);
    TEST_ASSERT_TRUE(halfway < existingBrightness);
    TEST_ASSERT_FALSE(f.controller.update(ms(0), true));
}

void test_background_updates_do_not_reset_display_idle() {
    Fixture f;

    for (int frame = 0; frame < 749; ++frame)
        TEST_ASSERT_FALSE(f.controller.update(ms(20), false));

    f.assertState(DisplayPowerState::Awake);

    TEST_ASSERT_FALSE(f.controller.update(ms(20), false));

    f.assertState(DisplayPowerState::Dimming);
}

void test_large_elapsed_update_preserves_all_state_durations() {
    Fixture f;

    // 15 s idle + 300 ms dim ramp + 120 s hold + 400 ms off ramp = 135.700 s.
    TEST_ASSERT_FALSE(f.controller.update(ms(135699), false));

    f.assertState(DisplayPowerState::TurningOff);
    TEST_ASSERT_TRUE(f.backlight.level() > 0);

    (void)f.controller.update(ms(1), false);

    f.assertState(DisplayPowerState::Off);
    TEST_ASSERT_EQUAL_UINT8(0, f.backlight.level());
}

void test_awake_request_wake_resets_idle_without_changing_brightness() {
    Fixture f;
    TEST_ASSERT_FALSE(f.controller.update(ms(10000), false));
    f.backlight.writes.clear();

    f.controller.requestWake();

    f.assertState(DisplayPowerState::Awake);
    TEST_ASSERT_EQUAL_UINT8(existingBrightness, f.backlight.level());
    TEST_ASSERT_EQUAL_UINT(0, f.backlight.writes.size());
    TEST_ASSERT_FALSE(f.controller.update(ms(14999), false));
    f.assertState(DisplayPowerState::Awake);
    TEST_ASSERT_FALSE(f.controller.update(ms(1), false));
    f.assertState(DisplayPowerState::Dimming);
}

void test_dimming_request_wake_reverses_from_current_brightness() {
    Fixture f;
    f.idleUntil(DisplayPowerState::Dimming);
    (void)f.controller.update(ms(150), false);
    const auto midFade = f.backlight.level();
    TEST_ASSERT_TRUE(midFade < existingBrightness);
    TEST_ASSERT_TRUE(midFade > expectedDimLevel);
    f.backlight.writes.clear();

    f.controller.requestWake();
    f.assertState(DisplayPowerState::Waking);
    TEST_ASSERT_EQUAL_UINT8(midFade, f.backlight.level());

    for (int step = 0; step < 10; ++step)
        TEST_ASSERT_TRUE(f.controller.update(ms(20), false));

    f.assertState(DisplayPowerState::Awake);
    TEST_ASSERT_EQUAL_UINT8(existingBrightness, f.backlight.level());
    TEST_ASSERT_TRUE(f.backlight.writes.size() > 0);
    for (const auto write : f.backlight.writes)
        TEST_ASSERT_TRUE(write >= midFade);
    TEST_ASSERT_TRUE(f.writesAreMonotonic());
}

void test_dimmed_request_wake_enters_waking() {
    Fixture f;
    f.idleUntil(DisplayPowerState::Dimmed);

    f.controller.requestWake();

    f.assertState(DisplayPowerState::Waking);
    TEST_ASSERT_EQUAL_UINT8(expectedDimLevel, f.backlight.level());
    TEST_ASSERT_TRUE(f.controller.update(DisplayPowerController::wakeRampDuration, false));
    f.assertState(DisplayPowerState::Awake);
    TEST_ASSERT_EQUAL_UINT8(existingBrightness, f.backlight.level());
}

void test_turning_off_request_wake_reverses_from_current_brightness() {
    Fixture f;
    f.idleUntil(DisplayPowerState::TurningOff);
    (void)f.controller.update(ms(200), false);
    const auto midFade = f.backlight.level();
    TEST_ASSERT_TRUE(midFade > 0);
    TEST_ASSERT_TRUE(midFade < expectedDimLevel);
    f.backlight.writes.clear();

    f.controller.requestWake();
    f.assertState(DisplayPowerState::Waking);
    TEST_ASSERT_EQUAL_UINT8(midFade, f.backlight.level());

    for (int step = 0; step < 10; ++step)
        TEST_ASSERT_TRUE(f.controller.update(ms(20), false));

    f.assertState(DisplayPowerState::Awake);
    TEST_ASSERT_EQUAL_UINT8(existingBrightness, f.backlight.level());
    TEST_ASSERT_TRUE(f.backlight.writes.size() > 0);
    for (const auto write : f.backlight.writes)
        TEST_ASSERT_TRUE(write >= midFade);
    TEST_ASSERT_TRUE(f.writesAreMonotonic());
}

void test_off_request_wake_enters_waking() {
    Fixture f;
    f.idleUntil(DisplayPowerState::Off);

    f.controller.requestWake();

    f.assertState(DisplayPowerState::Waking);
    TEST_ASSERT_EQUAL_UINT8(0, f.backlight.level());
}

void test_waking_request_wake_does_not_restart_ramp() {
    Fixture f;
    f.idleUntil(DisplayPowerState::Off);
    f.controller.requestWake();
    TEST_ASSERT_TRUE(f.controller.update(ms(100), false));
    f.assertState(DisplayPowerState::Waking);
    const auto halfway = f.backlight.level();

    f.controller.requestWake();
    TEST_ASSERT_TRUE(f.controller.update(ms(100), false));

    f.assertState(DisplayPowerState::Awake);
    TEST_ASSERT_EQUAL_UINT8(existingBrightness, f.backlight.level());
    TEST_ASSERT_TRUE(halfway > 0);
    TEST_ASSERT_TRUE(halfway < existingBrightness);
}

void test_programmatic_wake_completes_at_normal_level_and_idle_policy_resumes() {
    Fixture f;
    f.idleUntil(DisplayPowerState::Off);
    f.controller.requestWake();
    TEST_ASSERT_TRUE(f.controller.update(DisplayPowerController::wakeRampDuration, false));

    f.assertState(DisplayPowerState::Awake);
    TEST_ASSERT_EQUAL_UINT8(existingBrightness, f.backlight.level());
    TEST_ASSERT_FALSE(f.controller.update(ms(14999), false));
    f.assertState(DisplayPowerState::Awake);
    TEST_ASSERT_FALSE(f.controller.update(ms(1), false));
    f.assertState(DisplayPowerState::Dimming);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_normal_level_is_captured_from_existing_firmware_brightness);
    RUN_TEST(test_display_starts_dimming_at_idle_threshold);
    RUN_TEST(test_dim_hold_starts_only_after_dim_ramp_completes);
    RUN_TEST(test_display_stays_dimmed_for_full_hold_duration);
    RUN_TEST(test_display_reaches_off_after_off_ramp);
    RUN_TEST(test_awake_physical_press_resets_idle_without_consumption);
    RUN_TEST(test_dimmed_input_wakes_and_is_consumed);
    RUN_TEST(test_off_input_wakes_and_is_consumed);
    RUN_TEST(test_physical_press_after_delayed_update_resets_idle_to_zero);
    RUN_TEST(test_delayed_wake_press_starts_ramp_without_consuming_prior_elapsed);
    RUN_TEST(test_wake_reverses_active_fade_from_current_level);
    RUN_TEST(test_wake_reverses_turning_off_fade_from_current_level);
    RUN_TEST(test_input_during_waking_is_consumed_without_restarting_ramp);
    RUN_TEST(test_background_updates_do_not_reset_display_idle);
    RUN_TEST(test_large_elapsed_update_preserves_all_state_durations);
    RUN_TEST(test_awake_request_wake_resets_idle_without_changing_brightness);
    RUN_TEST(test_dimming_request_wake_reverses_from_current_brightness);
    RUN_TEST(test_dimmed_request_wake_enters_waking);
    RUN_TEST(test_turning_off_request_wake_reverses_from_current_brightness);
    RUN_TEST(test_off_request_wake_enters_waking);
    RUN_TEST(test_waking_request_wake_does_not_restart_ramp);
    RUN_TEST(test_programmatic_wake_completes_at_normal_level_and_idle_policy_resumes);
    return UNITY_END();
}
