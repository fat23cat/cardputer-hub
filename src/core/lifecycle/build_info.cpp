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

const BuildInfo& firmwareBuildInfo() noexcept {
    static const BuildInfo buildInfo{
        "Cardputer Hub",
        CARDPUTER_HUB_VERSION,
        CARDPUTER_HUB_COMMIT,
        CARDPUTER_HUB_BUILD_TYPE,
    };
    return buildInfo;
}

} // namespace cardputer_hub::core
