#pragma once

#include "connectivity/companion/companion_protocol.h"

#include <optional>
#include <string_view>

namespace cardputer_hub::services {

// Reads an Atlassian Statuspage `/api/v2/status.json` body:
// `status.indicator` (none, minor, major, critical, maintenance) and
// `status.description`, cut to 48 bytes at a character boundary. Empty when
// the body has no status object with a known indicator.
std::optional<connectivity::CompanionServiceStatus> parseStatusPage(std::string_view body);

} // namespace cardputer_hub::services
