#include "apps/runtime/mini_app_runtime.h"

#include <algorithm>
#include <utility>

namespace cardputer_hub::apps {

MiniAppRuntime::MiniAppRuntime(const core::AppRegistry& apps,
                               const core::CapabilityRegistry& capabilities)
    : apps_(apps), capabilities_(capabilities) {}

MiniAppInstanceRegistrationResult MiniAppRuntime::registerInstance(std::string appId,
                                                                   IMiniApp& app) {
    if (appId.empty())
        return MiniAppInstanceRegistrationResult::InvalidId;
    if (apps_.find(appId) == nullptr)
        return MiniAppInstanceRegistrationResult::UnknownDescriptor;
    if (findInstance(appId) != nullptr)
        return MiniAppInstanceRegistrationResult::DuplicateId;

    instances_.push_back(RegisteredInstance{std::move(appId), &app});
    return MiniAppInstanceRegistrationResult::Registered;
}

const core::AppRegistry& MiniAppRuntime::apps() const noexcept { return apps_; }

MiniAppEligibility MiniAppRuntime::eligibility(const std::string& appId) const {
    return availability(appId).eligibility;
}

MiniAppAvailability MiniAppRuntime::availability(const std::string& appId) const {
    const auto* descriptor = apps_.find(appId);
    const auto* instance = findInstance(appId);
    MiniAppAvailability detail;
    detail.eligibility = eligibilityFor(descriptor, instance);
    if (detail.eligibility == MiniAppEligibility::MissingCapability && descriptor != nullptr)
        detail.missingCapability = firstMissingCapability(*descriptor);
    return detail;
}

MiniAppActivationResult MiniAppRuntime::activate(const std::string& appId) {
    if (activeId_ && *activeId_ == appId)
        return MiniAppActivationResult::AlreadyActive;

    const auto* instance = findInstance(appId);
    switch (eligibilityFor(apps_.find(appId), instance)) {
    case MiniAppEligibility::UnknownApp:
        return MiniAppActivationResult::UnknownApp;
    case MiniAppEligibility::MissingInstance:
        return MiniAppActivationResult::MissingInstance;
    case MiniAppEligibility::MissingCapability:
        return MiniAppActivationResult::MissingCapability;
    case MiniAppEligibility::Eligible:
        break;
    }

    if (activeApp_ != nullptr)
        activeApp_->onDeactivate();

    activeId_ = appId;
    activeApp_ = instance->app;
    activeApp_->onActivate();
    return MiniAppActivationResult::Activated;
}

MiniAppDeactivationResult MiniAppRuntime::deactivate() {
    if (activeApp_ == nullptr)
        return MiniAppDeactivationResult::AlreadyInactive;

    activeApp_->onDeactivate();
    clearActive();
    return MiniAppDeactivationResult::Deactivated;
}

MiniAppUpdateResult MiniAppRuntime::update(const core::InputEvents& input,
                                           std::chrono::milliseconds elapsed) {
    if (activeApp_ == nullptr || !activeId_)
        return MiniAppUpdateResult::Idle;

    if (eligibility(*activeId_) == MiniAppEligibility::MissingCapability) {
        activeApp_->onDeactivate();
        clearActive();
        return MiniAppUpdateResult::DeactivatedMissingCapability;
    }

    activeApp_->update(input, elapsed);
    return MiniAppUpdateResult::Updated;
}

bool MiniAppRuntime::hasActiveApp() const noexcept { return activeApp_ != nullptr; }

std::optional<std::string> MiniAppRuntime::activeAppId() const { return activeId_; }

const MiniAppRuntime::RegisteredInstance*
MiniAppRuntime::findInstance(const std::string& appId) const {
    const auto instance = std::find_if(
        instances_.begin(), instances_.end(),
        [&appId](const RegisteredInstance& registered) { return registered.id == appId; });
    return instance == instances_.end() ? nullptr : &*instance;
}

MiniAppEligibility MiniAppRuntime::eligibilityFor(const core::AppDescriptor* descriptor,
                                                  const RegisteredInstance* instance) const {
    if (descriptor == nullptr)
        return MiniAppEligibility::UnknownApp;
    if (instance == nullptr)
        return MiniAppEligibility::MissingInstance;
    if (!requiredCapabilitiesAvailable(*descriptor))
        return MiniAppEligibility::MissingCapability;
    return MiniAppEligibility::Eligible;
}

bool MiniAppRuntime::requiredCapabilitiesAvailable(const core::AppDescriptor& descriptor) const {
    return !firstMissingCapability(descriptor).has_value();
}

std::optional<std::string>
MiniAppRuntime::firstMissingCapability(const core::AppDescriptor& descriptor) const {
    const auto missing =
        std::find_if(descriptor.requiredCapabilities.begin(), descriptor.requiredCapabilities.end(),
                     [this](const std::string& capabilityId) {
                         return !capabilities_.isAvailable(capabilityId);
                     });
    if (missing == descriptor.requiredCapabilities.end())
        return std::nullopt;
    return *missing;
}

void MiniAppRuntime::clearActive() {
    activeId_.reset();
    activeApp_ = nullptr;
}

} // namespace cardputer_hub::apps
