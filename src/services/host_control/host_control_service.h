#pragma once

#include "core/actions/action_bus.h"
#include "core/capabilities/capability_registry.h"
#include "services/companion/companion_service.h"
#include "services/hosts/host_service.h"

#include <cstdint>

namespace cardputer_hub::services {

inline constexpr char hostAppActivateActionId[] = "host.app.activate";
inline constexpr char hostAppActivateBundleParameter[] = "bundleId";

enum class HostControlCommandState : std::uint8_t {
    Idle,
    Pending,
    Succeeded,
    Failed,
};

enum class HostControlFailure : std::uint8_t {
    None,
    NotFound,
    Unavailable,
    Busy,
    Invalid,
    Timeout,
    ProtocolError,
};

struct HostControlStatus {
    std::uint32_t generation = 0;
    HostControlCommandState state = HostControlCommandState::Idle;
    HostControlFailure failure = HostControlFailure::None;
};

class HostControlService final : public core::IActionHandler {
  public:
    HostControlService(HostService& hosts, CompanionService& companion,
                       core::CapabilityRegistry& capabilities);

    void update();
    [[nodiscard]] HostControlStatus status() const noexcept { return status_; }
    core::ActionHandlingResult handle(const core::Action& action) override;

  private:
    void failPending(HostControlFailure failure);
    void applyActivateCompletion(const CompanionCompletedRequest& completed);

    HostService& hosts_;
    CompanionService& companion_;
    core::CapabilityRegistry& capabilities_;
    HostControlStatus status_{};
};

} // namespace cardputer_hub::services
