#include "services/host_control/host_control_service.h"

#include "connectivity/companion/companion_protocol.h"

#include <algorithm>
#include <string>

namespace cardputer_hub::services {
namespace {
template <typename T> const T* parameter(const core::Action& action, const char* name) {
    const auto* value = action.findParameter(name);
    return value ? std::get_if<T>(value) : nullptr;
}

bool selectedHostExists(const HostService& hosts) {
    const auto snapshot = hosts.status();
    if (!snapshot.activeHostId.has_value())
        return false;
    const auto& configured = hosts.settings();
    return std::any_of(configured.hosts.begin(), configured.hosts.end(),
                       [&](const auto& host) { return host.id == *snapshot.activeHostId; });
}

HostControlFailure failureFor(CompanionServiceState state,
                              connectivity::CompanionStatus status) noexcept {
    switch (status) {
    case connectivity::CompanionStatus::Ok:
        return HostControlFailure::None;
    case connectivity::CompanionStatus::NotFound:
        return HostControlFailure::NotFound;
    case connectivity::CompanionStatus::NotAvailable:
        return HostControlFailure::Unavailable;
    case connectivity::CompanionStatus::Unsupported:
        return HostControlFailure::ProtocolError;
    case connectivity::CompanionStatus::Malformed:
        return state == CompanionServiceState::ProtocolError ? HostControlFailure::ProtocolError
                                                             : HostControlFailure::Timeout;
    }
    return HostControlFailure::ProtocolError;
}
} // namespace

HostControlService::HostControlService(HostService& hosts, CompanionService& companion,
                                       core::CapabilityRegistry& capabilities)
    : hosts_(hosts), companion_(companion), capabilities_(capabilities) {}

void HostControlService::failPending(HostControlFailure failure) {
    status_.state = HostControlCommandState::Failed;
    status_.failure = failure;
}

void HostControlService::applyActivateCompletion(const CompanionCompletedRequest& completed) {
    if (status_.state != HostControlCommandState::Pending)
        return;
    if (completed.operation != connectivity::CompanionOperation::AppActivate)
        return;
    if (completed.status == connectivity::CompanionStatus::Ok) {
        status_.state = HostControlCommandState::Succeeded;
        status_.failure = HostControlFailure::None;
        return;
    }
    failPending(failureFor(companion_.state(), completed.status));
}

void HostControlService::update() {
    while (const auto completed =
               companion_.takeCompletedRequest(connectivity::CompanionOperation::AppActivate))
        applyActivateCompletion(*completed);
    if (status_.state == HostControlCommandState::Pending &&
        (!companion_.hasLiveCompanion() ||
         !capabilities_.isAvailable(connectivity::companionCapabilityId))) {
        failPending(HostControlFailure::Unavailable);
    }
}

core::ActionHandlingResult HostControlService::handle(const core::Action& action) {
    if (action.id != hostAppActivateActionId)
        return core::ActionHandlingResult::Rejected;
    if (status_.state == HostControlCommandState::Pending)
        return core::ActionHandlingResult::Rejected;
    if (!selectedHostExists(hosts_) ||
        !capabilities_.isAvailable(connectivity::companionCapabilityId)) {
        return core::ActionHandlingResult::Rejected;
    }
    const auto* bundleId = parameter<std::string>(action, hostAppActivateBundleParameter);
    if (bundleId == nullptr || bundleId->empty())
        return core::ActionHandlingResult::Rejected;

    const auto submitted = companion_.activateApplication(*bundleId);
    if (submitted != CompanionSubmitResult::Submitted)
        return core::ActionHandlingResult::Rejected;

    ++status_.generation;
    status_.state = HostControlCommandState::Pending;
    status_.failure = HostControlFailure::None;
    return core::ActionHandlingResult::Handled;
}

} // namespace cardputer_hub::services
