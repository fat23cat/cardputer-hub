#include "services/battery/battery_service.h"
#include <unity.h>
using namespace cardputer_hub;
using namespace std::chrono_literals;
namespace {
class Battery final : public core::IBatteryAdapter {
  public:
    int value = 78, reads = 0;
    int readPercent() override {
        ++reads;
        return value;
    }
};
void test_battery_samples_immediately_then_at_most_once_per_five_seconds() {
    Battery adapter;
    services::BatteryService battery(adapter);
    TEST_ASSERT_FALSE(battery.percent());
    battery.update(0ms);
    TEST_ASSERT_EQUAL(78, battery.percent().value_or(255));
    battery.update(4999ms);
    TEST_ASSERT_EQUAL(1, adapter.reads);
    adapter.value = 77;
    battery.update(1ms);
    TEST_ASSERT_EQUAL(2, adapter.reads);
    TEST_ASSERT_EQUAL(77, battery.percent().value_or(255));
    battery.update(60000ms);
    TEST_ASSERT_EQUAL(3, adapter.reads);
    battery.update(0ms);
    TEST_ASSERT_EQUAL(3, adapter.reads);
}
void test_failed_battery_read_does_not_retain_a_stale_value() {
    Battery adapter;
    services::BatteryService battery(adapter);
    battery.update(0ms);
    adapter.value = -1;
    battery.update(5000ms);
    TEST_ASSERT_FALSE(battery.percent());
    adapter.value = 101;
    battery.update(5000ms);
    TEST_ASSERT_FALSE(battery.percent());
    adapter.value = 0;
    battery.update(5000ms);
    TEST_ASSERT_EQUAL(0, battery.percent().value_or(255));
}
} // namespace
void setUp() {}
void tearDown() {}
int main() {
    UNITY_BEGIN();
    RUN_TEST(test_battery_samples_immediately_then_at_most_once_per_five_seconds);
    RUN_TEST(test_failed_battery_read_does_not_retain_a_stale_value);
    return UNITY_END();
}
