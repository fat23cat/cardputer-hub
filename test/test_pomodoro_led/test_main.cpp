#include <unity.h>

#include <algorithm>
#include <deque>
#include <optional>
#include <string>
#include <vector>

#include "apps/pomodoro/pomodoro_app.h"
#include "apps/runtime/mini_app_runtime.h"
#include "core/app_registry/app_registry.h"
#include "core/audio/audio_adapter.h"
#include "core/capabilities/capability_registry.h"
#include "core/display/palette.h"
#include "core/power/display_power_controller.h"
#include "core/storage/storage.h"
#include "services/audio/audio_service.h"
#include "services/configuration/configuration_service.h"
#include "services/indicator/indicator_service.h"
#include "services/pomodoro/pomodoro_led_controller.h"
#include "services/pomodoro/pomodoro_service.h"

using namespace cardputer_hub;
using namespace cardputer_hub::apps;
using namespace cardputer_hub::core;
using namespace cardputer_hub::services;
using namespace std::chrono_literals;

namespace {
class FakeLed final : public ILEDAdapter {
  public:
    void writeFrame(const LedHardwareFrame& frame) override {
        last = frame;
        ++writes;
    }
    LedHardwareFrame last{};
    int writes = 0;
};

class Memory final : public IStorageAdapter {
  public:
    StorageReadResult read(const StorageAddress&) override {
        return {bytes.empty() ? StorageReadStatus::NotFound : StorageReadStatus::Found, bytes};
    }
    StorageWriteStatus write(const StorageAddress&, const StorageBytes& value) override {
        bytes = value;
        return StorageWriteStatus::Stored;
    }
    StorageRemoveStatus remove(const StorageAddress&) override {
        return StorageRemoveStatus::NotFound;
    }
    StorageBytes bytes;
};

class AudioAdapter final : public IAudioAdapter {
  public:
    bool begin(std::uint8_t) override { return true; }
    void setVolume(std::uint8_t) override {}
    bool isPlaying() const override { return playing; }
    bool play(const AudioClip& clip) override {
        clips.push_back(clip);
        return true;
    }
    bool playing = false;
    std::vector<AudioClip> clips;
};

PomodoroDurations durations() { return {64s, 16s, 32s}; }

std::size_t countColor(const IndicatorFrame& frame, RgbColor color) {
    return static_cast<std::size_t>(
        std::count_if(frame.pixels.begin(), frame.pixels.end(), [&](const RgbColor& pixel) {
            return pixel.red == color.red && pixel.green == color.green && pixel.blue == color.blue;
        }));
}

bool pixelEquals(const RgbColor& pixel, RgbColor color) {
    return pixel.red == color.red && pixel.green == color.green && pixel.blue == color.blue;
}

bool pixelOff(const RgbColor& pixel) { return pixelEquals(pixel, {}); }

bool extinctionPrefixOff(const IndicatorFrame& frame, std::uint8_t lit, RgbColor color) {
    const auto offPixels = static_cast<std::uint8_t>(ledMatrixPixelCount - lit);
    for (std::uint8_t i = 0; i < offPixels; ++i) {
        if (!pixelOff(frame.pixels[i]))
            return false;
    }
    for (std::uint8_t i = offPixels; i < ledMatrixPixelCount; ++i) {
        if (!pixelEquals(frame.pixels[i], color))
            return false;
    }
    return true;
}

bool monotonicWithinPhase(PomodoroService& pomodoro, PomodoroLedController& leds,
                          IndicatorService& indicator) {
    std::uint8_t previous = 64;
    const auto phase = pomodoro.snapshot().phase;
    while (pomodoro.snapshot().phase == phase &&
           pomodoro.snapshot().runState == PomodoroRunState::Running &&
           pomodoro.snapshot().remaining > 1s) {
        pomodoro.update(1s);
        if (pomodoro.snapshot().phase != phase)
            return false;
        leds.update(1s);
        indicator.update();
        const auto lit = countColor(indicator.resolved().frame, pomodoroPhaseColor(phase));
        if (lit > previous)
            return false;
        previous = static_cast<std::uint8_t>(lit);
    }
    return true;
}

void test_progress_pixels_and_phase_colors() {
    FakeLed led;
    IndicatorService indicator(led);
    PomodoroService pomodoro(durations());
    PomodoroLedController controller(pomodoro, indicator);
    controller.update(0ms);
    indicator.update();
    TEST_ASSERT_FALSE(indicator.resolved().hasFrame);

    pomodoro.start();
    controller.update(0ms);
    indicator.update();
    TEST_ASSERT_EQUAL(64, countColor(indicator.resolved().frame, pomodoroWorkLed));
    TEST_ASSERT_EQUAL_UINT8(pomodoroBrightnessPercent, indicator.resolved().brightnessPercent);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(IndicatorPriority::BackgroundApplication),
                            static_cast<unsigned>(indicator.resolved().priority));

