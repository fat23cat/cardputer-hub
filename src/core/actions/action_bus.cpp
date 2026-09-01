#include "core/actions/action_bus.h"

#include <algorithm>

namespace cardputer_hub::core {
namespace {

bool isValid(const Action& action) {
    if (action.id.empty() || action.source.empty()) {
        return false;
    }

    for (auto parameter = action.parameters.cbegin(); parameter != action.parameters.cend();
         ++parameter) {
        if (parameter->name.empty()) {
            return false;
        }

        const bool duplicate =
            std::any_of(action.parameters.cbegin(), parameter, [&parameter](const auto& previous) {
                return previous.name == parameter->name;
            });
        if (duplicate) {
            return false;
        }
    }

    return true;
}

} // namespace

RegistrationResult ActionBus::registerHandler(std::string_view actionId, IActionHandler& handler) {
    if (actionId.empty()) {
        return RegistrationResult::InvalidId;
    }

    const bool duplicate = std::any_of(
        registrations_.cbegin(), registrations_.cend(),
        [actionId](const auto& registration) { return registration.actionId == actionId; });
    if (duplicate) {
        return RegistrationResult::DuplicateId;
    }

    registrations_.push_back({std::string(actionId), &handler});
    return RegistrationResult::Registered;
}

DispatchResult ActionBus::dispatch(const Action& action) {
    if (!isValid(action)) {
        return DispatchResult::Invalid;
    }

    const auto registration =
        std::find_if(registrations_.cbegin(), registrations_.cend(),
                     [&action](const auto& candidate) { return candidate.actionId == action.id; });
    if (registration == registrations_.cend()) {
        return DispatchResult::Unsupported;
    }

    const auto result = registration->handler->handle(action);
    return result == ActionHandlingResult::Handled ? DispatchResult::Handled
                                                   : DispatchResult::Rejected;
}

} // namespace cardputer_hub::core
