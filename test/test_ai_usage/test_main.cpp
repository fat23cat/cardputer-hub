#include <unity.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

#include "apps/ai_usage/ai_usage_app.h"
#include "connectivity/companion/companion_protocol.h"
#include "core/capabilities/capability_registry.h"
#include "core/display/palette.h"
#include "services/ai_usage/ai_usage_indicator_controller.h"

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
        auto value = incoming.front();
        incoming.pop_front();
        return value;
    }
    CompanionEnvelope last() const {
        const auto value = decodeCompanionMessage(sent.back().bytes.data(), sent.back().size);
        TEST_ASSERT_TRUE(value.has_value());
        return *value;
    }
    CompanionTransportState transportState = CompanionTransportState::Ready;
    std::deque<CompanionPayload> incoming;
    std::vector<CompanionPayload> sent;
};

class Leds : public core::ILEDAdapter {
  public:
    void writeFrame(const core::LedHardwareFrame&) override { ++writes; }
    int writes = 0;
};

class Display : public core::IDisplayAdapter {
  public:
    void clear(core::RgbColor) override { ++draws; }
    void fillRectangle(core::PixelPosition position, std::int32_t width, std::int32_t height,
                       core::RgbColor) override {
        rectangles.push_back({position, width, height});
        ++draws;
    }
    void drawText(core::PixelPosition position, const char* text, core::TextStyle) override {
        positions.push_back(position);
        labels.emplace_back(text);
        ++draws;
    }
    int draws = 0;
    struct Rectangle {
        core::PixelPosition position;
        std::int32_t width;
        std::int32_t height;
    };
    std::vector<Rectangle> rectangles;
    std::vector<std::string> labels;
    std::vector<core::PixelPosition> positions;
};

struct Fixture {
    Transport transport;
    core::CapabilityRegistry capabilities;
    services::CompanionService companion{transport, capabilities};
    services::AiUsageService usage{companion};
    Leds leds;
    services::IndicatorService indicator{leds};
    services::AiUsageIndicatorController gauge{usage, indicator};

    void ready(std::uint8_t version = 3) {
        transport.transportState = CompanionTransportState::Ready;
        const std::uint8_t versions[] = {4, 3, 2, 1};
        transport.incoming.push_back(wire(makeHello(versions + (4 - version), version)));
        companion.update({});
        auto capsRequest = transport.last();
        TEST_ASSERT_EQUAL_UINT8(version, capsRequest.version);
        auto caps = makeResponse(companion.session(), capsRequest.requestId,
                                 CompanionOperation::Capabilities, CompanionStatus::Ok);
        caps.version = version;
        const CompanionCapability ids[] = {CompanionCapability::AppActive,
                                           CompanionCapability::SystemMetrics,
                                           CompanionCapability::AiUsage};
        TEST_ASSERT_TRUE(setCapabilityList(caps, ids, 3));
        transport.incoming.push_back(wire(caps));
        companion.update({});
        auto activeRequest = transport.last();
        auto active = makeResponse(companion.session(), activeRequest.requestId,
                                   CompanionOperation::AppActive, CompanionStatus::NotAvailable);
        active.version = version;
        transport.incoming.push_back(wire(active));
        companion.update({});
        TEST_ASSERT_TRUE(companion.supportsAiUsage());
        TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(services::CompanionSubmitResult::Submitted),
                                static_cast<unsigned>(companion.requestSystemMetrics()));
        usage.update({});
        TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CompanionOperation::AiUsage),
                                static_cast<unsigned>(transport.last().operation));
    }

    void respondValue(const CompanionAiUsage& value) {
        const auto request = transport.last();
        auto reply = makeResponse(companion.session(), request.requestId,
                                  CompanionOperation::AiUsage, CompanionStatus::Ok);
        reply.version = companion.selectedProtocolVersion();
        TEST_ASSERT_TRUE(setAiUsage(reply, value));
        transport.incoming.push_back(wire(reply));
        companion.update({});
        usage.update({});
        gauge.update({});
        indicator.update();
    }

    void respond(std::uint8_t percent, bool cursor = false) {
        CompanionAiUsage value{};
        value.state = AiUsageState::Ready;
        value.providerCount = cursor ? 2 : 1;
        auto& codex = value.providers[0];
        codex.provider = AiProvider::Codex;
        codex.plan = AiPlan::Plus;
        codex.metricCount = 1;
        codex.metrics[0].kind = AiMetricKind::FiveHour;
        codex.metrics[0].limit = 100;
        codex.metrics[0].used = 100 - percent;
        codex.metrics[0].remaining = percent;
        codex.metrics[0].remainingPercent = percent;
        if (cursor) {
            auto& provider = value.providers[1];
            provider.provider = AiProvider::Cursor;
            provider.plan = AiPlan::Enterprise;
            provider.metricCount = 1;
            provider.metrics[0].kind = AiMetricKind::Money;
            provider.metrics[0].unit = AiMetricUnit::Cents;
            provider.metrics[0].limit = 100;
            provider.metrics[0].remaining = 96;
            provider.metrics[0].used = 4;
            provider.metrics[0].remainingPercent = 96;
        }
        respondValue(value);
    }
};

int lit(const services::IndicatorFrame& frame, int begin, int end) {
    int count = 0;
    for (int i = begin; i < end; ++i) {
        const auto& color = frame.pixels[i];
        if (color.red || color.green || color.blue)
            ++count;
    }
    return count;
}

