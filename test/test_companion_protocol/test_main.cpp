#include <unity.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "companion/companion_fixtures.h"
#include "connectivity/companion/companion_protocol.h"

namespace {

using cardputer_hub::connectivity::CompanionCapability;
using cardputer_hub::connectivity::CompanionEnvelope;
using cardputer_hub::connectivity::companionEnvelopeSize;
using cardputer_hub::connectivity::CompanionKind;
using cardputer_hub::connectivity::companionMaxBundleIdSize;
using cardputer_hub::connectivity::CompanionOperation;
using cardputer_hub::connectivity::companionPingTokenSize;
using cardputer_hub::connectivity::CompanionStatus;
using cardputer_hub::connectivity::decodeCompanionMessage;
using cardputer_hub::connectivity::encodeCompanionMessage;
using cardputer_hub::connectivity::isUtf8BundleIdentifier;
using cardputer_hub::connectivity::makeEvent;
using cardputer_hub::connectivity::makeHello;
using cardputer_hub::connectivity::makeHelloAck;
using cardputer_hub::connectivity::makeRequest;
using cardputer_hub::connectivity::makeResponse;
using cardputer_hub::connectivity::readBundleIdentifier;
using cardputer_hub::connectivity::readCapabilityList;
using cardputer_hub::connectivity::readPingToken;
using cardputer_hub::connectivity::setBundleIdentifier;
using cardputer_hub::connectivity::setCapabilityList;
using cardputer_hub::connectivity::setPingToken;

std::vector<std::uint8_t> loadFixture(const char* name) {
    const auto* fixture = cardputer_hub::companion_fixtures::find(name);
    TEST_ASSERT_NOT_NULL(fixture);
    return std::vector<std::uint8_t>(fixture->bytes, fixture->bytes + fixture->size);
}

void assertEncodedMatchesFixture(const CompanionEnvelope& message, const char* name) {
    const auto encoded = encodeCompanionMessage(message);
    TEST_ASSERT_TRUE(encoded.has_value());
    const auto fixture = loadFixture(name);
    TEST_ASSERT_EQUAL_UINT(fixture.size(), encoded->size);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(fixture.data(), encoded->bytes.data(), fixture.size());
}

void test_hello_and_ack_match_fixtures() {
    const std::uint8_t versions[] = {1};
    assertEncodedMatchesFixture(makeHello(versions, 1), "hello-v1.bin");
    assertEncodedMatchesFixture(makeHelloAck(42, 1), "hello-ack-v1.bin");
}

void test_ping_request_and_response_match_fixtures() {
    const std::array<std::uint8_t, companionPingTokenSize> token{1, 2, 3, 4};
    auto request = makeRequest(42, 1, CompanionOperation::Ping);
    TEST_ASSERT_TRUE(setPingToken(request, token));
    assertEncodedMatchesFixture(request, "ping-request-v1.bin");

    auto response = makeResponse(42, 1, CompanionOperation::Ping, CompanionStatus::Ok);
    TEST_ASSERT_TRUE(setPingToken(response, token));
    assertEncodedMatchesFixture(response, "ping-response-v1.bin");
}

void test_capabilities_and_app_messages_match_fixtures() {
    assertEncodedMatchesFixture(makeRequest(42, 2, CompanionOperation::Capabilities),
                                "capabilities-request-v1.bin");
    auto capabilities = makeResponse(42, 2, CompanionOperation::Capabilities, CompanionStatus::Ok);
    const CompanionCapability ids[] = {CompanionCapability::AppActive,
                                       CompanionCapability::AppActivate,
                                       CompanionCapability::AppActiveEvents};
    TEST_ASSERT_TRUE(setCapabilityList(capabilities, ids, 3));
    assertEncodedMatchesFixture(capabilities, "capabilities-response-v1.bin");

    assertEncodedMatchesFixture(makeRequest(42, 3, CompanionOperation::AppActive),
                                "app-active-request-v1.bin");
    auto active = makeResponse(42, 3, CompanionOperation::AppActive, CompanionStatus::Ok);
    TEST_ASSERT_TRUE(setBundleIdentifier(active, "dev.zed.Zed"));
    assertEncodedMatchesFixture(active, "app-active-response-v1.bin");

    auto activate = makeRequest(42, 4, CompanionOperation::AppActivate);
    TEST_ASSERT_TRUE(setBundleIdentifier(activate, "org.telegram.desktop"));
    assertEncodedMatchesFixture(activate, "app-activate-request-v1.bin");
    assertEncodedMatchesFixture(
        makeResponse(42, 4, CompanionOperation::AppActivate, CompanionStatus::Ok),
        "app-activate-response-v1.bin");

    auto changed = makeEvent(42, CompanionOperation::AppActiveChanged);
    TEST_ASSERT_TRUE(setBundleIdentifier(changed, "dev.zed.Zed"));
    assertEncodedMatchesFixture(changed, "app-active-changed-event-v1.bin");
}

void test_fixtures_decode_to_expected_operations() {
    const auto hello = decodeCompanionMessage(loadFixture("hello-v1.bin").data(),
                                              loadFixture("hello-v1.bin").size());
    TEST_ASSERT_TRUE(hello.has_value());
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CompanionKind::Hello),
                            static_cast<unsigned>(hello->kind));

    const auto ping = loadFixture("ping-response-v1.bin");
    const auto decoded = decodeCompanionMessage(ping.data(), ping.size());
    TEST_ASSERT_TRUE(decoded.has_value());
    std::array<std::uint8_t, companionPingTokenSize> token{};
    TEST_ASSERT_TRUE(readPingToken(*decoded, token));
    TEST_ASSERT_EQUAL_UINT8(1, token[0]);
    TEST_ASSERT_EQUAL_UINT8(4, token[3]);
}

