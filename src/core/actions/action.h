#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace cardputer_hub::core {

using ActionValue = std::variant<bool, std::int32_t, std::string>;

struct ActionParameter {
    std::string name;
    ActionValue value;
};

struct Action {
    std::string id;
    std::string source;
    std::vector<ActionParameter> parameters;

    [[nodiscard]] const ActionValue* findParameter(std::string_view name) const noexcept;
};

} // namespace cardputer_hub::core
