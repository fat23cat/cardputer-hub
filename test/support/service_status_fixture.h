#pragma once

// Fakes for ServiceStatusService: an HTTP client, Wi-Fi, a Companion link and
// audio output. Include after <unity.h>.

#include "companion_session.h"

#include "connectivity/companion/companion_protocol.h"
#include "connectivity/http/http_client.h"
#include "connectivity/wifi/wifi_service.h"
#include "core/audio/audio_adapter.h"
#include "core/capabilities/capability_registry.h"
#include "core/storage/storage.h"
#include "services/audio/audio_service.h"
#include "services/configuration/configuration_service.h"
#include "services/service_status/service_status_service.h"

#include <deque>
#include <string>
#include <vector>

namespace cardputer_hub::test_support {

class FakeHttpClient final : public connectivity::IHttpClient {
  public:
    connectivity::HttpStartResult start(std::string_view url) override {
        if (startFails)
            return connectivity::HttpStartResult::Failed;
        if (holdsAbandoned || state_ == connectivity::HttpRequestState::Running)
            return connectivity::HttpStartResult::Busy;
        urls.emplace_back(url);
        state_ = connectivity::HttpRequestState::Running;
        return connectivity::HttpStartResult::Started;
    }
    connectivity::HttpRequestState state() const override { return state_; }
    int status() const override { return code; }
    std::string_view body() const override { return text; }
    void reset() override {
        if (state_ == connectivity::HttpRequestState::Running)
            ++abandoned;
        state_ = connectivity::HttpRequestState::Idle;
    }
    void answer(int status, std::string body) {
        TEST_ASSERT_TRUE(state_ == connectivity::HttpRequestState::Running);
        code = status;
        text = std::move(body);
        state_ = connectivity::HttpRequestState::Done;
    }
    void failNetwork() {
        TEST_ASSERT_TRUE(state_ == connectivity::HttpRequestState::Running);
        state_ = connectivity::HttpRequestState::Failed;
    }
    bool running() const { return state_ == connectivity::HttpRequestState::Running; }

    std::vector<std::string> urls;
    int abandoned = 0;
    bool holdsAbandoned = false;
    bool startFails = false;

  private:
    connectivity::HttpRequestState state_ = connectivity::HttpRequestState::Idle;
    int code = 0;
    std::string text;
};

class FakeWifiAdapter final : public connectivity::IWifiAdapter {
  public:
    connectivity::WifiAdapterResult initializeStation() override {
        return connectivity::WifiAdapterResult::Success;
    }
    connectivity::WifiAdapterResult connect(const connectivity::WifiNetworkConfig&) override {
        return connectivity::WifiAdapterResult::Success;
    }
    connectivity::WifiAdapterResult disconnect() override {
        return connectivity::WifiAdapterResult::Success;
    }
    connectivity::WifiAdapterState state() const override { return adapterState; }
    std::optional<std::int32_t> signalStrengthDbm() const override { return std::nullopt; }
    connectivity::WifiAdapterState adapterState = connectivity::WifiAdapterState::Connecting;
};

class FakeCompanionTransport final : public connectivity::ICompanionTransport {
  public:
    connectivity::CompanionTransportState state() const noexcept override {
        return connectivity::CompanionTransportState::Ready;
    }
    connectivity::CompanionSendResult send(const connectivity::CompanionPayload& payload) override {
        sent.push_back(payload);
        return connectivity::CompanionSendResult::Sent;
    }
    std::optional<connectivity::CompanionPayload> receive() override {
        if (incoming.empty())
            return std::nullopt;
        auto value = incoming.front();
        incoming.pop_front();
        return value;
    }
    std::vector<connectivity::CompanionEnvelope> statusRequests() const {
        std::vector<connectivity::CompanionEnvelope> result;
        for (const auto& payload : sent) {
            const auto message = companionDecode(payload);
            if (message.kind == connectivity::CompanionKind::Request &&
                message.operation == connectivity::CompanionOperation::ServiceStatus)
                result.push_back(message);
        }
        return result;
    }
    std::deque<connectivity::CompanionPayload> incoming;
    std::vector<connectivity::CompanionPayload> sent;
};

class MemoryStorage final : public core::IStorageAdapter {
  public:
    core::StorageReadResult read(const core::StorageAddress&) override {
        return {bytes.empty() ? core::StorageReadStatus::NotFound : core::StorageReadStatus::Found,
                bytes};
    }
    core::StorageWriteStatus write(const core::StorageAddress&,
                                   const core::StorageBytes& value) override {
        bytes = value;
        return core::StorageWriteStatus::Stored;
    }
    core::StorageRemoveStatus remove(const core::StorageAddress&) override {
        return core::StorageRemoveStatus::NotFound;
    }
    core::StorageBytes bytes;
};

class RecordingAudioAdapter final : public core::IAudioAdapter {
  public:
    bool begin(std::uint8_t) override { return true; }
    void setVolume(std::uint8_t) override {}
    bool isPlaying() const override { return playing; }
    bool play(const core::AudioClip& clip) override {
        clips.push_back(clip);
        return true;
    }
    std::vector<core::AudioClip> clips;
    bool playing = false;
};

inline std::string statusBody(const char* indicator, const char* description = "") {
    return std::string(R"({"page":{"name":"Test"},"status":{"indicator":")") + indicator +
           R"(","description":")" + description + R"("}})";
}

