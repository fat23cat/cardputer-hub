#pragma once

#include "apps/runtime/mini_app.h"
#include "core/app_registry/app_registry.h"
#include "core/capabilities/capability_registry.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace cardputer_hub::apps {

enum class MiniAppInstanceRegistrationResult : std::uint8_t {
    Registered,
    InvalidId,
    UnknownDescriptor,
    DuplicateId,
};

enum class MiniAppEligibility : std::uint8_t {
    Eligible,
    UnknownApp,
    MissingInstance,
    MissingCapability,
};

enum class MiniAppActivationResult : std::uint8_t {
    Activated,
    AlreadyActive,
    UnknownApp,
    MissingInstance,
    MissingCapability,
};

enum class MiniAppDeactivationResult : std::uint8_t {
    Deactivated,
    AlreadyInactive,
};

enum class MiniAppUpdateResult : std::uint8_t {
    Idle,
    Updated,
    DeactivatedMissingCapability,
};

struct MiniAppAvailability {
    MiniAppEligibility eligibility = MiniAppEligibility::UnknownApp;
    std::optional<std::string> missingCapability;
};

class MiniAppRuntime {
  public:
    MiniAppRuntime(const core::AppRegistry& apps, const core::CapabilityRegistry& capabilities);

    MiniAppInstanceRegistrationResult registerInstance(std::string appId, IMiniApp& app);

    [[nodiscard]] const core::AppRegistry& apps() const noexcept;
    [[nodiscard]] MiniAppEligibility eligibility(const std::string& appId) const;
    [[nodiscard]] MiniAppAvailability availability(const std::string& appId) const;

    MiniAppActivationResult activate(const std::string& appId);
    MiniAppDeactivationResult deactivate();
    MiniAppUpdateResult update(const core::InputEvents& input, std::chrono::milliseconds elapsed);

    [[nodiscard]] bool hasActiveApp() const noexcept;
    [[nodiscard]] std::optional<std::string> activeAppId() const;

  private:
    struct RegisteredInstance {
        std::string id;
        IMiniApp* app = nullptr;
    };

    [[nodiscard]] const RegisteredInstance* findInstance(const std::string& appId) const;
    [[nodiscard]] MiniAppEligibility eligibilityFor(const core::AppDescriptor* descriptor,
                                                    const RegisteredInstance* instance) const;
    [[nodiscard]] bool requiredCapabilitiesAvailable(const core::AppDescriptor& descriptor) const;
    [[nodiscard]] std::optional<std::string>
    firstMissingCapability(const core::AppDescriptor& descriptor) const;
    void clearActive();

    const core::AppRegistry& apps_;
    const core::CapabilityRegistry& capabilities_;
    std::vector<RegisteredInstance> instances_;
    std::optional<std::string> activeId_;
    IMiniApp* activeApp_ = nullptr;
};

} // namespace cardputer_hub::apps