void test_service_polls_cached_snapshot_and_clears_on_session_change() {
    Fixture f;
    f.ready();
    f.respond(63);
    TEST_ASSERT_EQUAL_UINT8(1, f.usage.snapshot().providerCount);
    f.usage.update(std::chrono::seconds(30));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CompanionOperation::AiUsage),
                            static_cast<unsigned>(f.transport.last().operation));
    const auto before = f.transport.sent.size();
    f.usage.update(std::chrono::seconds(30));
    TEST_ASSERT_EQUAL_UINT(before, f.transport.sent.size());
    f.transport.transportState = CompanionTransportState::Unavailable;
    f.companion.update({});
    f.usage.update({});
    f.gauge.update({});
    f.indicator.update();
    TEST_ASSERT_EQUAL_UINT8(0, f.usage.snapshot().providerCount);
    TEST_ASSERT_FALSE(f.gauge.focused());
    TEST_ASSERT_FALSE(f.indicator.resolved().hasFrame);
}

void test_split_gauge_marks_both_ends_with_purple_and_30_quota_pixels_per_half() {
    CompanionAiUsage value{};
    value.providers[0].metrics[0].remainingPercent = 100;
    value.providers[1].metrics[0].remainingPercent = 100;
    const auto frame =
        services::aiUsageGauge(&value.providers[0].metrics[0], &value.providers[1].metrics[0]);
    TEST_ASSERT_EQUAL_INT(32, lit(frame, 0, 32));
    TEST_ASSERT_EQUAL_INT(32, lit(frame, 32, 64));
    for (const int marker : {0, 31, 32, 63}) {
        TEST_ASSERT_EQUAL_UINT8(0xA0, frame.pixels[marker].red);
        TEST_ASSERT_EQUAL_UINT8(0x50, frame.pixels[marker].green);
        TEST_ASSERT_EQUAL_UINT8(0xD0, frame.pixels[marker].blue);
    }
    value.providers[0].metrics[0].remainingPercent = 50;
    value.providers[1].metrics[0].remainingPercent = 0;
    const auto partial =
        services::aiUsageGauge(&value.providers[0].metrics[0], &value.providers[1].metrics[0]);
    TEST_ASSERT_EQUAL_INT(17, lit(partial, 0, 32)); // 2 markers + 15 quota pixels.
    TEST_ASSERT_EQUAL_INT(2, lit(partial, 32, 64)); // Markers remain at zero.
    TEST_ASSERT_EQUAL_UINT8(core::palette::blue.red, partial.pixels[1].red);
    TEST_ASSERT_EQUAL_UINT8(0, partial.pixels[30].red);
    value.providers[0].metrics[0].remainingPercent = 0;
    const auto empty = services::aiUsageGauge(&value.providers[0].metrics[0], nullptr);
    TEST_ASSERT_EQUAL_INT(0, lit(empty, 0, 64));
    value.providers[0].metrics[0].remainingPercent = 100;
    const auto focused = services::aiUsageGauge(&value.providers[0].metrics[0], nullptr);
    TEST_ASSERT_EQUAL_UINT8(core::palette::leaf.red, focused.pixels[0].red);
    TEST_ASSERT_EQUAL_UINT8(core::palette::leaf.red, focused.pixels[63].red);
}

void test_app_draws_remaining_quota_only_on_change() {
    Fixture f;
    f.ready();
    f.respond(4, true);
    Display display;
    apps::AiUsageApp app(f.usage, f.gauge, display);
    app.onActivate();
    app.update({}, {});
    TEST_ASSERT_TRUE(display.draws > 0);
    bool critical = false;
    for (const auto& label : display.labels)
        if (label.find("4% LEFT !") != std::string::npos)
            critical = true;
    TEST_ASSERT_TRUE(critical);
    const auto draws = display.draws;
    app.update({}, std::chrono::milliseconds(100));
    TEST_ASSERT_EQUAL_INT(draws, display.draws);
}

void test_provider_title_uses_font_safe_text_and_drawn_dot() {
    Fixture f;
    f.ready();
    f.respond(63);
    Display display;
    apps::AiUsageApp app(f.usage, f.gauge, display);
    app.onActivate();
    app.update({}, {});
    TEST_ASSERT_TRUE(std::find(display.labels.begin(), display.labels.end(), "CODEX") !=
                     display.labels.end());
    TEST_ASSERT_TRUE(std::find(display.labels.begin(), display.labels.end(), "PLUS") !=
                     display.labels.end());
    for (const auto& label : display.labels)
        for (const auto character : label)
            TEST_ASSERT_TRUE(static_cast<unsigned char>(character) < 128);
    const auto dot = std::find_if(display.rectangles.begin(), display.rectangles.end(),
                                  [](const Display::Rectangle& rectangle) {
                                      return rectangle.position.x == 84 &&
                                             rectangle.position.y == 18 && rectangle.width == 4 &&
                                             rectangle.height == 4;
                                  });
    TEST_ASSERT_TRUE(dot != display.rectangles.end());
}

void test_malformed_ai_response_retains_previous_snapshot() {
    Fixture f;
    f.ready();
    f.respond(63);
    f.usage.update(std::chrono::seconds(30));
    auto reply = makeResponse(f.companion.session(), f.transport.last().requestId,
                              CompanionOperation::AiUsage, CompanionStatus::Ok);
    reply.version = 3;
    CompanionAiUsage value{};
    value.state = AiUsageState::Ready;
    value.providerCount = 1;
    value.providers[0].metricCount = 1;
    TEST_ASSERT_TRUE(setAiUsage(reply, value));
    auto damaged = wire(reply);
    damaged.bytes[8 + 7 + 4 + 14] = 101;
    f.transport.incoming.push_back(damaged);
    f.companion.update({});
    f.usage.update({});
    TEST_ASSERT_TRUE(f.companion.hasLiveCompanion());
    TEST_ASSERT_EQUAL_UINT8(63, f.usage.snapshot().providers[0].metrics[0].remainingPercent);
}

