#include "hardware/cardputer/cardputer_keyboard_adapter.h"

#include <M5Cardputer.h>

namespace cardputer_hub::hardware {
namespace {

void setNamedKey(core::KeyboardSnapshot& snapshot, core::NamedKey key, bool pressed) {
    snapshot.namedKeys[static_cast<std::size_t>(key)] = pressed;
}

bool isNamedKeyCode(std::uint8_t keyCode) {
    switch (keyCode) {
    case KEY_TAB:
    case KEY_ENTER:
    case KEY_BACKSPACE:
    case KEY_DELETE:
    case KEY_ESCAPE:
    case KEY_UP:
    case KEY_DOWN:
    case KEY_LEFT:
    case KEY_RIGHT:
    case KEY_F1:
    case KEY_F2:
    case KEY_F3:
    case KEY_F4:
    case KEY_F5:
    case KEY_F6:
    case KEY_F7:
    case KEY_F8:
    case KEY_F9:
    case KEY_F10:
    case KEY_F11:
    case KEY_F12:
        return true;
    default:
        return false;
    }
}

core::KeyboardSnapshot extractSnapshot(const Keyboard_Class::KeysState& state) {
    core::KeyboardSnapshot snapshot;
    snapshot.modifiers = {state.shift, state.ctrl, state.alt, state.opt, state.fn};

    std::size_t wordIndex = 0;
    for (const auto rawKeyCode : state.hid_keys) {
        const auto keyCode = static_cast<std::uint8_t>(rawKeyCode & 0x7fU);
        if (!isNamedKeyCode(keyCode) && wordIndex < state.word.size()) {
            snapshot.printableKeys.push_back({keyCode, state.word[wordIndex]});
            ++wordIndex;
        }
    }

    setNamedKey(snapshot, core::NamedKey::Tab, state.tab);
    setNamedKey(snapshot, core::NamedKey::Enter, state.enter);
    setNamedKey(snapshot, core::NamedKey::Backspace, state.backspace);
    setNamedKey(snapshot, core::NamedKey::Delete, state.del);
    setNamedKey(snapshot, core::NamedKey::Escape, state.esc);
    setNamedKey(snapshot, core::NamedKey::Up, state.up);
    setNamedKey(snapshot, core::NamedKey::Down, state.down);
    setNamedKey(snapshot, core::NamedKey::Left, state.left);
    setNamedKey(snapshot, core::NamedKey::Right, state.right);
    setNamedKey(snapshot, core::NamedKey::F1, state.f1);
    setNamedKey(snapshot, core::NamedKey::F2, state.f2);
    setNamedKey(snapshot, core::NamedKey::F3, state.f3);
    setNamedKey(snapshot, core::NamedKey::F4, state.f4);
    setNamedKey(snapshot, core::NamedKey::F5, state.f5);
    setNamedKey(snapshot, core::NamedKey::F6, state.f6);
    setNamedKey(snapshot, core::NamedKey::F7, state.f7);
    setNamedKey(snapshot, core::NamedKey::F8, state.f8);
    setNamedKey(snapshot, core::NamedKey::F9, state.f9);
    setNamedKey(snapshot, core::NamedKey::F10, state.f10);
    setNamedKey(snapshot, core::NamedKey::F11, state.f11);
    setNamedKey(snapshot, core::NamedKey::F12, state.f12);
    return snapshot;
}

} // namespace

void CardputerKeyboardAdapter::poll(core::InputEvents& events) {
    translator_.translate(extractSnapshot(M5Cardputer.Keyboard.keysState()), events);
}

} // namespace cardputer_hub::hardware
