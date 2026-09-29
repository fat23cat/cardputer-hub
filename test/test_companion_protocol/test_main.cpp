#include <unity.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "companion/companion_fixtures.h"
#include "connectivity/companion/companion_fingerprint.h"
#include "connectivity/companion/companion_protocol.h"

namespace {

using namespace cardputer_hub::connectivity;

std::vector<std::uint8_t> loadFixture(const char* name) {
    const auto* fixture = cardputer_hub::companion_fixtures::find(name);
    TEST_ASSERT_NOT_NULL(fixture);
    return std::vector<std::uint8_t>(fixture->bytes, fixture->bytes + fixture->size);
}

CompanionEnvelope decodeFixture(const char* name) {
    const auto bytes = loadFixture(name);
    const auto decoded = decodeCompanionMessage(bytes.data(), bytes.size());
    TEST_ASSERT_TRUE(decoded.has_value());
    return *decoded;
}

void assertEncodedMatchesFixture(const CompanionEnvelope& message, const char* name) {
    const auto encoded = encodeCompanionMessage(message);
    TEST_ASSERT_TRUE(encoded.has_value());
    const auto fixture = loadFixture(name);
    TEST_ASSERT_EQUAL_UINT(fixture.size(), encoded->size);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(fixture.data(), encoded->bytes.data(), fixture.size());
}

CompanionHello helloOf(const std::array<std::uint8_t, companionFingerprintSize>& fingerprint,
                       const char* buildId) {
    CompanionHello hello{};
    hello.fingerprint = fingerprint;
    std::strncpy(hello.buildId.data(), buildId, hello.buildId.size() - 1);
    return hello;
}

void test_hello_carries_fingerprint_and_build_id() {
    const auto hello = makeHello(helloOf(companionProtocolFingerprint, "2026-09-29 abc1234"));
    TEST_ASSERT_TRUE(hello.has_value());
    assertEncodedMatchesFixture(*hello, "hello.bin");
    CompanionHello read{};
    TEST_ASSERT_TRUE(readHello(decodeFixture("hello.bin"), read));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(companionProtocolFingerprint.data(), read.fingerprint.data(),
                                  companionFingerprintSize);
    TEST_ASSERT_EQUAL_STRING("2026-09-29 abc1234", read.buildId.data());

    const auto accepted = makeHelloAck(42, CompanionStatus::Ok,
                                       helloOf(companionProtocolFingerprint, "2026-09-29 abc1234"));
    TEST_ASSERT_TRUE(accepted.has_value());
    assertEncodedMatchesFixture(*accepted, "hello-ack.bin");
    const std::array<std::uint8_t, 8> other{0xFF, 0xEE, 0xDD, 0xCC, 0xBB, 0xAA, 0x99, 0x88};
    const auto mismatch =
        makeHelloAck(0, CompanionStatus::Unsupported, helloOf(other, "2026-09-29 abc1234"));
    TEST_ASSERT_TRUE(mismatch.has_value());
    assertEncodedMatchesFixture(*mismatch, "hello-ack-mismatch.bin");

    // An accepted ACK needs a session; a mismatch ACK must not have one.
    TEST_ASSERT_FALSE(makeHelloAck(0, CompanionStatus::Ok, helloOf(other, "x")).has_value());
    TEST_ASSERT_FALSE(
        makeHelloAck(7, CompanionStatus::Unsupported, helloOf(other, "x")).has_value());
    // Build ids are 1-24 printable ASCII bytes.
    TEST_ASSERT_FALSE(makeHello(helloOf(other, "")).has_value());
    TEST_ASSERT_FALSE(makeHello(helloOf(other, "2026-09-29 caf\xC3\xA9")).has_value());
    TEST_ASSERT_TRUE(makeHello(helloOf(other, "123456789012345678901234")).has_value());
    auto longHello = helloOf(other, "x");
    std::memset(longHello.buildId.data(), 'a', longHello.buildId.size());
    TEST_ASSERT_FALSE(makeHello(longHello).has_value());
}

void test_legacy_frames_are_recognised_not_decoded() {
    const auto legacy = loadFixture("legacy-hello.bin");
    TEST_ASSERT_FALSE(decodeCompanionMessage(legacy.data(), legacy.size()).has_value());
    TEST_ASSERT_TRUE(isLegacyCompanionFrame(legacy.data(), legacy.size()));
    TEST_ASSERT_TRUE(isLegacyCompanionHello(legacy.data(), legacy.size()));
    auto legacyPing = loadFixture("ping-request.bin");
    legacyPing[0] = 5;
    TEST_ASSERT_TRUE(isLegacyCompanionFrame(legacyPing.data(), legacyPing.size()));
    TEST_ASSERT_FALSE(isLegacyCompanionHello(legacyPing.data(), legacyPing.size()));
    const auto current = loadFixture("hello.bin");
    TEST_ASSERT_FALSE(isLegacyCompanionFrame(current.data(), current.size()));
    const auto unknown = loadFixture("unknown-marker.bin");
    TEST_ASSERT_FALSE(isLegacyCompanionFrame(unknown.data(), unknown.size()));
    TEST_ASSERT_FALSE(decodeCompanionMessage(unknown.data(), unknown.size()).has_value());
}

void test_ping_and_application_messages_match_fixtures() {
    auto ping = makeRequest(42, 1, CompanionOperation::Ping);
    const std::array<std::uint8_t, companionPingTokenSize> token{1, 2, 3, 4};
    TEST_ASSERT_TRUE(setPingToken(ping, token));
    assertEncodedMatchesFixture(ping, "ping-request.bin");
    auto pong = makeResponse(42, 1, CompanionOperation::Ping, CompanionStatus::Ok);
    TEST_ASSERT_TRUE(setPingToken(pong, token));
    assertEncodedMatchesFixture(pong, "ping-response.bin");
    std::array<std::uint8_t, companionPingTokenSize> read{};
    TEST_ASSERT_TRUE(readPingToken(decodeFixture("ping-response.bin"), read));
    TEST_ASSERT_EQUAL_UINT8(4, read[3]);

    assertEncodedMatchesFixture(makeRequest(42, 3, CompanionOperation::AppActive),
                                "app-active-request.bin");
    auto active = makeResponse(42, 3, CompanionOperation::AppActive, CompanionStatus::Ok);
    TEST_ASSERT_TRUE(setBundleIdentifier(active, "dev.zed.Zed"));
    assertEncodedMatchesFixture(active, "app-active-response.bin");
    auto activate = makeRequest(42, 4, CompanionOperation::AppActivate);
    TEST_ASSERT_TRUE(setBundleIdentifier(activate, "org.telegram.desktop"));
    assertEncodedMatchesFixture(activate, "app-activate-request.bin");
    assertEncodedMatchesFixture(
        makeResponse(42, 4, CompanionOperation::AppActivate, CompanionStatus::Ok),
        "app-activate-response.bin");
    auto changed = makeEvent(42, CompanionOperation::AppActiveChanged);
    TEST_ASSERT_TRUE(setBundleIdentifier(changed, "dev.zed.Zed"));
    assertEncodedMatchesFixture(changed, "app-active-changed-event.bin");
    TEST_ASSERT_EQUAL_UINT16(99, decodeFixture("wrong-session.bin").session);
}

void test_codec_rejects_malformed_frames_and_semantics() {
    for (const char* name : {"malformed-length.bin", "unknown-operation.bin"}) {
        const auto bytes = loadFixture(name);
        TEST_ASSERT_FALSE(decodeCompanionMessage(bytes.data(), bytes.size()).has_value());
    }
    auto capabilities = loadFixture("app-active-request.bin");
    capabilities[5] = 2; // CAPABILITIES no longer exists
    TEST_ASSERT_FALSE(decodeCompanionMessage(capabilities.data(), capabilities.size()).has_value());
    auto badStatus = loadFixture("ping-response.bin");
    badStatus[6] = 9;
    TEST_ASSERT_FALSE(decodeCompanionMessage(badStatus.data(), badStatus.size()).has_value());
    auto zeroRequestId = loadFixture("ping-request.bin");
    zeroRequestId[4] = 0;
    TEST_ASSERT_FALSE(
        decodeCompanionMessage(zeroRequestId.data(), zeroRequestId.size()).has_value());
    auto activeRequest = loadFixture("app-active-request.bin");
    activeRequest[7] = 1;
    activeRequest.push_back(1);
    TEST_ASSERT_FALSE(
        decodeCompanionMessage(activeRequest.data(), activeRequest.size()).has_value());
    auto helloWithOperation = loadFixture("hello.bin");
    helloWithOperation[5] = static_cast<std::uint8_t>(CompanionOperation::AppActivate);
    TEST_ASSERT_FALSE(
        decodeCompanionMessage(helloWithOperation.data(), helloWithOperation.size()).has_value());
    auto shortHello = loadFixture("hello.bin");
    shortHello[7] = static_cast<std::uint8_t>(shortHello[7] - 1);
    shortHello.pop_back();
    TEST_ASSERT_FALSE(decodeCompanionMessage(shortHello.data(), shortHello.size()).has_value());
}

void test_bundle_identifiers_are_bounded_utf8() {
    TEST_ASSERT_FALSE(isUtf8BundleIdentifier(""));
    TEST_ASSERT_FALSE(isUtf8BundleIdentifier(std::string(companionMaxBundleIdSize + 1, 'a')));
    TEST_ASSERT_FALSE(isUtf8BundleIdentifier(std::string("a\0b", 3)));
    TEST_ASSERT_FALSE(isUtf8BundleIdentifier(std::string("\xE0\x80\xAF", 3)));
    TEST_ASSERT_FALSE(isUtf8BundleIdentifier(std::string("\xED\xA0\x80", 3)));
    TEST_ASSERT_TRUE(isUtf8BundleIdentifier(std::string("caf\xC3\xA9", 5)));
    TEST_ASSERT_TRUE(isUtf8BundleIdentifier(std::string("\xF0\x9F\x98\x80", 4)));
    auto request = makeRequest(42, 4, CompanionOperation::AppActivate);
    TEST_ASSERT_FALSE(setBundleIdentifier(request, std::string(companionMaxBundleIdSize + 1, 'a')));
    TEST_ASSERT_TRUE(setBundleIdentifier(request, std::string(companionMaxBundleIdSize, 'a')));
}

void test_system_metrics_round_trip_and_bounds() {
    assertEncodedMatchesFixture(makeRequest(42, 5, CompanionOperation::SystemMetrics),
                                "system-metrics-request.bin");
    CompanionSystemMetrics metrics{};
    metrics.validity = 0x1ff;
    metrics.cpuPercent = 34;
    metrics.memoryUsedMiB = 11500;
    metrics.memoryTotalMiB = 16384;
    metrics.memoryPressure = 1;
    metrics.diskUsedPercent = 63;
    metrics.batteryPercent = 82;
    metrics.thermalState = 2;
    metrics.downloadKiBps = 12698;
    metrics.uploadKiBps = 1843;
    metrics.powerSource = 2;
    metrics.batteryMinutes = 102;
    auto response = makeResponse(42, 5, CompanionOperation::SystemMetrics, CompanionStatus::Ok);
    TEST_ASSERT_TRUE(setSystemMetrics(response, metrics));
    assertEncodedMatchesFixture(response, "system-metrics-response.bin");
    CompanionSystemMetrics read{};
    TEST_ASSERT_TRUE(readSystemMetrics(decodeFixture("system-metrics-response.bin"), read));
    TEST_ASSERT_EQUAL_UINT8(34, read.cpuPercent);
    TEST_ASSERT_EQUAL_UINT16(102, read.batteryMinutes);
    auto bad = loadFixture("system-metrics-response.bin");
    bad[companionEnvelopeSize + 2] = 101; // CPU above 100%
    TEST_ASSERT_FALSE(decodeCompanionMessage(bad.data(), bad.size()).has_value());
    bad = loadFixture("system-metrics-response.bin");
    bad[companionEnvelopeSize + 23] = 4; // unknown power source
    TEST_ASSERT_FALSE(decodeCompanionMessage(bad.data(), bad.size()).has_value());
    bad = loadFixture("system-metrics-response.bin");
    bad[companionEnvelopeSize + 1] = 0x02; // validity bit 9
    TEST_ASSERT_FALSE(decodeCompanionMessage(bad.data(), bad.size()).has_value());
}

void test_ai_usage_round_trip_and_reset_credit_rules() {
    assertEncodedMatchesFixture(makeRequest(42, 7, CompanionOperation::AiUsage),
                                "ai-usage-request.bin");
    CompanionAiUsage usage{};
    TEST_ASSERT_TRUE(readAiUsage(decodeFixture("ai-usage-response.bin"), usage));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(AiProvider::Claude),
                            static_cast<unsigned>(usage.providers[0].provider));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(AiPlan::Pro),
                            static_cast<unsigned>(usage.providers[0].plan));
    auto response = makeResponse(42, 7, CompanionOperation::AiUsage, CompanionStatus::Ok);
    TEST_ASSERT_TRUE(setAiUsage(response, usage));
    assertEncodedMatchesFixture(response, "ai-usage-response.bin");

    // Reset details belong to Codex Plus only and never exceed the available count.
    auto& provider = usage.providers[0];
    provider.resetCredits.known = true;
    provider.resetCredits.availableCount = 1;
    provider.resetCredits.creditCount = 1;
    std::strcpy(provider.resetCredits.credits[0].title.data(), "FULL RESET");
    TEST_ASSERT_FALSE(setAiUsage(response, usage));
    provider.provider = AiProvider::Codex;
    provider.plan = AiPlan::Plus;
    TEST_ASSERT_TRUE(setAiUsage(response, usage));
    CompanionAiUsage codex{};
    TEST_ASSERT_TRUE(readAiUsage(response, codex));
    TEST_ASSERT_EQUAL_STRING("FULL RESET", codex.providers[0].resetCredits.credits[0].title.data());
    provider.resetCredits.creditCount = 2;
    TEST_ASSERT_FALSE(setAiUsage(response, usage));
    usage.providerCount = 2;
    usage.providers[1] = usage.providers[0];
    provider.resetCredits.creditCount = 1;
    TEST_ASSERT_FALSE(setAiUsage(response, usage)); // the same provider twice
}