void test_gauge_priority_and_temporary_focus() {
    Fixture f;
    f.ready();
    f.respond(63, true);
    TEST_ASSERT_EQUAL_STRING("ai-usage", f.indicator.resolved().owner.c_str());
    auto pomodoro =
        f.indicator.acquire("pomodoro", services::IndicatorPriority::BackgroundApplication);
    services::IndicatorFrame frame{};
    frame.pixels[0] = {1, 2, 3};
    pomodoro.setFrame(frame);
    f.indicator.update();
    TEST_ASSERT_EQUAL_STRING("pomodoro", f.indicator.resolved().owner.c_str());
    f.gauge.focus(1);
    f.indicator.update();
    TEST_ASSERT_EQUAL_STRING("ai-usage", f.indicator.resolved().owner.c_str());
    f.gauge.update(std::chrono::seconds(3));
    f.indicator.update();
    TEST_ASSERT_EQUAL_STRING("pomodoro", f.indicator.resolved().owner.c_str());
    auto gallery =
        f.indicator.acquire("led-gallery", services::IndicatorPriority::ForegroundApplication);
    gallery.setFrame(frame);
    f.indicator.update();
    TEST_ASSERT_EQUAL_STRING("led-gallery", f.indicator.resolved().owner.c_str());
    gallery.release();
    pomodoro.release();
    f.indicator.update();
    TEST_ASSERT_EQUAL_STRING("ai-usage", f.indicator.resolved().owner.c_str());
    TEST_ASSERT_TRUE(f.indicator.maximumBrightnessPercent() <= 10);
}

void test_up_and_down_select_different_puzzle_metrics() {
    Fixture f;
    f.ready();
    f.respond(4, true);
    Display display;
    apps::AiUsageApp app(f.usage, f.gauge, display);
    app.onActivate();
    app.update({}, {});
    const core::InputEvent up{core::InputEventType::NamedKey, 0, core::NamedKey::Up, {}};
    const core::InputEvent down{core::InputEventType::NamedKey, 0, core::NamedKey::Down, {}};
    app.update({up}, {});
    f.indicator.update();
    TEST_ASSERT_TRUE(lit(f.indicator.resolved().frame, 0, 64) > 50);
    app.update({down}, {});
    f.indicator.update();
    TEST_ASSERT_TRUE(lit(f.indicator.resolved().frame, 0, 64) < 10);
}

void test_new_host_replaces_old_provider_set() {
    Fixture f;
    f.ready();
    f.respond(5, true);
    TEST_ASSERT_EQUAL_UINT8(2, f.usage.snapshot().providerCount);
    f.transport.transportState = CompanionTransportState::Unavailable;
    f.companion.update({});
    f.usage.update({});
    TEST_ASSERT_EQUAL_UINT8(0, f.usage.snapshot().providerCount);
    f.ready();
    f.respond(70);
    TEST_ASSERT_EQUAL_UINT8(1, f.usage.snapshot().providerCount);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(AiProvider::Codex),
                            static_cast<unsigned>(f.usage.snapshot().providers[0].provider));
    TEST_ASSERT_EQUAL_UINT8(70, f.usage.snapshot().providers[0].metrics[0].remainingPercent);
}

void test_gauge_does_not_warn_when_second_metric_changes_provider() {
    Fixture f;
    f.ready();
    CompanionAiUsage value{};
    value.state = AiUsageState::Ready;
    value.providerCount = 1;
    auto& codex = value.providers[0];
    codex.provider = AiProvider::Codex;
    codex.plan = AiPlan::Plus;
    codex.metricCount = 2;
    codex.metrics[0].kind = AiMetricKind::FiveHour;
    codex.metrics[0].limit = 100;
    codex.metrics[0].remaining = 80;
    codex.metrics[0].remainingPercent = 80;
    codex.metrics[1].kind = AiMetricKind::Week;
    codex.metrics[1].limit = 100;
    codex.metrics[1].remaining = 70;
    codex.metrics[1].remainingPercent = 70;
    f.respondValue(value);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(services::IndicatorPriority::Idle),
                            static_cast<unsigned>(f.indicator.resolved().priority));

    f.usage.update(std::chrono::seconds(30));
    value.providerCount = 2;
    auto& cursor = value.providers[1];
    cursor.provider = AiProvider::Cursor;
    cursor.plan = AiPlan::Enterprise;
    cursor.metricCount = 1;
    cursor.metrics[0].kind = AiMetricKind::Money;
    cursor.metrics[0].unit = AiMetricUnit::Cents;
    cursor.metrics[0].limit = 100;
    cursor.metrics[0].used = 96;
    cursor.metrics[0].remaining = 4;
    cursor.metrics[0].remainingPercent = 4;
    f.respondValue(value);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(services::IndicatorPriority::Idle),
                            static_cast<unsigned>(f.indicator.resolved().priority));
}

void test_gauge_warns_when_same_metric_crosses_critical_threshold() {
    Fixture f;
    f.ready();
    f.respond(30);
    f.usage.update(std::chrono::seconds(30));
    f.respond(4);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(services::IndicatorPriority::Warning),
                            static_cast<unsigned>(f.indicator.resolved().priority));
}

