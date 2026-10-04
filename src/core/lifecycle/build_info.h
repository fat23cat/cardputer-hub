#pragma once

namespace cardputer_hub::core {

struct BuildInfo {
    const char* const name;
    const char* const version;
    const char* const commit;
    const char* const buildType;
    // YYYY-MM-DD of this build, refreshed on every firmware build.
    const char* const buildDate;
    // "YYYY-MM-DD <short commit>", with "+" for a dirty tree; at most 24 ASCII
    // bytes. The Companion shows it, and Cardputer shows the Companion's.
    const char* const buildId;
};

const BuildInfo& firmwareBuildInfo() noexcept;

} // namespace cardputer_hub::core
