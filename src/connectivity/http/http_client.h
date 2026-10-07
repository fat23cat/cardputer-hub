#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace cardputer_hub::connectivity {

enum class HttpRequestState : std::uint8_t {
    Idle,
    Running,
    // A response arrived: status() and body() are valid.
    Done,
    // No usable response: DNS, TLS, timeout, or a body over maxBodySize.
    Failed,
};

enum class HttpStartResult : std::uint8_t {
    Started,
    // An earlier request, possibly abandoned, still holds the client.
    Busy,
    // The request cannot run: not https, or the client could not start.
    Failed,
};

// One HTTPS GET at a time, performed away from the main loop. The caller
// polls state(); nothing it calls blocks on the network.
class IHttpClient {
  public:
    static constexpr std::size_t maxBodySize = 2048;

    virtual ~IHttpClient() = default;
    virtual HttpStartResult start(std::string_view url) = 0;
    virtual HttpRequestState state() const = 0;
    virtual int status() const = 0;
    virtual std::string_view body() const = 0;
    // Returns to Idle. A running request is abandoned and its result dropped.
    virtual void reset() = 0;
};

} // namespace cardputer_hub::connectivity
