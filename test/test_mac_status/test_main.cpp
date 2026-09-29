#include <unity.h>

#include <cstring>
#include <deque>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include "../support/ui_capture.h"
#include "apps/mac_status/mac_status_app.h"
#include "apps/runtime/mini_app_runtime.h"
#include "connectivity/companion/companion_protocol.h"
#include "core/capabilities/capability_registry.h"
#include "core/display/palette.h"
#include "core/display/text_layout.h"

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

    std::uint8_t version = 2;

    // Protocol v6 with SYSTEM_DETAILS when `details`, otherwise the v2 overview only.
    void ready(std::uint8_t protocol = 2, bool details = false) {
        version = protocol;
        const std::uint8_t v2[] = {2, 1};
        const std::uint8_t v6[] = {6, 5, 4, 3};
        transport.incoming.push_back(wire(protocol >= 6 ? makeHello(v6, 4) : makeHello(v2, 2)));
        companion.update(std::chrono::milliseconds(0));
        const auto capRequest = transport.last();
        auto caps = makeResponse(companion.session(), capRequest.requestId,
                                 CompanionOperation::Capabilities, CompanionStatus::Ok);
        caps.version = protocol;
        const CompanionCapability ids[] = {CompanionCapability::AppActive,
                                           CompanionCapability::SystemMetrics,
                                           CompanionCapability::SystemDetails};
        TEST_ASSERT_TRUE(setCapabilityList(caps, ids, details ? 3 : 2));
        transport.incoming.push_back(wire(caps));
        companion.update(std::chrono::milliseconds(0));
        const auto activeRequest = transport.last();
        auto active = makeResponse(companion.session(), activeRequest.requestId,
                                   CompanionOperation::AppActive, CompanionStatus::NotAvailable);
        active.version = protocol;
        transport.incoming.push_back(wire(active));
        companion.update(std::chrono::milliseconds(0));
    }

    void respond(const CompanionEnvelope& request, std::uint8_t cpu, std::uint8_t powerSource = 0,
                 std::uint16_t minutes = 0) {
        auto response = makeResponse(companion.session(), request.requestId,
                                     CompanionOperation::SystemMetrics, CompanionStatus::Ok);
        response.version = version;
        CompanionSystemMetrics metrics{};
        metrics.validity = 1U | 2U | 8U | 32U | 64U;
        metrics.cpuPercent = cpu;
        metrics.memoryUsedMiB = 8192;
        metrics.memoryTotalMiB = 16384;
        metrics.diskUsedPercent = 63;
        metrics.thermalState = 1;
        metrics.downloadKiBps = 12698;
        metrics.uploadKiBps = 820;
        if (powerSource != 0) {
            metrics.validity |= 16U | 128U;
            metrics.batteryPercent = 78;
            metrics.powerSource = powerSource;
            if (minutes != 0) {
                metrics.validity |= 256U;
                metrics.batteryMinutes = minutes;
            }
        }
        TEST_ASSERT_TRUE(setSystemMetrics(response, metrics));
        transport.incoming.push_back(wire(response));
        companion.update(std::chrono::milliseconds(0));
    }

    // The most recent request of `operation`, searching back from the newest message.
    std::optional<CompanionEnvelope> lastOf(CompanionOperation operation) const {
        for (auto it = transport.sent.rbegin(); it != transport.sent.rend(); ++it) {
            const auto decoded = decodeCompanionMessage(it->bytes.data(), it->size);
            if (decoded && decoded->operation == operation)
                return decoded;
        }
        return std::nullopt;
    }

    std::size_t count(CompanionOperation operation) const {
        std::size_t result = 0;
        for (const auto& payload : transport.sent) {
            const auto decoded = decodeCompanionMessage(payload.bytes.data(), payload.size);
            if (decoded && decoded->operation == operation)
                ++result;
        }
        return result;
    }

    void respondDetails(const CompanionEnvelope& request, SystemDetailsGroup group) {
        auto response = makeResponse(companion.session(), request.requestId,
                                     CompanionOperation::SystemDetails, CompanionStatus::Ok);
        response.version = 6;
        CompanionSystemDetails details{};
        details.group = group;
        switch (group) {
        case SystemDetailsGroup::Cpu:
            details.validity = 0x1f;
            details.performancePercent = 61;
            details.efficiencyPercent = 18;
            details.gpuPercent = 27;
            details.loadCenti = 310;
            details.processCount = 4;
            for (const auto& [index, name, percent] :
                 {std::tuple{0, "Xcode", 38}, std::tuple{1, "Google Chrome", 21},
                  std::tuple{2, "Telegram", 9}, std::tuple{3, "Claude", 6}}) {
                std::strcpy(details.processes[index].name.data(), name);
                details.processes[index].percent = static_cast<std::uint8_t>(percent);
            }
            break;
        case SystemDetailsGroup::Power:
            details.validity = 0x1f;
            details.systemDrawDeciwatts = 142;
            details.adapterWatts = 96;
            details.healthPercent = 91;
            details.cycleCount = 214;
            details.peripheralPercent = 12;
            std::strcpy(details.peripheralName.data(), "Magic Mouse");
            break;
        case SystemDetailsGroup::Network:
            details.validity = 0x1f;
            details.internetRttMs = 18;
            details.routerRttMs = 3;
            details.wifiRssiDbm = -54;
            details.wifiLinkMbps = 866;
            details.vpnActive = true;
            break;
        case SystemDetailsGroup::Memory:
            details.validity = 0x0f;
            details.appMiB = 14438;
            details.wiredMiB = 3994;
            details.compressedMiB = 3482;
            details.swapUsedMiB = 1229;
            details.ssdFreeGB = 212;
            details.ssdTotalGB = 994;
            details.diskReadKiBps = 348160;
            details.diskWriteKiBps = 59392;
            break;
        }
        TEST_ASSERT_TRUE(setSystemDetails(response, details));
        transport.incoming.push_back(wire(response));
        companion.update(std::chrono::milliseconds(0));
    }

    void tick(std::chrono::milliseconds elapsed) {
        companion.update(elapsed);
        status.update(elapsed);
    }
};