void test_low_and_reset_feedback_respect_pomodoro_and_gallery_priority() {
    Fixture f;
    f.ready();
    f.respond(30);
    services::IndicatorFrame frame{};
    frame.pixels[0] = {1, 2, 3};
    auto pomodoro =
        f.indicator.acquire("pomodoro", services::IndicatorPriority::BackgroundApplication);
    auto gallery =
        f.indicator.acquire("led-gallery", services::IndicatorPriority::ForegroundApplication);
    pomodoro.setFrame(frame);
    gallery.setFrame(frame);
    f.indicator.update();

    f.usage.update(std::chrono::seconds(30));
    f.respond(15);
    TEST_ASSERT_EQUAL_STRING("led-gallery", f.indicator.resolved().owner.c_str());
    f.gauge.update(std::chrono::milliseconds(300));
    auto value = f.usage.snapshot();
    value.providers[0].metrics[0].resetAt = 123;
    f.usage.update(std::chrono::seconds(30));
    f.respondValue(value);
    value.providers[0].metrics[0].resetAt = 456;
    value.providers[0].metrics[0].remaining = 70;
    value.providers[0].metrics[0].used = 30;
    value.providers[0].metrics[0].remainingPercent = 70;
    f.usage.update(std::chrono::seconds(30));
    f.respondValue(value);
    TEST_ASSERT_EQUAL_STRING("led-gallery", f.indicator.resolved().owner.c_str());
    gallery.release();
    f.indicator.update();
    TEST_ASSERT_EQUAL_STRING("pomodoro", f.indicator.resolved().owner.c_str());
}

void test_stale_label_does_not_overlap_single_metric_provider_titles() {
    for (const auto provider : {AiProvider::Codex, AiProvider::Cursor}) {
        Fixture f;
        f.ready();
        CompanionAiUsage value{};
        value.state = AiUsageState::Ready;
        value.providerCount = 1;
        auto& row = value.providers[0];
        row.provider = provider;
        row.plan = provider == AiProvider::Codex ? AiPlan::Business : AiPlan::Enterprise;
        row.freshness = AiFreshness::Stale;
        row.metricCount = 1;
        row.metrics[0].kind =
            provider == AiProvider::Codex ? AiMetricKind::Credits : AiMetricKind::Money;
        row.metrics[0].limit = 100;
        row.metrics[0].remaining = 63;
        row.metrics[0].remainingPercent = 63;
        f.respondValue(value);
        Display display;
        apps::AiUsageApp app(f.usage, f.gauge, display);
        app.onActivate();
        app.update({}, {});
        const auto marker = std::find(display.labels.begin(), display.labels.end(), "STALE");
        TEST_ASSERT_TRUE(marker != display.labels.end());
        const auto markerIndex = static_cast<std::size_t>(marker - display.labels.begin());
        TEST_ASSERT_GREATER_THAN_INT(28, display.positions[markerIndex].y);
        TEST_ASSERT_LESS_THAN_INT(47, display.positions[markerIndex].y + 8);
    }
}

void test_identical_poll_keeps_revision_freshness_and_countdown() {
    Fixture f;
    f.ready();
    f.respond(63);
    auto value = f.usage.snapshot();
    value.providers[0].metrics[0].resetAt = 1;
    value.providers[0].metrics[0].resetRemainingSeconds = 3600;
    f.usage.update(std::chrono::seconds(30));
    f.respondValue(value);
    Display display;
    apps::AiUsageApp app(f.usage, f.gauge, display);
    app.onActivate();
    app.update({}, std::chrono::seconds(60));
    const auto draws = display.draws;
    const auto revision = f.usage.revision();

    f.usage.update(std::chrono::seconds(30));
    value.providers[0].metrics[0].resetRemainingSeconds = 3570;
    f.respondValue(value);
    TEST_ASSERT_EQUAL_UINT32(revision, f.usage.revision());
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(AiFreshness::Fresh),
                            static_cast<unsigned>(f.usage.snapshot().providers[0].freshness));
    app.update({}, {});
    TEST_ASSERT_EQUAL_INT(draws, display.draws);
    display.labels.clear();
    f.usage.update(std::chrono::seconds(60));
    app.update({}, std::chrono::seconds(60));
    TEST_ASSERT_TRUE(std::find(display.labels.begin(), display.labels.end(), "RESET 0H 58M") !=
                     display.labels.end());

    f.usage.update(std::chrono::seconds(30));
    value.providers[0].metrics[0].remainingPercent = 62;
    value.providers[0].metrics[0].remaining = 62;
    value.providers[0].metrics[0].used = 38;
    f.respondValue(value);
    TEST_ASSERT_TRUE(f.usage.revision() > revision);
    const auto changedRevision = f.usage.revision();
    f.usage.update(std::chrono::seconds(90));
    TEST_ASSERT_TRUE(f.usage.revision() > changedRevision);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(AiFreshness::Stale),
                            static_cast<unsigned>(f.usage.snapshot().providers[0].freshness));
    const auto staleRevision = f.usage.revision();
    f.respondValue(value);
    TEST_ASSERT_TRUE(f.usage.revision() > staleRevision);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(AiFreshness::Fresh),
                            static_cast<unsigned>(f.usage.snapshot().providers[0].freshness));
}

