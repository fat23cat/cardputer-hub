#pragma once

#include <array>
#include <cstdint>
#include <optional>

#include "core/input/keyboard_event_translator.h"

namespace cardputer_hub::hardware {

constexpr std::size_t cardputerAdvKeyboardRows = 4;
constexpr std::size_t cardputerAdvKeyboardColumns = 14;
constexpr std::size_t cardputerAdvKeyboardKeyCount =
    cardputerAdvKeyboardRows * cardputerAdvKeyboardColumns;

using CardputerAdvPressedKeys = std::array<bool, cardputerAdvKeyboardKeyCount>;

struct CardputerAdvKeyEdge {
    std::uint8_t row;
    std::uint8_t column;
    bool pressed;
};

std::optional<CardputerAdvKeyEdge> decodeCardputerAdvKeyEvent(std::uint8_t rawEvent);
core::KeyboardSnapshot cardputerAdvKeyboardSnapshot(const CardputerAdvPressedKeys& pressedKeys);

} // namespace cardputer_hub::hardware