class Display : public core::IDisplayAdapter {
  public:
    void clear(core::RgbColor color) override {
        capture.clear(color);
        ++draws;
    }
    void fillRectangle(core::PixelPosition position, std::int32_t width, std::int32_t height,
                       core::RgbColor color) override {
        TEST_ASSERT_TRUE(position.x >= 0 && position.y >= 0);
        TEST_ASSERT_TRUE(position.x + width <= 240 && position.y + height <= 135);
        capture.rectangle(position, width, height, color);
        rectangles.push_back({position.x, position.y, width, height, color});
        ++draws;
    }
    void drawText(core::PixelPosition position, const char* text, core::TextStyle style) override {
        TEST_ASSERT_TRUE(position.x >= 0 && position.y >= 0);
        TEST_ASSERT_TRUE(position.x + core::textWidth(text, style.scale) <= 240);
        TEST_ASSERT_TRUE(position.y + std::ceil(8 * style.scale) <= 135);
        capture.text(position, text, style);
        ++draws;
        labels.emplace_back(text);
    }
    struct Rectangle {
        std::int32_t x, y, width, height;
        core::RgbColor color;
    };
    bool hasLabel(const char* text) const {
        for (const auto& label : labels)
            if (label == text)
                return true;
        return false;
    }
    std::size_t rectanglesIn(std::int32_t x0, std::int32_t y0, std::int32_t x1, std::int32_t y1,
                             core::RgbColor color) const {
        std::size_t count = 0;
        for (const auto& r : rectangles)
            if (r.x >= x0 && r.y >= y0 && r.x + r.width <= x1 && r.y + r.height <= y1 &&
                r.color.red == color.red && r.color.green == color.green &&
                r.color.blue == color.blue)
                ++count;
        return count;
    }
    void reset() {
        labels.clear();
        rectangles.clear();
    }
    int draws = 0;
    cardputer_hub::test_support::UiCapture capture;
    std::vector<std::string> labels;
    std::vector<Rectangle> rectangles;
};

core::InputEvents key(core::NamedKey named) {
    return {core::InputEvent{core::InputEventType::NamedKey, 0, named, {}}};
}

core::InputEvents character(char value) {
    return {core::InputEvent{
        core::InputEventType::PrintableCharacter, value, core::NamedKey::Count, {}}};
}

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

void test_history_appends_samples_gaps_and_clears_on_session_change() {
    Fixture f;
    f.ready();
    f.status.startMonitoring();
    f.respond(f.transport.last(), 10);
    f.status.update(std::chrono::milliseconds(0));
    const auto& history = *f.status.history();
    TEST_ASSERT_EQUAL_UINT(1, history.size());
    TEST_ASSERT_TRUE(history.at(0).cpuValid);
    TEST_ASSERT_EQUAL_UINT8(10, history.at(0).cpu);
    TEST_ASSERT_TRUE(history.at(0).networkValid);
    TEST_ASSERT_EQUAL_UINT32(12698, history.at(0).downloadKiBps);
    f.status.update(std::chrono::milliseconds(1000));
    const auto slow = f.transport.last();
    // A response that is merely late is not a gap: the line stays continuous.
    f.status.update(std::chrono::milliseconds(1000));
    f.status.update(std::chrono::milliseconds(1000));
    TEST_ASSERT_EQUAL_UINT(1, history.size());
    f.respond(slow, 20);
    f.status.update(std::chrono::milliseconds(0));
    TEST_ASSERT_EQUAL_UINT(2, history.size());
    TEST_ASSERT_EQUAL_UINT8(20, history.at(1).cpu);
    // A request that fails is a gap.
    f.status.update(std::chrono::milliseconds(1000));
    f.companion.update(std::chrono::milliseconds(2000));
    f.status.update(std::chrono::milliseconds(0));
    TEST_ASSERT_EQUAL_UINT(3, history.size());
    TEST_ASSERT_FALSE(history.at(2).cpuValid);
    TEST_ASSERT_FALSE(history.at(2).networkValid);
    for (int second = 0; second < 70; ++second) {
        f.status.update(std::chrono::milliseconds(1000));
        f.respond(*f.lastOf(CompanionOperation::SystemMetrics), 30);
        f.status.update(std::chrono::milliseconds(0));
    }
    TEST_ASSERT_EQUAL_UINT(services::MacStatusHistory::capacity, history.size());
    const auto before = history.generation();
    f.ready();
    f.status.update(std::chrono::milliseconds(0));
    TEST_ASSERT_EQUAL_UINT(0, history.size());
    TEST_ASSERT_TRUE(history.generation() != before);
    f.respond(*f.lastOf(CompanionOperation::SystemMetrics), 30);
    f.status.update(std::chrono::milliseconds(0));
    TEST_ASSERT_EQUAL_UINT(1, history.size());
    f.status.stopMonitoring();
    TEST_ASSERT_NULL(f.status.history());
}

