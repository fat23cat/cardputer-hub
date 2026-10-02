#include "services/inventory/inventory_record.h"

#include "core/text/utf8.h"

#include <limits>

namespace cardputer_hub::services {
namespace {

bool isForbiddenCharacter(char32_t codePoint) {
    return codePoint < 0x20 || (codePoint >= 0x7F && codePoint <= 0x9F) || codePoint == 0x2028 ||
           codePoint == 0x2029;
}

// A strict reader for exactly the record object. Anything outside RFC 8259,
// and anything the record does not define, is refused.
class RecordParser {
  public:
    explicit RecordParser(std::string_view text) : text_(text) {}

    InventoryRecordDecode parse() {
        InventoryRecordDecode result;
        if (text_.size() > inventoryMaxRecordBytes)
            return fail(InventoryRecordError::TooLarge);
        if (!core::isValidUtf8(text_))
            return fail(InventoryRecordError::InvalidUtf8);
        InventoryRecord record;
        bool seenSchema = false;
        bool seenId = false;
        bool seenRevision = false;
        bool seenName = false;
        bool seenDescription = false;
        skipSpace();
        if (!consume('{'))
            return fail(InventoryRecordError::Malformed);
        skipSpace();
        if (consume('}'))
            return fail(InventoryRecordError::MissingField);
        while (true) {
            skipSpace();
            std::string key;
            if (!readString(key))
                return fail(error_);
            skipSpace();
            if (!consume(':'))
                return fail(InventoryRecordError::Malformed);
            skipSpace();
            const auto once = [](bool& seen) {
                const bool first = !seen;
                seen = true;
                return first;
            };
            if (key == "schema") {
                if (!once(seenSchema))
                    return fail(InventoryRecordError::DuplicateField);
                std::uint32_t schema = 0;
                if (!readUnsigned(schema))
                    return fail(InventoryRecordError::Malformed);
                if (schema != inventorySchemaVersion)
                    return fail(InventoryRecordError::WrongSchema);
            } else if (key == "id") {
                if (!once(seenId))
                    return fail(InventoryRecordError::DuplicateField);
                std::string hex;
                if (!readString(hex))
                    return fail(error_);
                const auto id = parseInventoryIdHex(hex);
                if (!id)
                    return fail(InventoryRecordError::BadId);
                record.id = *id;
            } else if (key == "revision") {
                if (!once(seenRevision))
                    return fail(InventoryRecordError::DuplicateField);
                if (!readUnsigned(record.revision))
                    return fail(InventoryRecordError::Malformed);
            } else if (key == "name") {
                if (!once(seenName))
                    return fail(InventoryRecordError::DuplicateField);
                if (!readString(record.name))
                    return fail(error_);
            } else if (key == "description") {
                if (!once(seenDescription))
                    return fail(InventoryRecordError::DuplicateField);
                if (!readString(record.description))
                    return fail(error_);
            } else {
                return fail(InventoryRecordError::UnknownField);
            }
            skipSpace();
            if (consume(','))
                continue;
            if (consume('}'))
                break;
            return fail(InventoryRecordError::Malformed);
        }
        skipSpace();
        if (position_ != text_.size())
            return fail(InventoryRecordError::Malformed);
        if (!seenSchema || !seenId || !seenRevision || !seenName || !seenDescription)
            return fail(InventoryRecordError::MissingField);
        if (const auto invalid = validateInventoryRecord(record);
            invalid != InventoryRecordError::None)
            return fail(invalid);
        result.record = std::move(record);
        return result;
    }

  private:
    static InventoryRecordDecode fail(InventoryRecordError error) { return {std::nullopt, error}; }

    void skipSpace() {
        while (position_ < text_.size() && (text_[position_] == ' ' || text_[position_] == '\t' ||
                                            text_[position_] == '\n' || text_[position_] == '\r'))
            ++position_;
    }

    bool consume(char expected) {
        if (position_ < text_.size() && text_[position_] == expected) {
            ++position_;
            return true;
        }
        return false;
    }

    bool readUnsigned(std::uint32_t& value) {
        const auto start = position_;
        std::uint64_t parsed = 0;
        while (position_ < text_.size() && text_[position_] >= '0' && text_[position_] <= '9') {
            parsed = parsed * 10 + static_cast<std::uint64_t>(text_[position_] - '0');
            if (parsed > std::numeric_limits<std::uint32_t>::max())
                return false;
            ++position_;
        }
        const auto digits = position_ - start;
        if (digits == 0 || (digits > 1 && text_[start] == '0'))
            return false;
        value = static_cast<std::uint32_t>(parsed);
        return true;
    }

    bool readHex4(std::uint32_t& value) {
        if (position_ + 4 > text_.size())
            return false;
        value = 0;
        for (int index = 0; index < 4; ++index) {
            const char digit = text_[position_++];
            value <<= 4U;
            if (digit >= '0' && digit <= '9')
                value |= static_cast<std::uint32_t>(digit - '0');
            else if (digit >= 'a' && digit <= 'f')
                value |= static_cast<std::uint32_t>(digit - 'a' + 10);
            else if (digit >= 'A' && digit <= 'F')
                value |= static_cast<std::uint32_t>(digit - 'A' + 10);
            else
                return false;
        }
        return true;
    }

