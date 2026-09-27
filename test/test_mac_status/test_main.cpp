#include <unity.h>

#include <cstring>
#include <deque>
#include <string>
#include <vector>

#include "apps/mac_status/mac_status_app.h"
#include "apps/runtime/mini_app_runtime.h"
#include "connectivity/companion/companion_protocol.h"
#include "core/capabilities/capability_registry.h"

namespace {
using namespace cardputer_hub;
using namespace connectivity;

CompanionPayload wire(const CompanionEnvelope& message) {
    const auto encoded = encodeCompanionMessage(message);
    TEST_ASSERT_TRUE(encoded.has_value());
    CompanionPayload payload{};
    payload.size = encoded->size;
    std::memcpy(payload.bytes.data(), encoded->bytes.data(), encoded->size);
    return payload;
}

class Transport : public ICompanionTransport {
  public:
    CompanionTransportState state() const noexcept override { return transportState; }
    CompanionSendResult send(const CompanionPayload& payload) override {
        sent.push_back(payload);
        return CompanionSendResult::Sent;
    }
    std::optional<CompanionPayload> receive() override {
        if (incoming.empty())
            return std::nullopt;
        auto result = incoming.front();
        incoming.pop_front();
        return result;
    }
    CompanionEnvelope last() const {
        const auto result = decodeCompanionMessage(sent.back().bytes.data(), sent.back().size);
        TEST_ASSERT_TRUE(result.has_value());
        return *result;
    }
    CompanionTransportState transportState = CompanionTransportState::Ready;
    std::deque<CompanionPayload> incoming;
    std::vector<CompanionPayload> sent;
};

struct Fixture {
    Transport transport;
    core::CapabilityRegistry capabilities;
    services::CompanionService companion{transport, capabilities};
    services::MacStatusService status{companion};

    void ready() {
        const std::uint8_t versions[] = {2, 1};
        transport.incoming.push_back(wire(makeHello(versions, 2)));
        companion.update(std::chrono::milliseconds(0));
        const auto capRequest = transport.last();
        auto caps = makeResponse(companion.session(), capRequest.requestId,
                                 CompanionOperation::Capabilities, CompanionStatus::Ok);
        caps.version = 2;
        const CompanionCapability ids[] = {CompanionCapability::AppActive,
                                           CompanionCapability::SystemMetrics};
        TEST_ASSERT_TRUE(setCapabilityList(caps, ids, 2));
        transport.incoming.push_back(wire(caps));
        companion.update(std::chrono::milliseconds(0));
        const auto activeRequest = transport.last();
        auto active = makeResponse(companion.session(), activeRequest.requestId,
                                   CompanionOperation::AppActive, CompanionStatus::NotAvailable);
        active.version = 2;
        transport.incoming.push_back(wire(active));
        companion.update(std::chrono::milliseconds(0));
    }

    void respond(const CompanionEnvelope& request, std::uint8_t cpu) {
        auto response = makeResponse(companion.session(), request.requestId,
                                     CompanionOperation::SystemMetrics, CompanionStatus::Ok);
        response.version = 2;
        CompanionSystemMetrics metrics{};
        metrics.validity = 1U | 2U | 8U | 64U;
        metrics.cpuPercent = cpu;
        metrics.memoryUsedMiB = 8192;
        metrics.memoryTotalMiB = 16384;
        metrics.diskUsedPercent = 63;
        metrics.thermalState = 1;
        TEST_ASSERT_TRUE(setSystemMetrics(response, metrics));
        transport.incoming.push_back(wire(response));
        companion.update(std::chrono::milliseconds(0));
    }
};

class Display : public core::IDisplayAdapter {
  public:
    void clear(core::RgbColor) override { ++draws; }
    void fillRectangle(core::PixelPosition, std::int32_t, std::int32_t, core::RgbColor) override {
        ++draws;
    }
    void drawText(core::PixelPosition, const char* text, core::TextStyle) override {
        ++draws;
        labels.emplace_back(text);
    }
    int draws = 0;
    std::vector<std::string> labels;
};

void test_monitoring_cadence_freshness_and_lifecycle() {
    Fixture f;
    f.ready();
    f.status.update(std::chrono::milliseconds(1000));
    TEST_ASSERT_EQUAL_UINT(3, f.transport.sent.size());
    f.status.startMonitoring();
    TEST_ASSERT_EQUAL_UINT(4, f.transport.sent.size());
    const auto first = f.transport.last();
    f.status.update(std::chrono::milliseconds(1000));
    TEST_ASSERT_EQUAL_UINT(4, f.transport.sent.size());
    f.respond(first, 34);
    f.status.update(std::chrono::milliseconds(0));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(services::MacStatusFreshness::Fresh),
                            static_cast<unsigned>(f.status.snapshot().freshness));
    TEST_ASSERT_EQUAL_UINT8(34, f.status.snapshot().cpuPercent);
    const auto generation = f.status.snapshot().generation;
    f.status.update(std::chrono::milliseconds(1000));
    TEST_ASSERT_EQUAL_UINT(5, f.transport.sent.size());
    f.status.update(std::chrono::milliseconds(2100));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(services::MacStatusFreshness::Stale),
                            static_cast<unsigned>(f.status.snapshot().freshness));
    TEST_ASSERT_EQUAL_UINT32(generation + 1, f.status.snapshot().generation);
    f.status.stopMonitoring();
    f.status.update(std::chrono::milliseconds(5000));
    TEST_ASSERT_EQUAL_UINT(5, f.transport.sent.size());
    f.status.startMonitoring();
    TEST_ASSERT_EQUAL_UINT(5, f.transport.sent.size());
    f.companion.update(std::chrono::milliseconds(2000));
    f.status.update(std::chrono::milliseconds(1000));
    TEST_ASSERT_EQUAL_UINT(6, f.transport.sent.size());
    f.respond(f.transport.last(), 44);
    f.status.update(std::chrono::milliseconds(0));
    TEST_ASSERT_EQUAL_UINT8(44, f.status.snapshot().cpuPercent);
}

