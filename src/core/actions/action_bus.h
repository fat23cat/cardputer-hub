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

enum class DispatchResult : std::uint8_t {
    Handled,
    Rejected,
    Invalid,
    Unsupported,
};

class ActionBus {
  public:
    RegistrationResult registerHandler(std::string_view actionId, IActionHandler& handler);
    DispatchResult dispatch(const Action& action);

  private:
    struct Registration {
        std::string actionId;
        IActionHandler* handler;
    };

    std::vector<Registration> registrations_;
};

} // namespace cardputer_hub::core