void test_system_details_groups_round_trip_and_reject_bad_names() {
    auto request = makeRequest(42, 8, CompanionOperation::SystemDetails);
    TEST_ASSERT_TRUE(setSystemDetailsRequest(request, SystemDetailsGroup::Cpu));
    assertEncodedMatchesFixture(request, "system-details-request.bin");
    request.payload[0] = 5;
    TEST_ASSERT_FALSE(encodeCompanionMessage(request).has_value());

    const char* names[] = {"system-details-response-cpu.bin", "system-details-response-power.bin",
                           "system-details-response-network.bin",
                           "system-details-response-memory.bin"};
    for (unsigned index = 0; index < 4; ++index) {
        CompanionSystemDetails details{};
        TEST_ASSERT_TRUE(readSystemDetails(decodeFixture(names[index]), details));
        TEST_ASSERT_EQUAL_UINT8(index + 1, static_cast<unsigned>(details.group));
        auto response = makeResponse(42, 8, CompanionOperation::SystemDetails, CompanionStatus::Ok);
        TEST_ASSERT_TRUE(setSystemDetails(response, details));
        assertEncodedMatchesFixture(response, names[index]);
        if (index == 0) {
            TEST_ASSERT_EQUAL_UINT8(61, details.performancePercent);
            TEST_ASSERT_EQUAL_STRING("Google Chrome", details.processes[1].name.data());
        } else if (index == 1) {
            TEST_ASSERT_EQUAL_UINT16(142, details.systemDrawDeciwatts);
            TEST_ASSERT_EQUAL_STRING("Magic Mouse", details.peripheralName.data());
        } else if (index == 2) {
            TEST_ASSERT_EQUAL_INT8(-54, details.wifiRssiDbm);
            TEST_ASSERT_EQUAL_UINT16(866, details.wifiLinkMbps);
        } else {
            TEST_ASSERT_EQUAL_UINT32(14438, details.appMiB);
            TEST_ASSERT_EQUAL_UINT32(59392, details.diskWriteKiBps);
        }
    }
    const auto cpu = loadFixture("system-details-response-cpu.bin");
    auto damaged = cpu;
    damaged[companionEnvelopeSize + 9] = 0xC3; // non-ASCII name byte
    TEST_ASSERT_FALSE(decodeCompanionMessage(damaged.data(), damaged.size()).has_value());
    damaged = cpu;
    damaged[companionEnvelopeSize + 6] = 5; // more than four apps
    TEST_ASSERT_FALSE(decodeCompanionMessage(damaged.data(), damaged.size()).has_value());
    damaged = cpu;
    damaged[companionEnvelopeSize + 1] = 0x10; // no such CPU field
    TEST_ASSERT_FALSE(decodeCompanionMessage(damaged.data(), damaged.size()).has_value());
    const auto memory = loadFixture("system-details-response-memory.bin");
    damaged = memory;
    damaged[companionEnvelopeSize + 19] = 0xFF; // free above total
    damaged[companionEnvelopeSize + 20] = 0x0F;
    TEST_ASSERT_FALSE(decodeCompanionMessage(damaged.data(), damaged.size()).has_value());
    auto unavailable =
        makeResponse(42, 8, CompanionOperation::SystemDetails, CompanionStatus::NotAvailable);
    TEST_ASSERT_TRUE(encodeCompanionMessage(unavailable).has_value());
}