void test_v6_overview_reports_power_source_and_minutes() {
    Fixture f;
    f.ready(6, true);
    f.status.startMonitoring();
    f.respond(f.transport.last(), 42, 2, 102);
    f.status.update(std::chrono::milliseconds(0));
    const auto snapshot = f.status.snapshot();
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(services::MacPowerSource::Charging),
                            static_cast<unsigned>(snapshot.powerSource));
    TEST_ASSERT_TRUE(snapshot.batteryMinutesAvailable);
    TEST_ASSERT_EQUAL_UINT16(102, snapshot.batteryMinutes);
}

void test_detail_polling_requests_only_the_selected_group() {
    Fixture f;
    f.ready(6, true);
    TEST_ASSERT_TRUE(f.status.detailsSupported());
    f.status.startMonitoring();
    f.status.update(std::chrono::milliseconds(0));
    TEST_ASSERT_EQUAL_UINT(0, f.count(CompanionOperation::SystemDetails));
    f.status.setDetailGroup(services::MacDetailGroup::Cpu);
    TEST_ASSERT_EQUAL_UINT(1, f.count(CompanionOperation::SystemDetails));
    const auto request = *f.lastOf(CompanionOperation::SystemDetails);
    TEST_ASSERT_EQUAL_UINT8(1, request.payload[0]);
    f.respondDetails(request, SystemDetailsGroup::Cpu);
    f.tick(std::chrono::milliseconds(0));
    const auto details = *f.status.details();
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(services::MacStatusFreshness::Fresh),
                            static_cast<unsigned>(details.freshness));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(services::MacDetailGroup::Cpu),
                            static_cast<unsigned>(details.group));
    TEST_ASSERT_EQUAL_UINT8(61, details.performancePercent.value());
    TEST_ASSERT_EQUAL_UINT8(4, details.appCount);
    TEST_ASSERT_EQUAL_STRING("Google Chrome", details.apps[1].name.c_str());
    f.tick(std::chrono::milliseconds(1000));
    TEST_ASSERT_EQUAL_UINT(1, f.count(CompanionOperation::SystemDetails));
    f.tick(std::chrono::milliseconds(1000));
    TEST_ASSERT_EQUAL_UINT(2, f.count(CompanionOperation::SystemDetails));
    f.respondDetails(*f.lastOf(CompanionOperation::SystemDetails), SystemDetailsGroup::Cpu);
    f.status.setDetailGroup(services::MacDetailGroup::None);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(services::MacStatusFreshness::Empty),
                            static_cast<unsigned>(f.status.details()->freshness));
    for (int second = 0; second < 6; ++second)
        f.tick(std::chrono::milliseconds(1000));
    TEST_ASSERT_EQUAL_UINT(2, f.count(CompanionOperation::SystemDetails));
    f.status.setDetailGroup(services::MacDetailGroup::Memory);
    f.status.stopMonitoring();
    for (int second = 0; second < 6; ++second)
        f.tick(std::chrono::milliseconds(1000));
    TEST_ASSERT_EQUAL_UINT(3, f.count(CompanionOperation::SystemDetails));
}

void test_mismatched_group_completion_is_discarded() {
    Fixture f;
    f.ready(6, true);
    f.status.startMonitoring();
    f.status.setDetailGroup(services::MacDetailGroup::Cpu);
    const auto cpuRequest = *f.lastOf(CompanionOperation::SystemDetails);
    f.status.setDetailGroup(services::MacDetailGroup::Power);
    TEST_ASSERT_EQUAL_UINT(1, f.count(CompanionOperation::SystemDetails));
    f.respondDetails(cpuRequest, SystemDetailsGroup::Cpu);
    f.tick(std::chrono::milliseconds(0));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(services::MacStatusFreshness::Empty),
                            static_cast<unsigned>(f.status.details()->freshness));
    TEST_ASSERT_EQUAL_UINT(2, f.count(CompanionOperation::SystemDetails));
    const auto powerRequest = *f.lastOf(CompanionOperation::SystemDetails);
    TEST_ASSERT_EQUAL_UINT8(2, powerRequest.payload[0]);
    f.respondDetails(powerRequest, SystemDetailsGroup::Power);
    f.tick(std::chrono::milliseconds(0));
    TEST_ASSERT_EQUAL_UINT16(142, f.status.details()->systemDrawDeciwatts.value());
}

