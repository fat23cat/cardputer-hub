#include "core/display/display_glyphs.h"

#include "core/text/utf8.h"

namespace cardputer_hub::core {
namespace {

std::uint8_t glyphFor(char32_t codePoint) {
    using namespace display_glyph;
    if (codePoint >= 0x20 && codePoint <= 0x7E)
        return static_cast<std::uint8_t>(codePoint);
    if (codePoint >= 0x0410 && codePoint <= 0x042F)
        return static_cast<std::uint8_t>(cyrillicCapitalA + (codePoint - 0x0410));
    if (codePoint >= 0x0430 && codePoint <= 0x044F)
        return static_cast<std::uint8_t>(cyrillicSmallA + (codePoint - 0x0430));
    switch (codePoint) {
    case 0x0401:
        return cyrillicCapitalIo;
    case 0x0451:
        return cyrillicSmallIo;
    case 0x00AB:
        return leftGuillemet;
    case 0x00BB:
        return rightGuillemet;
    case 0x2116:
        return numeroSign;
    case 0x00B0:
        return degreeSign;
    case 0x2026:
        return ellipsis;
    // Characters macOS substitutes while typing, drawn with their ASCII shape.
    case 0x00A0: // no-break space
    case 0x2009: // thin space
        return ' ';
    case 0x2010: // hyphen
    case 0x2011: // non-breaking hyphen
    case 0x2012: // figure dash
    case 0x2013: // en dash
    case 0x2014: // em dash
    case 0x2212: // minus sign
        return '-';
    case 0x2018:
    case 0x2019:
        return '\'';
    case 0x201C:
    case 0x201D:
    case 0x201E:
        return '"';
    case 0x0406: // Cyrillic capital I (Belarusian-Ukrainian)
        return 'I';
    case 0x0456:
        return 'i';
    case 0x0408:
        return 'J';
    case 0x0458:
        return 'j';
    default:
        break;
    }
    return unsupported;
}

} // namespace

std::string displayGlyphs(std::string_view utf8) {
    std::string glyphs;
    glyphs.reserve(utf8.size());
    std::size_t position = 0;
    while (position < utf8.size()) {
        const auto codePoint = decodeUtf8(utf8, position);
        glyphs.push_back(
            static_cast<char>(codePoint ? glyphFor(*codePoint) : display_glyph::unsupported));
    }
    return glyphs;
}

} // namespace cardputer_hub::core
