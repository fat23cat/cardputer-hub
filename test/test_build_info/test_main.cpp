#include <unity.h>

#include "core/lifecycle/build_info.h"

void setUp() {}

void tearDown() {}

void test_build_information_is_present() {
    TEST_ASSERT_EQUAL_STRING("Cardputer Hub", cardputer_hub::core::firmwareName());
    TEST_ASSERT_NOT_EMPTY(cardputer_hub::core::firmwareVersion());
    TEST_ASSERT_NOT_EMPTY(cardputer_hub::core::firmwareCommit());
    TEST_ASSERT_NOT_EMPTY(cardputer_hub::core::firmwareBuildType());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_build_information_is_present);
    return UNITY_END();
}