void test_codec_rejects_malformed_version_length_and_unknown_operation() {
    const auto malformed = loadFixture("malformed-length.bin");
    TEST_ASSERT_FALSE(decodeCompanionMessage(malformed.data(), malformed.size()).has_value());
    const auto version = loadFixture("unsupported-version.bin");
    TEST_ASSERT_FALSE(decodeCompanionMessage(version.data(), version.size()).has_value());
    const auto unknown = loadFixture("unknown-operation.bin");
    TEST_ASSERT_FALSE(decodeCompanionMessage(unknown.data(), unknown.size()).has_value());
    auto invalidStatus = loadFixture("ping-response-v1.bin");
    invalidStatus[6] = 9;
    TEST_ASSERT_FALSE(
        decodeCompanionMessage(invalidStatus.data(), invalidStatus.size()).has_value());
}

void test_codec_rejects_invalid_semantics_and_payloads() {
    auto helloActivate = loadFixture("hello-v1.bin");
    helloActivate[5] = static_cast<std::uint8_t>(CompanionOperation::AppActivate);
    TEST_ASSERT_FALSE(
        decodeCompanionMessage(helloActivate.data(), helloActivate.size()).has_value());

    auto requestStatus = loadFixture("capabilities-request-v1.bin");
    requestStatus[6] = static_cast<std::uint8_t>(CompanionStatus::NotFound);
    TEST_ASSERT_FALSE(
        decodeCompanionMessage(requestStatus.data(), requestStatus.size()).has_value());

    auto zeroRequestId = loadFixture("ping-request-v1.bin");
    zeroRequestId[4] = 0;
    TEST_ASSERT_FALSE(
        decodeCompanionMessage(zeroRequestId.data(), zeroRequestId.size()).has_value());

    auto shortPing = loadFixture("ping-request-v1.bin");
    shortPing[7] = 3;
    shortPing.resize(companionEnvelopeSize + 3);
    TEST_ASSERT_FALSE(decodeCompanionMessage(shortPing.data(), shortPing.size()).has_value());

    auto capsRequest = loadFixture("capabilities-request-v1.bin");
    capsRequest[7] = 1;
    capsRequest.push_back(0x01);
    TEST_ASSERT_FALSE(decodeCompanionMessage(capsRequest.data(), capsRequest.size()).has_value());

    auto activeRequest = loadFixture("app-active-request-v1.bin");
    activeRequest[7] = 1;
    activeRequest.push_back(0x01);
    TEST_ASSERT_FALSE(
        decodeCompanionMessage(activeRequest.data(), activeRequest.size()).has_value());

    const std::uint8_t version = 1;
    auto hello = makeHello(&version, 1);
    hello.operation = CompanionOperation::AppActivate;
    TEST_ASSERT_FALSE(encodeCompanionMessage(hello).has_value());
}

