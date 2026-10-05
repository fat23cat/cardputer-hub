#include "services/ai_agent_status/ai_agent_status_service.h"

namespace cardputer_hub::services {
using connectivity::CompanionOperation;
using connectivity::CompanionStatus;

namespace {
// Zero while no Companion session is live, so losing the link also clears.
std::uint16_t liveSession(const CompanionService& companion) {
    return companion.hasLiveCompanion() ? companion.session() : 0;
}
} // namespace

void AiAgentStatusService::startMonitoring() {
    while (companion_.takeCompletedRequest(CompanionOperation::AiAgentStatus)) {
    }
    monitoring_ = true;
    session_ = liveSession(companion_);
    clear();
    request();
}

void AiAgentStatusService::stopMonitoring() {
    monitoring_ = false;
    clear();
}

void AiAgentStatusService::clear() {
    const auto generation = snapshot_.generation + 1;
    snapshot_ = {};
    snapshot_.generation = generation;
    inFlight_ = false;
    pollElapsed_ = {};
    sinceSample_ = {};
}

void AiAgentStatusService::request() {
    if (!monitoring_ || inFlight_ ||
        companion_.hasPendingRequest(CompanionOperation::AiAgentStatus))
        return;
    if (companion_.requestAgentStatus() != CompanionSubmitResult::Submitted)
        return;
    inFlight_ = true;
    inFlightId_ = companion_.lastSubmittedRequestId();
}

void AiAgentStatusService::update(std::chrono::milliseconds elapsed) {
    if (elapsed < std::chrono::milliseconds::zero())
        elapsed = {};
    // A completion that arrives after stop, a host change or a session reset
    // belongs to nobody and is dropped here.
    while (const auto completion =
               companion_.takeCompletedRequest(CompanionOperation::AiAgentStatus)) {
        if (!monitoring_ || !inFlight_ || completion->requestId != inFlightId_ ||
            liveSession(companion_) != session_)
            continue;
        inFlight_ = false;
        connectivity::CompanionAgentStatus status{};
        if (completion->status != CompanionStatus::Ok ||
            !connectivity::readAgentStatus(completion->message, status))
            continue;
        const bool changed =
            snapshot_.freshness != AgentStatusFreshness::Fresh || snapshot_.status != status;
        snapshot_.status = status;
        snapshot_.freshness = AgentStatusFreshness::Fresh;
        sinceSample_ = {};
        if (changed)
            ++snapshot_.generation;
    }
    if (!monitoring_)
        return;
    if (liveSession(companion_) != session_) {
        // A new Companion session never inherits the previous host's states.
        session_ = liveSession(companion_);
        clear();
        request();
        return;
    }
    if (snapshot_.freshness == AgentStatusFreshness::Fresh) {
        sinceSample_ += elapsed;
        if (sinceSample_ > freshnessTimeout) {
            snapshot_.freshness = AgentStatusFreshness::Stale;
            ++snapshot_.generation;
        }
    }
    pollElapsed_ += elapsed;
    if (pollElapsed_ >= pollInterval) {
        pollElapsed_ %= pollInterval;
        request();
    }
}

} // namespace cardputer_hub::services
