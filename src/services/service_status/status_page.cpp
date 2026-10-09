#include "services/service_status/status_page.h"

#include <algorithm>
#include <cstring>
#include <string>

namespace cardputer_hub::services {
namespace {
using connectivity::ServiceStatusLevel;

class Reader {
  public:
    explicit Reader(std::string_view text) : text_(text) {}

    void skipSpace() {
        while (position_ < text_.size() && (text_[position_] == ' ' || text_[position_] == '\n' ||
                                            text_[position_] == '\r' || text_[position_] == '\t'))
            ++position_;
    }
    bool consume(char expected) {
        skipSpace();
        if (position_ >= text_.size() || text_[position_] != expected)
            return false;
        ++position_;
        return true;
    }
    bool peek(char expected) {
        skipSpace();
        return position_ < text_.size() && text_[position_] == expected;
    }
    // A JSON string, unescaped to UTF-8. Escaped surrogate pairs become '?'.
    std::optional<std::string> string() {
        if (!consume('"'))
            return std::nullopt;
        std::string result;
        while (position_ < text_.size()) {
            const char current = text_[position_++];
            if (current == '"')
                return result;
            if (current != '\\') {
                result.push_back(current);
                continue;
            }
            if (position_ >= text_.size())
                return std::nullopt;
            const char escape = text_[position_++];
            switch (escape) {
            case '"':
            case '\\':
            case '/':
                result.push_back(escape);
                break;
            case 'b':
            case 'f':
            case 'n':
            case 'r':
            case 't':
                result.push_back(' ');
                break;
            case 'u': {
                if (position_ + 4 > text_.size())
                    return std::nullopt;
                unsigned value = 0;
                for (int i = 0; i < 4; ++i) {
                    const char digit = text_[position_++];
                    value <<= 4U;
                    if (digit >= '0' && digit <= '9')
                        value |= static_cast<unsigned>(digit - '0');
                    else if (digit >= 'a' && digit <= 'f')
                        value |= static_cast<unsigned>(digit - 'a' + 10);
                    else if (digit >= 'A' && digit <= 'F')
                        value |= static_cast<unsigned>(digit - 'A' + 10);
                    else
                        return std::nullopt;
                }
                appendUtf8(result, value);
                break;
            }
            default:
                return std::nullopt;
            }
        }
        return std::nullopt;
    }
    // Skips one value of any type; nested containers are skipped whole.
    bool skipValue() {
        skipSpace();
        if (peek('"'))
            return string().has_value();
        if (peek('{') || peek('[')) {
            int depth = 0;
            while (position_ < text_.size()) {
                const char current = text_[position_];
                if (current == '"') {
                    if (!string())
                        return false;
                    continue;
                }
                ++position_;
                if (current == '{' || current == '[')
                    ++depth;
                else if ((current == '}' || current == ']') && --depth == 0)
                    return true;
            }
            return false;
        }
        while (position_ < text_.size() && text_[position_] != ',' && text_[position_] != '}' &&
               text_[position_] != ']')
            ++position_;
        return true;
    }

  private:
    static void appendUtf8(std::string& out, unsigned value) {
        if (value >= 0xD800U && value <= 0xDFFFU) {
            out.push_back('?');
        } else if (value < 0x80U) {
            out.push_back(static_cast<char>(value));
        } else if (value < 0x800U) {
            out.push_back(static_cast<char>(0xC0U | (value >> 6U)));
            out.push_back(static_cast<char>(0x80U | (value & 0x3FU)));
        } else {
            out.push_back(static_cast<char>(0xE0U | (value >> 12U)));
            out.push_back(static_cast<char>(0x80U | ((value >> 6U) & 0x3FU)));
            out.push_back(static_cast<char>(0x80U | (value & 0x3FU)));
        }
    }

    std::string_view text_;
    std::size_t position_ = 0;
};

std::optional<ServiceStatusLevel> levelOf(std::string_view indicator) {
    if (indicator == "none")
        return ServiceStatusLevel::Operational;
    if (indicator == "minor")
        return ServiceStatusLevel::Minor;
    if (indicator == "major")
        return ServiceStatusLevel::Major;
    if (indicator == "critical")
        return ServiceStatusLevel::Critical;
    if (indicator == "maintenance")
        return ServiceStatusLevel::Maintenance;
    return std::nullopt;
}

// Reads `{"key": value, ...}` and calls `visit(key, reader)` positioned at
// each value; `visit` returns false when it did not consume the value.
template <typename Visit> bool readObject(Reader& reader, Visit visit) {
    if (!reader.consume('{'))
        return false;
    if (reader.consume('}'))
        return true;
    for (;;) {
        const auto key = reader.string();
        if (!key || !reader.consume(':'))
            return false;
        if (!visit(*key, reader) && !reader.skipValue())
            return false;
        if (reader.consume('}'))
            return true;
        if (!reader.consume(','))
            return false;
    }
}

void copyDescription(const std::string& text, connectivity::CompanionServiceStatus& status) {
    auto length = std::min(text.size(), connectivity::companionMaxStatusDescriptionSize);
    while (length > 0 && length < text.size() &&
           (static_cast<unsigned char>(text[length]) & 0xC0U) == 0x80U)
        --length;
    std::memcpy(status.description.data(), text.data(), length);
    status.description[length] = '\0';
}
} // namespace

std::optional<connectivity::CompanionServiceStatus> parseStatusPage(std::string_view body) {
    Reader reader(body);
    connectivity::CompanionServiceStatus result{};
    bool sawStatus = false;
    const bool valid = readObject(reader, [&](const std::string& key, Reader& value) {
        if (key != "status" || !value.peek('{'))
            return false;
        sawStatus = readObject(value, [&](const std::string& field, Reader& inner) {
            if (field != "indicator" && field != "description")
                return false;
            const auto text = inner.string();
            if (!text)
                return false;
            if (field == "description")
                copyDescription(*text, result);
            else if (const auto level = levelOf(*text))
                result.level = *level;
            return true;
        });
        return sawStatus;
    });
    if (!valid || !sawStatus || result.level == ServiceStatusLevel::Unknown)
        return std::nullopt;
    return result;
}

} // namespace cardputer_hub::services