    pomodoro.update(32s);
    controller.update(0ms);
    indicator.update();
    TEST_ASSERT_EQUAL(32, countColor(indicator.resolved().frame, pomodoroWorkLed));

    pomodoro.pause();
    controller.update(0ms);
    const auto pausedWrites = led.writes;
    pomodoro.update(8s);
    controller.update(8s);
    indicator.update();
    TEST_ASSERT_EQUAL(32, countColor(indicator.resolved().frame, pomodoroWorkLed));
    TEST_ASSERT_EQUAL(pausedWrites, led.writes);

    pomodoro.resume();
    pomodoro.update(32s);
    controller.update(0ms);
    indicator.update();
    TEST_ASSERT_EQUAL(64, countColor(indicator.resolved().frame, pomodoroBreakLed));
    TEST_ASSERT_EQUAL_UINT8(pomodoroBrightnessPercent, indicator.resolved().brightnessPercent);

    controller.update(pomodoroTransitionFeedback);
    pomodoro.update(8s);
    controller.update(0ms);
    indicator.update();
    TEST_ASSERT_EQUAL(32, countColor(indicator.resolved().frame, pomodoroBreakLed));
}

void test_long_break_uses_leaf_and_reset_releases_claim() {
    FakeLed led;
    IndicatorService indicator(led);
    PomodoroService pomodoro(durations());
    PomodoroLedController controller(pomodoro, indicator);
    pomodoro.start();
    for (int i = 0; i < 7; ++i)
        pomodoro.skip();
    controller.update(0ms);
    indicator.update();
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(PomodoroPhase::LongBreak),
                            static_cast<unsigned>(pomodoro.snapshot().phase));
    TEST_ASSERT_EQUAL(64, countColor(indicator.resolved().frame, pomodoroBreakLed));
    TEST_ASSERT_EQUAL_UINT8(pomodoroBrightnessPercent, indicator.resolved().brightnessPercent);
    pomodoro.reset();
    controller.update(0ms);
    indicator.update();
    TEST_ASSERT_FALSE(indicator.resolved().hasFrame);
}

void test_transition_stays_at_policy_brightness_and_is_not_brighter() {
    FakeLed led;
    IndicatorService indicator(led);
    PomodoroService pomodoro(durations());
    PomodoroLedController controller(pomodoro, indicator);
    pomodoro.start();
    pomodoro.skip();
    controller.update(0ms);
    indicator.update();
    TEST_ASSERT_EQUAL_UINT8(pomodoroBrightnessPercent, indicator.resolved().brightnessPercent);
    TEST_ASSERT_EQUAL(64, countColor(indicator.resolved().frame, pomodoroBreakLed));
}

void test_pixel_count_is_monotonic_within_a_phase() {
    FakeLed led;
    IndicatorService indicator(led);
    PomodoroService pomodoro({8s, 4s, 4s});
    PomodoroLedController controller(pomodoro, indicator);
    pomodoro.start();
    controller.update(0ms);
    indicator.update();
    TEST_ASSERT_TRUE(monotonicWithinPhase(pomodoro, controller, indicator));
}

