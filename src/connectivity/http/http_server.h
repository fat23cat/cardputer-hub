#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cardputer_hub::connectivity {

// Larger bodies are refused by the server before they are read.
inline constexpr std::size_t httpMaxRequestBodyBytes = 5 * 1024;

enum class HttpMethod : std::uint8_t { Get, Post, Put, Delete, Other };

// One inbound request, copied out of the server. Only the headers the web UI
// needs are kept.
struct HttpRequest {
    std::uint32_t id = 0;
    HttpMethod method = HttpMethod::Get;
    // Without the query string.
    std::string path;
    // After '?', not decoded.
    std::string query;
    // The Cookie header.
    std::string cookie;
    // The X-Hub-Request header.
    std::string requestMarker;
    std::string body;
};

struct HttpHeader {
    std::string name;
    std::string value;
};

struct HttpResponse {
    std::uint16_t status = 200;
    std::string contentType;
    std::vector<HttpHeader> headers;
    std::string body;
    // Constant data that outlives the server (a page compiled into the
    // firmware); sent instead of `body` when not empty.
    std::string_view staticBody;
};

// The inbound HTTP boundary of the local web UI. The server runs on its own
// task and holds at most one request at a time; the main loop takes it with
// poll() and answers it with respond(). The server knows nothing about routes
// or sessions. An unanswered request is ended by the server after its own
// deadline, and stop() ends a request that is still waiting.
class IHttpServer {
  public:
    virtual ~IHttpServer() = default;

    [[nodiscard]] virtual bool start() = 0;
    virtual void stop() = 0;
    [[nodiscard]] virtual bool running() const = 0;
    [[nodiscard]] virtual std::optional<HttpRequest> poll() = 0;
    // An answer to a request that is no longer waiting is dropped.
    virtual void respond(std::uint32_t id, HttpResponse response) = 0;
};

// The undecoded value of `name` in a query string such as "a=1&b=2".
[[nodiscard]] inline std::optional<std::string_view> httpQueryParameter(std::string_view query,
                                                                        std::string_view name) {
    while (!query.empty()) {
        const auto end = query.find('&');
        const auto pair = query.substr(0, end);
        const auto equals = pair.find('=');
        if (pair.substr(0, equals) == name)
            return equals == std::string_view::npos ? std::string_view() : pair.substr(equals + 1);
        if (end == std::string_view::npos)
            break;
        query.remove_prefix(end + 1);
    }
    return std::nullopt;
}

// The value of cookie `name` in a Cookie header such as "a=1; b=2".
[[nodiscard]] inline std::optional<std::string_view> httpCookieValue(std::string_view header,
                                                                     std::string_view name) {
    while (!header.empty()) {
        while (!header.empty() && (header.front() == ' ' || header.front() == ';'))
            header.remove_prefix(1);
        const auto end = header.find(';');
        const auto pair = header.substr(0, end);
        const auto equals = pair.find('=');
        if (equals != std::string_view::npos && pair.substr(0, equals) == name)
            return pair.substr(equals + 1);
        if (end == std::string_view::npos)
            break;
        header.remove_prefix(end + 1);
    }
    return std::nullopt;
}

} // namespace cardputer_hub::connectivity
