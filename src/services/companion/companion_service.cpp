#include "services/companion/companion_service.h"

#include "connectivity/companion/companion_fingerprint.h"
#include "core/lifecycle/build_info.h"

#include <cstring>

namespace cardputer_hub::services {
namespace {

using connectivity::companionCapabilityId;
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
using connectivity::makeRequest;
using connectivity::readBundleIdentifier;
using connectivity::readPingToken;
using connectivity::setBundleIdentifier;
using connectivity::setPingToken;

CompanionPayload toPayload(const connectivity::CompanionEncodedMessage& encoded) {
    CompanionPayload payload{};
    payload.size = encoded.size;
    std::memcpy(payload.bytes.data(), encoded.bytes.data(), encoded.size);
    return payload;
}

// Build ids start with the build date, "YYYY-MM-DD".
bool buildDate(const char* buildId, char (&date)[11]) {
    if (buildId == nullptr || std::strlen(buildId) < 10)
        return false;
    for (int index = 0; index < 10; ++index) {
        const char c = buildId[index];
        const bool digit = c >= '0' && c <= '9';
        if ((index == 4 || index == 7) ? c != '-' : !digit)
            return false;
        date[index] = c;
    }
    date[10] = '\0';
    return true;
}

} // namespace

CompanionMismatchAdvice companionMismatchAdvice(const char* firmwareBuildId,
                                                const char* peerBuildId, bool peerLegacy) {
    if (peerLegacy)
        return CompanionMismatchAdvice::UpdateCompanion;
    char firmware[11]{};
    char peer[11]{};
    if (!buildDate(firmwareBuildId, firmware) || !buildDate(peerBuildId, peer))
        return CompanionMismatchAdvice::RebuildBoth;
    const auto order = std::strcmp(peer, firmware);
    if (order < 0)
        return CompanionMismatchAdvice::UpdateCompanion;
    if (order > 0)
        return CompanionMismatchAdvice::UpdateFirmware;
    return CompanionMismatchAdvice::RebuildBoth;
}

CompanionMismatchAdvice CompanionService::mismatchAdvice() const noexcept {
    return companionMismatchAdvice(core::firmwareBuildInfo().buildId, peerBuildId_.data(),
                                   peerLegacy_);
}

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
}