void test_hidden_claim_keeps_updating_and_restore_stays_policy_brightness() {
    FakeLed led;
    IndicatorService indicator(led);
    PomodoroService pomodoro(durations());
    PomodoroLedController controller(pomodoro, indicator);
    pomodoro.start();
    controller.update(0ms);
    indicator.update();
    auto foreground = indicator.acquire("led-control", IndicatorPriority::ForegroundApplication);
    IndicatorFrame bright{};
    bright.pixels.fill(palette::vermilion);
    foreground.setFrame(bright);
    pomodoro.update(32s);
    controller.update(0ms);
    indicator.update();
    TEST_ASSERT_EQUAL_STRING("led-control", indicator.resolved().owner.c_str());
    TEST_ASSERT_EQUAL_UINT8(100, indicator.resolved().brightnessPercent);
    foreground.release();
    indicator.update();
    TEST_ASSERT_EQUAL_STRING("pomodoro", indicator.resolved().owner.c_str());
    TEST_ASSERT_EQUAL_UINT8(pomodoroBrightnessPercent, indicator.resolved().brightnessPercent);
    TEST_ASSERT_EQUAL(32, countColor(indicator.resolved().frame, pomodoroWorkLed));
}

void test_same_visible_frame_is_not_rewritten() {
    FakeLed led;
    IndicatorService indicator(led);
    PomodoroService pomodoro(durations());
    PomodoroLedController controller(pomodoro, indicator);
    pomodoro.start();
    controller.update(0ms);
    indicator.update();
    const auto writes = led.writes;
    controller.update(0ms);
    indicator.update();
    TEST_ASSERT_EQUAL(writes, led.writes);
}

void test_phase_change_plays_audio_cue_without_led_hardware() {
    FakeLed led;
    IndicatorService indicator(led);
    Memory memory;
    Storage storage{memory};
    ConfigurationService configuration{storage};
    AudioAdapter audioAdapter;
    AudioService audio{configuration, audioAdapter};
    TEST_ASSERT_TRUE(audio.start() == AudioResult::Success);
    PomodoroService pomodoro(durations());
    PomodoroLedController controller(pomodoro, indicator, &audio);
    pomodoro.start();
    controller.update(0ms);
    TEST_ASSERT_TRUE(audioAdapter.clips.empty());
    pomodoro.skip();
    controller.update(0ms);
    TEST_ASSERT_EQUAL_UINT(1, audioAdapter.clips.size());
}

void test_led_phase_colors_are_muted_not_lcd_palette() {
    TEST_ASSERT_FALSE(pixelEquals(pomodoroWorkLed, palette::blue));
    TEST_ASSERT_FALSE(pixelEquals(pomodoroBreakLed, palette::leaf));
    TEST_ASSERT_FALSE(pixelEquals(pomodoroWorkLed, pomodoroBreakLed));
    TEST_ASSERT_TRUE(pixelEquals(pomodoroPhaseColor(PomodoroPhase::Work), pomodoroWorkLed));
    TEST_ASSERT_TRUE(pixelEquals(pomodoroPhaseColor(PomodoroPhase::ShortBreak), pomodoroBreakLed));
    TEST_ASSERT_TRUE(pixelEquals(pomodoroPhaseColor(PomodoroPhase::LongBreak), pomodoroBreakLed));

    const auto scale = [](std::uint8_t channel) {
        return static_cast<std::uint8_t>(
            (static_cast<unsigned>(channel) * pomodoroBrightnessPercent + 50U) / 100U);
    };
    const RgbColor workHw{scale(pomodoroWorkLed.red), scale(pomodoroWorkLed.green),
                          scale(pomodoroWorkLed.blue)};
    const RgbColor breakHw{scale(pomodoroBreakLed.red), scale(pomodoroBreakLed.green),
                           scale(pomodoroBreakLed.blue)};
    TEST_ASSERT_FALSE(pixelEquals(workHw, breakHw));
    TEST_ASSERT_TRUE(workHw.blue > workHw.green);
    TEST_ASSERT_TRUE(breakHw.green > breakHw.blue);
}

void test_lit_pixel_helpers() {
    PomodoroSnapshot snapshot;
    snapshot.duration = 64s;
    snapshot.remaining = 64s;
    TEST_ASSERT_EQUAL_UINT8(64, pomodoroLitPixels(snapshot));
    snapshot.remaining = 32s;
    TEST_ASSERT_EQUAL_UINT8(32, pomodoroLitPixels(snapshot));
    snapshot.remaining = 1s;
    TEST_ASSERT_TRUE(pomodoroLitPixels(snapshot) >= 1);
    snapshot.remaining = 0s;
    TEST_ASSERT_EQUAL_UINT8(0, pomodoroLitPixels(snapshot));
}

