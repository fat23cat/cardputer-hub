#include <unity.h>

#include <cstring>
#include <deque>
#include <string>
#include <vector>

#include "connectivity/companion/companion_protocol.h"
#include "core/capabilities/capability_registry.h"
#include "core/logging/logger.h"
#include "services/companion/companion_service.h"

namespace {

using cardputer_hub::connectivity::companionAppActiveCapabilityId;
using cardputer_hub::connectivity::CompanionCapability;
using cardputer_hub::connectivity::companionCapabilityId;
using cardputer_hub::connectivity::CompanionEnvelope;
using cardputer_hub::connectivity::CompanionKind;
using cardputer_hub::connectivity::CompanionOperation;
using cardputer_hub::connectivity::CompanionPayload;
using cardputer_hub::connectivity::CompanionSendResult;
using cardputer_hub::connectivity::CompanionStatus;
using cardputer_hub::connectivity::CompanionTransportState;
using cardputer_hub::connectivity::decodeCompanionMessage;
using cardputer_hub::connectivity::encodeCompanionMessage;
using cardputer_hub::connectivity::ICompanionTransport;
using cardputer_hub::connectivity::makeEvent;
using cardputer_hub::connectivity::makeHello;
using cardputer_hub::connectivity::makeResponse;
using cardputer_hub::connectivity::setBundleIdentifier;
using cardputer_hub::connectivity::setCapabilityList;
using cardputer_hub::connectivity::setPingToken;
using cardputer_hub::core::CapabilityRegistry;
using cardputer_hub::core::ILogSink;
using cardputer_hub::core::Logger;
using cardputer_hub::core::LogLevel;
using cardputer_hub::core::LogRecord;
using cardputer_hub::services::CompanionService;
using cardputer_hub::services::CompanionServiceState;
using cardputer_hub::services::CompanionSubmitResult;

CompanionPayload encode(const CompanionEnvelope& message) {
    const auto encoded = encodeCompanionMessage(message);
    TEST_ASSERT_TRUE(encoded.has_value());
    CompanionPayload payload{};
    payload.size = encoded->size;
    std::memcpy(payload.bytes.data(), encoded->bytes.data(), encoded->size);
    return payload;
}

class FakeTransport final : public ICompanionTransport {
  public:
    CompanionTransportState state() const noexcept override { return transportState; }
    CompanionSendResult send(const CompanionPayload& payload) override {
        sent.push_back(payload);
        return sendResult;
    }
    std::optional<CompanionPayload> receive() override {
        if (incoming.empty()) {
            return std::nullopt;
        }
        const auto payload = incoming.front();
        incoming.pop_front();
        return payload;
    }