void test_reset_countdown_does_not_rewind_on_cached_poll_or_percent_change() {
    Fixture f;
    f.ready();
    f.respond(1);
    auto value = f.usage.snapshot();
    value.providers[0].metrics[0].resetAt = 12345;
    value.providers[0].metrics[0].resetRemainingSeconds = 51 * 60;
    f.usage.update(std::chrono::seconds(30));
    f.respondValue(value);

    Display display;
    apps::AiUsageApp app(f.usage, f.gauge, display);
    app.onActivate();
    app.update({}, {});
    const auto unchangedRevision = f.usage.revision();
    for (int i = 0; i < 4; ++i) {
        f.usage.update(std::chrono::seconds(30));
        f.respondValue(value); // Companion can return the same cached sample.
        app.update({}, std::chrono::seconds(30));
    }
    TEST_ASSERT_EQUAL_UINT32(49 * 60,
                             f.usage.snapshot().providers[0].metrics[0].resetRemainingSeconds);
    TEST_ASSERT_EQUAL_UINT32(unchangedRevision, f.usage.revision());
    TEST_ASSERT_TRUE(std::find(display.labels.begin(), display.labels.end(), "RESET 0H 49M") !=
                     display.labels.end());

    value.providers[0].metrics[0].remainingPercent = 0;
    value.providers[0].metrics[0].remaining = 0;
    value.providers[0].metrics[0].used = 100;
    f.usage.update(std::chrono::seconds(30));
    f.respondValue(value); // Percent changed, but reset data is still cached.
    display.labels.clear();
    app.update({}, std::chrono::seconds(30));
    TEST_ASSERT_EQUAL_UINT32(48 * 60 + 30,
                             f.usage.snapshot().providers[0].metrics[0].resetRemainingSeconds);
    TEST_ASSERT_TRUE(std::find(display.labels.begin(), display.labels.end(), "RESET 0H 48M") !=
                     display.labels.end());

    app.onDeactivate();
    app.onActivate();
    display.labels.clear();
    app.update({}, {});
    TEST_ASSERT_TRUE(std::find(display.labels.begin(), display.labels.end(), "RESET 0H 48M") !=
                     display.labels.end());

    value.providers[0].metrics[0].resetRemainingSeconds = 44 * 60;
    f.usage.update(std::chrono::seconds(30));
    const auto beforeCorrection = f.usage.revision();
    f.respondValue(value);
    TEST_ASSERT_EQUAL_UINT32(44 * 60,
                             f.usage.snapshot().providers[0].metrics[0].resetRemainingSeconds);
    TEST_ASSERT_TRUE(f.usage.revision() > beforeCorrection);
    display.labels.clear();
    app.update({}, {});
    TEST_ASSERT_TRUE(std::find(display.labels.begin(), display.labels.end(), "RESET 0H 44M") !=
                     display.labels.end());

    value.providers[0].metrics[0].resetAt = 12346;
    value.providers[0].metrics[0].resetRemainingSeconds = 5 * 3600;
    f.usage.update(std::chrono::seconds(30));
    f.respondValue(value);
    TEST_ASSERT_EQUAL_UINT32(5 * 3600,
                             f.usage.snapshot().providers[0].metrics[0].resetRemainingSeconds);
}

void test_reset_text_distinguishes_unknown_from_known_zero() {
    Fixture f;
    f.ready();
    f.respond(63);
    auto value = f.usage.snapshot();
    Display display;
    apps::AiUsageApp app(f.usage, f.gauge, display);
    app.onActivate();
    app.update({}, {});
    TEST_ASSERT_TRUE(std::find(display.labels.begin(), display.labels.end(), "RESET --") !=
                     display.labels.end());

    f.usage.update(std::chrono::seconds(30));
    value.providers[0].metrics[0].resetAt = 1;
    value.providers[0].metrics[0].resetRemainingSeconds = 4 * 3600 + 58 * 60;
    f.respondValue(value);
    display.labels.clear();
    app.update({}, {});
    TEST_ASSERT_TRUE(std::find(display.labels.begin(), display.labels.end(), "RESET 4H 58M") !=
                     display.labels.end());

    f.usage.update(std::chrono::seconds(30));
    value.providers[0].metrics[0].resetAt = 2;
    value.providers[0].metrics[0].resetRemainingSeconds = 6 * 86400 + 23 * 3600;
    f.respondValue(value);
    display.labels.clear();
    app.update({}, {});
    TEST_ASSERT_TRUE(std::find(display.labels.begin(), display.labels.end(), "RESET 6D 23H") !=
                     display.labels.end());
}

void test_remaining_percent_is_right_aligned_in_all_layouts() {
    Fixture f;
    f.ready();
    f.respond(4);
    Display display;
    apps::AiUsageApp app(f.usage, f.gauge, display);
    app.onActivate();
    app.update({}, {});
    for (std::size_t i = 0; i < display.labels.size(); ++i) {
        if (display.labels[i].find("% LEFT") != std::string::npos)
            TEST_ASSERT_EQUAL_INT(232, display.positions[i].x +
                                           static_cast<int>(display.labels[i].size()) * 6);
    }

    f.usage.update(std::chrono::seconds(30));
    auto value = f.usage.snapshot();
    value.providerCount = 2;
    value.providers[0].metrics[0].remainingPercent = 96;
    value.providers[0].metrics[0].remaining = 96;
    value.providers[0].metrics[0].used = 4;
    auto& cursor = value.providers[1];
    cursor.provider = AiProvider::Cursor;
    cursor.metricCount = 1;
    cursor.metrics[0].kind = AiMetricKind::Money;
    cursor.metrics[0].unit = AiMetricUnit::Cents;
    cursor.metrics[0].limit = 100;
    cursor.metrics[0].remaining = 4;
    cursor.metrics[0].used = 96;
    cursor.metrics[0].remainingPercent = 4;
    f.respondValue(value);
    display.labels.clear();
    display.positions.clear();
    app.update({}, {});
    int count = 0;
    for (std::size_t i = 0; i < display.labels.size(); ++i) {
        if (display.labels[i].find("% LEFT") != std::string::npos) {
            TEST_ASSERT_EQUAL_INT(232, display.positions[i].x +
                                           static_cast<int>(display.labels[i].size()) * 6);
            ++count;
        }
    }
    TEST_ASSERT_EQUAL_INT(2, count);

    f.usage.update(std::chrono::seconds(30));
    value.providerCount = 1;
    value.providers[0].metricCount = 2;
    value.providers[0].metrics[1].kind = AiMetricKind::Week;
    value.providers[0].metrics[1].limit = 100;
    value.providers[0].metrics[1].remaining = 73;
    value.providers[0].metrics[1].used = 27;
    value.providers[0].metrics[1].remainingPercent = 73;
    f.respondValue(value);
    display.labels.clear();
    display.positions.clear();
    app.update({}, {});
    count = 0;
    for (std::size_t i = 0; i < display.labels.size(); ++i) {
        if (display.labels[i].find("% LEFT") != std::string::npos) {
            TEST_ASSERT_EQUAL_INT(232, display.positions[i].x +
                                           static_cast<int>(display.labels[i].size()) * 6);
            ++count;
        }
    }
    TEST_ASSERT_EQUAL_INT(2, count);
}