void test_wrong_session_fixture_is_structurally_valid() {
    const auto bytes = loadFixture("wrong-session.bin");
    const auto decoded = decodeCompanionMessage(bytes.data(), bytes.size());
    TEST_ASSERT_TRUE(decoded.has_value());
    TEST_ASSERT_EQUAL_UINT16(99, decoded->session);
}

void test_oversized_and_invalid_bundle_identifiers_are_rejected() {
    TEST_ASSERT_FALSE(isUtf8BundleIdentifier(""));
    TEST_ASSERT_FALSE(isUtf8BundleIdentifier(std::string(companionMaxBundleIdSize + 1, 'a')));
    TEST_ASSERT_FALSE(isUtf8BundleIdentifier(std::string("a\0b", 3)));
    TEST_ASSERT_FALSE(isUtf8BundleIdentifier(std::string("\xE0\x80\xAF", 3)));
    TEST_ASSERT_FALSE(isUtf8BundleIdentifier(std::string("\xED\xA0\x80", 3)));
    TEST_ASSERT_FALSE(isUtf8BundleIdentifier(std::string("\xF4\x90\x80\x80", 4)));
    TEST_ASSERT_TRUE(isUtf8BundleIdentifier(std::string("caf\xC3\xA9", 5)));
    TEST_ASSERT_TRUE(isUtf8BundleIdentifier(std::string("\xE2\x82\xAC", 3)));
    TEST_ASSERT_TRUE(isUtf8BundleIdentifier(std::string("\xF0\x9F\x98\x80", 4)));
    auto request = makeRequest(42, 4, CompanionOperation::AppActivate);
    TEST_ASSERT_FALSE(setBundleIdentifier(request, std::string(companionMaxBundleIdSize + 1, 'a')));
    TEST_ASSERT_TRUE(setBundleIdentifier(request, std::string(companionMaxBundleIdSize, 'a')));
}

void test_capabilities_round_trip_rejects_unknown_ids() {
    auto response = makeResponse(1, 1, CompanionOperation::Capabilities, CompanionStatus::Ok);
    const CompanionCapability ids[] = {CompanionCapability::AppActive};
    TEST_ASSERT_TRUE(setCapabilityList(response, ids, 1));
    CompanionCapability decoded[8]{};
    std::uint8_t count = 0;
    TEST_ASSERT_TRUE(readCapabilityList(response, decoded, count, 8));
    TEST_ASSERT_EQUAL_UINT8(1, count);
    response.payload[1] = 9;
    TEST_ASSERT_FALSE(readCapabilityList(response, decoded, count, 8));
}

