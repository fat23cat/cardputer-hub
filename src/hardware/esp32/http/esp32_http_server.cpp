#include "hardware/esp32/http/esp32_http_server.h"

#include <atomic>
#include <cerrno>
#include <cstring>
#include <string>
#include <utility>

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "lwip/sockets.h"

namespace cardputer_hub::hardware {
namespace {

using connectivity::HttpMethod;

constexpr char accessPointInterfaceKey[] = "WIFI_AP_DEF";
constexpr std::size_t maxCookieBytes = 512;
constexpr std::size_t maxMarkerBytes = 8;
// A body arrives within this time or the connection is closed, so a slow or
// stalled sender cannot hold the server task, or stop(), for long.
constexpr std::int64_t bodyDeadlineUs = 3'000'000;

// Set while stop() runs. Every receive of every session, headers included,
// checks it first, so a client feeding a request slowly cannot keep the server
// task, and with it stop(), busy. There is one server instance.
std::atomic<bool> serverStopping{false};

int stoppableReceive(httpd_handle_t, int socket, char* buffer, size_t size, int flags) {
    if (serverStopping.load())
        return HTTPD_SOCK_ERR_FAIL;
    const auto received = recv(socket, buffer, size, flags);
    if (received < 0)
        return errno == EAGAIN || errno == EWOULDBLOCK ? HTTPD_SOCK_ERR_TIMEOUT
                                                       : HTTPD_SOCK_ERR_FAIL;
    return static_cast<int>(received);
}

esp_err_t openSession(httpd_handle_t server, int socket) {
    return httpd_sess_set_recv_override(server, socket, &stoppableReceive);
}

// The server task keeps a status line's pointer until the response is sent.
const char* statusLine(std::uint16_t status) {
    switch (status) {
    case 200:
        return "200 OK";
    case 204:
        return "204 No Content";
    case 400:
        return "400 Bad Request";
    case 401:
        return "401 Unauthorized";
    case 403:
        return "403 Forbidden";
    case 404:
        return "404 Not Found";
    case 405:
        return "405 Method Not Allowed";
    case 409:
        return "409 Conflict";
    case 413:
        return "413 Payload Too Large";
    case 422:
        return "422 Unprocessable Entity";
    case 429:
        return "429 Too Many Requests";
    case 503:
        return "503 Service Unavailable";
    default:
        break;
    }
    return "500 Internal Server Error";
}

HttpMethod methodOf(int method) {
    switch (method) {
    case HTTP_GET:
        return HttpMethod::Get;
    case HTTP_POST:
        return HttpMethod::Post;
    case HTTP_PUT:
        return HttpMethod::Put;
    case HTTP_DELETE:
        return HttpMethod::Delete;
    default:
        break;
    }
    return HttpMethod::Other;
}

// True when the connection arrived on the access point, not on the station's
// home network.
bool arrivedOnAccessPoint(httpd_req_t* request) {
    auto* interface = esp_netif_get_handle_from_ifkey(accessPointInterfaceKey);
    esp_netif_ip_info_t info{};
    if (interface == nullptr || esp_netif_get_ip_info(interface, &info) != ESP_OK ||
        info.ip.addr == 0)
        return false;
    sockaddr_storage local{};
    socklen_t size = sizeof(local);
    if (getsockname(httpd_req_to_sockfd(request), reinterpret_cast<sockaddr*>(&local), &size) != 0)
        return false;
    std::uint32_t address = 0;
    if (local.ss_family == AF_INET) {
        address = reinterpret_cast<const sockaddr_in*>(&local)->sin_addr.s_addr;
    } else if (local.ss_family == AF_INET6) {
        const auto& bytes = reinterpret_cast<const sockaddr_in6*>(&local)->sin6_addr.s6_addr;
        // An IPv4 client on a dual-stack socket: ::ffff:a.b.c.d.
        static constexpr std::uint8_t mapped[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff};
        if (std::memcmp(bytes, mapped, sizeof(mapped)) != 0)
            return false;
        std::memcpy(&address, bytes + 12, sizeof(address));
    } else {
        return false;
    }
    return address == info.ip.addr;
}

std::string header(httpd_req_t* request, const char* name, std::size_t limit) {
    const auto size = httpd_req_get_hdr_value_len(request, name);
    if (size == 0 || size > limit)
        return {};
    std::string value(size + 1, '\0');
    if (httpd_req_get_hdr_value_str(request, name, value.data(), value.size()) != ESP_OK)
        return {};
    value.resize(size);
    return value;
}

esp_err_t sendStatusOnly(httpd_req_t* request, std::uint16_t status) {
    (void)httpd_resp_set_status(request, statusLine(status));
    (void)httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_send(request, nullptr, 0);
}

} // namespace

bool Esp32HttpServer::start() {
    if (server_ != nullptr)
        return true;
    if (lock_ == nullptr)
        lock_ = xSemaphoreCreateMutex();
    if (answered_ == nullptr)
        answered_ = xSemaphoreCreateBinary();
    if (lock_ == nullptr || answered_ == nullptr)
        return false;
    // URIs name records; they are not logged.
    esp_log_level_set("httpd_uri", ESP_LOG_NONE);
    esp_log_level_set("httpd_txrx", ESP_LOG_ERROR);
    esp_log_level_set("httpd_parse", ESP_LOG_ERROR);

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.max_open_sockets = 4;
    config.lru_purge_enable = true;
    // One blocked receive bounds how long stop() waits for the server task.
    config.recv_wait_timeout = 1;
    config.send_wait_timeout = 2;
    config.max_uri_handlers = 4;
    config.max_resp_headers = 10;
    config.stack_size = 6144;
    // Away from the UI loop on CPU0.
    config.core_id = 1;
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.open_fn = &openSession;
    httpd_handle_t handle = nullptr;
    if (httpd_start(&handle, &config) != ESP_OK)
        return false;
    for (const auto method : {HTTP_GET, HTTP_POST, HTTP_PUT, HTTP_DELETE}) {
        httpd_uri_t uri{};
        uri.uri = "/*";
        uri.method = method;
        uri.handler = [](httpd_req_t* request) {
            return static_cast<esp_err_t>(Esp32HttpServer::handleEntry(request));
        };
        uri.user_ctx = this;
        if (httpd_register_uri_handler(handle, &uri) != ESP_OK) {
            (void)httpd_stop(handle);
            return false;
        }
    }
    server_ = handle;
    return true;
}

void Esp32HttpServer::stop() {
    if (server_ == nullptr)
        return;
    // A body still arriving is abandoned at the next receive, and a request
    // still waiting is answered, so the server task can end.
    serverStopping.store(true);
    xSemaphoreTake(lock_, portMAX_DELAY);
    if (pending_) {
        pending_.reset();
        connectivity::HttpResponse closing;
        closing.status = 503;
        response_ = std::move(closing);
        xSemaphoreGive(answered_);
    }
    xSemaphoreGive(lock_);
    (void)httpd_stop(static_cast<httpd_handle_t>(server_));
    server_ = nullptr;
    xSemaphoreTake(lock_, portMAX_DELAY);
    pending_.reset();
    response_.reset();
    polled_ = false;
    xSemaphoreGive(lock_);
    serverStopping.store(false);
}

std::optional<connectivity::HttpRequest> Esp32HttpServer::poll() {
    if (server_ == nullptr || xSemaphoreTake(lock_, 0) != pdTRUE)
        return std::nullopt;
    std::optional<connectivity::HttpRequest> request;
    if (pending_ && !polled_) {
        polled_ = true;
        request = pending_;
    }
    xSemaphoreGive(lock_);
    return request;
}

void Esp32HttpServer::respond(std::uint32_t id, connectivity::HttpResponse response) {
    xSemaphoreTake(lock_, portMAX_DELAY);
    if (pending_ && polled_ && pending_->id == id) {
        pending_.reset();
        response_ = std::move(response);
        xSemaphoreGive(answered_);
    }
    xSemaphoreGive(lock_);
}

int Esp32HttpServer::handleEntry(void* request) {
    auto* typed = static_cast<httpd_req_t*>(request);
    return static_cast<Esp32HttpServer*>(typed->user_ctx)->handle(request);
}

int Esp32HttpServer::handle(void* raw) {
    auto* request = static_cast<httpd_req_t*>(raw);
    if (!arrivedOnAccessPoint(request))
        return ESP_FAIL; // closes the connection without an answer

    if (request->content_len > connectivity::httpMaxRequestBodyBytes) {
        (void)sendStatusOnly(request, 413);
        return ESP_FAIL; // the unread body cannot stay on the connection
    }

    connectivity::HttpRequest copy;
    copy.method = methodOf(request->method);
    const std::string uri = request->uri;
    const auto question = uri.find('?');
    copy.path = uri.substr(0, question);
    if (question != std::string::npos)
        copy.query = uri.substr(question + 1);
    copy.cookie = header(request, "Cookie", maxCookieBytes);
    copy.requestMarker = header(request, "X-Hub-Request", maxMarkerBytes);
    copy.body.resize(request->content_len);
    std::size_t received = 0;
    const auto started = esp_timer_get_time();
    while (received < copy.body.size()) {
        if (serverStopping.load() || esp_timer_get_time() - started > bodyDeadlineUs)
            return ESP_FAIL;
        const int read =
            httpd_req_recv(request, copy.body.data() + received, copy.body.size() - received);
        if (read == HTTPD_SOCK_ERR_TIMEOUT)
            continue;
        if (read <= 0)
            return ESP_FAIL;
        received += static_cast<std::size_t>(read);
    }

    xSemaphoreTake(lock_, portMAX_DELAY);
    if (serverStopping.load()) {
        xSemaphoreGive(lock_);
        return ESP_FAIL;
    }
    copy.id = nextId_++;
    if (nextId_ == 0)
        nextId_ = 1;
    const auto id = copy.id;
    pending_ = std::move(copy);
    polled_ = false;
    response_.reset();
    (void)xSemaphoreTake(answered_, 0);
    xSemaphoreGive(lock_);

    std::optional<connectivity::HttpResponse> response;
    (void)xSemaphoreTake(answered_, pdMS_TO_TICKS(answerTimeoutMs));
    xSemaphoreTake(lock_, portMAX_DELAY);
    // An answer that lands just after the wait ended is still used.
    if (response_) {
        response = std::move(response_);
        response_.reset();
    } else if (pending_ && pending_->id == id) {
        // The main loop did not answer in time; a later answer is dropped.
        pending_.reset();
    }
    xSemaphoreGive(lock_);
    if (!response)
        return sendStatusOnly(request, 503);

    (void)httpd_resp_set_status(request, statusLine(response->status));
    if (!response->contentType.empty())
        (void)httpd_resp_set_type(request, response->contentType.c_str());
    for (const auto& item : response->headers)
        (void)httpd_resp_set_hdr(request, item.name.c_str(), item.value.c_str());
    const std::string_view body =
        response->staticBody.empty() ? std::string_view(response->body) : response->staticBody;
    return httpd_resp_send(request, body.data(), static_cast<ssize_t>(body.size()));
}

} // namespace cardputer_hub::hardware
