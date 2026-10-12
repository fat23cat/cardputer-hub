#include "services/web_access/web_json.h"

#include <utility>

#include "core/text/utf8.h"

namespace cardputer_hub::services {
namespace {

class StringObjectParser {
  public:
    explicit StringObjectParser(std::string_view text) : text_(text) {}

    std::optional<std::map<std::string, std::string>> parse(std::size_t maxMembers) {
        std::map<std::string, std::string> members;
        skipSpace();
        if (!consume('{'))
            return std::nullopt;
        skipSpace();
        if (consume('}'))
            return finish(std::move(members));
        while (true) {
            skipSpace();
            auto key = string();
            skipSpace();
            if (!key || !consume(':'))
                return std::nullopt;
            skipSpace();
            auto value = string();
            if (!value || members.size() == maxMembers ||
                !members.emplace(std::move(*key), std::move(*value)).second)
                return std::nullopt;
            skipSpace();
            if (consume('}'))
                return finish(std::move(members));
            if (!consume(','))
                return std::nullopt;
        }
    }

  private:
    std::optional<std::map<std::string, std::string>>
    finish(std::map<std::string, std::string> members) {
        skipSpace();
        if (position_ != text_.size())
            return std::nullopt;
        return members;
    }

    void skipSpace() {
        while (position_ < text_.size() && (text_[position_] == ' ' || text_[position_] == '\t' ||
                                            text_[position_] == '\n' || text_[position_] == '\r'))
            ++position_;
    }

    bool consume(char expected) {
        if (position_ >= text_.size() || text_[position_] != expected)
            return false;
        ++position_;
        return true;
    }

    std::optional<std::uint16_t> hexQuad() {
        if (text_.size() - position_ < 4)
            return std::nullopt;
        std::uint16_t value = 0;
        for (int index = 0; index < 4; ++index) {
            const char digit = text_[position_++];
            value = static_cast<std::uint16_t>(value << 4);
            if (digit >= '0' && digit <= '9')
                value = static_cast<std::uint16_t>(value | (digit - '0'));
            else if (digit >= 'a' && digit <= 'f')
                value = static_cast<std::uint16_t>(value | (digit - 'a' + 10));
            else if (digit >= 'A' && digit <= 'F')
                value = static_cast<std::uint16_t>(value | (digit - 'A' + 10));
            else
                return std::nullopt;
        }
        return value;
    }

    std::optional<std::string> string() {
        if (!consume('"'))
            return std::nullopt;
        std::string result;
        while (position_ < text_.size()) {
            const char character = text_[position_++];
            if (character == '"')
                return result;
            if (static_cast<unsigned char>(character) < 0x20)
                return std::nullopt;
            if (character != '\\') {
                result.push_back(character);
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
                result.push_back('\b');
                break;
            case 'f':
                result.push_back('\f');
                break;
            case 'n':
                result.push_back('\n');
                break;
            case 'r':
                result.push_back('\r');
                break;
            case 't':
                result.push_back('\t');
                break;
            case 'u': {
                const auto high = hexQuad();
                if (!high || (*high >= 0xdc00 && *high <= 0xdfff))
                    return std::nullopt;
                char32_t codePoint = *high;
                if (*high >= 0xd800 && *high <= 0xdbff) {
                    if (!consume('\\') || !consume('u'))
                        return std::nullopt;
                    const auto low = hexQuad();
                    if (!low || *low < 0xdc00 || *low > 0xdfff)
                        return std::nullopt;
                    codePoint = 0x10000 + ((static_cast<char32_t>(*high) - 0xd800) << 10) +
                                (static_cast<char32_t>(*low) - 0xdc00);
                }
                core::appendUtf8(result, codePoint);
                break;
            }
            default:
                return std::nullopt;
            }
        }
        return std::nullopt;
    }

    std::string_view text_;
    std::size_t position_ = 0;
};

const char* hexDigits = "0123456789abcdef";

} // namespace

void appendWebJsonString(std::string& output, std::string_view value) {
    output.push_back('"');
    for (const char character : value) {
        const auto byte = static_cast<unsigned char>(character);
        if (character == '"' || character == '\\') {
            output.push_back('\\');
            output.push_back(character);
        } else if (character == '\n') {
            output += "\\n";
        } else if (byte < 0x20) {
            output += "\\u00";
            output.push_back(hexDigits[byte >> 4]);
            output.push_back(hexDigits[byte & 0x0f]);
        } else {
            output.push_back(character);
        }
    }
    output.push_back('"');
}

std::optional<std::map<std::string, std::string>> parseWebJsonStringObject(std::string_view json,
                                                                           std::size_t maxMembers) {
    if (!core::isValidUtf8(json))
        return std::nullopt;
    return StringObjectParser(json).parse(maxMembers);
}

connectivity::HttpResponse webJsonResponse(std::uint16_t status, std::string body) {
    connectivity::HttpResponse response;
    response.status = status;
    response.contentType = "application/json";
    response.headers = {{"Cache-Control", "no-store"}};
    response.body = std::move(body);
    return response;
}

connectivity::HttpResponse webErrorResponse(std::uint16_t status, std::string_view code) {
    std::string body = R"({"error":)";
    appendWebJsonString(body, code);
    body.push_back('}');
    return webJsonResponse(status, std::move(body));
}

connectivity::HttpResponse webEmptyResponse(std::uint16_t status) {
    connectivity::HttpResponse response;
    response.status = status;
    response.headers = {{"Cache-Control", "no-store"}};
    return response;
}

} // namespace cardputer_hub::services
