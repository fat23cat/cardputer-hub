#include "core/text/utf8.h"

namespace cardputer_hub::core {
namespace {

bool isContinuation(unsigned char byte) { return (byte & 0xC0U) == 0x80U; }

} // namespace

std::optional<char32_t> decodeUtf8(std::string_view text, std::size_t& position) {
    if (position >= text.size())
        return std::nullopt;
    const auto lead = static_cast<unsigned char>(text[position]);
    std::size_t length = 0;
    char32_t value = 0;
    char32_t minimum = 0;
    if (lead < 0x80U) {
        ++position;
        return lead;
    }
    if ((lead & 0xE0U) == 0xC0U) {
        length = 2;
        value = lead & 0x1FU;
        minimum = 0x80;
    } else if ((lead & 0xF0U) == 0xE0U) {
        length = 3;
        value = lead & 0x0FU;
        minimum = 0x800;
    } else if ((lead & 0xF8U) == 0xF0U) {
        length = 4;
        value = lead & 0x07U;
        minimum = 0x10000;
    } else {
        ++position;
        return std::nullopt;
    }
    if (position + length > text.size()) {
        ++position;
        return std::nullopt;
    }
    for (std::size_t index = 1; index < length; ++index) {
        const auto byte = static_cast<unsigned char>(text[position + index]);
        if (!isContinuation(byte)) {
            ++position;
            return std::nullopt;
        }
        value = (value << 6U) | (byte & 0x3FU);
    }
    if (value < minimum || value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF)) {
        ++position;
        return std::nullopt;
    }
    position += length;
    return value;
}

bool isValidUtf8(std::string_view text) {
    std::size_t position = 0;
    while (position < text.size()) {
        if (!decodeUtf8(text, position))
            return false;
    }
    return true;
}

std::size_t utf8Length(std::string_view text) {
    std::size_t count = 0;
    std::size_t position = 0;
    while (position < text.size()) {
        (void)decodeUtf8(text, position);
        ++count;
    }
    return count;
}

std::string utf8Prefix(std::string_view text, std::size_t codePoints) {
    std::size_t position = 0;
    for (std::size_t count = 0; count < codePoints && position < text.size(); ++count)
        (void)decodeUtf8(text, position);
    return std::string(text.substr(0, position));
}

void appendUtf8(std::string& output, char32_t codePoint) {
    if (codePoint < 0x80) {
        output.push_back(static_cast<char>(codePoint));
    } else if (codePoint < 0x800) {
        output.push_back(static_cast<char>(0xC0U | (codePoint >> 6U)));
        output.push_back(static_cast<char>(0x80U | (codePoint & 0x3FU)));
    } else if (codePoint < 0x10000) {
        output.push_back(static_cast<char>(0xE0U | (codePoint >> 12U)));
        output.push_back(static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3FU)));
        output.push_back(static_cast<char>(0x80U | (codePoint & 0x3FU)));
    } else {
        output.push_back(static_cast<char>(0xF0U | (codePoint >> 18U)));
        output.push_back(static_cast<char>(0x80U | ((codePoint >> 12U) & 0x3FU)));
        output.push_back(static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3FU)));
        output.push_back(static_cast<char>(0x80U | (codePoint & 0x3FU)));
    }
}

} // namespace cardputer_hub::core