void test_home_then_work_replaces_provider_set_and_puzzle() {
    Fixture f;
    f.ready();
    f.respond(70);
    TEST_ASSERT_EQUAL_UINT8(1, f.usage.snapshot().providerCount);
    f.transport.transportState = CompanionTransportState::Unavailable;
    f.companion.update({});
    f.usage.update({});
    f.gauge.update({});
    f.indicator.update();
    TEST_ASSERT_FALSE(f.indicator.resolved().hasFrame);
    f.ready();
    f.respond(4, true);
    TEST_ASSERT_EQUAL_UINT8(2, f.usage.snapshot().providerCount);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(AiProvider::Cursor),
                            static_cast<unsigned>(f.usage.snapshot().providers[1].provider));
    TEST_ASSERT_EQUAL_STRING("ai-usage", f.indicator.resolved().owner.c_str());
}

void test_work_usage_renders_business_credits_and_cursor_spend() {
    Fixture f;
    f.ready();
    CompanionAiUsage value{};
    value.state = AiUsageState::Ready;
    value.providerCount = 2;
    auto& codex = value.providers[0];
    codex.provider = AiProvider::Codex;
    codex.plan = AiPlan::Business;
    codex.metricCount = 1;
    codex.metrics[0].kind = AiMetricKind::Credits;
    codex.metrics[0].unit = AiMetricUnit::Credits;
    codex.metrics[0].used = 19765;
    codex.metrics[0].limit = 20000;
    codex.metrics[0].remaining = 235;
    codex.metrics[0].remainingPercent = 1;
    auto& cursor = value.providers[1];
    cursor.provider = AiProvider::Cursor;
    cursor.plan = AiPlan::Enterprise;
    cursor.metricCount = 1;
    cursor.metrics[0].kind = AiMetricKind::Money;
    cursor.metrics[0].unit = AiMetricUnit::Cents;
    cursor.metrics[0].used = 9458;
    cursor.metrics[0].limit = 255000;
    cursor.metrics[0].remaining = 245542;
    cursor.metrics[0].remainingPercent = 96;
    f.respondValue(value);

    Display display;
    apps::AiUsageApp app(f.usage, f.gauge, display);
    app.onActivate();
    app.update({}, {});
    TEST_ASSERT_TRUE(std::find(display.labels.begin(), display.labels.end(),
                               "19,765 / 20,000 CR") != display.labels.end());
    TEST_ASSERT_TRUE(std::find(display.labels.begin(), display.labels.end(),
                               "$94.58 / $2,550.00") != display.labels.end());
    TEST_ASSERT_TRUE(std::find(display.labels.begin(), display.labels.end(), "1% LEFT !") !=
                     display.labels.end());
    TEST_ASSERT_TRUE(std::find(display.labels.begin(), display.labels.end(), "96% LEFT") !=
                     display.labels.end());
}

