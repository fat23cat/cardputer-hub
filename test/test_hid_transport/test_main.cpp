#include <unity.h>

#include <array>
#include <cstdint>
#include <variant>

#include "connectivity/hid/hid_transport.h"

namespace {

using cardputer_hub::connectivity::HidConsumerReport;
using cardputer_hub::connectivity::HidKeyboardReport;
using cardputer_hub::connectivity::HidReport;
using cardputer_hub::connectivity::isValidHidReport;

void test_keyboard_and_consumer_reports_preserve_hardware_neutral_values() {
    const HidKeyboardReport keyboard{0x03, {0x04, 0x05, 0x06, 0, 0, 0}};
    const HidConsumerReport consumer{0x00E9};
    const HidReport keyboardReport = keyboard;
    const HidReport consumerReport = consumer;

    TEST_ASSERT_TRUE(std::holds_alternative<HidKeyboardReport>(keyboardReport));
    TEST_ASSERT_EQUAL_HEX8(0x03, std::get<HidKeyboardReport>(keyboardReport).modifiers);
    TEST_ASSERT_EQUAL_HEX8(0x06, std::get<HidKeyboardReport>(keyboardReport).usages[2]);
    TEST_ASSERT_TRUE(std::holds_alternative<HidConsumerReport>(consumerReport));
    TEST_ASSERT_EQUAL_HEX16(0x00E9, std::get<HidConsumerReport>(consumerReport).usage);
}

void test_neutral_reports_are_zero_filled_and_valid() {
    const auto keyboard = HidKeyboardReport::neutral();
    const auto consumer = HidConsumerReport::neutral();

    TEST_ASSERT_EQUAL_HEX8(0, keyboard.modifiers);
    for (const auto usage : keyboard.usages) {
        TEST_ASSERT_EQUAL_HEX8(0, usage);
    }
    TEST_ASSERT_EQUAL_HEX16(0, consumer.usage);
    TEST_ASSERT_TRUE(isValidHidReport(HidReport{keyboard}));
    TEST_ASSERT_TRUE(isValidHidReport(HidReport{consumer}));
}

void test_keyboard_validation_rejects_duplicate_and_reserved_rollover_usages() {
    TEST_ASSERT_FALSE(isValidHidReport(HidReport{HidKeyboardReport{0, {0x04, 0x04}}}));
    TEST_ASSERT_FALSE(isValidHidReport(HidReport{HidKeyboardReport{0, {0x01}}}));
    TEST_ASSERT_FALSE(isValidHidReport(HidReport{HidKeyboardReport{0, {0x02}}}));
    TEST_ASSERT_FALSE(isValidHidReport(HidReport{HidKeyboardReport{0, {0x03}}}));
    TEST_ASSERT_TRUE(isValidHidReport(HidReport{HidKeyboardReport{0, {0x04, 0x05}}}));
}

} // namespace

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_keyboard_and_consumer_reports_preserve_hardware_neutral_values);
    RUN_TEST(test_neutral_reports_are_zero_filled_and_valid);
    RUN_TEST(test_keyboard_validation_rejects_duplicate_and_reserved_rollover_usages);
    return UNITY_END();
}