    CompanionTransportState transportState = CompanionTransportState::Unavailable;
    CompanionSendResult sendResult = CompanionSendResult::Sent;
    std::vector<CompanionPayload> sent;
    std::deque<CompanionPayload> incoming;
};

class CapturingLogSink final : public ILogSink {
  public:
    void write(const LogRecord& record) override { messages.emplace_back(record.message); }
    std::vector<std::string> messages;
};

CompanionEnvelope lastSent(const FakeTransport& transport) {
    TEST_ASSERT_FALSE(transport.sent.empty());
    const auto decoded =
        decodeCompanionMessage(transport.sent.back().bytes.data(), transport.sent.back().size);
    TEST_ASSERT_TRUE(decoded.has_value());
    return *decoded;
}

CompanionEnvelope sentAt(const FakeTransport& transport, std::size_t index) {
    TEST_ASSERT_TRUE(index < transport.sent.size());
    const auto decoded =
        decodeCompanionMessage(transport.sent[index].bytes.data(), transport.sent[index].size);
    TEST_ASSERT_TRUE(decoded.has_value());
    return *decoded;
}

void completeHandshake(FakeTransport& transport, CompanionService& service,
                       CapabilityRegistry& capabilities) {
    transport.transportState = CompanionTransportState::Ready;
    const auto sentBefore = transport.sent.size();
    const std::uint8_t versions[] = {1};
    transport.incoming.push_back(encode(makeHello(versions, 1)));
    service.update(std::chrono::milliseconds::zero());
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CompanionServiceState::Handshaking),
                            static_cast<unsigned>(service.state()));
    TEST_ASSERT_TRUE(transport.sent.size() >= sentBefore + 2);
    const auto ack = sentAt(transport, sentBefore);
    const auto capsRequest = sentAt(transport, sentBefore + 1);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CompanionKind::HelloAck),
                            static_cast<unsigned>(ack.kind));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CompanionOperation::Capabilities),
                            static_cast<unsigned>(capsRequest.operation));
    auto capabilitiesResponse = makeResponse(ack.session, capsRequest.requestId,
                                             CompanionOperation::Capabilities, CompanionStatus::Ok);
    const CompanionCapability ids[] = {CompanionCapability::AppActive,
                                       CompanionCapability::AppActivate,
                                       CompanionCapability::AppActiveEvents};
    TEST_ASSERT_TRUE(setCapabilityList(capabilitiesResponse, ids, 3));
    transport.incoming.push_back(encode(capabilitiesResponse));
    service.update(std::chrono::milliseconds::zero());
    const auto activeRequest = lastSent(transport);
    auto activeResponse = makeResponse(ack.session, activeRequest.requestId,
                                       CompanionOperation::AppActive, CompanionStatus::Ok);
    TEST_ASSERT_TRUE(setBundleIdentifier(activeResponse, "dev.zed.Zed"));
    transport.incoming.push_back(encode(activeResponse));
    service.update(std::chrono::milliseconds::zero());
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CompanionServiceState::Ready),
                            static_cast<unsigned>(service.state()));
    TEST_ASSERT_TRUE(capabilities.isAvailable(companionCapabilityId));
    TEST_ASSERT_TRUE(capabilities.isAvailable(companionAppActiveCapabilityId));
    while (service.takeCompletedRequest().has_value()) {
    }
}

void test_handshake_reaches_ready_and_registers_capabilities() {
    FakeTransport transport;
    CapabilityRegistry capabilities;
    CompanionService service(transport, capabilities);
    completeHandshake(transport, service, capabilities);
    char bundle[64]{};
    std::uint8_t length = 0;
    TEST_ASSERT_TRUE(service.readActiveBundleIdentifier(bundle, sizeof(bundle), length));
    TEST_ASSERT_EQUAL_STRING("dev.zed.Zed", bundle);
}

void test_new_hello_invalidates_old_session_and_pending_requests() {
    FakeTransport transport;
    CapabilityRegistry capabilities;
    CompanionService service(transport, capabilities);
    completeHandshake(transport, service, capabilities);
    const auto firstSession = service.session();
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<unsigned>(CompanionSubmitResult::Submitted),
        static_cast<unsigned>(service.activateApplication("org.telegram.desktop")));
    const std::uint8_t versions[] = {1};
    transport.incoming.push_back(encode(makeHello(versions, 1)));
    service.update(std::chrono::milliseconds::zero());
    TEST_ASSERT_NOT_EQUAL(firstSession, service.session());
    TEST_ASSERT_FALSE(capabilities.isAvailable(companionCapabilityId));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CompanionServiceState::Handshaking),
                            static_cast<unsigned>(service.state()));
}

void test_stale_response_and_event_are_ignored() {
    FakeTransport transport;
    CapabilityRegistry capabilities;
    CompanionService service(transport, capabilities);
    completeHandshake(transport, service, capabilities);
    auto stale = makeResponse(99, 1, CompanionOperation::Ping, CompanionStatus::Ok);
    const std::array<std::uint8_t, 4> token{1, 2, 3, 4};
    TEST_ASSERT_TRUE(setPingToken(stale, token));
    transport.incoming.push_back(encode(stale));
    auto event = makeEvent(99, CompanionOperation::AppActiveChanged);
    TEST_ASSERT_TRUE(setBundleIdentifier(event, "com.apple.Safari"));
    transport.incoming.push_back(encode(event));
    service.update(std::chrono::milliseconds::zero());
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CompanionServiceState::Ready),
                            static_cast<unsigned>(service.state()));
    char bundle[64]{};
    std::uint8_t length = 0;
    TEST_ASSERT_TRUE(service.readActiveBundleIdentifier(bundle, sizeof(bundle), length));
    TEST_ASSERT_EQUAL_STRING("dev.zed.Zed", bundle);
}

