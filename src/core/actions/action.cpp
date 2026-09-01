#include "core/actions/action.h"

#include <algorithm>

namespace cardputer_hub::core {

const ActionValue* Action::findParameter(std::string_view name) const noexcept {
    const auto parameter =
        std::find_if(parameters.cbegin(), parameters.cend(),
                     [name](const auto& candidate) { return candidate.name == name; });

    return parameter == parameters.cend() ? nullptr : &parameter->value;
}

} // namespace cardputer_hub::core
