#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core/actions/action.h"

namespace cardputer_hub::core {

enum class ActionHandlingResult : std::uint8_t {
    Handled,
    Rejected,
};

class IActionHandler {
  public:
    virtual ~IActionHandler() = default;
    virtual ActionHandlingResult handle(const Action& action) = 0;
};

enum class RegistrationResult : std::uint8_t {
    Registered,
    InvalidId,
    DuplicateId,
};

struct RejectedRegistration {
    std::string actionId;
    RegistrationResult result;
};

enum class DispatchResult : std::uint8_t {
    Handled,
    Rejected,
    Invalid,
    Unsupported,
};

class ActionBus {
  public:
    RegistrationResult registerHandler(std::string_view actionId, IActionHandler& handler);
    // Registers each ID in order and returns the first failure, or Registered.
    template <typename ActionIds>
    RegistrationResult registerHandlers(const ActionIds& actionIds, IActionHandler& handler) {
        auto result = RegistrationResult::Registered;
        for (std::string_view actionId : actionIds) {
            const auto current = registerHandler(actionId, handler);
            if (result == RegistrationResult::Registered)
                result = current;
        }
        return result;
    }
    DispatchResult dispatch(const Action& action);
    // Every refused registration, kept so startup can report wiring errors
    // that individual callers did not check.
    const std::vector<RejectedRegistration>& rejectedRegistrations() const noexcept {
        return rejected_;
    }

  private:
    struct Registration {
        std::string actionId;
        IActionHandler* handler;
    };

    std::vector<Registration> registrations_;
    std::vector<RejectedRegistration> rejected_;
};

} // namespace cardputer_hub::core