void test_progress_frame_extinguishes_from_logical_origin() {
    const auto full = pomodoroProgressFrame(palette::blue, 64);
    TEST_ASSERT_TRUE(extinctionPrefixOff(full, 64, palette::blue));
    const auto sixtyThree = pomodoroProgressFrame(palette::blue, 63);
    TEST_ASSERT_TRUE(pixelOff(sixtyThree.pixels[0]));
    TEST_ASSERT_TRUE(extinctionPrefixOff(sixtyThree, 63, palette::blue));
    const auto sixtyTwo = pomodoroProgressFrame(palette::leaf, 62);
    TEST_ASSERT_TRUE(pixelOff(sixtyTwo.pixels[0]));
    TEST_ASSERT_TRUE(pixelOff(sixtyTwo.pixels[1]));
    TEST_ASSERT_TRUE(extinctionPrefixOff(sixtyTwo, 62, palette::leaf));
}

void test_controller_keeps_transition_after_stalled_boundary() {
    FakeLed led;
    IndicatorService indicator(led);
    PomodoroService pomodoro(durations());
    PomodoroLedController controller(pomodoro, indicator);
    pomodoro.start();
    controller.update(0ms);
    pomodoro.update(63s + 900ms);
    controller.update(0ms);
    indicator.update();
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(PomodoroPhase::Work),
                            static_cast<unsigned>(pomodoro.snapshot().phase));
    TEST_ASSERT_EQUAL(1, countColor(indicator.resolved().frame, pomodoroWorkLed));
    TEST_ASSERT_TRUE(extinctionPrefixOff(indicator.resolved().frame, 1, pomodoroWorkLed));

    pomodoro.update(500ms);
    controller.update(500ms);
    indicator.update();
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(PomodoroPhase::ShortBreak),
                            static_cast<unsigned>(pomodoro.snapshot().phase));
    TEST_ASSERT_EQUAL(64, countColor(indicator.resolved().frame, pomodoroBreakLed));
    TEST_ASSERT_TRUE(extinctionPrefixOff(indicator.resolved().frame, 64, pomodoroBreakLed));
}

void test_controller_reports_same_phase_after_multiple_boundaries() {
    FakeLed led;
    IndicatorService indicator(led);
    Memory memory;
    Storage storage{memory};
    ConfigurationService configuration{storage};
    AudioAdapter audioAdapter;
    AudioService audio{configuration, audioAdapter};
    TEST_ASSERT_TRUE(audio.start() == AudioResult::Success);
    PomodoroService pomodoro({10s, 5s, 15s});
    PomodoroLedController controller(pomodoro, indicator, &audio);
    pomodoro.start();
    controller.update(0ms);
    TEST_ASSERT_TRUE(audioAdapter.clips.empty());
    pomodoro.update(17s);
    controller.update(17s);
    indicator.update();
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(PomodoroPhase::Work),
                            static_cast<unsigned>(pomodoro.snapshot().phase));
    TEST_ASSERT_EQUAL(64, countColor(indicator.resolved().frame, pomodoroWorkLed));
    TEST_ASSERT_EQUAL_UINT(2, audioAdapter.clips.size());
}

class FakeBacklight final : public IBacklightAdapter {
  public:
    explicit FakeBacklight(std::uint8_t initial = 128) : level_(initial) {}
    std::uint8_t level() const override { return level_; }
    void setLevel(std::uint8_t level) override { level_ = level; }

  private:
    std::uint8_t level_ = 0;
};

void idleUntilOff(DisplayPowerController& display) {
    (void)display.update(DisplayPowerController::idleThreshold, false);
    (void)display.update(DisplayPowerController::dimRampDuration, false);
    (void)display.update(DisplayPowerController::dimHoldDuration, false);
    (void)display.update(DisplayPowerController::offRampDuration, false);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(DisplayPowerState::Off),
                            static_cast<unsigned>(display.state()));
}