struct ServiceStatusFixture {
    FakeHttpClient http;
    FakeWifiAdapter wifiAdapter;
    connectivity::WiFiService wifi{wifiAdapter};
    FakeCompanionTransport transport;
    core::CapabilityRegistry capabilities;
    services::CompanionService companion{transport, capabilities};
    MemoryStorage memory;
    core::Storage storage{memory};
    services::ConfigurationService configuration{storage};
    RecordingAudioAdapter audioAdapter;
    services::AudioService audio{configuration, audioAdapter};
    services::ServiceStatusService service{http, wifi, companion, capabilities, &audio};

    ServiceStatusFixture() {
        TEST_ASSERT_TRUE(configuration.load() == services::ConfigurationResult::Success);
        TEST_ASSERT_TRUE(audio.start() == services::AudioResult::Success);
    }

    void connectWifi() {
        TEST_ASSERT_TRUE(wifi.connect({"home", "password1"}) ==
                         connectivity::WifiConnectResult::Started);
        wifiAdapter.adapterState = connectivity::WifiAdapterState::Connected;
        wifi.update(std::chrono::milliseconds::zero());
        TEST_ASSERT_TRUE(wifi.state() == connectivity::WifiState::Connected);
    }
    void connectCompanion() { completeCompanionHandshake(transport, companion); }

    void tick(std::chrono::milliseconds elapsed = std::chrono::milliseconds::zero()) {
        companion.update(elapsed);
        service.update(elapsed);
    }

    // Answers the newest SERVICE_STATUS request.
    void answerCompanion(connectivity::ServiceStatusLevel level, const char* description = "") {
        const auto requests = transport.statusRequests();
        TEST_ASSERT_FALSE(requests.empty());
        auto response = connectivity::makeResponse(companion.session(), requests.back().requestId,
                                                   connectivity::CompanionOperation::ServiceStatus,
                                                   connectivity::CompanionStatus::Ok);
        TEST_ASSERT_TRUE(connectivity::setServiceStatus(response, level, description));
        transport.incoming.push_back(companionWire(response));
        tick();
    }
    void failCompanion() {
        const auto requests = transport.statusRequests();
        TEST_ASSERT_FALSE(requests.empty());
        transport.incoming.push_back(companionWire(
            connectivity::makeResponse(companion.session(), requests.back().requestId,
                                       connectivity::CompanionOperation::ServiceStatus,
                                       connectivity::CompanionStatus::NotAvailable)));
        tick();
    }
    // Answers every page of a Wi-Fi round with the same indicator.
    void answerRoundOverWifi(const char* indicator) {
        for (std::size_t page = 0; page < services::statusSources.size(); ++page) {
            TEST_ASSERT_TRUE(http.running());
            http.answer(200, statusBody(indicator));
            tick();
        }
    }
    const services::ServiceStatusEntry& entry(std::size_t index) const {
        return service.snapshot().entries[index];
    }
};

} // namespace cardputer_hub::test_support
