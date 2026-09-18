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
};

enum class CompanionSubmitResult : std::uint8_t { Submitted, NotReady, Busy, Invalid };

struct CompanionCompletedRequest {
    std::uint8_t requestId = 0;
    connectivity::CompanionOperation operation = connectivity::CompanionOperation::None;
    connectivity::CompanionStatus status = connectivity::CompanionStatus::Ok;
    connectivity::CompanionEnvelope message{};
};

class CompanionService {
  public:
    static constexpr auto requestTimeout = std::chrono::seconds(2);
    static constexpr auto heartbeatInterval = std::chrono::seconds(3);
    static constexpr auto livenessTimeout = std::chrono::seconds(9);

    CompanionService(connectivity::ICompanionTransport& transport,
                     core::CapabilityRegistry& capabilities, core::Logger* logger = nullptr);

    void update(std::chrono::milliseconds elapsed);
    CompanionServiceState state() const noexcept { return state_; }
    std::uint16_t session() const noexcept { return session_; }
    bool hasLiveCompanion() const noexcept { return state_ == CompanionServiceState::Ready; }
    CompanionSubmitResult requestActiveApplication();
    CompanionSubmitResult activateApplication(std::string_view bundleId);
    std::optional<CompanionCompletedRequest> takeCompletedRequest();
    bool readActiveBundleIdentifier(char* destination, std::size_t capacity,
                                    std::uint8_t& length) const;

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
    void clearLiveCapabilities();
    void publishLiveCapabilities();
    bool sendMessage(const connectivity::CompanionEnvelope& message);
    void handleIncoming(const connectivity::CompanionPayload& payload);
    void handleHello(const connectivity::CompanionEnvelope& message);
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

    connectivity::ICompanionTransport& transport_;
    core::CapabilityRegistry& capabilities_;
    core::Logger* logger_ = nullptr;
    CompanionServiceState state_ = CompanionServiceState::Unavailable;
    std::uint16_t session_ = 0;
    std::uint16_t nextSession_ = 1;
    std::uint8_t nextRequestId_ = 1;
    std::array<PendingRequest, connectivity::companionMaxOutstandingRequests> pending_{};
    std::array<CompanionCompletedRequest, connectivity::companionMaxOutstandingRequests>
        completed_{};
    std::uint8_t completedHead_ = 0;
    std::uint8_t completedCount_ = 0;
    std::array<char, connectivity::companionMaxBundleIdSize + 1> activeBundle_{};
    std::uint8_t activeBundleLength_ = 0;
    bool hasActiveBundle_ = false;
    std::array<connectivity::CompanionCapability, connectivity::companionMaxCapabilities>
        liveCapabilities_{};
    std::uint8_t liveCapabilityCount_ = 0;
    bool companionPublished_ = false;
    std::chrono::milliseconds sinceHeartbeat_{0};
    std::chrono::milliseconds sinceHeartbeatSend_{0};
    bool heartbeatInFlight_ = false;
};

} // namespace cardputer_hub::services