void test_dashboard_uses_full_screen_placeholders_and_dirty_blocks() {
    Fixture f;
    f.ready();
    Display display;
    apps::MacStatusApp app(f.status, display);
    app.onActivate();
    app.update({}, std::chrono::milliseconds(0));
    TEST_ASSERT_EQUAL_UINT(8, display.labels.size());
    TEST_ASSERT_EQUAL_STRING("CPU --", display.labels[0].c_str());
    TEST_ASSERT_EQUAL_STRING("RAM --", display.labels[1].c_str());
    TEST_ASSERT_EQUAL_STRING("THERM --", display.labels[7].c_str());
    const auto initialDraws = display.draws;
    app.update({}, std::chrono::milliseconds(100));
    TEST_ASSERT_EQUAL_INT(initialDraws, display.draws);
    f.respond(f.transport.last(), 34);
    f.status.update(std::chrono::milliseconds(0));
    app.update({}, std::chrono::milliseconds(0));
    TEST_ASSERT_EQUAL_STRING("CPU 34%", display.labels[8].c_str());
    TEST_ASSERT_EQUAL_STRING("RAM 8.0/16G", display.labels[9].c_str());
    const auto afterSample = display.draws;
    app.update({}, std::chrono::milliseconds(0));
    TEST_ASSERT_EQUAL_INT(afterSample, display.draws);
    const auto labelsBeforeStale = display.labels.size();
    f.status.update(std::chrono::milliseconds(3001));
    app.update({}, std::chrono::milliseconds(0));
    TEST_ASSERT_TRUE(display.labels.size() > labelsBeforeStale);
    TEST_ASSERT_EQUAL_STRING("CPU --", display.labels[labelsBeforeStale].c_str());
    app.onDeactivate();
    TEST_ASSERT_FALSE(f.status.monitoring());
}

void test_mac_status_requires_live_metric_capability() {
    Fixture f;
    Display display;
    apps::MacStatusApp app(f.status, display);
    core::AppRegistry registry;
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<unsigned>(core::AppRegistrationResult::Registered),
        static_cast<unsigned>(registry.registerApp({"mac-status",
                                                    "MAC STATUS",
                                                    "mac-status",
                                                    "mac-status",
                                                    {companionSystemMetricsCapabilityId}})));
    apps::MiniAppRuntime runtime(registry, f.capabilities);
    (void)runtime.registerInstance("mac-status", app);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(apps::MiniAppEligibility::MissingCapability),
                            static_cast<unsigned>(runtime.eligibility("mac-status")));
    f.ready();
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(apps::MiniAppActivationResult::Activated),
                            static_cast<unsigned>(runtime.activate("mac-status")));
    TEST_ASSERT_TRUE(f.status.monitoring());
    (void)f.capabilities.removeCapability(companionSystemMetricsCapabilityId);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<unsigned>(apps::MiniAppUpdateResult::DeactivatedMissingCapability),
        static_cast<unsigned>(runtime.update({}, std::chrono::milliseconds(0))));
    TEST_ASSERT_FALSE(runtime.hasActiveApp());
    TEST_ASSERT_FALSE(f.status.monitoring());
}
} // namespace

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_monitoring_cadence_freshness_and_lifecycle);
    RUN_TEST(test_dashboard_uses_full_screen_placeholders_and_dirty_blocks);
    RUN_TEST(test_mac_status_requires_live_metric_capability);
    return UNITY_END();
}
