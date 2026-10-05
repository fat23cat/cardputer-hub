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

void test_agent_status_round_trip_and_rejects_bad_pairs() {
    assertEncodedMatchesFixture(makeRequest(42, 13, CompanionOperation::AiAgentStatus),
                                "ai-agent-status-request.bin");
    CompanionAgentStatus status{};
    TEST_ASSERT_TRUE(readAgentStatus(decodeFixture("ai-agent-status-response.bin"), status));
    TEST_ASSERT_TRUE(status.installed(AiProvider::Codex));
    TEST_ASSERT_TRUE(status.installed(AiProvider::Claude));
    TEST_ASSERT_TRUE(status.installed(AiProvider::Cursor));
    TEST_ASSERT_EQUAL_UINT8(3, status.installedCount());
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(AgentState::DoneEarlier),
                            static_cast<unsigned>(status.state(AiProvider::Cursor)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(AgentState::Working),
                            static_cast<unsigned>(status.state(AiProvider::Codex)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(AgentState::NeedsYou),
                            static_cast<unsigned>(status.state(AiProvider::Claude)));
    auto response = makeResponse(42, 13, CompanionOperation::AiAgentStatus, CompanionStatus::Ok);
    TEST_ASSERT_TRUE(setAgentStatus(response, status));
    assertEncodedMatchesFixture(response, "ai-agent-status-response.bin");

    CompanionAgentStatus none{};
    TEST_ASSERT_TRUE(readAgentStatus(decodeFixture("ai-agent-status-response-none.bin"), none));
    TEST_ASSERT_EQUAL_UINT8(0, none.installedCount());
    auto empty = makeResponse(42, 13, CompanionOperation::AiAgentStatus, CompanionStatus::Ok);
    TEST_ASSERT_TRUE(setAgentStatus(empty, none));
    assertEncodedMatchesFixture(empty, "ai-agent-status-response-none.bin");

    const auto wire = loadFixture("ai-agent-status-response.bin");
    auto damaged = wire;
    damaged[companionEnvelopeSize + 2] = 5; // no such state
    TEST_ASSERT_FALSE(decodeCompanionMessage(damaged.data(), damaged.size()).has_value());
    damaged = wire;
    damaged[companionEnvelopeSize + 3] = 1; // Codex twice
    TEST_ASSERT_FALSE(decodeCompanionMessage(damaged.data(), damaged.size()).has_value());
    damaged = wire;
    damaged[companionEnvelopeSize + 1] = 3; // Claude before Codex
    damaged[companionEnvelopeSize + 3] = 1;
    TEST_ASSERT_FALSE(decodeCompanionMessage(damaged.data(), damaged.size()).has_value());
    damaged = wire;
    damaged[companionEnvelopeSize + 3] = 0; // no such application
    TEST_ASSERT_FALSE(decodeCompanionMessage(damaged.data(), damaged.size()).has_value());
    damaged = wire;
    damaged[companionEnvelopeSize] = 2; // count smaller than the pairs
    TEST_ASSERT_FALSE(decodeCompanionMessage(damaged.data(), damaged.size()).has_value());
    damaged = wire;
    damaged[companionEnvelopeSize] = 4; // more than three applications
    TEST_ASSERT_FALSE(decodeCompanionMessage(damaged.data(), damaged.size()).has_value());
    auto request = makeRequest(42, 13, CompanionOperation::AiAgentStatus);
    request.payloadSize = 1;
    TEST_ASSERT_FALSE(encodeCompanionMessage(request).has_value());
    TEST_ASSERT_TRUE(encodeCompanionMessage(makeResponse(42, 13, CompanionOperation::AiAgentStatus,
                                                         CompanionStatus::NotAvailable))
                         .has_value());
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
    TEST_ASSERT_TRUE(companionResponseFailureIsIsolated(CompanionOperation::AiAgentStatus));
    TEST_ASSERT_FALSE(companionResponseFailureIsIsolated(CompanionOperation::AppActive));
    TEST_ASSERT_FALSE(companionResponseFailureIsIsolated(CompanionOperation::Ping));
}

// ---- Inventory ----------------------------------------------------------------

const CompanionInventoryId inventoryId{0x0f, 0x1e, 0x2d, 0x3c, 0x4b, 0x5a, 0x69, 0x78,
                                       0x87, 0x96, 0xa5, 0xb4, 0xc3, 0xd2, 0xe1, 0xf0};
const CompanionInventoryId otherInventoryId{0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
                                            0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa};

std::string getRecordJson() {
    return "{\"schema\":2,\"id\":\"0f1e2d3c4b5a69788796a5b4c3d2e1f0\",\"revision\":3,"
           "\"name\":\"Чемодан\",\"description\":\"Зарядка, свитер\"}";
}

std::string putRecordJson() {
    return "{\"schema\":2,\"id\":\"0f1e2d3c4b5a69788796a5b4c3d2e1f0\",\"revision\":3,"
           "\"name\":\"Синий чемодан\",\"description\":\"Штаны, шорты\\nНоски\\nЛыжи «Atomic»\"}";
}

void test_inventory_messages_match_fixtures() {
    auto list = makeRequest(42, 21, CompanionOperation::InventoryList);
    TEST_ASSERT_TRUE(setInventoryListRequest(list, 0));
    assertEncodedMatchesFixture(list, "inventory-list-request.bin");

    const CompanionInventoryListEntry entries[] = {{inventoryId, true, 3, "Чемодан"},
                                                   {otherInventoryId, false, 0, ""}};
    auto listed = makeResponse(42, 21, CompanionOperation::InventoryList, CompanionStatus::Ok);
    TEST_ASSERT_TRUE(setInventoryListResponse(listed, 2, 2, entries, 2));
    assertEncodedMatchesFixture(listed, "inventory-list-response.bin");

    auto get = makeRequest(42, 22, CompanionOperation::InventoryGet);
    TEST_ASSERT_TRUE(setInventoryGetRequest(get, inventoryId, 0));
    assertEncodedMatchesFixture(get, "inventory-get-request.bin");
    CompanionInventoryId readId{};
    std::uint16_t offset = 1;
    TEST_ASSERT_TRUE(
        readInventoryGetRequest(decodeFixture("inventory-get-request.bin"), readId, offset));
    TEST_ASSERT_TRUE(readId == inventoryId);
    TEST_ASSERT_EQUAL_UINT16(0, offset);

    const auto record = getRecordJson();
    CompanionInventoryChunk chunk{};
    chunk.id = inventoryId;
    chunk.revision = 3;
    chunk.total = static_cast<std::uint16_t>(record.size());
    chunk.data = reinterpret_cast<const std::uint8_t*>(record.data());
    chunk.size = record.size();
    auto got = makeResponse(42, 22, CompanionOperation::InventoryGet, CompanionStatus::Ok);
    TEST_ASSERT_TRUE(setInventoryGetResponse(got, chunk));
    assertEncodedMatchesFixture(got, "inventory-get-response.bin");
    assertEncodedMatchesFixture(
        makeResponse(42, 22, CompanionOperation::InventoryGet, CompanionStatus::NotAvailable),
        "inventory-get-not-available.bin");

    const auto edited = putRecordJson();
    chunk.total = static_cast<std::uint16_t>(edited.size());
    chunk.data = reinterpret_cast<const std::uint8_t*>(edited.data());
    chunk.size = edited.size();
    auto put = makeRequest(42, 23, CompanionOperation::InventoryPut);
    TEST_ASSERT_TRUE(setInventoryPutRequest(put, chunk));
    assertEncodedMatchesFixture(put, "inventory-put-request.bin");
    auto stored = makeResponse(42, 23, CompanionOperation::InventoryPut, CompanionStatus::Ok);
    TEST_ASSERT_TRUE(setInventoryPutResponse(stored, static_cast<std::uint16_t>(edited.size()), 4));
    assertEncodedMatchesFixture(stored, "inventory-put-response.bin");
    auto conflict =
        makeResponse(42, 23, CompanionOperation::InventoryPut, CompanionStatus::Conflict);
    TEST_ASSERT_TRUE(setInventoryConflict(conflict, 5));
    assertEncodedMatchesFixture(conflict, "inventory-put-conflict.bin");

    auto remove = makeRequest(42, 24, CompanionOperation::InventoryDelete);
    TEST_ASSERT_TRUE(setInventoryDeleteRequest(remove, inventoryId, 4));
    assertEncodedMatchesFixture(remove, "inventory-delete-request.bin");
    CompanionInventoryId removedId{};
    std::uint32_t removedRevision = 0;
    TEST_ASSERT_TRUE(readInventoryDeleteRequest(decodeFixture("inventory-delete-request.bin"),
                                                removedId, removedRevision));
    TEST_ASSERT_TRUE(removedId == inventoryId);
    TEST_ASSERT_EQUAL_UINT32(4, removedRevision);
    assertEncodedMatchesFixture(
        makeResponse(42, 24, CompanionOperation::InventoryDelete, CompanionStatus::Ok),
        "inventory-delete-response.bin");
}

void test_inventory_chunks_are_bounded() {
    std::vector<std::uint8_t> data(companionInventoryPutChunkSize + 1, 'x');
    CompanionInventoryChunk chunk{};
    chunk.id = inventoryId;
    chunk.revision = 1;
    chunk.total = 4096;
    chunk.data = data.data();
    auto put = makeRequest(42, 23, CompanionOperation::InventoryPut);
    chunk.size = companionInventoryPutChunkSize + 1;
    TEST_ASSERT_FALSE(setInventoryPutRequest(put, chunk));
    chunk.size = companionInventoryPutChunkSize;
    TEST_ASSERT_TRUE(setInventoryPutRequest(put, chunk));
    // Beyond the record bound, past the declared total, and with no base revision.
    chunk.total = 4097;
    TEST_ASSERT_FALSE(setInventoryPutRequest(put, chunk));
    chunk.total = 100;
    TEST_ASSERT_FALSE(setInventoryPutRequest(put, chunk));
    chunk.total = 4096;
    chunk.offset = 4000;
    TEST_ASSERT_FALSE(setInventoryPutRequest(put, chunk));
    chunk.offset = 0;
    chunk.revision = 0;
    TEST_ASSERT_FALSE(setInventoryPutRequest(put, chunk));

    // A list entry's name and validity must agree; names are UTF-8.
    const CompanionInventoryListEntry invalid[] = {{inventoryId, false, 0, "named"}};
    auto listed = makeResponse(42, 21, CompanionOperation::InventoryList, CompanionStatus::Ok);
    TEST_ASSERT_FALSE(setInventoryListResponse(listed, 1, 1, invalid, 1));
    const CompanionInventoryListEntry malformed[] = {{inventoryId, true, 1, "\xC3\x28"}};
    TEST_ASSERT_FALSE(setInventoryListResponse(listed, 1, 1, malformed, 1));
    // Only PUT answers CONFLICT, and an error answer carries no payload.
    auto conflictGet =
        makeResponse(42, 22, CompanionOperation::InventoryGet, CompanionStatus::Conflict);
    conflictGet.payloadSize = 4;
    TEST_ASSERT_FALSE(encodeCompanionMessage(conflictGet).has_value());
    auto rejected =
        makeResponse(42, 23, CompanionOperation::InventoryPut, CompanionStatus::Rejected);
    TEST_ASSERT_TRUE(encodeCompanionMessage(rejected).has_value());
    rejected.payloadSize = 1;
    TEST_ASSERT_FALSE(encodeCompanionMessage(rejected).has_value());
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
    RUN_TEST(test_agent_status_round_trip_and_rejects_bad_pairs);
    RUN_TEST(test_system_details_groups_round_trip_and_reject_bad_names);
    RUN_TEST(test_only_telemetry_failures_are_isolated);
    RUN_TEST(test_inventory_messages_match_fixtures);
    RUN_TEST(test_inventory_chunks_are_bounded);
    return UNITY_END();
}