void test_detail_values_expire_after_six_seconds_without_retry_burst() {
    Fixture f;
    f.ready(6, true);
    f.status.startMonitoring();
    f.status.setDetailGroup(services::MacDetailGroup::Network);
    f.respondDetails(*f.lastOf(CompanionOperation::SystemDetails), SystemDetailsGroup::Network);
    f.tick(std::chrono::milliseconds(0));
    TEST_ASSERT_EQUAL_INT8(-54, f.status.details()->wifiRssiDbm.value());
    for (int step = 0; step < 12; ++step) {
        f.tick(std::chrono::milliseconds(500));
        // Keep the overview answered so only the detail requests time out.
        if (const auto metrics = f.lastOf(CompanionOperation::SystemMetrics);
            metrics && f.companion.hasPendingRequest(CompanionOperation::SystemMetrics))
            f.respond(*metrics, 5);
    }
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(services::MacStatusFreshness::Fresh),
                            static_cast<unsigned>(f.status.details()->freshness));
    f.tick(std::chrono::milliseconds(500));
    f.tick(std::chrono::milliseconds(100));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(services::MacStatusFreshness::Stale),
                            static_cast<unsigned>(f.status.details()->freshness));
    TEST_ASSERT_TRUE(f.count(CompanionOperation::SystemDetails) <= 5);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(services::MacStatusFreshness::Fresh),
                            static_cast<unsigned>(f.status.snapshot().freshness));
}

void test_overview_draws_cpu_sparkline_from_history() {
    Fixture f;
    f.ready();
    Display display;
    apps::MacStatusApp app(f.status, display);
    app.onActivate();
    for (int second = 0; second < 10; ++second) {
        f.respond(f.transport.last(), static_cast<std::uint8_t>(10 + second * 8));
        f.status.update(std::chrono::milliseconds(1000));
    }
    app.update({}, std::chrono::milliseconds(0));
    display.capture.save("mac-status-sparkline");
    TEST_ASSERT_EQUAL_UINT(10, f.status.history()->size());
    TEST_ASSERT_TRUE(display.hasLabel("CPU 82%"));
    TEST_ASSERT_TRUE(display.hasLabel("TEMP OK"));
    // The line occupies the right side of the CPU block, and no bar is drawn.
    TEST_ASSERT_TRUE(display.rectanglesIn(80, 26, 110, 41, core::palette::blue) > 0);
    TEST_ASSERT_EQUAL_UINT(0, display.rectanglesIn(8, 26, 60, 41, core::palette::blue));
    TEST_ASSERT_EQUAL_UINT(0, display.rectanglesIn(100, 131, 140, 134, core::palette::pale));
    const auto before = display.draws;
    app.update({}, std::chrono::milliseconds(0));
    TEST_ASSERT_EQUAL_INT(before, display.draws);
}

void test_overview_battery_shows_charge_state_and_time() {
    Fixture f;
    f.ready(6, true);
    Display display;
    apps::MacStatusApp app(f.status, display);
    app.onActivate();
    f.respond(f.transport.last(), 42, 2, 102);
    f.status.update(std::chrono::milliseconds(0));
    app.update({}, std::chrono::milliseconds(0));
    display.capture.save("mac-status-v6");
    TEST_ASSERT_TRUE(display.hasLabel("BAT 78% 1:42"));
    TEST_ASSERT_TRUE(display.rectanglesIn(224, 47, 232, 60, core::palette::leaf) > 0);
    // Charging is a green dot, not a blue bolt.
    TEST_ASSERT_EQUAL_UINT(0, display.rectanglesIn(224, 47, 232, 60, core::palette::blue));
    // Five page dots show that detail pages exist.
    TEST_ASSERT_EQUAL_UINT(4, display.rectanglesIn(100, 131, 140, 134, core::palette::pale));
    display.reset();
    f.status.update(std::chrono::milliseconds(1000));
    f.respond(*f.lastOf(CompanionOperation::SystemMetrics), 42, 1, 185);
    f.status.update(std::chrono::milliseconds(0));
    app.update({}, std::chrono::milliseconds(0));
    TEST_ASSERT_TRUE(display.hasLabel("BAT 78% 3:05"));
    display.reset();
    app.update(key(core::NamedKey::Right), std::chrono::milliseconds(0));
    app.update(key(core::NamedKey::Right), std::chrono::milliseconds(0));
    TEST_ASSERT_TRUE(display.hasLabel("ON BATTERY"));
    TEST_ASSERT_TRUE(display.hasLabel("EMPTY IN 3:05"));
    app.update(key(core::NamedKey::Left), std::chrono::milliseconds(0));
    app.update(key(core::NamedKey::Left), std::chrono::milliseconds(0));
    TEST_ASSERT_EQUAL_UINT(0, display.rectanglesIn(224, 47, 232, 60, core::palette::leaf));
    display.reset();
    f.status.update(std::chrono::milliseconds(1000));
    f.respond(*f.lastOf(CompanionOperation::SystemMetrics), 42, 3);
    f.status.update(std::chrono::milliseconds(0));
    app.update({}, std::chrono::milliseconds(0));
    TEST_ASSERT_TRUE(display.hasLabel("BAT 78% AC"));
}

