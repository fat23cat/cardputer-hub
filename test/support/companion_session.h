#pragma once

// Companion handshake helpers shared by service tests. Include after <unity.h>.

#include "connectivity/companion/companion_fingerprint.h"
#include "connectivity/companion/companion_protocol.h"
#include "services/companion/companion_service.h"

#include <chrono>
#include <cstring>

namespace cardputer_hub::test_support {

inline connectivity::CompanionPayload
companionWire(const connectivity::CompanionEnvelope& message) {
    const auto encoded = connectivity::encodeCompanionMessage(message);
    TEST_ASSERT_TRUE(encoded.has_value());
    connectivity::CompanionPayload payload{};
    payload.size = encoded->size;
    std::memcpy(payload.bytes.data(), encoded->bytes.data(), encoded->size);
    return payload;
}

inline connectivity::CompanionEnvelope
companionDecode(const connectivity::CompanionPayload& payload) {
    const auto decoded = connectivity::decodeCompanionMessage(payload.bytes.data(), payload.size);
    TEST_ASSERT_TRUE(decoded.has_value());
    return *decoded;
}

// A HELLO from a Companion built from this protocol, or from another one.
inline connectivity::CompanionEnvelope companionHello(const char* buildId = "2026-09-29 abc1234",
                                                      bool matching = true) {
    connectivity::CompanionHello hello{};
    hello.fingerprint = connectivity::companionProtocolFingerprint;
    if (!matching)
        hello.fingerprint[0] = static_cast<std::uint8_t>(hello.fingerprint[0] ^ 0xFFU);
    std::strncpy(hello.buildId.data(), buildId, hello.buildId.size() - 1);
    const auto message = connectivity::makeHello(hello);
    TEST_ASSERT_TRUE(message.has_value());
    return *message;
}

template <typename Transport>
connectivity::CompanionEnvelope lastSentMessage(const Transport& transport) {
    TEST_ASSERT_FALSE(transport.sent.empty());
    return companionDecode(transport.sent.back());
}

// Sends a matching HELLO and answers the handshake's APP_ACTIVE request, so the
// service ends Ready with COMPANION published.
template <typename Transport>
void completeCompanionHandshake(Transport& transport, services::CompanionService& service,
                                const char* activeBundle = nullptr) {
    transport.incoming.push_back(companionWire(companionHello()));
    service.update(std::chrono::milliseconds::zero());
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(services::CompanionServiceState::Handshaking),
                            static_cast<unsigned>(service.state()));
    const auto request = lastSentMessage(transport);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(connectivity::CompanionOperation::AppActive),
                            static_cast<unsigned>(request.operation));
    auto response = connectivity::makeResponse(
        service.session(), request.requestId, connectivity::CompanionOperation::AppActive,
        activeBundle != nullptr ? connectivity::CompanionStatus::Ok
                                : connectivity::CompanionStatus::NotAvailable);
    if (activeBundle != nullptr)
        TEST_ASSERT_TRUE(connectivity::setBundleIdentifier(response, activeBundle));
    transport.incoming.push_back(companionWire(response));
    service.update(std::chrono::milliseconds::zero());
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(services::CompanionServiceState::Ready),
                            static_cast<unsigned>(service.state()));
}

} // namespace cardputer_hub::test_support