void test_plus_reset_details_navigation_and_session_clear() {
    Fixture f;
    f.ready(4);
    CompanionAiUsage value{};
    value.state = AiUsageState::Ready;
    value.providerCount = 1;
    auto& provider = value.providers[0];
    provider.plan = AiPlan::Plus;
    provider.metricCount = 2;
    for (int i = 0; i < 2; ++i) {
        auto& metric = provider.metrics[i];
        metric.kind = i == 0 ? AiMetricKind::FiveHour : AiMetricKind::Week;
        metric.limit = 100;
        metric.used = i == 0 ? 37 : 19;
        metric.remaining = 100 - metric.used;
        metric.remainingPercent = static_cast<std::uint8_t>(metric.remaining);
    }
    auto& resets = provider.resetCredits;
    resets.known = true;
    resets.availableCount = 3;
    resets.creditCount = 3;
    for (int i = 0; i < 3; ++i) {
        std::snprintf(resets.credits[i].title.data(), resets.credits[i].title.size(), "CREDIT %d",
                      i + 1);
        resets.credits[i].expiresAt = 100 + i;
        resets.credits[i].expiresRemainingSeconds = 86400 * (i + 1);
    }
    f.respondValue(value);
    Display display;
    apps::AiUsageApp app(f.usage, f.gauge, display);
    app.onActivate();
    app.update({}, {});
    TEST_ASSERT_TRUE(std::find(display.labels.begin(), display.labels.end(), "R") !=
                     display.labels.end());
    TEST_ASSERT_TRUE(std::find(display.labels.begin(), display.labels.end(), "3") !=
                     display.labels.end());
    const core::InputEvent enter{core::InputEventType::NamedKey, 0, core::NamedKey::Enter, {}};
    const core::InputEvent right{core::InputEventType::NamedKey, 0, core::NamedKey::Right, {}};
    const core::InputEvent down{core::InputEventType::NamedKey, 0, core::NamedKey::Down, {}};
    display.labels.clear();
    app.update({enter}, {});
    TEST_ASSERT_TRUE(std::find(display.labels.begin(), display.labels.end(), "USED") !=
                     display.labels.end());
    TEST_ASSERT_TRUE(std::find(display.labels.begin(), display.labels.end(), "STALE") ==
                     display.labels.end());
    f.usage.update(std::chrono::seconds(30));
    provider.freshness = AiFreshness::Stale;
    f.respondValue(value);
    display.labels.clear();
    app.update({}, {});
    TEST_ASSERT_TRUE(std::find(display.labels.begin(), display.labels.end(), "STALE") !=
                     display.labels.end());
    display.labels.clear();
    app.update({right}, {});
    TEST_ASSERT_TRUE(std::find(display.labels.begin(), display.labels.end(), "#1  CREDIT 1") !=
                     display.labels.end());
    TEST_ASSERT_TRUE(std::find(display.labels.begin(), display.labels.end(), "STALE") !=
                     display.labels.end());
    display.labels.clear();
    app.update({down}, {});
    TEST_ASSERT_TRUE(std::find(display.labels.begin(), display.labels.end(), "#3  CREDIT 3") !=
                     display.labels.end());
    display.labels.clear();
    app.update({enter}, {});
    TEST_ASSERT_TRUE(std::find(display.labels.begin(), display.labels.end(), "63% LEFT") !=
                     display.labels.end());
    f.usage.update(std::chrono::seconds(30));
    value.providerCount = 2;
    auto& cursor = value.providers[1];
    cursor.provider = AiProvider::Cursor;
    cursor.plan = AiPlan::Enterprise;
    cursor.metricCount = 1;
    cursor.metrics[0].kind = AiMetricKind::Money;
    cursor.metrics[0].unit = AiMetricUnit::Cents;
    cursor.metrics[0].limit = 100;
    cursor.metrics[0].remaining = 80;
    cursor.metrics[0].used = 20;
    cursor.metrics[0].remainingPercent = 80;
    resets.availableCount = 0;
    resets.creditCount = 0;
    f.respondValue(value);
    display.labels.clear();
    app.update({}, {});
    TEST_ASSERT_TRUE(std::find(display.labels.begin(), display.labels.end(), "R") !=
                     display.labels.end());
    TEST_ASSERT_TRUE(std::find(display.labels.begin(), display.labels.end(), "0") !=
                     display.labels.end());
    f.usage.update(std::chrono::seconds(30));
    resets.known = false;
    f.respondValue(value);
    display.labels.clear();
    app.update({}, {});
    TEST_ASSERT_TRUE(std::find(display.labels.begin(), display.labels.end(), "R") ==
                     display.labels.end());
    f.transport.transportState = CompanionTransportState::Unavailable;
    f.companion.update({});
    f.usage.update({});
    app.update({}, {});
    TEST_ASSERT_EQUAL_UINT8(0, f.usage.snapshot().providerCount);
}

void test_plus_reset_expiry_formats_short_intervals() {
    struct Case {
        std::uint32_t expiresAt;
        std::uint32_t seconds;
        const char* expected;
    };
    const Case cases[] = {{1, 49 * 3600, "EXP IN 2D"},
                          {1, 24 * 3600, "EXP IN 1D"},
                          {1, 23 * 3600, "EXP IN 23H"},
                          {1, 3600, "EXP IN 1H"},
                          {1, 59 * 60, "EXP IN 59M"},
                          {1, 60, "EXP IN 1M"},
                          {1, 0, "EXP NOW"},
                          {0, 0, "EXP --"}};
    for (const auto& item : cases) {
        Fixture f;
        f.ready(4);
        CompanionAiUsage value{};
        value.state = AiUsageState::Ready;
        value.providerCount = 1;
        auto& provider = value.providers[0];
        provider.plan = AiPlan::Plus;
        provider.metricCount = 2;
        provider.metrics[0].limit = 100;
        provider.metrics[0].remaining = 100;
        provider.metrics[1].kind = AiMetricKind::Week;
        provider.metrics[1].limit = 100;
        provider.metrics[1].remaining = 100;
        provider.resetCredits.known = true;
        provider.resetCredits.availableCount = 1;
        provider.resetCredits.creditCount = 1;
        auto& credit = provider.resetCredits.credits[0];
        std::memcpy(credit.title.data(), "RESET", 5);
        credit.expiresAt = item.expiresAt;
        credit.expiresRemainingSeconds = item.seconds;
        f.respondValue(value);
        Display display;
        apps::AiUsageApp app(f.usage, f.gauge, display);
        app.onActivate();
        const core::InputEvent enter{core::InputEventType::NamedKey, 0, core::NamedKey::Enter, {}};
        const core::InputEvent right{core::InputEventType::NamedKey, 0, core::NamedKey::Right, {}};
        app.update({enter, right}, {});
        TEST_ASSERT_TRUE(std::find(display.labels.begin(), display.labels.end(), item.expected) !=
                         display.labels.end());
    }
}

