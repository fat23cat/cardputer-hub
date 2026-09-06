#include "hardware/cardputer/cardputer_adv_keyboard_layout.h"

namespace cardputer_hub::hardware {
namespace {

constexpr std::size_t keyIndex(std::size_t row, std::size_t column) {
    return row * cardputerAdvKeyboardColumns + column;
}

bool isModifier(std::size_t row, std::size_t column) {
    return (row == 2 && (column == 0 || column == 1)) || (row == 3 && column <= 2);
}

bool namedKey(std::size_t row, std::size_t column, bool functionLayer, core::NamedKey& result) {
    if (functionLayer) {
        if (row == 0 && column == 0) {
            result = core::NamedKey::Escape;
            return true;
        }
        if (row == 0 && column >= 1 && column <= 12) {
            result = static_cast<core::NamedKey>(static_cast<std::size_t>(core::NamedKey::F1) +
                                                 column - 1);
            return true;
        }
        if (row == 0 && column == 13) {
            result = core::NamedKey::Delete;
            return true;
        }
        if (row == 2 && column == 11) {
            result = core::NamedKey::Up;
            return true;
        }
        if (row == 3 && column >= 10 && column <= 12) {
            constexpr core::NamedKey arrows[] = {core::NamedKey::Left, core::NamedKey::Down,
                                                 core::NamedKey::Right};
            result = arrows[column - 10];
            return true;
        }
        return false;
    }

    if (row == 0 && column == 13) {
        result = core::NamedKey::Backspace;
        return true;
    }
    if (row == 1 && column == 0) {
        result = core::NamedKey::Tab;
        return true;
    }
    if (row == 2 && column == 13) {
        result = core::NamedKey::Enter;
        return true;
    }
    return false;
}

char printableKey(std::size_t row, std::size_t column, bool shifted) {
    constexpr char normalRow0[] = "`1234567890-=";
    constexpr char shiftedRow0[] = "~!@#$%^&*()_+";
    constexpr char normalRow1[] = "qwertyuiop[]\\";
    constexpr char shiftedRow1[] = "QWERTYUIOP{}|";
    constexpr char normalRow2[] = "asdfghjkl;'";
    constexpr char shiftedRow2[] = "ASDFGHJKL:\"";
    constexpr char normalRow3[] = "zxcvbnm,./ ";
    constexpr char shiftedRow3[] = "ZXCVBNM<>? ";

    if (row == 0 && column < 13) {
        return shifted ? shiftedRow0[column] : normalRow0[column];
    }
    if (row == 1 && column >= 1) {
        return shifted ? shiftedRow1[column - 1] : normalRow1[column - 1];
    }
    if (row == 2 && column >= 2 && column <= 12) {
        return shifted ? shiftedRow2[column - 2] : normalRow2[column - 2];
    }
    if (row == 3 && column >= 3) {
        return shifted ? shiftedRow3[column - 3] : normalRow3[column - 3];
    }
    return '\0';
}

} // namespace

std::optional<CardputerAdvKeyEdge> decodeCardputerAdvKeyEvent(std::uint8_t rawEvent) {
    const auto encodedPosition = static_cast<std::uint8_t>(rawEvent & 0x7fU);
    if (encodedPosition == 0) {
        return std::nullopt;
    }

    const auto zeroBased = static_cast<std::uint8_t>(encodedPosition - 1U);
    const auto controllerRow = static_cast<std::uint8_t>(zeroBased / 10U);
    const auto controllerColumn = static_cast<std::uint8_t>(zeroBased % 10U);
    if (controllerRow >= 7U || controllerColumn >= 8U) {
        return std::nullopt;
    }

    const auto row = static_cast<std::uint8_t>(controllerColumn % 4U);
    const auto column =
        static_cast<std::uint8_t>(controllerRow * 2U + (controllerColumn > 3U ? 1U : 0U));
    return CardputerAdvKeyEdge{row, column, (rawEvent & 0x80U) != 0};
}

core::KeyboardSnapshot cardputerAdvKeyboardSnapshot(const CardputerAdvPressedKeys& pressedKeys) {
    core::KeyboardSnapshot snapshot;
    snapshot.modifiers.fn = pressedKeys[keyIndex(2, 0)];
    snapshot.modifiers.shift = pressedKeys[keyIndex(2, 1)];
    snapshot.modifiers.ctrl = pressedKeys[keyIndex(3, 0)];
    snapshot.modifiers.option = pressedKeys[keyIndex(3, 1)];
    snapshot.modifiers.alt = pressedKeys[keyIndex(3, 2)];

    for (std::size_t row = 0; row < cardputerAdvKeyboardRows; ++row) {
        for (std::size_t column = 0; column < cardputerAdvKeyboardColumns; ++column) {
            const auto identity = keyIndex(row, column);
            if (!pressedKeys[identity] || isModifier(row, column)) {
                continue;
            }

            core::PhysicalKeyState key{static_cast<std::uint16_t>(identity),
                                       core::KeyRepresentation::Inactive, '\0',
                                       core::NamedKey::Tab};
            if (namedKey(row, column, snapshot.modifiers.fn, key.namedKey)) {
                key.representation = core::KeyRepresentation::NamedKey;
            } else if (!snapshot.modifiers.fn) {
                key.character = printableKey(row, column, snapshot.modifiers.shift);
                if (key.character != '\0') {
                    key.representation = core::KeyRepresentation::PrintableCharacter;
                }
            }
            snapshot.keys.push_back(key);
        }
    }
    return snapshot;
}

} // namespace cardputer_hub::hardware
