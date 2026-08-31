#pragma once

namespace cardputer_hub::core {

struct BuildInfo {
    const char* const name;
    const char* const version;
    const char* const commit;
    const char* const buildType;
};

const BuildInfo& firmwareBuildInfo() noexcept;

} // namespace cardputer_hub::core
