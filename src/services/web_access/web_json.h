#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>

#include "connectivity/http/http_server.h"

namespace cardputer_hub::services {

// Appends `value` as a JSON string. Control characters are escaped; other
// UTF-8 passes through unchanged.
void appendWebJsonString(std::string& output, std::string_view value);

// A JSON object whose values are all strings, such as the sign-in body. Keys
// must be unique; at most `maxMembers` members. Strict: invalid UTF-8, a
// trailing comma or any other value type is refused.
[[nodiscard]] std::optional<std::map<std::string, std::string>>
parseWebJsonStringObject(std::string_view json, std::size_t maxMembers = 8);

// API responses are never cached; WebAccessService adds the security headers.
[[nodiscard]] connectivity::HttpResponse webJsonResponse(std::uint16_t status, std::string body);
// {"error":"<code>"}
[[nodiscard]] connectivity::HttpResponse webErrorResponse(std::uint16_t status,
                                                          std::string_view code);
[[nodiscard]] connectivity::HttpResponse webEmptyResponse(std::uint16_t status);

} // namespace cardputer_hub::services