void test_stale_v1_response_does_not_break_new_v2_handshake() {
    FakeTransport transport;
    CapabilityRegistry capabilities;
    CompanionService service(transport, capabilities);
    completeHandshake(transport, service, capabilities);
    const auto oldSession = service.session();
    const std::uint8_t versions[] = {1, 2};
    transport.incoming.push_back(encode(makeHello(versions, 2)));
    auto stale = makeResponse(oldSession, 1, CompanionOperation::Capabilities,
                              CompanionStatus::NotAvailable);
    transport.incoming.push_back(encode(stale));
    service.update(std::chrono::milliseconds::zero());
    TEST_ASSERT_EQUAL_UINT8(2, service.selectedProtocolVersion());
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CompanionServiceState::Handshaking),
                            static_cast<unsigned>(service.state()));
    const auto capsRequest = lastSent(transport);
    TEST_ASSERT_EQUAL_UINT8(2, capsRequest.version);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CompanionOperation::Capabilities),
                            static_cast<unsigned>(capsRequest.operation));
}

void test_request_timeout_and_duplicate_response_do_not_disable_transport() {
    FakeTransport transport;
    CapabilityRegistry capabilities;
    CompanionService service(transport, capabilities);
    completeHandshake(transport, service, capabilities);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CompanionSubmitResult::Submitted),
                            static_cast<unsigned>(service.requestActiveApplication()));
    const auto request = lastSent(transport);
    service.update(std::chrono::seconds(2));
    const auto timedOut = service.takeCompletedRequest();
    TEST_ASSERT_TRUE(timedOut.has_value());
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CompanionStatus::Malformed),
                            static_cast<unsigned>(timedOut->status));
    auto late = makeResponse(service.session(), request.requestId, CompanionOperation::AppActive,
                             CompanionStatus::Ok);
    TEST_ASSERT_TRUE(setBundleIdentifier(late, "com.apple.Safari"));
    transport.incoming.push_back(encode(late));
    service.update(std::chrono::milliseconds::zero());
    char bundle[64]{};
    std::uint8_t length = 0;
    TEST_ASSERT_TRUE(service.readActiveBundleIdentifier(bundle, sizeof(bundle), length));
    TEST_ASSERT_EQUAL_STRING("dev.zed.Zed", bundle);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CompanionServiceState::Ready),
                            static_cast<unsigned>(service.state()));
}

void test_heartbeat_success_and_expiry_remove_capability() {
    FakeTransport transport;
    CapabilityRegistry capabilities;
    CompanionService service(transport, capabilities);
    completeHandshake(transport, service, capabilities);
    service.update(std::chrono::seconds(3));
    const auto ping = lastSent(transport);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CompanionOperation::Ping),
                            static_cast<unsigned>(ping.operation));
    auto pong =
        makeResponse(ping.session, ping.requestId, CompanionOperation::Ping, CompanionStatus::Ok);
    std::array<std::uint8_t, 4> token{};
    TEST_ASSERT_TRUE(cardputer_hub::connectivity::readPingToken(ping, token));
    TEST_ASSERT_TRUE(setPingToken(pong, token));
    transport.incoming.push_back(encode(pong));
    service.update(std::chrono::milliseconds::zero());
    TEST_ASSERT_TRUE(capabilities.isAvailable(companionCapabilityId));
    service.update(std::chrono::seconds(9));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CompanionServiceState::Unavailable),
                            static_cast<unsigned>(service.state()));
    TEST_ASSERT_FALSE(capabilities.isAvailable(companionCapabilityId));
}

void test_transport_drop_invalidates_session_immediately() {
    FakeTransport transport;
    CapabilityRegistry capabilities;
    CompanionService service(transport, capabilities);
    completeHandshake(transport, service, capabilities);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<unsigned>(CompanionSubmitResult::Submitted),
        static_cast<unsigned>(service.activateApplication("org.telegram.desktop")));
    transport.transportState = CompanionTransportState::Unavailable;
    service.update(std::chrono::milliseconds::zero());
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CompanionServiceState::Unavailable),
                            static_cast<unsigned>(service.state()));
    TEST_ASSERT_FALSE(capabilities.isAvailable(companionCapabilityId));
}

