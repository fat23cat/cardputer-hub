#include "hardware/esp32/http/esp32_http_client.h"

#include <cstring>

#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"

namespace cardputer_hub::hardware {
namespace {
using connectivity::HttpRequestState;

constexpr char logTag[] = "http";
// esp_http_client applies its timeout to each socket operation, not to the
// request, so a server that trickles bytes is cut off by the deadline instead.
constexpr int operationTimeoutMs = 4000;
constexpr std::int64_t requestDeadlineUs = 10'000'000;
constexpr int maxRedirects = 3;
// TLS handshakes run deep in mbedTLS.
constexpr std::uint32_t taskStackBytes = 8192;
// app_main runs at priority 1 on CPU0; a handshake's crypto must not preempt
// keyboard polling and rendering there.
constexpr UBaseType_t taskPriority = 1;
constexpr BaseType_t taskCore = 1;

bool isRedirect(int status) {
    return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

// Follows the Location header only to another https URL.
bool redirectToHttps(esp_http_client_handle_t client) {
    if (esp_http_client_flush_response(client, nullptr) != ESP_OK)
        return false;
    (void)esp_http_client_close(client);
    if (esp_http_client_set_redirection(client) != ESP_OK)
        return false;
    char url[201]{};
    return esp_http_client_get_url(client, url, sizeof(url)) == ESP_OK &&
           std::strncmp(url, "https://", 8) == 0;
}
} // namespace

connectivity::HttpStartResult Esp32HttpClient::start(std::string_view url) {
    if (url.size() >= url_.size() || url.substr(0, 8) != "https://")
        return connectivity::HttpStartResult::Failed;
    if (busy_.load())
        return connectivity::HttpStartResult::Busy;
    if (task_ == nullptr &&
        xTaskCreatePinnedToCore(&Esp32HttpClient::taskEntry, "http_client", taskStackBytes, this,
                                taskPriority, &task_, taskCore) != pdPASS) {
        task_ = nullptr;
        return connectivity::HttpStartResult::Failed;
    }
    std::memcpy(url_.data(), url.data(), url.size());
    url_[url.size()] = '\0';
    abandoned_.store(false);
    busy_.store(true);
    state_.store(HttpRequestState::Running);
    xTaskNotifyGive(task_);
    return connectivity::HttpStartResult::Started;
}

HttpRequestState Esp32HttpClient::state() const { return state_.load(); }

void Esp32HttpClient::reset() {
    if (state_.load() == HttpRequestState::Running)
        abandoned_.store(true);
    state_.store(HttpRequestState::Idle);
}

void Esp32HttpClient::taskEntry(void* context) { static_cast<Esp32HttpClient*>(context)->run(); }

void Esp32HttpClient::run() {
    for (;;) {
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        std::size_t size = 0;
        int status = 0;
        const bool ok = perform(size, status);
        ESP_LOGI(logTag, "GET %s: %s %d, %u bytes, free heap %u", url_.data(),
                 ok ? "HTTP" : "failed", status, static_cast<unsigned>(size),
                 static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)));
        if (!abandoned_.load()) {
            std::memcpy(body_.data(), scratch_.data(), size);
            bodySize_ = size;
            status_ = status;
            state_.store(ok ? HttpRequestState::Done : HttpRequestState::Failed);
        }
        busy_.store(false);
    }
}

bool Esp32HttpClient::perform(std::size_t& size, int& status) {
    esp_http_client_config_t config{};
    config.url = url_.data();
    config.method = HTTP_METHOD_GET;
    config.timeout_ms = operationTimeoutMs;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.buffer_size = 1024;
    config.buffer_size_tx = 512;
    config.user_agent = "cardputer-hub";
    config.keep_alive_enable = false;
    // Redirects are followed below, only to https.
    config.disable_auto_redirect = true;
    auto* client = esp_http_client_init(&config);
    if (client == nullptr)
        return false;
    const auto deadline = esp_timer_get_time() + requestDeadlineUs;
    const auto expired = [deadline] { return esp_timer_get_time() >= deadline; };
    bool ok = false;
    (void)esp_http_client_set_header(client, "Accept", "application/json");
    bool opened = false;
    for (int redirects = 0;; ++redirects) {
        opened = esp_http_client_open(client, 0) == ESP_OK && !expired() &&
                 esp_http_client_fetch_headers(client) >= 0 && !expired();
        if (!opened)
            break;
        status = esp_http_client_get_status_code(client);
        if (!isRedirect(status))
            break;
        if (redirects == maxRedirects || !redirectToHttps(client) || expired()) {
            opened = false;
            break;
        }
    }
    if (opened) {
        ok = true;
        while (ok) {
            if (expired()) {
                ok = false;
                break;
            }
            if (size == scratch_.size()) {
                // Still more to come: the body does not fit.
                char probe = 0;
                ok = esp_http_client_read(client, &probe, 1) == 0;
                break;
            }
            const int read = esp_http_client_read(client, scratch_.data() + size,
                                                  static_cast<int>(scratch_.size() - size));
            if (read < 0)
                ok = false;
            else if (read == 0)
                break;
            else
                size += static_cast<std::size_t>(read);
        }
    }
    (void)esp_http_client_close(client);
    (void)esp_http_client_cleanup(client);
    if (!ok)
        size = 0;
    return ok;
}

} // namespace cardputer_hub::hardware