void test_v2_metrics_round_trip_and_v1_rejection() {
    using cardputer_hub::connectivity::CompanionSystemMetrics;
    using cardputer_hub::connectivity::readSystemMetrics;
    using cardputer_hub::connectivity::setSystemMetrics;
    const std::uint8_t versions[] = {2, 1};
    TEST_ASSERT_TRUE(encodeCompanionMessage(makeHello(versions, 2)).has_value());
    TEST_ASSERT_TRUE(encodeCompanionMessage(makeHelloAck(42, 2)).has_value());
    assertEncodedMatchesFixture(makeHello(versions, 2), "hello-v2.bin");
    assertEncodedMatchesFixture(makeHelloAck(42, 2), "hello-ack-v2.bin");
    auto request = makeRequest(42, 1, CompanionOperation::SystemMetrics);
    TEST_ASSERT_FALSE(encodeCompanionMessage(request).has_value());
    request.version = 2;
    TEST_ASSERT_TRUE(encodeCompanionMessage(request).has_value());
    auto fixtureRequest = request;
    fixtureRequest.requestId = 5;
    assertEncodedMatchesFixture(fixtureRequest, "system-metrics-request-v2.bin");
    request.payloadSize = 1;
    TEST_ASSERT_FALSE(encodeCompanionMessage(request).has_value());

    auto response = makeResponse(42, 1, CompanionOperation::SystemMetrics, CompanionStatus::Ok);
    response.version = 2;
    CompanionSystemMetrics metrics{};
    metrics.validity = 0x7f;
    metrics.cpuPercent = 34;
    metrics.memoryUsedMiB = 11500;
    metrics.memoryTotalMiB = 16384;
    metrics.memoryPressure = 1;
    metrics.diskUsedPercent = 63;
    metrics.batteryPercent = 82;
    metrics.thermalState = 2;
    metrics.downloadKiBps = 12698;
    metrics.uploadKiBps = 1843;
    TEST_ASSERT_TRUE(setSystemMetrics(response, metrics));
    auto fixtureResponse = response;
    fixtureResponse.requestId = 5;
    assertEncodedMatchesFixture(fixtureResponse, "system-metrics-response-v2.bin");
    const auto encoded = encodeCompanionMessage(response);
    TEST_ASSERT_TRUE(encoded.has_value());
    const auto decoded = decodeCompanionMessage(encoded->bytes.data(), encoded->size);
    TEST_ASSERT_TRUE(decoded.has_value());
    CompanionSystemMetrics observed{};
    TEST_ASSERT_TRUE(readSystemMetrics(*decoded, observed));
    TEST_ASSERT_EQUAL_UINT8(34, observed.cpuPercent);
    TEST_ASSERT_EQUAL_UINT32(12698, observed.downloadKiBps);
    auto damaged = *encoded;
    damaged.bytes[8] = 2;
    TEST_ASSERT_FALSE(decodeCompanionMessage(damaged.bytes.data(), damaged.size).has_value());
    damaged = *encoded;
    damaged.bytes[11] = 101;
    TEST_ASSERT_FALSE(decodeCompanionMessage(damaged.bytes.data(), damaged.size).has_value());
    damaged = *encoded;
    damaged.bytes[7] = 23;
    TEST_ASSERT_FALSE(decodeCompanionMessage(damaged.bytes.data(), damaged.size).has_value());
    damaged = *encoded;
    damaged.bytes[7] = 25;
    TEST_ASSERT_FALSE(decodeCompanionMessage(damaged.bytes.data(), damaged.size).has_value());
    damaged = *encoded;
    damaged.bytes[23] = 9;
    TEST_ASSERT_FALSE(decodeCompanionMessage(damaged.bytes.data(), damaged.size).has_value());

    auto capabilities = makeResponse(42, 2, CompanionOperation::Capabilities, CompanionStatus::Ok);
    const CompanionCapability ids[] = {CompanionCapability::AppActive,
                                       CompanionCapability::SystemMetrics};
    TEST_ASSERT_FALSE(setCapabilityList(capabilities, ids, 2));
    capabilities.version = 2;
    TEST_ASSERT_TRUE(setCapabilityList(capabilities, ids, 2));
    TEST_ASSERT_TRUE(encodeCompanionMessage(capabilities).has_value());
    auto fixtureCapabilities =
        makeResponse(42, 2, CompanionOperation::Capabilities, CompanionStatus::Ok);
    fixtureCapabilities.version = 2;
    const CompanionCapability full[] = {
        CompanionCapability::AppActive, CompanionCapability::AppActivate,
        CompanionCapability::AppActiveEvents, CompanionCapability::SystemMetrics};
    TEST_ASSERT_TRUE(setCapabilityList(fixtureCapabilities, full, 4));
    assertEncodedMatchesFixture(fixtureCapabilities, "capabilities-response-v2.bin");
    capabilities.version = 1;
    TEST_ASSERT_FALSE(encodeCompanionMessage(capabilities).has_value());
}

} // namespace

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_hello_and_ack_match_fixtures);
    RUN_TEST(test_ping_request_and_response_match_fixtures);
    RUN_TEST(test_capabilities_and_app_messages_match_fixtures);
    RUN_TEST(test_fixtures_decode_to_expected_operations);
    RUN_TEST(test_codec_rejects_malformed_version_length_and_unknown_operation);
    RUN_TEST(test_codec_rejects_invalid_semantics_and_payloads);
    RUN_TEST(test_wrong_session_fixture_is_structurally_valid);
    RUN_TEST(test_oversized_and_invalid_bundle_identifiers_are_rejected);
    RUN_TEST(test_capabilities_round_trip_rejects_unknown_ids);
    RUN_TEST(test_v2_metrics_round_trip_and_v1_rejection);
    return UNITY_END();
}