void test_outstanding_request_limit_returns_busy() {
    FakeTransport transport;
    CapabilityRegistry capabilities;
    CompanionService service(transport, capabilities);
    completeHandshake(transport, service, capabilities);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CompanionSubmitResult::Submitted),
                            static_cast<unsigned>(service.activateApplication("a.b")));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CompanionSubmitResult::Submitted),
                            static_cast<unsigned>(service.activateApplication("c.d")));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CompanionSubmitResult::Submitted),
                            static_cast<unsigned>(service.activateApplication("e.f")));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CompanionSubmitResult::Submitted),
                            static_cast<unsigned>(service.activateApplication("g.h")));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CompanionSubmitResult::Busy),
                            static_cast<unsigned>(service.activateApplication("i.j")));
}

void test_four_outstanding_responses_are_all_completed() {
    FakeTransport transport;
    CapabilityRegistry capabilities;
    CompanionService service(transport, capabilities);
    completeHandshake(transport, service, capabilities);
    std::uint8_t ids[4]{};
    const char* bundles[] = {"a.b", "c.d", "e.f", "g.h"};
    for (int index = 0; index < 4; ++index) {
        TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CompanionSubmitResult::Submitted),
                                static_cast<unsigned>(service.activateApplication(bundles[index])));
        ids[index] = lastSent(transport).requestId;
    }
    const int order[] = {2, 0, 3, 1};
    for (int index : order) {
        transport.incoming.push_back(encode(makeResponse(
            service.session(), ids[index], CompanionOperation::AppActivate, CompanionStatus::Ok)));
    }
    service.update(std::chrono::milliseconds::zero());
    bool seen[4]{};
    for (int index = 0; index < 4; ++index) {
        const auto completed = service.takeCompletedRequest();
        TEST_ASSERT_TRUE(completed.has_value());
        TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CompanionStatus::Ok),
                                static_cast<unsigned>(completed->status));
        bool matched = false;
        for (int idIndex = 0; idIndex < 4; ++idIndex) {
            if (completed->requestId == ids[idIndex]) {
                TEST_ASSERT_FALSE(seen[idIndex]);
                seen[idIndex] = true;
                matched = true;
            }
        }
        TEST_ASSERT_TRUE(matched);
    }
    TEST_ASSERT_FALSE(service.takeCompletedRequest().has_value());
}

void test_fail_all_pending_preserves_every_completion() {
    FakeTransport transport;
    CapabilityRegistry capabilities;
    CompanionService service(transport, capabilities);
    completeHandshake(transport, service, capabilities);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CompanionSubmitResult::Submitted),
                            static_cast<unsigned>(service.activateApplication("a.b")));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CompanionSubmitResult::Submitted),
                            static_cast<unsigned>(service.activateApplication("c.d")));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CompanionSubmitResult::Submitted),
                            static_cast<unsigned>(service.activateApplication("e.f")));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CompanionSubmitResult::Submitted),
                            static_cast<unsigned>(service.activateApplication("g.h")));
    transport.transportState = CompanionTransportState::Unavailable;
    service.update(std::chrono::milliseconds::zero());
    for (int index = 0; index < 4; ++index) {
        const auto completed = service.takeCompletedRequest();
        TEST_ASSERT_TRUE(completed.has_value());
        TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CompanionStatus::NotAvailable),
                                static_cast<unsigned>(completed->status));
    }
    TEST_ASSERT_FALSE(service.takeCompletedRequest().has_value());
}

void test_heartbeat_token_mismatch_is_protocol_error() {
    FakeTransport transport;
    CapabilityRegistry capabilities;
    CompanionService service(transport, capabilities);
    completeHandshake(transport, service, capabilities);
    service.update(std::chrono::seconds(3));
    const auto ping = lastSent(transport);
    auto pong =
        makeResponse(ping.session, ping.requestId, CompanionOperation::Ping, CompanionStatus::Ok);
    const std::array<std::uint8_t, 4> wrong{0xDE, 0xAD, 0xBE, 0xEF};
    TEST_ASSERT_TRUE(setPingToken(pong, wrong));
    transport.incoming.push_back(encode(pong));
    service.update(std::chrono::milliseconds::zero());
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CompanionServiceState::ProtocolError),
                            static_cast<unsigned>(service.state()));
    TEST_ASSERT_FALSE(capabilities.isAvailable(companionCapabilityId));
}

