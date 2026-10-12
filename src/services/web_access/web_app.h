#pragma once

#include <string>
#include <string_view>

#include "connectivity/http/http_server.h"

namespace cardputer_hub::services {

// One app of the local web UI. WebAccessService signs the browser in and
// routes /api/<id> and /api/<id>/... here, from the main loop only. The page
// shows the app as a start tile and a header tab.
class IWebApp {
  public:
    virtual ~IWebApp() = default;

    // Lowercase letters, digits and '-'; the API path segment.
    [[nodiscard]] virtual std::string_view id() const = 0;
    // Shown on the tile and the tab, for example "INVENTORY".
    [[nodiscard]] virtual std::string_view title() const = 0;
    // A JSON object for the start tile.
    [[nodiscard]] virtual std::string summaryJson() = 0;
    // `subpath` is the rest of the path after /api/<id>: empty or starting
    // with '/'.
    [[nodiscard]] virtual connectivity::HttpResponse
    handle(const connectivity::HttpRequest& request, std::string_view subpath) = 0;
    // The service stopped: drop anything kept for the browser.
    virtual void reset() {}
};

} // namespace cardputer_hub::services
