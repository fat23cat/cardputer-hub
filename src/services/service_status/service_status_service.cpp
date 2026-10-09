#include "services/service_status/service_status_service.h"

#include "services/audio/audio_service.h"
#include "services/service_status/status_page.h"

#include <cstring>

namespace cardputer_hub::services {
using connectivity::CompanionOperation;
using connectivity::CompanionStatus;
using connectivity::HttpRequestState;
using connectivity::ServiceStatusLevel;

void ServiceStatusService::setActive(bool active) {
    if (active == active_)
        return;
    active_ = active;
    if (active) {
        startRound();
        return;
    }
    // Closing abandons the round; its answers arrive to nobody.
    if (fetch_ == Fetch::WiFi)
        http_.reset();
    fetch_ = Fetch::None;
    faultPending_ = false;
    if (roundActive_) {
        roundActive_ = false;
        snapshot_.checking = false;
        ++snapshot_.revision;
    }
}

core::ActionHandlingResult ServiceStatusService::handle(const core::Action& action) {
    if (action.id != serviceStatusRefreshActionId || !active_)
        return core::ActionHandlingResult::Rejected;
    if (!roundActive_)
        startRound();
    return core::ActionHandlingResult::Handled;
}

void ServiceStatusService::startRound() {
    roundActive_ = true;
    alerted_ = false;
    current_ = 0;
    wifiFailed_ = false;
    waitElapsed_ = {};
    snapshot_.sinceRound = {};
    snapshot_.checking = true;
    ++snapshot_.revision;
    startCurrent(true);
}

void ServiceStatusService::startCurrent(bool allowWiFi) {
    fetch_ = Fetch::None;
    const auto& source = statusSources[current_];
    const bool wifi = wifi_.state() == connectivity::WifiState::Connected;
    if (allowWiFi && wifi) {
        if (http_.state() != HttpRequestState::Running)
            http_.reset();
        switch (http_.start(source.url)) {
        case connectivity::HttpStartResult::Started:
            fetch_ = Fetch::WiFi;
            fetchElapsed_ = {};
            return;
        case connectivity::HttpStartResult::Busy:
            // An abandoned request still holds the client. The Companion can
            // take the page meanwhile; without it, wait a bounded time.
            if (companionReady())
                break;
            if (waitElapsed_ < directFetchTimeout)
                return;
            fail(StatusProblem::Unreachable, StatusRoute::WiFi);
            advance();
            return;
        case connectivity::HttpStartResult::Failed:
            wifiFailed_ = true;
            break;
        }
    }
    if (companionReady()) {
        // A SERVICE_STATUS from before the app was last closed still holds a
        // Companion slot; asking again would stack requests and starve the
        // heartbeat. Wait for it to answer or time out.
        if (companion_.hasPendingRequest(CompanionOperation::ServiceStatus))
            return;
        switch (companion_.requestServiceStatus(source.url)) {
        case CompanionSubmitResult::Submitted:
            fetch_ = Fetch::Companion;
            requestId_ = companion_.lastSubmittedRequestId();
            session_ = companion_.session();
            return;
        case CompanionSubmitResult::NotReady:
        case CompanionSubmitResult::Busy:
            return; // retry on the next update
        case CompanionSubmitResult::Invalid:
            // The protocol rejects this URL; asking again cannot help.
            fail(StatusProblem::Unreachable, StatusRoute::Companion);
            advance();
            return;
        }
    }
    fail(wifi ? StatusProblem::Unreachable : StatusProblem::NoConnection,
         wifi ? StatusRoute::WiFi : StatusRoute::None);
    advance();
}

void ServiceStatusService::advance() {
    fetch_ = Fetch::None;
    wifiFailed_ = false;
    waitElapsed_ = {};
    if (++current_ >= statusSources.size()) {
        endRound();
        return;
    }
    startCurrent(true);
}

void ServiceStatusService::endRound() {
    roundActive_ = false;
    snapshot_.sinceRound = {};
    snapshot_.checking = false;
    ++snapshot_.revision;
}

void ServiceStatusService::record(const connectivity::CompanionServiceStatus& status,
                                  StatusRoute route) {
    auto& entry = snapshot_.entries[current_];
    auto& known = lastKnown_[current_];
    // Maintenance is announced in advance, so only an incident sounds.
    if (known != ServiceStatusLevel::Unknown && status.level > known &&
        status.level >= ServiceStatusLevel::Minor && !alerted_) {
        alerted_ = true;
        faultPending_ = true;
        faultWait_ = {};
        playPendingFault({});
    }
    known = status.level;
    entry.level = status.level;
    entry.description = status.description;
    entry.route = route;
    entry.problem = StatusProblem::None;
    entry.checked = true;
    entry.sinceCheck = {};
    ++snapshot_.revision;
}

void ServiceStatusService::fail(StatusProblem problem, StatusRoute route) {
    auto& entry = snapshot_.entries[current_];
    entry.level = ServiceStatusLevel::Unknown;
    entry.description = {};
    entry.route = route;
    entry.problem = problem;
    entry.checked = true;
    entry.sinceCheck = {};
    ++snapshot_.revision;
}

void ServiceStatusService::finishDirect(std::chrono::milliseconds elapsed) {
    const auto state = http_.state();
    if (state == HttpRequestState::Running) {
        fetchElapsed_ += elapsed;
        if (fetchElapsed_ < directFetchTimeout)
            return;
        http_.reset(); // abandoned: its late result is dropped
        fallBackFromWiFi();
        return;
    }
    if (state == HttpRequestState::Done && http_.status() >= 200 && http_.status() < 300) {
        const auto parsed = parseStatusPage(http_.body());
        http_.reset();
        if (parsed)
            record(*parsed, StatusRoute::WiFi);
        else
            fail(StatusProblem::Unreadable, StatusRoute::WiFi);
        advance();
        return;
    }
    http_.reset();
    fallBackFromWiFi();
}

void ServiceStatusService::fallBackFromWiFi() {
    // The Mac may still reach the page when this network cannot.
    wifiFailed_ = true;
    if (companionReady()) {
        startCurrent(false);
        return;
    }
    fail(StatusProblem::Unreachable, StatusRoute::WiFi);
    advance();
}

void ServiceStatusService::finishCompanion() {
    while (const auto completion =
               companion_.takeCompletedRequest(CompanionOperation::ServiceStatus)) {
        if (fetch_ != Fetch::Companion || completion->requestId != requestId_ ||
            !companionReady() || companion_.session() != session_)
            continue;
        connectivity::CompanionServiceStatus status{};
        if (completion->status == CompanionStatus::Ok &&
            connectivity::readServiceStatus(completion->message, status))
            record(status, StatusRoute::Companion);
        else
            fail(StatusProblem::Unreachable, StatusRoute::Companion);
        advance();
        return;
    }
    if (fetch_ == Fetch::Companion && (!companionReady() || companion_.session() != session_)) {
        // The session ended with the request: it is never replayed.
        fail(StatusProblem::Unreachable, StatusRoute::Companion);
        advance();
    }
}

void ServiceStatusService::publishLink(std::chrono::milliseconds elapsed) {
    const bool link = wifi_.state() == connectivity::WifiState::Connected || companionReady();
    if (link) {
        linkLostElapsed_ = {};
        if (!linkPublished_) {
            linkPublished_ = true;
            (void)capabilities_.registerCapability(serviceStatusLinkCapabilityId);
        }
        return;
    }
    if (!linkPublished_)
        return;
    linkLostElapsed_ += elapsed;
    if (linkLostElapsed_ < linkLossGrace)
        return;
    linkPublished_ = false;
    (void)capabilities_.removeCapability(serviceStatusLinkCapabilityId);
}

void ServiceStatusService::playPendingFault(std::chrono::milliseconds elapsed) {
    if (!faultPending_)
        return;
    if (audio_ != nullptr && audio_->play(AudioCue::Fault)) {
        faultPending_ = false;
        return;
    }
    faultWait_ += elapsed;
    if (audio_ == nullptr || faultWait_ >= faultRetryWindow)
        faultPending_ = false;
}

void ServiceStatusService::update(std::chrono::milliseconds elapsed) {
    if (elapsed < std::chrono::milliseconds::zero())
        elapsed = {};
    publishLink(elapsed);
    playPendingFault(elapsed);
    for (auto& entry : snapshot_.entries)
        if (entry.checked)
            entry.sinceCheck += elapsed;
    finishCompanion();
    if (!active_) {
        if (http_.state() == HttpRequestState::Done || http_.state() == HttpRequestState::Failed)
            http_.reset();
        return;
    }
    if (roundActive_) {
        if (fetch_ == Fetch::WiFi)
            finishDirect(elapsed);
        else if (fetch_ == Fetch::None) {
            waitElapsed_ += elapsed;
            startCurrent(!wifiFailed_);
        }
        return;
    }
    snapshot_.sinceRound += elapsed;
    if (snapshot_.sinceRound >= pollInterval)
        startRound();
}

} // namespace cardputer_hub::services
