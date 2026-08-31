#include <unity.h>

#include "core/lifecycle/build_info.h"

void setUp() {}

void tearDown() {}

void test_build_information_is_present() {
    const auto& buildInfo = cardputer_hub::core::firmwareBuildInfo();

    TEST_ASSERT_EQUAL_STRING("Cardputer Hub", buildInfo.name);
    TEST_ASSERT_NOT_EMPTY(buildInfo.version);
    TEST_ASSERT_NOT_EMPTY(buildInfo.commit);
    TEST_ASSERT_NOT_EMPTY(buildInfo.buildType);
}

void test_build_information_is_stable() {
    const auto& first = cardputer_hub::core::firmwareBuildInfo();
    const auto& second = cardputer_hub::core::firmwareBuildInfo();

    TEST_ASSERT_EQUAL_PTR(&first, &second);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_build_information_is_present);
    RUN_TEST(test_build_information_is_stable);
    return UNITY_END();
}
