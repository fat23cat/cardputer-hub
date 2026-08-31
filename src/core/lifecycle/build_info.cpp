#include "core/lifecycle/build_info.h"

#ifndef CARDPUTER_HUB_VERSION
#define CARDPUTER_HUB_VERSION "0.1.0-dev"
#endif

#ifndef CARDPUTER_HUB_COMMIT
#define CARDPUTER_HUB_COMMIT "unknown"
#endif

#ifndef CARDPUTER_HUB_BUILD_TYPE
#define CARDPUTER_HUB_BUILD_TYPE "unknown"
#endif

namespace cardputer_hub::core {

const char* firmwareName() noexcept { return "Cardputer Hub"; }

const char* firmwareVersion() noexcept { return CARDPUTER_HUB_VERSION; }

const char* firmwareCommit() noexcept { return CARDPUTER_HUB_COMMIT; }

const char* firmwareBuildType() noexcept { return CARDPUTER_HUB_BUILD_TYPE; }

} // namespace cardputer_hub::core
