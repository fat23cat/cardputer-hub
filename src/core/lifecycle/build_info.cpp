#include "core/lifecycle/build_info.h"

// The firmware build writes this header on every build (main/CMakeLists.txt);
// host tests run without it and use the fallbacks below.
#if __has_include("cardputer_hub_build_identity.h")
#include "cardputer_hub_build_identity.h"
#endif

#ifndef CARDPUTER_HUB_VERSION
#define CARDPUTER_HUB_VERSION "0.1.0-dev"
#endif

#ifndef CARDPUTER_HUB_COMMIT
#define CARDPUTER_HUB_COMMIT "unknown"
#endif

#ifndef CARDPUTER_HUB_BUILD_TYPE
#define CARDPUTER_HUB_BUILD_TYPE "unknown"
#endif

#ifndef CARDPUTER_HUB_BUILD_DATE
#define CARDPUTER_HUB_BUILD_DATE "unknown"
#endif

#ifndef CARDPUTER_HUB_BUILD_ID
#define CARDPUTER_HUB_BUILD_ID "dev"
#endif

namespace cardputer_hub::core {

const BuildInfo& firmwareBuildInfo() noexcept {
    static const BuildInfo buildInfo{
        "Cardputer Hub",          CARDPUTER_HUB_VERSION,    CARDPUTER_HUB_COMMIT,
        CARDPUTER_HUB_BUILD_TYPE, CARDPUTER_HUB_BUILD_DATE, CARDPUTER_HUB_BUILD_ID,
    };
    return buildInfo;
}

} // namespace cardputer_hub::core