void test_malformed_app_active_payload_is_protocol_error() {
    FakeTransport transport;
    CapabilityRegistry capabilities;
    CompanionService service(transport, capabilities);
    completeHandshake(transport, service, capabilities);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CompanionSubmitResult::Submitted),
                            static_cast<unsigned>(service.requestActiveApplication()));
    const auto request = lastSent(transport);
    auto response = makeResponse(service.session(), request.requestId,
                                 CompanionOperation::AppActive, CompanionStatus::Ok);
    TEST_ASSERT_TRUE(setBundleIdentifier(response, "dev.zed.Zed"));
    auto payload = encode(response);
    payload.bytes[7] = 2;
    payload.bytes[8] = 10;
    payload.bytes[9] = 'x';
    payload.size = 10;
    transport.incoming.push_back(payload);
    service.update(std::chrono::milliseconds::zero());
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CompanionServiceState::ProtocolError),
                            static_cast<unsigned>(service.state()));
}

void test_empty_active_changed_clears_bundle_and_malformed_event_fails() {
    FakeTransport transport;
    CapabilityRegistry capabilities;
    CompanionService service(transport, capabilities);
    completeHandshake(transport, service, capabilities);
    auto empty = makeEvent(service.session(), CompanionOperation::AppActiveChanged);
    transport.incoming.push_back(encode(empty));
    service.update(std::chrono::milliseconds::zero());
    char bundle[64]{};
    std::uint8_t length = 0;
    TEST_ASSERT_FALSE(service.readActiveBundleIdentifier(bundle, sizeof(bundle), length));

    completeHandshake(transport, service, capabilities);
    auto malformed = makeEvent(service.session(), CompanionOperation::AppActiveChanged);
    TEST_ASSERT_TRUE(setBundleIdentifier(malformed, "dev.zed.Zed"));
    auto payload = encode(malformed);
    payload.bytes[7] = 2;
    payload.bytes[8] = 10;
    payload.bytes[9] = 'x';
    payload.size = 10;
    transport.incoming.push_back(payload);
    service.update(std::chrono::milliseconds::zero());
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CompanionServiceState::ProtocolError),
                            static_cast<unsigned>(service.state()));
}

void test_active_changed_event_updates_bundle_and_logs_omit_it() {
    FakeTransport transport;
    CapabilityRegistry capabilities;
    CapturingLogSink sink;
    Logger logger(sink, LogLevel::Debug);
    CompanionService service(transport, capabilities, &logger);
    completeHandshake(transport, service, capabilities);
    auto event = makeEvent(service.session(), CompanionOperation::AppActiveChanged);
    TEST_ASSERT_TRUE(setBundleIdentifier(event, "com.apple.Safari"));
    transport.incoming.push_back(encode(event));
    service.update(std::chrono::milliseconds::zero());
    char bundle[64]{};
    std::uint8_t length = 0;
    TEST_ASSERT_TRUE(service.readActiveBundleIdentifier(bundle, sizeof(bundle), length));
    TEST_ASSERT_EQUAL_STRING("com.apple.Safari", bundle);
    for (const auto& message : sink.messages) {
        TEST_ASSERT_TRUE(message.find("Safari") == std::string::npos);
        TEST_ASSERT_TRUE(message.find("dev.zed") == std::string::npos);
    }
}

