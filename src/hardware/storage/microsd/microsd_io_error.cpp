#include "hardware/storage/microsd/microsd_io_error.h"

namespace cardputer_hub::hardware::microsd_detail {

int firstOperationError(int firstError, int secondError, int thirdError) {
    if (firstError != 0) {
        return firstError;
    }
    return secondError != 0 ? secondError : thirdError;
}

} // namespace cardputer_hub::hardware::microsd_detail
