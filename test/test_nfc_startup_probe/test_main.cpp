#include <unity.h>

#include <cstdint>
#include <vector>

#include "hardware/nfc/nfc_startup_probe.h"

using cardputer_hub::hardware::probeNfcAtStartup;

namespace {

void test_transient_nack_is_retried_before_puzzle_claims_grove() {
    std::vector<std::uint32_t> delays;
    int probes = 0;
    const bool found =
        probeNfcAtStartup([&] { return ++probes == 3; },
                          [&](std::uint32_t milliseconds) { delays.push_back(milliseconds); });

    TEST_ASSERT_TRUE(found);
    TEST_ASSERT_EQUAL_INT(3, probes);
    const std::vector<std::uint32_t> expected{50, 20, 20};
    TEST_ASSERT_TRUE(delays == expected);
}

void test_absent_reader_has_a_bounded_startup_probe() {
    std::vector<std::uint32_t> delays;
    int probes = 0;
    const bool found = probeNfcAtStartup(
        [&] {
            ++probes;
            return false;
        },
        [&](std::uint32_t milliseconds) { delays.push_back(milliseconds); });

    TEST_ASSERT_FALSE(found);
    TEST_ASSERT_EQUAL_INT(5, probes);
    const std::vector<std::uint32_t> expected{50, 20, 20, 20, 20};
    TEST_ASSERT_TRUE(delays == expected);
}

} // namespace

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_transient_nack_is_retried_before_puzzle_claims_grove);
    RUN_TEST(test_absent_reader_has_a_bounded_startup_probe);
    return UNITY_END();
}