void test_v2_negotiation_and_operation_owned_completions() {
    using cardputer_hub::connectivity::CompanionSystemMetrics;
    using cardputer_hub::connectivity::companionSystemMetricsCapabilityId;
    using cardputer_hub::connectivity::setSystemMetrics;
    FakeTransport transport;
    CapabilityRegistry capabilities;
    CompanionService service(transport, capabilities);
    transport.transportState = CompanionTransportState::Ready;
    const std::uint8_t versions[] = {1, 2};
    transport.incoming.push_back(encode(makeHello(versions, 2)));
    service.update(std::chrono::milliseconds::zero());
    const auto ack = sentAt(transport, 0);
    TEST_ASSERT_EQUAL_UINT8(2, ack.payload[0]);
    TEST_ASSERT_EQUAL_UINT8(2, service.selectedProtocolVersion());
    const auto capsRequest = sentAt(transport, 1);
    TEST_ASSERT_EQUAL_UINT8(2, capsRequest.version);
    auto caps = makeResponse(ack.session, capsRequest.requestId, CompanionOperation::Capabilities,
                             CompanionStatus::Ok);
    caps.version = 2;
    const CompanionCapability ids[] = {
        CompanionCapability::AppActive, CompanionCapability::AppActivate,
        CompanionCapability::AppActiveEvents, CompanionCapability::SystemMetrics};
    TEST_ASSERT_TRUE(setCapabilityList(caps, ids, 4));
    transport.incoming.push_back(encode(caps));
    service.update(std::chrono::milliseconds::zero());
    const auto activeRequest = lastSent(transport);
    auto active = makeResponse(ack.session, activeRequest.requestId, CompanionOperation::AppActive,
                               CompanionStatus::NotAvailable);
    active.version = 2;
    transport.incoming.push_back(encode(active));
    service.update(std::chrono::milliseconds::zero());
    TEST_ASSERT_TRUE(capabilities.isAvailable(companionSystemMetricsCapabilityId));
    TEST_ASSERT_FALSE(service.takeCompletedRequest().has_value());

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CompanionSubmitResult::Submitted),
                            static_cast<unsigned>(service.activateApplication("dev.zed.Zed")));
    const auto activateRequest = lastSent(transport);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CompanionSubmitResult::Submitted),
                            static_cast<unsigned>(service.requestSystemMetrics()));
    const auto metricsRequest = lastSent(transport);
    TEST_ASSERT_EQUAL_UINT8(2, metricsRequest.version);
    auto metricsResponse = makeResponse(ack.session, metricsRequest.requestId,
                                        CompanionOperation::SystemMetrics, CompanionStatus::Ok);
    metricsResponse.version = 2;
    CompanionSystemMetrics metrics{};
    metrics.validity = 1;
    metrics.cpuPercent = 50;
    TEST_ASSERT_TRUE(setSystemMetrics(metricsResponse, metrics));
    transport.incoming.push_back(encode(metricsResponse));
    service.update(std::chrono::milliseconds::zero());
    TEST_ASSERT_FALSE(service.takeCompletedRequest(CompanionOperation::AppActivate).has_value());
    TEST_ASSERT_TRUE(service.takeCompletedRequest(CompanionOperation::SystemMetrics).has_value());
    auto activateResponse = makeResponse(ack.session, activateRequest.requestId,
                                         CompanionOperation::AppActivate, CompanionStatus::Ok);
    activateResponse.version = 2;
    transport.incoming.push_back(encode(activateResponse));
    service.update(std::chrono::milliseconds::zero());
    TEST_ASSERT_FALSE(service.takeCompletedRequest(CompanionOperation::SystemMetrics).has_value());
    TEST_ASSERT_TRUE(service.takeCompletedRequest(CompanionOperation::AppActivate).has_value());
    transport.transportState = CompanionTransportState::Unavailable;
    service.update(std::chrono::milliseconds::zero());
    TEST_ASSERT_FALSE(capabilities.isAvailable(companionSystemMetricsCapabilityId));
}

} // namespace

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_handshake_reaches_ready_and_registers_capabilities);
    RUN_TEST(test_new_hello_invalidates_old_session_and_pending_requests);
    RUN_TEST(test_stale_response_and_event_are_ignored);
    RUN_TEST(test_stale_v1_response_does_not_break_new_v2_handshake);
    RUN_TEST(test_request_timeout_and_duplicate_response_do_not_disable_transport);
    RUN_TEST(test_heartbeat_success_and_expiry_remove_capability);
    RUN_TEST(test_transport_drop_invalidates_session_immediately);
    RUN_TEST(test_outstanding_request_limit_returns_busy);
    RUN_TEST(test_four_outstanding_responses_are_all_completed);
    RUN_TEST(test_fail_all_pending_preserves_every_completion);
    RUN_TEST(test_heartbeat_token_mismatch_is_protocol_error);
    RUN_TEST(test_malformed_app_active_payload_is_protocol_error);
    RUN_TEST(test_empty_active_changed_clears_bundle_and_malformed_event_fails);
    RUN_TEST(test_active_changed_event_updates_bundle_and_logs_omit_it);
    RUN_TEST(test_v2_negotiation_and_operation_owned_completions);
    return UNITY_END();
}
