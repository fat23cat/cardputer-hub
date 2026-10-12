#pragma once

#include "connectivity/http/http_server.h"

#include <cstdint>
#include <optional>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace cardputer_hub::hardware {

// esp_http_server on port 80 for the WEB ACCESS access point. The server task
// runs on CPU1 and handles one request at a time: it copies the request into
// the single slot, waits up to three seconds for the main loop to answer it
// through respond(), and otherwise sends 503. It answers only connections
// that arrived on the access-point interface; anything else is closed.
// Request bodies over connectivity::httpMaxRequestBodyBytes get 413 unread,
// and a body that takes longer than three seconds closes the connection.
// stop() returns within about a second even while a request, its headers or
// its body, is still arriving: every receive checks for a stop first.
class Esp32HttpServer final : public connectivity::IHttpServer {
  public:
    static constexpr std::uint32_t answerTimeoutMs = 3000;

    bool start() override;
    void stop() override;
    bool running() const override { return server_ != nullptr; }
    std::optional<connectivity::HttpRequest> poll() override;
    void respond(std::uint32_t id, connectivity::HttpResponse response) override;

  private:
    static int handleEntry(void* request);
    int handle(void* request);

    void* server_ = nullptr;
    SemaphoreHandle_t lock_ = nullptr;
    SemaphoreHandle_t answered_ = nullptr;
    // Guarded by lock_.
    std::optional<connectivity::HttpRequest> pending_;
    bool polled_ = false;
    std::optional<connectivity::HttpResponse> response_;
    std::uint32_t nextId_ = 1;
};

} // namespace cardputer_hub::hardware
