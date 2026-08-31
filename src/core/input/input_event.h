#pragma once

#include <cstdint>
#include <vector>

namespace cardputer_hub::core {

enum class NamedKey : std::uint8_t {
    Tab,
    Enter,
    Backspace,
    Delete,
    Escape,
    Up,
    Down,
    Left,
    Right,
    F1,
    F2,
    F3,
    F4,
    F5,
    F6,
    F7,
    F8,
    F9,
    F10,
    F11,
    F12,
    Count,
};

struct Modifiers {
    bool shift = false;
    bool ctrl = false;
    bool alt = false;
    bool option = false;
    bool fn = false;
};

enum class InputEventType : std::uint8_t {
    PrintableCharacter,
    NamedKey,
};

struct InputEvent {
    InputEventType type;
    char character;
    NamedKey namedKey;
    Modifiers modifiers;
};

using InputEvents = std::vector<InputEvent>;

} // namespace cardputer_hub::core