void test_detail_pages_wrap_and_request_only_visible_group() {
    Fixture f;
    f.ready(6, true);
    Display display;
    apps::MacStatusApp app(f.status, display);
    app.onActivate();
    f.respond(f.transport.last(), 42, 2, 102);
    f.status.update(std::chrono::milliseconds(0));
    app.update({}, std::chrono::milliseconds(0));
    TEST_ASSERT_EQUAL_UINT(0, f.count(CompanionOperation::SystemDetails));
    const apps::MacStatusPage expected[] = {apps::MacStatusPage::Cpu, apps::MacStatusPage::Power,
                                            apps::MacStatusPage::Network,
                                            apps::MacStatusPage::Memory};
    const SystemDetailsGroup groups[] = {SystemDetailsGroup::Cpu, SystemDetailsGroup::Power,
                                         SystemDetailsGroup::Network, SystemDetailsGroup::Memory};
    const char* counters[] = {"2/5", "3/5", "4/5", "5/5"};
    const char* captures[] = {"mac-status-cpu", "mac-status-power", "mac-status-network",
                              "mac-status-memory"};
    const char* content[] = {"GOOGLE CHROME", "14.2 W", "12.4 MB/s", "212 G / 994 G"};
    for (int index = 0; index < 4; ++index) {
        display.reset();
        app.update(index % 2 == 0 ? key(core::NamedKey::Right) : character('/'),
                   std::chrono::milliseconds(0));
        TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(expected[index]),
                                static_cast<unsigned>(app.page()));
        TEST_ASSERT_TRUE(display.hasLabel(counters[index]));
        const auto request = *f.lastOf(CompanionOperation::SystemDetails);
        TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(groups[index]), request.payload[0]);
        f.respondDetails(request, groups[index]);
        f.status.update(std::chrono::milliseconds(0));
        app.update({}, std::chrono::milliseconds(0));
        TEST_ASSERT_TRUE(display.hasLabel(content[index]));
        display.capture.save(captures[index]);
    }
    TEST_ASSERT_EQUAL_UINT(4, f.count(CompanionOperation::SystemDetails));
    TEST_ASSERT_TRUE(display.hasLabel("READ 340 WRITE 58 MB/s"));
    TEST_ASSERT_TRUE(display.hasLabel("SWAPPED TO DISK"));
    TEST_ASSERT_TRUE(display.hasLabel("MACOS"));
    TEST_ASSERT_TRUE(display.hasLabel("COMPRESSED"));
    TEST_ASSERT_TRUE(display.hasLabel("14.1 G"));
    app.update(key(core::NamedKey::Right), std::chrono::milliseconds(0));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(apps::MacStatusPage::Overview),
                            static_cast<unsigned>(app.page()));
    for (int second = 0; second < 6; ++second)
        f.tick(std::chrono::milliseconds(1000));
    TEST_ASSERT_EQUAL_UINT(4, f.count(CompanionOperation::SystemDetails));
    app.update(character(','), std::chrono::milliseconds(0));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(apps::MacStatusPage::Memory),
                            static_cast<unsigned>(app.page()));
    TEST_ASSERT_EQUAL_UINT(5, f.count(CompanionOperation::SystemDetails));
}

