#pragma once

namespace cardputer_hub::hardware::microsd_detail {

[[nodiscard]] int firstOperationError(int firstError, int secondError, int thirdError = 0);

} // namespace cardputer_hub::hardware::microsd_detail
