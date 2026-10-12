#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "connectivity/http/http_server.h"
#include "connectivity/wifi/wifi_access_point.h"
#include "core/platform/random_source.h"
#include "core/storage/storage.h"

namespace cardputer_hub::test_support {

class FakeAccessPoint final : public connectivity::IWifiAccessPointAdapter {
  public:
    connectivity::WifiAdapterResult
    startAccessPoint(const connectivity::WifiAccessPointConfig& config) override {
        ++starts;
        if (failStart)
            return connectivity::WifiAdapterResult::Error;
        running = true;
        last = config;
        return connectivity::WifiAdapterResult::Success;
    }
    connectivity::WifiAdapterResult stopAccessPoint() override {
        ++stops;
        running = false;
        return connectivity::WifiAdapterResult::Success;
    }
    std::size_t accessPointClientCount() const override { return clients; }

    bool failStart = false;
    bool running = false;
    int starts = 0;
    int stops = 0;
    std::size_t clients = 0;
    connectivity::WifiAccessPointConfig last;
};

class FakeHttpServer final : public connectivity::IHttpServer {
  public:
    bool start() override {
        ++starts;
        if (failStart)
            return false;
        isRunning = true;
        return true;
    }
    void stop() override {
        ++stops;
        isRunning = false;
        // A request still waiting in the server is ended by the server.
        if (waiting) {
            ended.push_back(*waiting);
            waiting.reset();
        }
        incoming.clear();
    }
    bool running() const override { return isRunning; }
    std::optional<connectivity::HttpRequest> poll() override {
        if (!isRunning || incoming.empty())
            return std::nullopt;
        auto request = std::move(incoming.front());
        incoming.pop_front();
        waiting = request.id;
        return request;
    }
    void respond(std::uint32_t id, connectivity::HttpResponse response) override {
        if (waiting && *waiting == id)
            waiting.reset();
        responses.emplace_back(id, std::move(response));
    }

    // Queues a request and returns its id.
    std::uint32_t push(connectivity::HttpRequest request) {
        request.id = nextId++;
        incoming.push_back(std::move(request));
        return request.id;
    }
    [[nodiscard]] const connectivity::HttpResponse* responseFor(std::uint32_t id) const {
        for (const auto& [responseId, response] : responses) {
            if (responseId == id)
                return &response;
        }
        return nullptr;
    }

    bool failStart = false;
    bool isRunning = false;
    int starts = 0;
    int stops = 0;
    std::uint32_t nextId = 1;
    std::optional<std::uint32_t> waiting;
    std::vector<std::uint32_t> ended;
    std::deque<connectivity::HttpRequest> incoming;
    std::vector<std::pair<std::uint32_t, connectivity::HttpResponse>> responses;
};

class MapStorage final : public core::IStorageAdapter {
  public:
    core::StorageReadResult read(const core::StorageAddress& address) override {
        const auto found = values.find(address.scope + "/" + address.key);
        if (found == values.end())
            return {core::StorageReadStatus::NotFound, {}};
        return {core::StorageReadStatus::Found, found->second};
    }
    core::StorageWriteStatus write(const core::StorageAddress& address,
                                   const core::StorageBytes& data) override {
        ++writes;
        if (failWrites)
            return core::StorageWriteStatus::BackendError;
        values[address.scope + "/" + address.key] = data;
        return core::StorageWriteStatus::Stored;
    }
    core::StorageRemoveStatus remove(const core::StorageAddress& address) override {
        return values.erase(address.scope + "/" + address.key) != 0
                   ? core::StorageRemoveStatus::Removed
                   : core::StorageRemoveStatus::NotFound;
    }

    std::map<std::string, core::StorageBytes> values;
    bool failWrites = false;
    int writes = 0;
};

// Bytes 0, 1, 2, ... continuing across calls, so every token differs.
class CountingRandom final : public core::IRandomSource {
  public:
    void fill(std::uint8_t* destination, std::size_t size) override {
        for (std::size_t index = 0; index < size; ++index)
            destination[index] = next++;
    }
    std::uint8_t next = 0;
};

[[nodiscard]] inline std::string responseText(const connectivity::HttpResponse& response) {
    return response.staticBody.empty() ? response.body : std::string(response.staticBody);
}

[[nodiscard]] inline std::optional<std::string>
responseHeader(const connectivity::HttpResponse& response, const std::string& name) {
    for (const auto& header : response.headers) {
        if (header.name == name)
            return header.value;
    }
    return std::nullopt;
}

} // namespace cardputer_hub::test_support