void test_detail_page_content_matches_details() {
    Fixture f;
    f.ready(6, true);
    Display display;
    apps::MacStatusApp app(f.status, display);
    app.onActivate();
    f.respond(f.transport.last(), 42, 2, 102);
    f.status.update(std::chrono::milliseconds(0));
    app.update(key(core::NamedKey::Right), std::chrono::milliseconds(0));
    f.respondDetails(*f.lastOf(CompanionOperation::SystemDetails), SystemDetailsGroup::Cpu);
    f.status.update(std::chrono::milliseconds(0));
    app.update({}, std::chrono::milliseconds(0));
    TEST_ASSERT_TRUE(display.hasLabel("42%"));
    TEST_ASSERT_TRUE(display.hasLabel("CORES"));
    TEST_ASSERT_TRUE(display.hasLabel("FAST 61%  EFF 18%"));
    TEST_ASSERT_TRUE(display.hasLabel("GPU"));
    TEST_ASSERT_TRUE(display.hasLabel("27%"));
    TEST_ASSERT_FALSE(display.hasLabel("LOAD 3.1"));
    TEST_ASSERT_TRUE(display.hasLabel("GOOGLE CHROME"));
    TEST_ASSERT_TRUE(display.hasLabel("04"));
    display.reset();
    app.update(key(core::NamedKey::Right), std::chrono::milliseconds(0));
    f.respondDetails(*f.lastOf(CompanionOperation::SystemDetails), SystemDetailsGroup::Power);
    f.status.update(std::chrono::milliseconds(0));
    app.update({}, std::chrono::milliseconds(0));
    TEST_ASSERT_TRUE(display.hasLabel("78%"));
    TEST_ASSERT_TRUE(display.hasLabel("CHARGING"));
    TEST_ASSERT_TRUE(display.hasLabel("FULL IN 1:42"));
    TEST_ASSERT_TRUE(display.hasLabel("14.2 W"));
    TEST_ASSERT_TRUE(display.hasLabel("POWER USE"));
    TEST_ASSERT_TRUE(display.hasLabel("CHARGER"));
    TEST_ASSERT_TRUE(display.hasLabel("96 W"));
    TEST_ASSERT_TRUE(display.hasLabel("BATTERY HEALTH"));
    TEST_ASSERT_TRUE(display.hasLabel("91%"));
    TEST_ASSERT_TRUE(display.hasLabel("CHARGE CYCLES"));
    TEST_ASSERT_TRUE(display.hasLabel("214"));
    TEST_ASSERT_TRUE(display.hasLabel("MAGIC MOUSE"));
    TEST_ASSERT_TRUE(display.hasLabel("LOW 12%"));
    TEST_ASSERT_EQUAL_UINT(16, display.rectanglesIn(8, 46, 232, 53, core::palette::blue));
    display.reset();
    app.update(key(core::NamedKey::Right), std::chrono::milliseconds(0));
    f.respondDetails(*f.lastOf(CompanionOperation::SystemDetails), SystemDetailsGroup::Network);
    f.status.update(std::chrono::milliseconds(0));
    app.update({}, std::chrono::milliseconds(0));
    TEST_ASSERT_TRUE(display.hasLabel("12.4 MB/s"));
    TEST_ASSERT_TRUE(display.hasLabel("INTERNET PING"));
    TEST_ASSERT_TRUE(display.hasLabel("18 MS"));
    TEST_ASSERT_TRUE(display.hasLabel("3 MS"));
    TEST_ASSERT_TRUE(display.hasLabel("WI-FI SIGNAL"));
    TEST_ASSERT_TRUE(display.hasLabel("STRONG"));
    TEST_ASSERT_TRUE(display.hasLabel("WI-FI SPEED"));
    TEST_ASSERT_TRUE(display.hasLabel("866 MBIT/S"));
    TEST_ASSERT_FALSE(display.hasLabel("-54 DBM 866 MBPS"));
    TEST_ASSERT_FALSE(display.hasLabel("VPN"));
    // Unchanged regions are not repainted on the next frame.
    const auto before = display.draws;
    app.update({}, std::chrono::milliseconds(0));
    TEST_ASSERT_EQUAL_INT(before, display.draws);
}

void test_cpu_page_marks_idle_apps_and_keeps_rows_apart() {
    Fixture f;
    f.ready(6, true);
    Display display;
    apps::MacStatusApp app(f.status, display);
    app.onActivate();
    f.respond(f.transport.last(), 100);
    f.status.update(std::chrono::milliseconds(0));
    app.update(key(core::NamedKey::Right), std::chrono::milliseconds(0));
    const auto request = *f.lastOf(CompanionOperation::SystemDetails);
    auto response = makeResponse(f.companion.session(), request.requestId,
                                 CompanionOperation::SystemDetails, CompanionStatus::Ok);
    response.version = 6;
    CompanionSystemDetails details{};
    details.group = SystemDetailsGroup::Cpu;
    details.validity = 0x1f;
    details.performancePercent = 100;
    details.efficiencyPercent = 100;
    details.gpuPercent = 100;
    details.loadCenti = 12345;
    details.processCount = 0;
    TEST_ASSERT_TRUE(setSystemDetails(response, details));
    f.transport.incoming.push_back(wire(response));
    f.companion.update(std::chrono::milliseconds(0));
    f.status.update(std::chrono::milliseconds(0));
    display.reset();
    app.update({}, std::chrono::milliseconds(0));
    display.capture.save("mac-status-cpu-extreme");
    // A Mac with no busy app says so instead of leaving the list blank.
    TEST_ASSERT_TRUE(display.hasLabel("APPS"));
    TEST_ASSERT_TRUE(display.hasLabel("IDLE"));
    TEST_ASSERT_TRUE(display.hasLabel("FAST 100%  EFF 100%"));
    TEST_ASSERT_TRUE(display.hasLabel("100%"));
    // The widest cores value still leaves a glyph of space after its label.
    const auto labelEnd = 8 + core::systemTextWidth("CORES");
    const auto valueStart = core::rightAlignedTextX("FAST 100%  EFF 100%", 232);
    TEST_ASSERT_TRUE(valueStart - labelEnd >= 7);
}

