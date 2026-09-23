#include <unity.h>

#include "core/display/palette.h"
#include "services/indicator/indicator_service.h"

using namespace cardputer_hub::core;
using namespace cardputer_hub::services;

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

IndicatorFrame solid(RgbColor color) {
    IndicatorFrame frame{};
    frame.pixels.fill(color);
    return frame;
}

std::size_t litCount(const IndicatorFrame& frame, RgbColor color) {
    std::size_t count = 0;
    for (const auto& pixel : frame.pixels) {
        if (pixel.red == color.red && pixel.green == color.green && pixel.blue == color.blue)
            ++count;
    }
    return count;
}

bool hardwareEquals(const LedHardwareFrame& frame, RgbColor color) {
    for (const auto& pixel : frame.pixels) {
        if (pixel.red != color.red || pixel.green != color.green || pixel.blue != color.blue)
            return false;
    }
    return true;
}

RgbColor scaled(RgbColor color, std::uint8_t percent) {
    return {static_cast<std::uint8_t>((static_cast<unsigned>(color.red) * percent + 50U) / 100U),
            static_cast<std::uint8_t>((static_cast<unsigned>(color.green) * percent + 50U) / 100U),
            static_cast<std::uint8_t>((static_cast<unsigned>(color.blue) * percent + 50U) / 100U)};
}

void test_single_claim_becomes_visible() {
    FakeLed led;
    IndicatorService indicator(led);
    auto claim = indicator.acquire("foreground", IndicatorPriority::ForegroundApplication);
    claim.setFrame(solid(palette::vermilion));
    indicator.update();
    TEST_ASSERT_TRUE(indicator.resolved().hasFrame);
    TEST_ASSERT_EQUAL_STRING("foreground", indicator.resolved().owner.c_str());
    TEST_ASSERT_EQUAL(1, led.writes);
    TEST_ASSERT_TRUE(hardwareEquals(led.last, palette::vermilion));
}

void test_sound_claim_uses_three_percent_and_restores_latest_background() {
    FakeLed led;
    IndicatorService indicator(led);
    auto background =
        indicator.acquire(pomodoroIndicatorOwner, IndicatorPriority::BackgroundApplication);
    background.setFrame(solid({0, 255, 0}));
    auto sound =
        indicator.acquire(soundReactiveIndicatorOwner, IndicatorPriority::ForegroundApplication);
    sound.setFrame(solid({255, 0, 0}));
    indicator.update();
    TEST_ASSERT_EQUAL_STRING(soundReactiveIndicatorOwner, indicator.resolved().owner.c_str());
    TEST_ASSERT_EQUAL_UINT8(3, indicator.resolved().brightnessPercent);
    TEST_ASSERT_TRUE(hardwareEquals(led.last, {8, 0, 0}));
    background.setFrame(solid({0, 0, 255}));
    sound.release();
    indicator.update();
    TEST_ASSERT_EQUAL_STRING(pomodoroIndicatorOwner, indicator.resolved().owner.c_str());
    TEST_ASSERT_TRUE(hardwareEquals(led.last, {0, 0, 8}));
}

void test_higher_priority_replaces_lower_and_hidden_claim_may_update() {
    FakeLed led;
    IndicatorService indicator(led);
    auto background = indicator.acquire("pomodoro", IndicatorPriority::BackgroundApplication);
    background.setFrame(solid(palette::blue));
    indicator.update();
    auto foreground = indicator.acquire("led-control", IndicatorPriority::ForegroundApplication);
    foreground.setFrame(solid(palette::vermilion));
    indicator.update();
    TEST_ASSERT_EQUAL_STRING("led-control", indicator.resolved().owner.c_str());
    TEST_ASSERT_TRUE(hardwareEquals(led.last, palette::vermilion));

    IndicatorFrame progress{};
    progress.pixels[0] = palette::blue;
    background.setFrame(progress);
    indicator.update();
    TEST_ASSERT_EQUAL_STRING("led-control", indicator.resolved().owner.c_str());
    TEST_ASSERT_TRUE(hardwareEquals(led.last, palette::vermilion));

    foreground.release();
    indicator.update();
    TEST_ASSERT_EQUAL_STRING("pomodoro", indicator.resolved().owner.c_str());
    TEST_ASSERT_EQUAL_UINT8(pomodoroBrightnessPercent, indicator.resolved().brightnessPercent);
    TEST_ASSERT_EQUAL(1, litCount(indicator.resolved().frame, palette::blue));
}