void test_only_telemetry_failures_are_isolated() {
    TEST_ASSERT_TRUE(companionResponseFailureIsIsolated(CompanionOperation::SystemMetrics));
    TEST_ASSERT_TRUE(companionResponseFailureIsIsolated(CompanionOperation::AiUsage));
    TEST_ASSERT_TRUE(companionResponseFailureIsIsolated(CompanionOperation::SystemDetails));
    TEST_ASSERT_FALSE(companionResponseFailureIsIsolated(CompanionOperation::AppActive));
    TEST_ASSERT_FALSE(companionResponseFailureIsIsolated(CompanionOperation::Ping));
}

} // namespace

void setUp() {}
void tearDown() {}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_hello_carries_fingerprint_and_build_id);
    RUN_TEST(test_legacy_frames_are_recognised_not_decoded);
    RUN_TEST(test_ping_and_application_messages_match_fixtures);
    RUN_TEST(test_codec_rejects_malformed_frames_and_semantics);
    RUN_TEST(test_bundle_identifiers_are_bounded_utf8);
    RUN_TEST(test_system_metrics_round_trip_and_bounds);
    RUN_TEST(test_ai_usage_round_trip_and_reset_credit_rules);
    RUN_TEST(test_system_details_groups_round_trip_and_reject_bad_names);
    RUN_TEST(test_only_telemetry_failures_are_isolated);
    return UNITY_END();
}