void test_single_page_without_system_details_ignores_left_right() {
    Fixture f;
    f.ready(2);
    Display display;
    apps::MacStatusApp app(f.status, display);
    app.onActivate();
    f.respond(f.transport.last(), 42);
    f.status.update(std::chrono::milliseconds(0));
    app.update(key(core::NamedKey::Right), std::chrono::milliseconds(0));
    app.update(character(','), std::chrono::milliseconds(0));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(apps::MacStatusPage::Overview),
                            static_cast<unsigned>(app.page()));
    TEST_ASSERT_EQUAL_UINT(0, f.count(CompanionOperation::SystemDetails));
    TEST_ASSERT_EQUAL_UINT(0, display.rectanglesIn(100, 131, 140, 134, core::palette::pale));
    TEST_ASSERT_TRUE(display.hasLabel("BAT --"));
}

void test_detail_pages_render_placeholders_for_invalid_fields() {
    Fixture f;
    f.ready(6, true);
    Display display;
    apps::MacStatusApp app(f.status, display);
    app.onActivate();
    f.respond(f.transport.last(), 42);
    f.status.update(std::chrono::milliseconds(0));
    app.update(key(core::NamedKey::Right), std::chrono::milliseconds(0));
    TEST_ASSERT_TRUE(display.hasLabel("FAST --  EFF --"));
    TEST_ASSERT_TRUE(display.hasLabel("APPS"));
    display.reset();
    app.update(key(core::NamedKey::Right), std::chrono::milliseconds(0));
    TEST_ASSERT_TRUE(display.hasLabel("NO BATTERY"));
    TEST_ASSERT_TRUE(display.hasLabel("POWER USE"));
    TEST_ASSERT_EQUAL_UINT(0, display.rectanglesIn(8, 46, 232, 53, core::palette::blue));
    // The CPU request from the previous page is still in flight; its late
    // answer is dropped and the POWER request follows.
    const auto cpuRequest = *f.lastOf(CompanionOperation::SystemDetails);
    TEST_ASSERT_EQUAL_UINT8(1, cpuRequest.payload[0]);
    auto unavailable =
        makeResponse(f.companion.session(), cpuRequest.requestId, CompanionOperation::SystemDetails,
                     CompanionStatus::NotAvailable);
    unavailable.version = 6;
    f.transport.incoming.push_back(wire(unavailable));
    f.companion.update(std::chrono::milliseconds(0));
    f.status.update(std::chrono::milliseconds(0));
    auto request = *f.lastOf(CompanionOperation::SystemDetails);
    TEST_ASSERT_EQUAL_UINT8(2, request.payload[0]);
    auto response = makeResponse(f.companion.session(), request.requestId,
                                 CompanionOperation::SystemDetails, CompanionStatus::Ok);
    response.version = 6;
    CompanionSystemDetails details{};
    details.group = SystemDetailsGroup::Power;
    details.validity = 2;
    details.adapterWatts = 65;
    TEST_ASSERT_TRUE(setSystemDetails(response, details));
    f.transport.incoming.push_back(wire(response));
    f.companion.update(std::chrono::milliseconds(0));
    f.status.update(std::chrono::milliseconds(0));
    display.reset();
    app.update({}, std::chrono::milliseconds(0));
    TEST_ASSERT_TRUE(display.hasLabel("65 W"));
    TEST_ASSERT_FALSE(display.hasLabel("14.2 W"));
    display.capture.save("mac-status-power-desktop");
}

void test_companion_loss_resets_pages_and_history() {
    Fixture f;
    Display display;
    apps::MacStatusApp app(f.status, display);
    core::AppRegistry registry;
    (void)registry.registerApp({"mac-status",
                                "MAC STATUS",
                                "mac-status",
                                "mac-status",
                                {companionSystemMetricsCapabilityId}});
    apps::MiniAppRuntime runtime(registry, f.capabilities);
    (void)runtime.registerInstance("mac-status", app);
    f.ready(6, true);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(apps::MiniAppActivationResult::Activated),
                            static_cast<unsigned>(runtime.activate("mac-status")));
    f.respond(*f.lastOf(CompanionOperation::SystemMetrics), 42);
    (void)runtime.update(key(core::NamedKey::Right), std::chrono::milliseconds(0));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(apps::MacStatusPage::Cpu),
                            static_cast<unsigned>(app.page()));
    f.transport.transportState = CompanionTransportState::Unavailable;
    f.companion.update(std::chrono::milliseconds(0));
    (void)runtime.update({}, std::chrono::milliseconds(0));
    TEST_ASSERT_FALSE(runtime.hasActiveApp());
    TEST_ASSERT_FALSE(f.status.monitoring());
    TEST_ASSERT_NULL(f.status.history());
    TEST_ASSERT_NULL(f.status.details());
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(apps::MacStatusPage::Overview),
                            static_cast<unsigned>(app.page()));
}

