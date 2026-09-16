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
    SystemMenu,
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

inline bool isPlainEscape(const InputEvent& event) {
    if (event.modifiers.ctrl || event.modifiers.alt || event.modifiers.option ||
        event.modifiers.shift)
        return false;
    if (event.type == InputEventType::NamedKey && event.namedKey == NamedKey::Escape)
        return true;
    return event.type == InputEventType::PrintableCharacter && event.character == '`';
}

} // namespace cardputer_hub::core
