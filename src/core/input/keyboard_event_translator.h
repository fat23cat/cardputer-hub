#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/input/input_event.h"

namespace cardputer_hub::core {

struct PrintableKeyState {
    std::uint16_t identity;
    char character;
};

constexpr std::size_t namedKeyCount = static_cast<std::size_t>(NamedKey::Count);

struct KeyboardSnapshot {
    Modifiers modifiers;
    std::vector<PrintableKeyState> printableKeys;
    std::array<bool, namedKeyCount> namedKeys{};
};

class KeyboardEventTranslator {
  public:
    void translate(const KeyboardSnapshot& snapshot, InputEvents& events);

  private:
    bool wasPrintableKeyPressed(std::uint16_t identity) const;

    KeyboardSnapshot previous_;
};

} // namespace cardputer_hub::core
