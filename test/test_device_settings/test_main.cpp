#include <unity.h>

#include "services/device_settings/device_settings_service.h"

using namespace cardputer_hub;

namespace {
class Memory final : public core::IStorageAdapter {
  public:
    core::StorageReadResult read(const core::StorageAddress&) override {
        return {bytes.empty() ? core::StorageReadStatus::NotFound : core::StorageReadStatus::Found,
                bytes};
    }
    core::StorageWriteStatus write(const core::StorageAddress&,
                                   const core::StorageBytes& value) override {
        ++writes;
        if (fail)
            return core::StorageWriteStatus::BackendError;
        bytes = value;
        return core::StorageWriteStatus::Stored;
    }
    core::StorageRemoveStatus remove(const core::StorageAddress&) override {
        return core::StorageRemoveStatus::NotFound;
    }
    core::StorageBytes bytes;
    bool fail = false;
    int writes = 0;
};

class Backlight final : public core::IBacklightAdapter {
  public:
    std::uint8_t level() const override { return value; }
    void setLevel(std::uint8_t next) override { value = next; }
    std::uint8_t value = 255;
};

class Led final : public core::ILEDAdapter {
  public:
    void writeFrame(const core::LedHardwareFrame& frame) override {
        last = frame;
        ++writes;
    }
    core::LedHardwareFrame last{};
    int writes = 0;
};

struct Fixture {
    Memory memory;
    core::Storage storage{memory};
    services::ConfigurationService configuration{storage};
    Backlight backlight;
    core::DisplayPowerController display{backlight};
    Led led;
    services::IndicatorService indicator{led};
    services::DeviceSettingsService settings{configuration, display, indicator};
    Fixture() {
        display.captureNormalLevel();
        TEST_ASSERT_TRUE(settings.start());
    }
    bool step(const char* id, int delta) {
        return settings.handle({id, "test", {{"delta", std::int32_t{delta}}}}) ==
               core::ActionHandlingResult::Handled;
    }
};

void test_actions_persist_and_apply_each_setting() {
    Fixture f;
    TEST_ASSERT_TRUE(f.step("display.timeout.step", 1));
    TEST_ASSERT_TRUE(f.settings.screenTimeout() == core::ScreenTimeoutMode::Long);
    TEST_ASSERT_TRUE(f.display.timeoutMode() == core::ScreenTimeoutMode::Long);
    TEST_ASSERT_TRUE(f.step("display.brightness.step", -10));
    TEST_ASSERT_EQUAL_UINT8(90, f.settings.screenBrightness());
    TEST_ASSERT_EQUAL_UINT8(230, f.backlight.value);
    TEST_ASSERT_TRUE(f.step("indicator.brightness.step", 1));
    TEST_ASSERT_EQUAL_UINT8(4, f.indicator.maximumBrightnessPercent());
    TEST_ASSERT_EQUAL(3, f.memory.writes);
    services::ConfigurationService reloaded(f.storage);
    TEST_ASSERT_TRUE(reloaded.load() == services::ConfigurationResult::Success);
    TEST_ASSERT_TRUE(reloaded.value().device.screenTimeout == core::ScreenTimeoutMode::Long);
    TEST_ASSERT_EQUAL_UINT8(90, reloaded.value().device.screenBrightness);
    TEST_ASSERT_EQUAL_UINT8(4, reloaded.value().device.ledBrightness);
}

void test_boundaries_and_invalid_deltas_do_not_write() {
    Fixture f;
    TEST_ASSERT_FALSE(f.step("display.timeout.step", -1));
    TEST_ASSERT_FALSE(f.step("display.brightness.step", 10));
    TEST_ASSERT_FALSE(f.step("display.brightness.step", 1));
    for (int i = 0; i < 8; ++i)
        TEST_ASSERT_TRUE(f.step("display.brightness.step", -10));
    TEST_ASSERT_EQUAL_UINT8(20, f.settings.screenBrightness());
    const auto writes = f.memory.writes;
    TEST_ASSERT_FALSE(f.step("display.brightness.step", -10));
    TEST_ASSERT_EQUAL(writes, f.memory.writes);
    for (int i = 0; i < 7; ++i)
        TEST_ASSERT_TRUE(f.step("indicator.brightness.step", 1));
    TEST_ASSERT_EQUAL_UINT8(10, f.settings.ledBrightness());
    TEST_ASSERT_FALSE(f.step("indicator.brightness.step", 1));
    TEST_ASSERT_EQUAL_UINT8(10, f.indicator.maximumBrightnessPercent());
    TEST_ASSERT_FALSE(f.step("indicator.brightness.step", 10));
    TEST_ASSERT_TRUE(f.step("display.timeout.step", 1));
    TEST_ASSERT_TRUE(f.step("display.timeout.step", 1));
    const auto before = f.memory.writes;
    TEST_ASSERT_FALSE(f.step("display.timeout.step", 1));
    TEST_ASSERT_EQUAL(before, f.memory.writes);
    for (int i = 0; i < 9; ++i)
        TEST_ASSERT_TRUE(f.step("indicator.brightness.step", -1));
    TEST_ASSERT_EQUAL_UINT8(1, f.settings.ledBrightness());
    TEST_ASSERT_FALSE(f.step("indicator.brightness.step", -1));
    TEST_ASSERT_EQUAL_UINT8(1, f.indicator.maximumBrightnessPercent());
}

void test_failed_persistence_keeps_runtime_unchanged() {
    Fixture f;
    f.memory.fail = true;
    TEST_ASSERT_FALSE(f.step("display.timeout.step", 1));
    TEST_ASSERT_FALSE(f.step("display.brightness.step", -10));
    TEST_ASSERT_FALSE(f.step("indicator.brightness.step", 1));
    TEST_ASSERT_TRUE(f.display.timeoutMode() == core::ScreenTimeoutMode::Normal);
    TEST_ASSERT_EQUAL_UINT8(100, f.display.brightnessPercent());
    TEST_ASSERT_EQUAL_UINT8(255, f.backlight.value);
    TEST_ASSERT_EQUAL_UINT8(3, f.indicator.maximumBrightnessPercent());
}
} // namespace

void setUp() {}
void tearDown() {}
int main() {
    UNITY_BEGIN();
    RUN_TEST(test_actions_persist_and_apply_each_setting);
    RUN_TEST(test_boundaries_and_invalid_deltas_do_not_write);
    RUN_TEST(test_failed_persistence_keeps_runtime_unchanged);
    return UNITY_END();
}