void assertWaking(const DisplayPowerController& display) {
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(DisplayPowerState::Waking),
                            static_cast<unsigned>(display.state()));
}

void test_work_to_short_break_requests_display_wake() {
    FakeLed led;
    IndicatorService indicator(led);
    FakeBacklight backlight;
    DisplayPowerController display(backlight);
    display.captureNormalLevel();
    idleUntilOff(display);
    PomodoroService pomodoro(durations());
    PomodoroLedController controller(pomodoro, indicator, nullptr, &display);
    pomodoro.start();
    controller.update(0ms);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(DisplayPowerState::Off),
                            static_cast<unsigned>(display.state()));
    pomodoro.skip();
    controller.update(0ms);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(PomodoroPhase::ShortBreak),
                            static_cast<unsigned>(pomodoro.snapshot().phase));
    assertWaking(display);
}

void test_short_break_to_work_requests_display_wake() {
    FakeLed led;
    IndicatorService indicator(led);
    FakeBacklight backlight;
    DisplayPowerController display(backlight);
    display.captureNormalLevel();
    PomodoroService pomodoro(durations());
    pomodoro.start();
    pomodoro.skip();
    idleUntilOff(display);
    PomodoroLedController controller(pomodoro, indicator, nullptr, &display);
    pomodoro.skip();
    controller.update(0ms);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(PomodoroPhase::Work),
                            static_cast<unsigned>(pomodoro.snapshot().phase));
    assertWaking(display);
}

void test_fourth_work_to_long_break_requests_display_wake() {
    FakeLed led;
    IndicatorService indicator(led);
    FakeBacklight backlight;
    DisplayPowerController display(backlight);
    display.captureNormalLevel();
    PomodoroService pomodoro(durations());
    pomodoro.start();
    for (int i = 0; i < 6; ++i)
        pomodoro.skip();
    idleUntilOff(display);
    PomodoroLedController controller(pomodoro, indicator, nullptr, &display);
    pomodoro.skip();
    controller.update(0ms);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(PomodoroPhase::LongBreak),
                            static_cast<unsigned>(pomodoro.snapshot().phase));
    assertWaking(display);
}

void test_long_break_to_work_requests_display_wake() {
    FakeLed led;
    IndicatorService indicator(led);
    FakeBacklight backlight;
    DisplayPowerController display(backlight);
    display.captureNormalLevel();
    PomodoroService pomodoro(durations());
    pomodoro.start();
    for (int i = 0; i < 7; ++i)
        pomodoro.skip();
    idleUntilOff(display);
    PomodoroLedController controller(pomodoro, indicator, nullptr, &display);
    pomodoro.skip();
    controller.update(0ms);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(PomodoroPhase::Work),
                            static_cast<unsigned>(pomodoro.snapshot().phase));
    assertWaking(display);
}

void test_multiple_transitions_request_wake_once_without_restarting_ramp() {
    FakeLed led;
    IndicatorService indicator(led);
    FakeBacklight backlight;
    DisplayPowerController display(backlight);
    display.captureNormalLevel();
    idleUntilOff(display);
    Memory memory;
    Storage storage{memory};
    ConfigurationService configuration{storage};
    AudioAdapter audioAdapter;
    AudioService audio{configuration, audioAdapter};
    TEST_ASSERT_TRUE(audio.start() == AudioResult::Success);
    PomodoroService pomodoro({10s, 5s, 15s});
    PomodoroLedController controller(pomodoro, indicator, &audio, &display);
    pomodoro.start();
    pomodoro.update(17s);
    controller.update(17s);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(PomodoroPhase::Work),
                            static_cast<unsigned>(pomodoro.snapshot().phase));
    TEST_ASSERT_EQUAL_UINT(2, audioAdapter.clips.size());
    assertWaking(display);
    TEST_ASSERT_TRUE(display.update(100ms, false));
    assertWaking(display);
    const auto halfway = backlight.level();
    pomodoro.skip();
    controller.update(0ms);
    TEST_ASSERT_EQUAL_UINT(3, audioAdapter.clips.size());
    TEST_ASSERT_TRUE(display.update(100ms, false));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(DisplayPowerState::Awake),
                            static_cast<unsigned>(display.state()));
    TEST_ASSERT_TRUE(halfway > 0);
    TEST_ASSERT_TRUE(halfway < display.normalLevel());
}

