#include <unity.h>

#include <cerrno>

#include "hardware/storage/microsd/microsd_io_error.h"

namespace {

using cardputer_hub::hardware::microsd_detail::firstOperationError;

void test_close_error_is_used_when_read_or_write_steps_succeed() {
    TEST_ASSERT_EQUAL_INT(EIO, firstOperationError(0, EIO));
    TEST_ASSERT_EQUAL_INT(EIO, firstOperationError(0, 0, EIO));
}

void test_first_operation_error_is_preserved() {
    TEST_ASSERT_EQUAL_INT(ENOSPC, firstOperationError(ENOSPC, EIO));
    TEST_ASSERT_EQUAL_INT(EACCES, firstOperationError(0, EACCES, EIO));
    TEST_ASSERT_EQUAL_INT(0, firstOperationError(0, 0, 0));
}

} // namespace

void setUp() {}

void tearDown() {}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_close_error_is_used_when_read_or_write_steps_succeed);
    RUN_TEST(test_first_operation_error_is_preserved);
    return UNITY_END();
}