void test_expanded_resets_clear_on_real_session_switch() {
    Fixture f;
    f.ready(4);
    const auto homeSession = f.companion.session();
    CompanionAiUsage home{};
    home.state = AiUsageState::Ready;
    home.providerCount = 1;
    auto& homeCodex = home.providers[0];
    homeCodex.plan = AiPlan::Plus;
    homeCodex.metricCount = 2;
    for (auto& metric : homeCodex.metrics) {
        metric.limit = 100;
        metric.remaining = 100;
    }
    homeCodex.metrics[1].kind = AiMetricKind::Week;
    homeCodex.resetCredits.known = true;
    homeCodex.resetCredits.availableCount = 2;
    homeCodex.resetCredits.creditCount = 2;
    std::memcpy(homeCodex.resetCredits.credits[0].title.data(), "OLD A", 5);
    std::memcpy(homeCodex.resetCredits.credits[1].title.data(), "OLD B", 5);
    f.respondValue(home);
    Display display;
    apps::AiUsageApp app(f.usage, f.gauge, display);
    app.onActivate();
    const core::InputEvent enter{core::InputEventType::NamedKey, 0, core::NamedKey::Enter, {}};
    const core::InputEvent right{core::InputEventType::NamedKey, 0, core::NamedKey::Right, {}};
    app.update({enter, right}, {});
    TEST_ASSERT_TRUE(std::find(display.labels.begin(), display.labels.end(), "#1  OLD A") !=
                     display.labels.end());

    f.ready(4);
    TEST_ASSERT_NOT_EQUAL(homeSession, f.companion.session());
    CompanionAiUsage work{};
    work.state = AiUsageState::Ready;
    work.providerCount = 2;
    work.providers[0].plan = AiPlan::Business;
    work.providers[0].metricCount = 1;
    work.providers[0].metrics[0].limit = 100;
    work.providers[0].metrics[0].remaining = 100;
    work.providers[1].provider = AiProvider::Cursor;
    work.providers[1].plan = AiPlan::Enterprise;
    work.providers[1].metricCount = 1;
    work.providers[1].metrics[0].kind = AiMetricKind::Money;
    work.providers[1].metrics[0].unit = AiMetricUnit::Cents;
    work.providers[1].metrics[0].limit = 100;
    work.providers[1].metrics[0].remaining = 100;
    f.respondValue(work);
    display.labels.clear();
    app.update({}, {});
    TEST_ASSERT_TRUE(std::find(display.labels.begin(), display.labels.end(), "RESET CREDITS") ==
                     display.labels.end());
    TEST_ASSERT_TRUE(std::find(display.labels.begin(), display.labels.end(), "#1  OLD A") ==
                     display.labels.end());
    TEST_ASSERT_TRUE(std::find(display.labels.begin(), display.labels.end(), "R") ==
                     display.labels.end());

    const auto workSession = f.companion.session();
    f.ready(4);
    TEST_ASSERT_NOT_EQUAL(workSession, f.companion.session());
    homeCodex.resetCredits.availableCount = 1;
    homeCodex.resetCredits.creditCount = 1;
    std::memcpy(homeCodex.resetCredits.credits[0].title.data(), "NEW", 4);
    f.respondValue(home);
    display.labels.clear();
    app.update({}, {});
    TEST_ASSERT_TRUE(std::find(display.labels.begin(), display.labels.end(), "R") !=
                     display.labels.end());
    TEST_ASSERT_TRUE(std::find(display.labels.begin(), display.labels.end(), "1") !=
                     display.labels.end());
    TEST_ASSERT_TRUE(std::find(display.labels.begin(), display.labels.end(), "2") ==
                     display.labels.end());
    TEST_ASSERT_TRUE(std::find(display.labels.begin(), display.labels.end(), "RESET CREDITS") ==
                     display.labels.end());
    display.labels.clear();
    app.update({enter, right}, {});
    TEST_ASSERT_TRUE(std::find(display.labels.begin(), display.labels.end(), "#1  NEW") !=
                     display.labels.end());
    TEST_ASSERT_TRUE(std::find(display.labels.begin(), display.labels.end(), "#1  OLD A") ==
                     display.labels.end());
}
} // namespace

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_service_polls_cached_snapshot_and_clears_on_session_change);
    RUN_TEST(test_split_gauge_marks_both_ends_with_purple_and_30_quota_pixels_per_half);
    RUN_TEST(test_app_draws_remaining_quota_only_on_change);
    RUN_TEST(test_provider_title_uses_font_safe_text_and_drawn_dot);
    RUN_TEST(test_malformed_ai_response_retains_previous_snapshot);
    RUN_TEST(test_gauge_priority_and_temporary_focus);
    RUN_TEST(test_up_and_down_select_different_puzzle_metrics);
    RUN_TEST(test_new_host_replaces_old_provider_set);
    RUN_TEST(test_gauge_does_not_warn_when_second_metric_changes_provider);
    RUN_TEST(test_gauge_warns_when_same_metric_crosses_critical_threshold);
    RUN_TEST(test_low_and_reset_feedback_respect_pomodoro_and_gallery_priority);
    RUN_TEST(test_stale_label_does_not_overlap_single_metric_provider_titles);
    RUN_TEST(test_identical_poll_keeps_revision_freshness_and_countdown);
    RUN_TEST(test_reset_countdown_does_not_rewind_on_cached_poll_or_percent_change);
    RUN_TEST(test_reset_text_distinguishes_unknown_from_known_zero);
    RUN_TEST(test_remaining_percent_is_right_aligned_in_all_layouts);
    RUN_TEST(test_home_then_work_replaces_provider_set_and_puzzle);
    RUN_TEST(test_work_usage_renders_business_credits_and_cursor_spend);
    RUN_TEST(test_plus_reset_details_navigation_and_session_clear);
    RUN_TEST(test_plus_reset_expiry_formats_short_intervals);
    RUN_TEST(test_expanded_resets_clear_on_real_session_switch);
    return UNITY_END();
}
