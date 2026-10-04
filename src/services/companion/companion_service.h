#pragma once

#include "connectivity/companion/companion_transport.h"
#include "core/capabilities/capability_registry.h"
#include "core/logging/logger.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string_view>

namespace cardputer_hub::services {

enum class CompanionServiceState : std::uint8_t {
    Unavailable,
    Attaching,
    Handshaking,
    Ready,
    ProtocolError,
    // The Mac was built from a different protocol definition; nothing but a
    // new HELLO is accepted until it is updated.
    Incompatible,
};

// Which side to update after a fingerprint mismatch, judged by build dates.
enum class CompanionMismatchAdvice : std::uint8_t { UpdateCompanion, UpdateFirmware, RebuildBoth };

// `peerBuildId` is the Companion's "YYYY-MM-DD <commit>"; a legacy peer (built
// before plan 043) always needs a Companion update.
CompanionMismatchAdvice companionMismatchAdvice(const char* firmwareBuildId,
                                                const char* peerBuildId, bool peerLegacy);

enum class CompanionSubmitResult : std::uint8_t { Submitted, NotReady, Busy, Invalid };

struct CompanionCompletedRequest {
    std::uint8_t requestId = 0;
    connectivity::CompanionOperation operation = connectivity::CompanionOperation::None;
    connectivity::CompanionStatus status = connectivity::CompanionStatus::Ok;
    connectivity::CompanionEnvelope message{};
};

// A request the Mac sent to the Cardputer (inventory operations only). It is
// answered with respond() while its session is still the live one.
struct CompanionInboundRequest {
    std::uint16_t session = 0;
    std::uint8_t requestId = 0;
    connectivity::CompanionOperation operation = connectivity::CompanionOperation::None;
    connectivity::CompanionEnvelope message{};
};

class CompanionService {
  public:
    static constexpr auto requestTimeout = std::chrono::seconds(2);
    static constexpr auto aiUsageRequestTimeout = std::chrono::seconds(6);
    static constexpr auto heartbeatInterval = std::chrono::seconds(3);
    static constexpr auto livenessTimeout = std::chrono::seconds(9);

    CompanionService(connectivity::ICompanionTransport& transport,
                     core::CapabilityRegistry& capabilities, core::Logger* logger = nullptr);

    void update(std::chrono::milliseconds elapsed);
    CompanionServiceState state() const noexcept { return state_; }
    std::uint16_t session() const noexcept { return session_; }
    std::uint8_t lastSubmittedRequestId() const noexcept { return lastSubmittedRequestId_; }
    bool hasLiveCompanion() const noexcept { return state_ == CompanionServiceState::Ready; }
    // The Companion's build id from its HELLO (empty for a legacy peer).
    const char* peerBuildId() const noexcept { return peerBuildId_.data(); }
    bool peerIsLegacy() const noexcept { return peerLegacy_; }
    // Meaningful only while Incompatible.
    CompanionMismatchAdvice mismatchAdvice() const noexcept;
    CompanionSubmitResult requestActiveApplication();
    CompanionSubmitResult activateApplication(std::string_view bundleId);
    CompanionSubmitResult requestSystemMetrics();
    CompanionSubmitResult requestSystemDetails(connectivity::SystemDetailsGroup group);
    CompanionSubmitResult requestAiUsage();
    bool hasPendingRequest(connectivity::CompanionOperation operation) const noexcept;
    std::optional<CompanionCompletedRequest> takeCompletedRequest();
    std::optional<CompanionCompletedRequest>
    takeCompletedRequest(connectivity::CompanionOperation operation);
    bool readActiveBundleIdentifier(char* destination, std::size_t capacity,
                                    std::uint8_t& length) const;

    std::optional<CompanionInboundRequest> takeInboundRequest();
    // Sends `response` (status and payload) as the answer to `request`. False
    // when the request's session has ended; nothing is sent then.
    bool respond(const CompanionInboundRequest& request,
                 const connectivity::CompanionEnvelope& response);
    // Changes whenever a session starts or ends, so state tied to one session
    // (a partial transfer) can be discarded.
    std::uint32_t sessionEpoch() const noexcept { return sessionEpoch_; }

    static constexpr std::size_t maxInboundRequests = 2;

  private:
    struct PendingRequest {
        std::uint8_t id = 0;
        connectivity::CompanionOperation operation = connectivity::CompanionOperation::None;
        std::chrono::milliseconds elapsed{0};
        bool used = false;
        bool heartbeat = false;
        std::array<std::uint8_t, connectivity::companionPingTokenSize> pingToken{};
    };

    void log(core::LogLevel level, const char* message) const;
    void becomeUnavailable();
    void enterProtocolError();
    void enterIncompatible(const char* peerBuildId, bool legacy);
    void clearLiveCapabilities();
    void publishLiveCapabilities();
    bool sendMessage(const connectivity::CompanionEnvelope& message);
    void handleIncoming(const connectivity::CompanionPayload& payload);
    void handleHello(const connectivity::CompanionEnvelope& message);
    bool sendHelloAck(std::uint16_t session, connectivity::CompanionStatus status);
    void handleResponse(const connectivity::CompanionEnvelope& message);
    void handleEvent(const connectivity::CompanionEnvelope& message);
    bool startHandshakeRequests();
    CompanionSubmitResult submit(connectivity::CompanionOperation operation,
                                 const connectivity::CompanionEnvelope& message, bool heartbeat);
    PendingRequest* findPending(std::uint8_t id);
    std::uint8_t pendingCount() const noexcept;
    void failAllPending(connectivity::CompanionStatus status);
    void completePending(PendingRequest& pending, const connectivity::CompanionEnvelope& message);
    void pushCompleted(const PendingRequest& pending,
                       const connectivity::CompanionEnvelope& message);
    void tickPending(std::chrono::milliseconds elapsed);
    void tickHeartbeat(std::chrono::milliseconds elapsed);
    bool setActiveBundle(const connectivity::CompanionEnvelope& message, bool allowEmpty);
    void handleInboundRequest(const connectivity::CompanionEnvelope& message);
    void endSessionState();

    connectivity::ICompanionTransport& transport_;
    core::CapabilityRegistry& capabilities_;
    core::Logger* logger_ = nullptr;
    CompanionServiceState state_ = CompanionServiceState::Unavailable;
    std::uint16_t session_ = 0;
    std::array<char, connectivity::companionMaxBuildIdSize + 1> peerBuildId_{};
    bool peerLegacy_ = false;
    std::uint16_t nextSession_ = 1;
    std::uint8_t nextRequestId_ = 1;
    std::uint8_t lastSubmittedRequestId_ = 0;
    std::array<PendingRequest, connectivity::companionMaxOutstandingRequests> pending_{};
    std::array<CompanionCompletedRequest, connectivity::companionMaxOutstandingRequests>
        completed_{};
    std::uint8_t completedHead_ = 0;
    std::uint8_t completedCount_ = 0;
    std::array<char, connectivity::companionMaxBundleIdSize + 1> activeBundle_{};
    std::uint8_t activeBundleLength_ = 0;
    bool hasActiveBundle_ = false;
    bool companionPublished_ = false;
    std::chrono::milliseconds sinceHeartbeat_{0};
    std::chrono::milliseconds sinceHeartbeatSend_{0};
    bool heartbeatInFlight_ = false;
    std::array<CompanionInboundRequest, maxInboundRequests> inbound_{};
    std::uint8_t inboundCount_ = 0;
    std::uint32_t sessionEpoch_ = 0;
};

} // namespace cardputer_hub::services
