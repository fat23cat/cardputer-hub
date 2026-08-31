#include "hardware/cardputer/cardputer_keyboard_adapter.h"

#include <M5Cardputer.h>

namespace cardputer_hub::hardware {
namespace {

constexpr std::uint16_t keyboardColumns = 14;

bool isModifierKeyCode(std::uint8_t keyCode) {
    return keyCode == KEY_FN || keyCode == KEY_OPT || keyCode == KEY_LEFT_CTRL ||
           keyCode == KEY_LEFT_SHIFT || keyCode == KEY_LEFT_ALT;
}

bool namedKeyFromCode(std::uint8_t keyCode, bool functionLayer, core::NamedKey& namedKey) {
    if (!functionLayer) {
        switch (keyCode) {
        case KEY_TAB:
            namedKey = core::NamedKey::Tab;
            return true;
        case KEY_ENTER:
            namedKey = core::NamedKey::Enter;
            return true;
        case KEY_BACKSPACE:
            namedKey = core::NamedKey::Backspace;
            return true;
        default:
            return false;
        }
    }

    switch (keyCode) {
    case KEY_TAB:
        namedKey = core::NamedKey::Tab;
        return true;
    case KEY_ENTER:
        namedKey = core::NamedKey::Enter;
        return true;
    case KEY_BACKSPACE:
        namedKey = core::NamedKey::Backspace;
        return true;
    case KEY_DELETE:
        namedKey = core::NamedKey::Delete;
        return true;
    case KEY_ESCAPE:
        namedKey = core::NamedKey::Escape;
        return true;
    case KEY_UP:
        namedKey = core::NamedKey::Up;
        return true;
    case KEY_DOWN:
        namedKey = core::NamedKey::Down;
        return true;
    case KEY_LEFT:
        namedKey = core::NamedKey::Left;
        return true;
    case KEY_RIGHT:
        namedKey = core::NamedKey::Right;
        return true;
    case KEY_F1:
        namedKey = core::NamedKey::F1;
        return true;
    case KEY_F2:
        namedKey = core::NamedKey::F2;
        return true;
    case KEY_F3:
        namedKey = core::NamedKey::F3;
        return true;
    case KEY_F4:
        namedKey = core::NamedKey::F4;
        return true;
    case KEY_F5:
        namedKey = core::NamedKey::F5;
        return true;
    case KEY_F6:
        namedKey = core::NamedKey::F6;
        return true;
    case KEY_F7:
        namedKey = core::NamedKey::F7;
        return true;
    case KEY_F8:
        namedKey = core::NamedKey::F8;
        return true;
    case KEY_F9:
        namedKey = core::NamedKey::F9;
        return true;
    case KEY_F10:
        namedKey = core::NamedKey::F10;
        return true;
    case KEY_F11:
        namedKey = core::NamedKey::F11;
        return true;
    case KEY_F12:
        namedKey = core::NamedKey::F12;
        return true;
    default:
        return false;
    }
}

core::KeyboardSnapshot extractSnapshot(Keyboard_Class& keyboard) {
    const auto& state = keyboard.keysState();
    core::KeyboardSnapshot snapshot;
    snapshot.modifiers = {state.shift, state.ctrl, state.alt, state.opt, state.fn};

    std::size_t wordIndex = 0;
    for (const auto& position : keyboard.keyList()) {
        const auto keyValue = keyboard.getKeyValue(position);
        if (isModifierKeyCode(keyValue.value_first)) {
            continue;
        }

        const auto identity =
            static_cast<std::uint16_t>(static_cast<std::uint16_t>(position.y) * keyboardColumns +
                                       static_cast<std::uint16_t>(position.x));
        core::PhysicalKeyState key{identity, core::KeyRepresentation::Inactive, '\0',
                                   core::NamedKey::Tab};
        const auto activeKeyCode =
            static_cast<std::uint8_t>(state.fn ? keyValue.value_third : keyValue.value_first);

        if (namedKeyFromCode(activeKeyCode, state.fn, key.namedKey)) {
            key.representation = core::KeyRepresentation::NamedKey;
        } else if (!state.fn && wordIndex < state.word.size()) {
            key.representation = core::KeyRepresentation::PrintableCharacter;
            key.character = state.word[wordIndex];
            ++wordIndex;
        }

        snapshot.keys.push_back(key);
    }
    return snapshot;
}

} // namespace

void CardputerKeyboardAdapter::poll(core::InputEvents& events) {
    translator_.translate(extractSnapshot(M5Cardputer.Keyboard), events);
}

} // namespace cardputer_hub::hardware
