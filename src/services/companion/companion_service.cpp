#include "services/companion/companion_service.h"

#include <cstring>

namespace cardputer_hub::services {
namespace {

using connectivity::CompanionCapability;
using connectivity::companionCapabilityId;
using connectivity::companionCapabilityName;
using connectivity::CompanionEnvelope;
using connectivity::CompanionKind;
using connectivity::companionMaxBundleIdSize;
using connectivity::CompanionOperation;
using connectivity::CompanionPayload;
using connectivity::companionPingTokenSize;
using connectivity::CompanionStatus;
using connectivity::CompanionTransportState;
using connectivity::decodeCompanionMessage;
using connectivity::encodeCompanionMessage;
using connectivity::makeHelloAck;
using connectivity::makeRequest;
using connectivity::readBundleIdentifier;
using connectivity::readCapabilityList;
using connectivity::readPingToken;
using connectivity::setBundleIdentifier;
using connectivity::setPingToken;

std::uint8_t selectedVersion(const CompanionEnvelope& message) {
    if (message.kind != CompanionKind::Hello || message.payloadSize < 2 ||
        message.payload[0] == 0 ||
        message.payloadSize != static_cast<std::uint8_t>(message.payload[0] + 1) ||
        message.payload[0] > connectivity::companionMaxSupportedVersions) {
        return 0;
    }
    std::uint8_t selected = 0;
    for (std::uint8_t index = 0; index < message.payload[0]; ++index) {
        const auto version = message.payload[index + 1];
        if (version >= connectivity::companionProtocolVersion &&
            version <= connectivity::companionLatestProtocolVersion && version > selected)
            selected = version;
    }
    return selected;
}

CompanionPayload toPayload(const connectivity::CompanionEncodedMessage& encoded) {
    CompanionPayload payload{};
    payload.size = encoded.size;
    std::memcpy(payload.bytes.data(), encoded.bytes.data(), encoded.size);
    return payload;
}

} // namespace

CompanionService::CompanionService(connectivity::ICompanionTransport& transport,
                                   core::CapabilityRegistry& capabilities, core::Logger* logger)
    : transport_(transport), capabilities_(capabilities), logger_(logger) {}

void CompanionService::log(core::LogLevel level, const char* message) const {
    if (logger_ != nullptr) {
        logger_->log({level, "companion", message});
    }
}

void CompanionService::clearLiveCapabilities() {
    if (companionPublished_) {
        (void)capabilities_.removeCapability(companionCapabilityId);
        companionPublished_ = false;
    }
    for (std::uint8_t index = 0; index < liveCapabilityCount_; ++index) {
        if (const auto* name = companionCapabilityName(liveCapabilities_[index]); name != nullptr) {
            (void)capabilities_.removeCapability(name);
        }
    }
    liveCapabilityCount_ = 0;
    liveCapabilities_ = {};
}

void CompanionService::publishLiveCapabilities() {
    if (!companionPublished_) {
        (void)capabilities_.registerCapability(companionCapabilityId);
        companionPublished_ = true;
    }
    for (std::uint8_t index = 0; index < liveCapabilityCount_; ++index) {
        if (const auto* name = companionCapabilityName(liveCapabilities_[index]); name != nullptr) {
            (void)capabilities_.registerCapability(name);
        }
    }
}

void CompanionService::failAllPending(CompanionStatus status) {
    for (auto& pending : pending_) {
        if (!pending.used) {
            continue;
        }
        completePending(
            pending, connectivity::makeResponse(session_, pending.id, pending.operation, status));
    }
}

void CompanionService::completePending(PendingRequest& pending, const CompanionEnvelope& message) {
    if (!pending.heartbeat && pending.operation != CompanionOperation::Capabilities &&
        !(state_ == CompanionServiceState::Handshaking &&
          pending.operation == CompanionOperation::AppActive)) {
        pushCompleted(pending, message);
    }
    if (pending.heartbeat) {
        heartbeatInFlight_ = false;
    }
    pending = {};
}

void CompanionService::pushCompleted(const PendingRequest& pending,
                                     const CompanionEnvelope& message) {
    if (completedCount_ == completed_.size()) {
        completedHead_ = static_cast<std::uint8_t>((completedHead_ + 1) % completed_.size());
        --completedCount_;
    }
    const auto tail =
        static_cast<std::uint8_t>((completedHead_ + completedCount_) % completed_.size());
    completed_[tail] =
        CompanionCompletedRequest{pending.id, pending.operation, message.status, message};
    ++completedCount_;
}

CompanionService::PendingRequest* CompanionService::findPending(std::uint8_t id) {
    for (auto& pending : pending_) {
        if (pending.used && pending.id == id) {
            return &pending;
        }
    }
    return nullptr;
}

std::uint8_t CompanionService::pendingCount() const noexcept {
    std::uint8_t count = 0;
    for (const auto& pending : pending_) {
        if (pending.used) {
            ++count;
        }
    }
    return count;
}

void CompanionService::becomeUnavailable() {
    failAllPending(CompanionStatus::NotAvailable);
    clearLiveCapabilities();
    hasActiveBundle_ = false;
    activeBundleLength_ = 0;
    activeBundle_ = {};
    heartbeatInFlight_ = false;
    sinceHeartbeat_ = {};
    sinceHeartbeatSend_ = {};
    if (state_ != CompanionServiceState::Unavailable) {
        log(core::LogLevel::Info, "session unavailable");
    }
    state_ = CompanionServiceState::Unavailable;
}

void CompanionService::enterProtocolError() {
    failAllPending(CompanionStatus::Malformed);
    clearLiveCapabilities();
    heartbeatInFlight_ = false;
    state_ = CompanionServiceState::ProtocolError;
    log(core::LogLevel::Warning, "protocol error");
}

bool CompanionService::sendMessage(const CompanionEnvelope& message) {
    const auto encoded = encodeCompanionMessage(message);
    if (!encoded.has_value()) {
        return false;
    }
    return transport_.send(toPayload(*encoded)) == connectivity::CompanionSendResult::Sent;
}

bool CompanionService::startHandshakeRequests() {
    auto capabilities = makeRequest(session_, 0, CompanionOperation::Capabilities);
    return submit(CompanionOperation::Capabilities, capabilities, false) ==
           CompanionSubmitResult::Submitted;
}

CompanionSubmitResult CompanionService::submit(CompanionOperation operation,
                                               const CompanionEnvelope& message, bool heartbeat) {
    if (state_ != CompanionServiceState::Ready && state_ != CompanionServiceState::Handshaking) {
        return CompanionSubmitResult::NotReady;
    }
    if (pendingCount() >= connectivity::companionMaxOutstandingRequests) {
        return CompanionSubmitResult::Busy;
    }
    PendingRequest* slot = nullptr;
    for (auto& pending : pending_) {
        if (!pending.used) {
            slot = &pending;
            break;
        }
    }
    if (slot == nullptr) {
        return CompanionSubmitResult::Busy;
    }
    auto outgoing = message;
    outgoing.session = session_;
    outgoing.kind = CompanionKind::Request;
    outgoing.operation = operation;
    outgoing.requestId = nextRequestId_;
    outgoing.version = selectedProtocolVersion_;
    nextRequestId_ = nextRequestId_ == 255 ? 1 : static_cast<std::uint8_t>(nextRequestId_ + 1);
    if (!sendMessage(outgoing)) {
        return CompanionSubmitResult::NotReady;
    }
    lastSubmittedRequestId_ = outgoing.requestId;
    *slot = PendingRequest{outgoing.requestId, operation, {}, true, heartbeat, {}};
    if (heartbeat) {
        (void)readPingToken(outgoing, slot->pingToken);
        heartbeatInFlight_ = true;
        sinceHeartbeatSend_ = {};
    }
    return CompanionSubmitResult::Submitted;
}

CompanionSubmitResult CompanionService::requestActiveApplication() {
    if (state_ != CompanionServiceState::Ready) {
        return CompanionSubmitResult::NotReady;
    }
    return submit(CompanionOperation::AppActive,
                  makeRequest(session_, 0, CompanionOperation::AppActive), false);
}

CompanionSubmitResult CompanionService::activateApplication(std::string_view bundleId) {
    if (state_ != CompanionServiceState::Ready) {
        return CompanionSubmitResult::NotReady;
    }
    auto request = makeRequest(session_, 0, CompanionOperation::AppActivate);
    if (!setBundleIdentifier(request, bundleId)) {
        return CompanionSubmitResult::Invalid;
    }
    return submit(CompanionOperation::AppActivate, request, false);
}

CompanionSubmitResult CompanionService::requestSystemMetrics() {
    if (state_ != CompanionServiceState::Ready ||
        selectedProtocolVersion_ != connectivity::companionLatestProtocolVersion ||
        !capabilities_.isAvailable(connectivity::companionSystemMetricsCapabilityId))
        return CompanionSubmitResult::NotReady;
    return submit(CompanionOperation::SystemMetrics,
                  makeRequest(session_, 0, CompanionOperation::SystemMetrics), false);
}

bool CompanionService::hasPendingRequest(CompanionOperation operation) const noexcept {
    for (const auto& pending : pending_)
        if (pending.used && pending.operation == operation)
            return true;
    return false;
}

std::optional<CompanionCompletedRequest> CompanionService::takeCompletedRequest() {
    if (completedCount_ == 0) {
        return std::nullopt;
    }
    const auto completed = completed_[completedHead_];
    completedHead_ = static_cast<std::uint8_t>((completedHead_ + 1) % completed_.size());
    --completedCount_;
    return completed;
}

std::optional<CompanionCompletedRequest>
CompanionService::takeCompletedRequest(CompanionOperation operation) {
    for (std::uint8_t offset = 0; offset < completedCount_; ++offset) {
        const auto index = static_cast<std::uint8_t>((completedHead_ + offset) % completed_.size());
        if (completed_[index].operation != operation)
            continue;
        const auto result = completed_[index];
        for (std::uint8_t move = offset; move + 1 < completedCount_; ++move) {
            const auto from =
                static_cast<std::uint8_t>((completedHead_ + move + 1) % completed_.size());
            const auto to = static_cast<std::uint8_t>((completedHead_ + move) % completed_.size());
            completed_[to] = completed_[from];
        }
        --completedCount_;
        return result;
    }
    return std::nullopt;
}

bool CompanionService::readActiveBundleIdentifier(char* destination, std::size_t capacity,
                                                  std::uint8_t& length) const {
    length = 0;
    if (!hasActiveBundle_ || destination == nullptr || capacity == 0 ||
        activeBundleLength_ >= capacity) {
        return false;
    }
    std::memcpy(destination, activeBundle_.data(), activeBundleLength_);
    destination[activeBundleLength_] = '\0';
    length = activeBundleLength_;
    return true;
}

bool CompanionService::setActiveBundle(const CompanionEnvelope& message, bool allowEmpty) {
    if (message.status == CompanionStatus::NotAvailable || message.payloadSize == 0) {
        if (!allowEmpty && message.status != CompanionStatus::NotAvailable) {
            return false;
        }
        hasActiveBundle_ = false;
        activeBundleLength_ = 0;
        return true;
    }
    char bundle[companionMaxBundleIdSize + 1]{};
    std::uint8_t length = 0;
    if (!readBundleIdentifier(message, bundle, sizeof(bundle), length)) {
        return false;
    }
    std::memcpy(activeBundle_.data(), bundle, length);
    activeBundle_[length] = '\0';
    activeBundleLength_ = length;
    hasActiveBundle_ = true;
    return true;
}

void CompanionService::handleHello(const CompanionEnvelope& message) {
    const auto version = selectedVersion(message);
    if (message.version != connectivity::companionProtocolVersion || version == 0) {
        enterProtocolError();
        return;
    }
    failAllPending(CompanionStatus::NotAvailable);
    clearLiveCapabilities();
    hasActiveBundle_ = false;
    session_ = nextSession_;
    selectedProtocolVersion_ = version;
    nextSession_ = nextSession_ == 65535 ? 1 : static_cast<std::uint16_t>(nextSession_ + 1);
    nextRequestId_ = 1;
    heartbeatInFlight_ = false;
    sinceHeartbeat_ = {};
    sinceHeartbeatSend_ = {};
    if (!sendMessage(makeHelloAck(session_, version))) {
        becomeUnavailable();
        return;
    }
    state_ = CompanionServiceState::Handshaking;
    if (!startHandshakeRequests()) {
        enterProtocolError();
    }
}

void CompanionService::handleResponse(const CompanionEnvelope& message) {
    if (message.session != session_) {
        return;
    }
    auto* pending = findPending(message.requestId);
    if (pending == nullptr || pending->operation != message.operation) {
        return;
    }
    if (pending->heartbeat) {
        std::array<std::uint8_t, companionPingTokenSize> token{};
        if (message.status != CompanionStatus::Ok || !readPingToken(message, token) ||
            token != pending->pingToken) {
            completePending(*pending, message);
            enterProtocolError();
            return;
        }
        sinceHeartbeat_ = {};
        completePending(*pending, message);
        return;
    }
    if (state_ == CompanionServiceState::Handshaking &&
        pending->operation == CompanionOperation::Capabilities) {
        if (message.status != CompanionStatus::Ok ||
            !readCapabilityList(message, liveCapabilities_.data(), liveCapabilityCount_,
                                connectivity::companionMaxCapabilities)) {
            completePending(*pending, message);
            enterProtocolError();
            return;
        }
        completePending(*pending, message);
        auto active = makeRequest(session_, 0, CompanionOperation::AppActive);
        if (submit(CompanionOperation::AppActive, active, false) !=
            CompanionSubmitResult::Submitted) {
            enterProtocolError();
        }
        return;
    }
    if (state_ == CompanionServiceState::Handshaking &&
        pending->operation == CompanionOperation::AppActive) {
        if (message.status != CompanionStatus::Ok &&
            message.status != CompanionStatus::NotAvailable) {
            completePending(*pending, message);
            enterProtocolError();
            return;
        }
        if (!setActiveBundle(message, true)) {
            completePending(*pending, message);
            enterProtocolError();
            return;
        }
        completePending(*pending, message);
        publishLiveCapabilities();
        sinceHeartbeat_ = {};
        sinceHeartbeatSend_ = {};
        state_ = CompanionServiceState::Ready;
        log(core::LogLevel::Info, "session ready");
        return;
    }
    if (pending->operation == CompanionOperation::AppActive &&
        (message.status == CompanionStatus::Ok ||
         message.status == CompanionStatus::NotAvailable)) {
        if (!setActiveBundle(message, true)) {
            completePending(*pending, makeResponse(session_, pending->id, pending->operation,
                                                   CompanionStatus::Malformed));
            enterProtocolError();
            return;
        }
    }
    completePending(*pending, message);
}

void CompanionService::handleEvent(const CompanionEnvelope& message) {
    if (state_ != CompanionServiceState::Ready || message.session != session_ ||
        message.operation != CompanionOperation::AppActiveChanged) {
        return;
    }
    if (message.status != CompanionStatus::Ok) {
        enterProtocolError();
        return;
    }
    if (message.payloadSize == 0) {
        hasActiveBundle_ = false;
        activeBundleLength_ = 0;
        return;
    }
    if (!setActiveBundle(message, false)) {
        enterProtocolError();
    }
}

void CompanionService::handleIncoming(const CompanionPayload& payload) {
    const auto decoded = decodeCompanionMessage(payload.bytes.data(), payload.size);
    if (!decoded.has_value()) {
        if (state_ != CompanionServiceState::Unavailable) {
            enterProtocolError();
        }
        return;
    }
    if ((decoded->kind == CompanionKind::Response || decoded->kind == CompanionKind::Event) &&
        decoded->session != session_)
        return;
    if (decoded->kind != CompanionKind::Hello && decoded->kind != CompanionKind::HelloAck &&
        decoded->version != selectedProtocolVersion_) {
        if (state_ != CompanionServiceState::Unavailable)
            enterProtocolError();
        return;
    }
    switch (decoded->kind) {
    case CompanionKind::Hello:
        handleHello(*decoded);
        return;
    case CompanionKind::Response:
        handleResponse(*decoded);
        return;
    case CompanionKind::Event:
        handleEvent(*decoded);
        return;
    case CompanionKind::HelloAck:
    case CompanionKind::Request:
        if (state_ != CompanionServiceState::Unavailable) {
            enterProtocolError();
        }
        return;
    }
}

void CompanionService::tickPending(std::chrono::milliseconds elapsed) {
    for (auto& pending : pending_) {
        if (!pending.used) {
            continue;
        }
        if (elapsed >= requestTimeout - pending.elapsed) {
            const auto handshake =
                state_ == CompanionServiceState::Handshaking && !pending.heartbeat;
            completePending(pending,
                            connectivity::makeResponse(session_, pending.id, pending.operation,
                                                       CompanionStatus::Malformed));
            if (handshake) {
                enterProtocolError();
                return;
            }
            continue;
        }
        pending.elapsed += elapsed;
    }
}

void CompanionService::tickHeartbeat(std::chrono::milliseconds elapsed) {
    if (state_ != CompanionServiceState::Ready) {
        return;
    }
    if (elapsed >= livenessTimeout - sinceHeartbeat_) {
        log(core::LogLevel::Info, "heartbeat expired");
        becomeUnavailable();
        return;
    }
    sinceHeartbeat_ += elapsed;
    sinceHeartbeatSend_ += elapsed;
    if (heartbeatInFlight_ || sinceHeartbeatSend_ < heartbeatInterval) {
        return;
    }
    auto ping = makeRequest(session_, 0, CompanionOperation::Ping);
    const std::array<std::uint8_t, companionPingTokenSize> token{0xC0, 0x01, 0xBE, 0xA7};
    if (!setPingToken(ping, token)) {
        return;
    }
    (void)submit(CompanionOperation::Ping, ping, true);
}

void CompanionService::update(std::chrono::milliseconds elapsed) {
    if (elapsed < std::chrono::milliseconds::zero()) {
        elapsed = std::chrono::milliseconds::zero();
    }
    const auto transportState = transport_.state();
    if (transportState != CompanionTransportState::Ready) {
        becomeUnavailable();
        while (transport_.receive().has_value()) {
        }
        return;
    }
    if (state_ == CompanionServiceState::Unavailable ||
        state_ == CompanionServiceState::ProtocolError) {
        if (state_ == CompanionServiceState::Unavailable) {
            state_ = CompanionServiceState::Attaching;
            log(core::LogLevel::Info, "transport ready");
        }
    }
    while (const auto incoming = transport_.receive()) {
        handleIncoming(*incoming);
        if (state_ == CompanionServiceState::Unavailable) {
            return;
        }
    }
    tickPending(elapsed);
    tickHeartbeat(elapsed);
}

} // namespace cardputer_hub::services
