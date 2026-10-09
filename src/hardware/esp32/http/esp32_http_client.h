#pragma once

#include "connectivity/http/http_client.h"

#include <array>
#include <atomic>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace cardputer_hub::hardware {

// esp_http_client with the ESP-IDF certificate bundle, run on its own task so a
// TLS handshake never stalls the UI loop. The task starts on the first request.
// Each socket operation may block for 4 s and none starts after 10 s, so a
// request ends about 14 s after it connects. DNS resolution inside the connect
// is bounded only by lwIP's own retries; callers must not assume a hard limit
// and route around a client that stays Busy. The task runs on CPU1 at low
// priority so the UI loop on CPU0 keeps its time. Redirects are followed
// (at most three) only to https.
class Esp32HttpClient final : public connectivity::IHttpClient {
  public:
    connectivity::HttpStartResult start(std::string_view url) override;
    connectivity::HttpRequestState state() const override;
    int status() const override { return status_; }
    std::string_view body() const override { return {body_.data(), bodySize_}; }
    void reset() override;

  private:
    static void taskEntry(void* context);
    void run();
    bool perform(std::size_t& size, int& status);

    TaskHandle_t task_ = nullptr;
    std::array<char, 201> url_{};
    // Written only by the task; copied out while no result is being read.
    std::array<char, maxBodySize> scratch_{};
    std::array<char, maxBodySize> body_{};
    std::size_t bodySize_ = 0;
    int status_ = 0;
    std::atomic<connectivity::HttpRequestState> state_{connectivity::HttpRequestState::Idle};
    std::atomic<bool> busy_{false};
    std::atomic<bool> abandoned_{false};
};

} // namespace cardputer_hub::hardware