void test_notification_overrides_foreground_and_background_overrides_connection() {
    FakeLed led;
    IndicatorService indicator(led);
    auto connection = indicator.acquire("link", IndicatorPriority::Connection);
    connection.setFrame(solid(palette::pale));
    auto background = indicator.acquire("pomodoro", IndicatorPriority::BackgroundApplication);
    background.setFrame(solid(palette::blue));
    auto foreground = indicator.acquire("led-control", IndicatorPriority::ForegroundApplication);
    foreground.setFrame(solid(palette::ink));
    auto notification = indicator.acquire("telegram", IndicatorPriority::Notification);
    notification.setFrame(solid(palette::leaf));
    indicator.update();
    TEST_ASSERT_EQUAL_STRING("telegram", indicator.resolved().owner.c_str());
    notification.release();
    indicator.update();
    TEST_ASSERT_EQUAL_STRING("led-control", indicator.resolved().owner.c_str());
    foreground.release();
    indicator.update();
    TEST_ASSERT_EQUAL_STRING("pomodoro", indicator.resolved().owner.c_str());
    background.release();
    indicator.update();
    TEST_ASSERT_EQUAL_STRING("link", indicator.resolved().owner.c_str());
}

void test_owner_cannot_clear_unrelated_claims() {
    FakeLed led;
    IndicatorService indicator(led);
    auto first = indicator.acquire("pomodoro", IndicatorPriority::BackgroundApplication);
    first.setFrame(solid(palette::blue));
    auto second = indicator.acquire("led-control", IndicatorPriority::ForegroundApplication);
    second.setFrame(solid(palette::vermilion));
    IndicatorClaim empty;
    empty.release();
    indicator.update();
    TEST_ASSERT_TRUE(first.valid());
    TEST_ASSERT_TRUE(second.valid());
    TEST_ASSERT_EQUAL_STRING("led-control", indicator.resolved().owner.c_str());
}

void test_pomodoro_brightness_is_three_percent_and_does_not_leak() {
    FakeLed led;
    IndicatorService indicator(led);
    auto pomodoro =
        indicator.acquire(pomodoroIndicatorOwner, IndicatorPriority::BackgroundApplication);
    pomodoro.setFrame(solid(palette::blue));
    auto foreground = indicator.acquire("led-control", IndicatorPriority::ForegroundApplication);
    foreground.setFrame(solid(palette::vermilion));
    indicator.update();
    TEST_ASSERT_EQUAL_UINT8(100, indicator.resolved().brightnessPercent);
    TEST_ASSERT_TRUE(hardwareEquals(led.last, palette::vermilion));
    foreground.release();
    indicator.update();
    TEST_ASSERT_EQUAL_UINT8(pomodoroBrightnessPercent, indicator.resolved().brightnessPercent);
    TEST_ASSERT_EQUAL(64, litCount(indicator.resolved().frame, palette::blue));
    TEST_ASSERT_TRUE(hardwareEquals(led.last, scaled(palette::blue, pomodoroBrightnessPercent)));
    TEST_ASSERT_FALSE(hardwareEquals(led.last, palette::blue));
}

void test_unchanged_frame_does_not_rewrite_adapter() {
    FakeLed led;
    IndicatorService indicator(led);
    auto claim = indicator.acquire("pomodoro", IndicatorPriority::BackgroundApplication);
    claim.setFrame(solid(palette::blue));
    indicator.update();
    indicator.update();
    claim.setFrame(solid(palette::blue));
    indicator.update();
    TEST_ASSERT_EQUAL(1, led.writes);
    TEST_ASSERT_EQUAL(1, indicator.adapterWrites());
}

void test_empty_owner_does_not_acquire() {
    FakeLed led;
    IndicatorService indicator(led);
    auto claim = indicator.acquire("", IndicatorPriority::Critical);
    TEST_ASSERT_FALSE(claim.valid());
    indicator.update();
    TEST_ASSERT_FALSE(indicator.resolved().hasFrame);
}
} // namespace

void setUp() {}
void tearDown() {}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_sound_claim_uses_three_percent_and_restores_latest_background);
    RUN_TEST(test_single_claim_becomes_visible);
    RUN_TEST(test_higher_priority_replaces_lower_and_hidden_claim_may_update);
    RUN_TEST(test_notification_overrides_foreground_and_background_overrides_connection);
    RUN_TEST(test_owner_cannot_clear_unrelated_claims);
    RUN_TEST(test_pomodoro_brightness_is_three_percent_and_does_not_leak);
    RUN_TEST(test_unchanged_frame_does_not_rewrite_adapter);
    RUN_TEST(test_empty_owner_does_not_acquire);
    return UNITY_END();
}