// A closed MAC STATUS keeps only references and one pointer each; history,
// details and the view are heap state that exists only while it is open.
static_assert(sizeof(services::MacStatusService) <= 2 * sizeof(void*));
static_assert(sizeof(apps::MacStatusApp) <= 4 * sizeof(void*));

void test_closed_mac_status_releases_state() {
    Fixture f;
    f.ready(6, true);
    Display display;
    apps::MacStatusApp app(f.status, display);
    TEST_ASSERT_NULL(f.status.history());
    TEST_ASSERT_NULL(f.status.details());
    app.onActivate();
    TEST_ASSERT_NOT_NULL(f.status.history());
    f.respond(f.transport.last(), 42);
    f.status.update(std::chrono::milliseconds(0));
    app.update(key(core::NamedKey::Right), std::chrono::milliseconds(0));
    TEST_ASSERT_EQUAL_UINT(1, f.status.history()->size());
    app.onDeactivate();
    TEST_ASSERT_FALSE(f.status.monitoring());
    TEST_ASSERT_NULL(f.status.history());
    TEST_ASSERT_NULL(f.status.details());
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(services::MacStatusFreshness::Empty),
                            static_cast<unsigned>(f.status.snapshot().freshness));
    // Late answers to the closed session are drained without state.
    for (int second = 0; second < 3; ++second)
        f.tick(std::chrono::milliseconds(1000));
    const auto draws = display.draws;
    app.update({}, std::chrono::milliseconds(0));
    TEST_ASSERT_EQUAL_INT(draws, display.draws);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(apps::MacStatusPage::Overview),
                            static_cast<unsigned>(app.page()));
    // Reopening starts from a fresh, empty state.
    app.onActivate();
    TEST_ASSERT_EQUAL_UINT(0, f.status.history()->size());
    app.update({}, std::chrono::milliseconds(0));
    TEST_ASSERT_TRUE(display.hasLabel("CPU --"));
}

void test_dashboard_uses_full_screen_placeholders_and_dirty_blocks() {
    Fixture f;
    f.ready();
    Display display;
    apps::MacStatusApp app(f.status, display);
    app.onActivate();
    app.update({}, std::chrono::milliseconds(0));
    display.capture.save("mac-status");
    TEST_ASSERT_EQUAL_UINT(8, display.labels.size());
    TEST_ASSERT_EQUAL_STRING("CPU --", display.labels[0].c_str());
    TEST_ASSERT_EQUAL_STRING("RAM --", display.labels[1].c_str());
    TEST_ASSERT_EQUAL_STRING("DISK --", display.labels[2].c_str());
    TEST_ASSERT_EQUAL_STRING("MEMORY --", display.labels[6].c_str());
    TEST_ASSERT_EQUAL_STRING("TEMP --", display.labels[7].c_str());
    const auto initialDraws = display.draws;
    app.update({}, std::chrono::milliseconds(100));
    TEST_ASSERT_EQUAL_INT(initialDraws, display.draws);
    f.respond(f.transport.last(), 34);
    f.status.update(std::chrono::milliseconds(0));
    app.update({}, std::chrono::milliseconds(0));
    TEST_ASSERT_EQUAL_STRING("CPU 34%", display.labels[8].c_str());
    TEST_ASSERT_EQUAL_STRING("RAM 8.0/16G", display.labels[9].c_str());
    TEST_ASSERT_EQUAL_STRING("DISK 63% USED", display.labels[10].c_str());
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
    RUN_TEST(test_history_appends_samples_gaps_and_clears_on_session_change);
    RUN_TEST(test_v6_overview_reports_power_source_and_minutes);
    RUN_TEST(test_detail_polling_requests_only_the_selected_group);
    RUN_TEST(test_mismatched_group_completion_is_discarded);
    RUN_TEST(test_detail_values_expire_after_six_seconds_without_retry_burst);
    RUN_TEST(test_overview_draws_cpu_sparkline_from_history);
    RUN_TEST(test_overview_battery_shows_charge_state_and_time);
    RUN_TEST(test_detail_pages_wrap_and_request_only_visible_group);
    RUN_TEST(test_detail_page_content_matches_details);
    RUN_TEST(test_cpu_page_marks_idle_apps_and_keeps_rows_apart);
    RUN_TEST(test_single_page_without_system_details_ignores_left_right);
    RUN_TEST(test_detail_pages_render_placeholders_for_invalid_fields);
    RUN_TEST(test_companion_loss_resets_pages_and_history);
    RUN_TEST(test_closed_mac_status_releases_state);
    RUN_TEST(test_dashboard_uses_full_screen_placeholders_and_dirty_blocks);
    RUN_TEST(test_mac_status_requires_live_metric_capability);
    return UNITY_END();
}
