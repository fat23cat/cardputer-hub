#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/input/input_event.h"

namespace cardputer_hub::core {

enum class KeyRepresentation : std::uint8_t {
    Inactive,
    PrintableCharacter,
    NamedKey,
};

struct PhysicalKeyState {
    std::uint16_t identity;
    KeyRepresentation representation;
    char character;
    NamedKey namedKey;
};

constexpr std::size_t namedKeyCount = static_cast<std::size_t>(NamedKey::Count);

struct KeyboardSnapshot {
    Modifiers modifiers;
    std::vector<PhysicalKeyState> keys;
};

class KeyboardEventTranslator {
  public:
    void translate(const KeyboardSnapshot& snapshot, InputEvents& events);

  private:
    bool wasPhysicalKeyPressed(std::uint16_t identity) const;

    KeyboardSnapshot previous_;
};

} // namespace cardputer_hub::core