    bool readString(std::string& value) {
        error_ = InventoryRecordError::Malformed;
        if (!consume('"'))
            return false;
        value.clear();
        while (position_ < text_.size()) {
            const char character = text_[position_];
            if (character == '"') {
                ++position_;
                return true;
            }
            if (static_cast<unsigned char>(character) < 0x20)
                return false;
            if (character != '\\') {
                value.push_back(character);
                ++position_;
                continue;
            }
            ++position_;
            if (position_ >= text_.size())
                return false;
            const char escape = text_[position_++];
            switch (escape) {
            case '"':
            case '\\':
            case '/':
                value.push_back(escape);
                break;
            case 'b':
                value.push_back('\b');
                break;
            case 'f':
                value.push_back('\f');
                break;
            case 'n':
                value.push_back('\n');
                break;
            case 'r':
                value.push_back('\r');
                break;
            case 't':
                value.push_back('\t');
                break;
            case 'u': {
                std::uint32_t unit = 0;
                if (!readHex4(unit))
                    return false;
                if (unit >= 0xDC00 && unit <= 0xDFFF) {
                    error_ = InventoryRecordError::InvalidUtf8;
                    return false;
                }
                if (unit >= 0xD800 && unit <= 0xDBFF) {
                    std::uint32_t low = 0;
                    if (!consume('\\') || !consume('u') || !readHex4(low) || low < 0xDC00 ||
                        low > 0xDFFF) {
                        error_ = InventoryRecordError::InvalidUtf8;
                        return false;
                    }
                    unit = 0x10000 + ((unit - 0xD800) << 10U) + (low - 0xDC00);
                }
                core::appendUtf8(value, static_cast<char32_t>(unit));
                break;
            }
            default:
                return false;
            }
        }
        return false;
    }

    std::string_view text_;
    std::size_t position_ = 0;
    InventoryRecordError error_ = InventoryRecordError::Malformed;
};

// Canonical form: only quote, backslash and line break are escaped.
void appendJsonString(std::string& output, std::string_view value) {
    output.push_back('"');
    for (const char character : value) {
        if (character == '\n') {
            output += "\\n";
            continue;
        }
        if (character == '"' || character == '\\')
            output.push_back('\\');
        output.push_back(character);
    }
    output.push_back('"');
}

} // namespace

bool isValidInventoryText(std::string_view text, std::size_t maxCodePoints) {
    if (text.empty() || text.front() == ' ' || text.back() == ' ')
        return false;
    std::size_t position = 0;
    std::size_t count = 0;
    while (position < text.size()) {
        const auto codePoint = core::decodeUtf8(text, position);
        if (!codePoint || isForbiddenCharacter(*codePoint) || ++count > maxCodePoints)
            return false;
    }
    return true;
}

bool isValidInventoryDescription(std::string_view text, std::size_t maxCodePoints) {
    if (text.empty())
        return true;
    const auto edge = [](char character) { return character == ' ' || character == '\n'; };
    if (edge(text.front()) || edge(text.back()))
        return false;
    std::size_t position = 0;
    std::size_t count = 0;
    while (position < text.size()) {
        const auto codePoint = core::decodeUtf8(text, position);
        if (!codePoint || (*codePoint != '\n' && isForbiddenCharacter(*codePoint)) ||
            ++count > maxCodePoints)
            return false;
    }
    return true;
}

InventoryRecordError validateInventoryRecord(const InventoryRecord& record) {
    if (!parseInventoryIdHex(inventoryIdHex(record.id)))
        return InventoryRecordError::BadId;
    if (record.revision == 0)
        return InventoryRecordError::BadRevision;
    if (!core::isValidUtf8(record.name))
        return InventoryRecordError::InvalidUtf8;
    if (!isValidInventoryText(record.name, inventoryMaxNameLength))
        return InventoryRecordError::BadName;
    if (!core::isValidUtf8(record.description))
        return InventoryRecordError::InvalidUtf8;
    if (!isValidInventoryDescription(record.description, inventoryMaxDescriptionLength))
        return InventoryRecordError::BadDescription;
    return InventoryRecordError::None;
}

InventoryRecordDecode decodeInventoryRecord(std::string_view json) {
    return RecordParser(json).parse();
}

std::optional<std::string> encodeInventoryRecord(const InventoryRecord& record) {
    if (validateInventoryRecord(record) != InventoryRecordError::None)
        return std::nullopt;
    std::string json = "{\"schema\":" + std::to_string(inventorySchemaVersion) + ",\"id\":";
    appendJsonString(json, inventoryIdHex(record.id));
    json += ",\"revision\":" + std::to_string(record.revision) + ",\"name\":";
    appendJsonString(json, record.name);
    json += ",\"description\":";
    appendJsonString(json, record.description);
    json += "}";
    if (json.size() > inventoryMaxRecordBytes)
        return std::nullopt;
    return json;
}

} // namespace cardputer_hub::services