void test_hidden_led_claim_does_not_suppress_display_wake() {
    FakeLed led;
    IndicatorService indicator(led);
    FakeBacklight backlight;
    DisplayPowerController display(backlight);
    display.captureNormalLevel();
    idleUntilOff(display);
    PomodoroService pomodoro(durations());
    PomodoroLedController controller(pomodoro, indicator, nullptr, &display);
    pomodoro.start();
    controller.update(0ms);
    auto foreground = indicator.acquire("led-control", IndicatorPriority::ForegroundApplication);
    IndicatorFrame bright{};
    bright.pixels.fill(palette::vermilion);
    foreground.setFrame(bright);
    pomodoro.skip();
    controller.update(0ms);
    assertWaking(display);
}

void test_audio_failure_does_not_suppress_display_wake() {
    FakeLed led;
    IndicatorService indicator(led);
    FakeBacklight backlight;
    DisplayPowerController display(backlight);
    display.captureNormalLevel();
    idleUntilOff(display);
    Memory memory;
    Storage storage{memory};
    ConfigurationService configuration{storage};
    AudioAdapter audioAdapter;
    audioAdapter.playing = true;
    AudioService audio{configuration, audioAdapter};
    TEST_ASSERT_TRUE(audio.start() == AudioResult::Success);
    PomodoroService pomodoro(durations());
    PomodoroLedController controller(pomodoro, indicator, &audio, &display);
    pomodoro.start();
    pomodoro.skip();
    controller.update(0ms);
    TEST_ASSERT_TRUE(audioAdapter.clips.empty());
    assertWaking(display);
}

void test_running_pomodoro_updates_do_not_keep_display_awake() {
    FakeLed led;
    IndicatorService indicator(led);
    FakeBacklight backlight;
    DisplayPowerController display(backlight);
    display.captureNormalLevel();
    PomodoroService pomodoro(durations());
    PomodoroLedController controller(pomodoro, indicator, nullptr, &display);
    pomodoro.start();
    controller.update(0ms);
    TEST_ASSERT_FALSE(display.update(DisplayPowerController::idleThreshold - 1ms, false));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(DisplayPowerState::Awake),
                            static_cast<unsigned>(display.state()));
    TEST_ASSERT_FALSE(display.update(1ms, false));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(DisplayPowerState::Dimming),
                            static_cast<unsigned>(display.state()));
}
} // namespace

void setUp() {}
void tearDown() {}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_progress_pixels_and_phase_colors);
    RUN_TEST(test_long_break_uses_leaf_and_reset_releases_claim);
    RUN_TEST(test_transition_stays_at_policy_brightness_and_is_not_brighter);
    RUN_TEST(test_pixel_count_is_monotonic_within_a_phase);
    RUN_TEST(test_hidden_claim_keeps_updating_and_restore_stays_policy_brightness);
    RUN_TEST(test_same_visible_frame_is_not_rewritten);
    RUN_TEST(test_phase_change_plays_audio_cue_without_led_hardware);
    RUN_TEST(test_led_phase_colors_are_muted_not_lcd_palette);
    RUN_TEST(test_lit_pixel_helpers);
    RUN_TEST(test_progress_frame_extinguishes_from_logical_origin);
    RUN_TEST(test_controller_keeps_transition_after_stalled_boundary);
    RUN_TEST(test_controller_reports_same_phase_after_multiple_boundaries);
    RUN_TEST(test_work_to_short_break_requests_display_wake);
    RUN_TEST(test_short_break_to_work_requests_display_wake);
    RUN_TEST(test_fourth_work_to_long_break_requests_display_wake);
    RUN_TEST(test_long_break_to_work_requests_display_wake);
    RUN_TEST(test_multiple_transitions_request_wake_once_without_restarting_ramp);
    RUN_TEST(test_hidden_led_claim_does_not_suppress_display_wake);
    RUN_TEST(test_audio_failure_does_not_suppress_display_wake);
    RUN_TEST(test_running_pomodoro_updates_do_not_keep_display_awake);
    return UNITY_END();
}