void CompanionService::publishLiveCapabilities() {
    if (!companionPublished_) {
        (void)capabilities_.registerCapability(companionCapabilityId);
        companionPublished_ = true;
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

void CompanionService::endSessionState() {
    inboundCount_ = 0;
    ++sessionEpoch_;
}

void CompanionService::completePending(PendingRequest& pending, const CompanionEnvelope& message) {
    if (!pending.heartbeat && !(state_ == CompanionServiceState::Handshaking &&
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
    if (state_ != CompanionServiceState::Unavailable)
        endSessionState();
    failAllPending(CompanionStatus::NotAvailable);
    clearLiveCapabilities();
    hasActiveBundle_ = false;
    activeBundleLength_ = 0;
    activeBundle_ = {};
    heartbeatInFlight_ = false;
    sinceHeartbeat_ = {};
    sinceHeartbeatSend_ = {};
    peerBuildId_ = {};
    peerLegacy_ = false;
    if (state_ != CompanionServiceState::Unavailable) {
        log(core::LogLevel::Info, "session unavailable");
    }
    state_ = CompanionServiceState::Unavailable;
}

void CompanionService::enterIncompatible(const char* peerBuildId, bool legacy) {
    endSessionState();
    failAllPending(CompanionStatus::NotAvailable);
    clearLiveCapabilities();
    hasActiveBundle_ = false;
    activeBundleLength_ = 0;
    heartbeatInFlight_ = false;
    session_ = 0;
    peerBuildId_ = {};
    std::strncpy(peerBuildId_.data(), peerBuildId, peerBuildId_.size() - 1);
    peerLegacy_ = legacy;
    state_ = CompanionServiceState::Incompatible;
    log(core::LogLevel::Warning, "companion built from a different protocol");
}

void CompanionService::enterProtocolError() {
    endSessionState();
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
    return submit(CompanionOperation::AppActive,
                  makeRequest(session_, 0, CompanionOperation::AppActive),
                  false) == CompanionSubmitResult::Submitted;
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
    if (state_ != CompanionServiceState::Ready)
        return CompanionSubmitResult::NotReady;
    return submit(CompanionOperation::SystemMetrics,
                  makeRequest(session_, 0, CompanionOperation::SystemMetrics), false);
}

CompanionSubmitResult
CompanionService::requestSystemDetails(connectivity::SystemDetailsGroup group) {
    if (state_ != CompanionServiceState::Ready)
        return CompanionSubmitResult::NotReady;
    auto request = makeRequest(session_, 0, CompanionOperation::SystemDetails);
    if (!connectivity::setSystemDetailsRequest(request, group))
        return CompanionSubmitResult::Invalid;
    return submit(CompanionOperation::SystemDetails, request, false);
}

CompanionSubmitResult CompanionService::requestAiUsage() {
    if (state_ != CompanionServiceState::Ready)
        return CompanionSubmitResult::NotReady;
    return submit(CompanionOperation::AiUsage,
                  makeRequest(session_, 0, CompanionOperation::AiUsage), false);
}

CompanionSubmitResult CompanionService::requestAgentStatus() {
    if (state_ != CompanionServiceState::Ready)
        return CompanionSubmitResult::NotReady;
    return submit(CompanionOperation::AiAgentStatus,
                  makeRequest(session_, 0, CompanionOperation::AiAgentStatus), false);
}

CompanionSubmitResult CompanionService::requestServiceStatus(std::string_view url) {
    if (state_ != CompanionServiceState::Ready)
        return CompanionSubmitResult::NotReady;
    auto message = makeRequest(session_, 0, CompanionOperation::ServiceStatus);
    if (!connectivity::setServiceStatusRequest(message, url))
        return CompanionSubmitResult::Invalid;
    return submit(CompanionOperation::ServiceStatus, message, false);
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

bool CompanionService::sendHelloAck(std::uint16_t session, CompanionStatus status) {
    connectivity::CompanionHello hello{};
    hello.fingerprint = connectivity::companionProtocolFingerprint;
    std::strncpy(hello.buildId.data(), core::firmwareBuildInfo().buildId, hello.buildId.size() - 1);
    const auto ack = connectivity::makeHelloAck(session, status, hello);
    return ack.has_value() && sendMessage(*ack);
}

void CompanionService::handleHello(const CompanionEnvelope& message) {
    connectivity::CompanionHello hello{};
    if (!connectivity::readHello(message, hello)) {
        enterProtocolError();
        return;
    }
    if (hello.fingerprint != connectivity::companionProtocolFingerprint) {
        // Tell the Mac which firmware it met, then accept nothing but a new HELLO.
        (void)sendHelloAck(0, CompanionStatus::Unsupported);
        enterIncompatible(hello.buildId.data(), false);
        return;
    }
    endSessionState();
    failAllPending(CompanionStatus::NotAvailable);
    clearLiveCapabilities();
    hasActiveBundle_ = false;
    peerBuildId_ = hello.buildId;
    peerLegacy_ = false;
    session_ = nextSession_;
    nextSession_ = nextSession_ == 65535 ? 1 : static_cast<std::uint16_t>(nextSession_ + 1);
    nextRequestId_ = 1;
    heartbeatInFlight_ = false;
    sinceHeartbeat_ = {};
    sinceHeartbeatSend_ = {};
    if (!sendHelloAck(session_, CompanionStatus::Ok)) {
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
    const auto* bytes = payload.bytes.data();
    if (connectivity::isLegacyCompanionFrame(bytes, payload.size)) {
        // A Companion built before plan 043: say so once, never parse it.
        if (connectivity::isLegacyCompanionHello(bytes, payload.size))
            enterIncompatible("", true);
        return;
    }
    const auto decoded = decodeCompanionMessage(bytes, payload.size);
    if (!decoded.has_value()) {
        if (state_ == CompanionServiceState::Incompatible)
            return;
        const auto operation =
            payload.size >= connectivity::companionEnvelopeSize ? bytes[5] : std::uint8_t{0};
        const bool isolated = connectivity::isKnownCompanionOperation(operation) &&
                              connectivity::companionResponseFailureIsIsolated(
                                  static_cast<CompanionOperation>(operation));
        if (isolated && bytes[0] == connectivity::companionFrameMarker &&
            bytes[1] == static_cast<std::uint8_t>(CompanionKind::Response) &&
            (std::uint16_t(bytes[2]) | (std::uint16_t(bytes[3]) << 8U)) == session_) {
            if (auto* pending = findPending(bytes[4]);
                pending != nullptr &&
                pending->operation == static_cast<CompanionOperation>(operation)) {
                completePending(*pending, makeResponse(session_, pending->id, pending->operation,
                                                       CompanionStatus::Malformed));
            }
            return;
        }
        if (state_ != CompanionServiceState::Unavailable)
            enterProtocolError();
        return;
    }
    if (state_ == CompanionServiceState::Incompatible && decoded->kind != CompanionKind::Hello)
        return;
    if ((decoded->kind == CompanionKind::Response || decoded->kind == CompanionKind::Event) &&
        decoded->session != session_)
        return;
    if (decoded->kind == CompanionKind::Request &&
        connectivity::isInventoryOperation(decoded->operation)) {
        handleInboundRequest(*decoded);
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
        if (state_ != CompanionServiceState::Unavailable)
            enterProtocolError();
        return;
    }
}

// Inventory requests from the Mac are queued for their owner. Requests from an
// earlier session are stale and dropped; requests received during the final
// handshake step wait for Ready.
void CompanionService::handleInboundRequest(const CompanionEnvelope& message) {
    // The Mac starts inventory loading as soon as it receives HELLO_ACK. Its
    // request can arrive before our APP_ACTIVE handshake response, so hold it
    // until the session becomes Ready.
    if (state_ != CompanionServiceState::Ready && state_ != CompanionServiceState::Handshaking) {
        if (state_ != CompanionServiceState::Unavailable)
            enterProtocolError();
        return;
    }
    if (message.session != session_)
        return;
    const CompanionInboundRequest request{message.session, message.requestId, message.operation,
                                          message};
    if (inboundCount_ >= inbound_.size()) {
        // respond() is Ready-only, but the Mac can fill the queue before the
        // final handshake response arrives. The session is already known.
        (void)sendMessage(connectivity::makeResponse(session_, message.requestId, message.operation,
                                                     CompanionStatus::NotAvailable));
        return;
    }
    inbound_[inboundCount_++] = request;
}

std::optional<CompanionInboundRequest> CompanionService::takeInboundRequest() {
    if (state_ != CompanionServiceState::Ready || inboundCount_ == 0)
        return std::nullopt;
    const auto request = inbound_[0];
    for (std::uint8_t index = 1; index < inboundCount_; ++index)
        inbound_[index - 1] = inbound_[index];
    --inboundCount_;
    return request;
}

bool CompanionService::respond(const CompanionInboundRequest& request,
                               const CompanionEnvelope& response) {
    if (state_ != CompanionServiceState::Ready || request.session != session_)
        return false;
    auto outgoing = response;
    outgoing.kind = CompanionKind::Response;
    outgoing.session = request.session;
    outgoing.requestId = request.requestId;
    outgoing.operation = request.operation;
    return sendMessage(outgoing);
}

void CompanionService::tickPending(std::chrono::milliseconds elapsed) {
    for (auto& pending : pending_) {
        if (!pending.used) {
            continue;
        }
        const auto timeout =
            pending.operation == CompanionOperation::AiUsage         ? aiUsageRequestTimeout
            : pending.operation == CompanionOperation::ServiceStatus ? serviceStatusRequestTimeout
                                                                     : requestTimeout;
        if (elapsed >= timeout - pending.elapsed) {
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
