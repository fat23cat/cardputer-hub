#include "core/display/text_layout.h"

namespace cardputer_hub::core {
namespace {

struct Word {
    std::string text;
    std::size_t length = 0;
};

std::vector<Word> splitWords(std::string_view text) {
    std::vector<Word> words;
    Word current;
    std::size_t position = 0;
    while (position < text.size()) {
        const auto start = position;
        const auto codePoint = decodeUtf8(text, position);
        if (codePoint && *codePoint == ' ') {
            if (current.length > 0)
                words.push_back(std::move(current));
            current = {};
            continue;
        }
        current.text.append(text.substr(start, position - start));
        ++current.length;
    }
    if (current.length > 0)
        words.push_back(std::move(current));
    return words;
}

} // namespace

std::vector<std::string> wrapText(std::string_view text, std::size_t columns) {
    std::vector<std::string> lines;
    if (columns == 0)
        return lines;
    std::string line;
    std::size_t lineLength = 0;
    for (auto& word : splitWords(text)) {
        while (word.length > columns) {
            // A word wider than a line fills the rest of the current line, or a
            // whole line of its own, then continues on the next.
            if (lineLength > 0 && lineLength + 1 >= columns) {
                lines.push_back(std::move(line));
                line.clear();
                lineLength = 0;
                continue;
            }
            const auto room = lineLength == 0 ? columns : columns - lineLength - 1;
            const auto head = utf8Prefix(word.text, room);
            if (lineLength > 0)
                line.push_back(' ');
            line += head;
            lines.push_back(std::move(line));
            line.clear();
            lineLength = 0;
            word.text.erase(0, head.size());
            word.length -= room;
        }
        if (word.length == 0)
            continue;
        const auto needed = lineLength == 0 ? word.length : lineLength + 1 + word.length;
        if (needed > columns) {
            lines.push_back(std::move(line));
            line.clear();
            lineLength = 0;
        }
        if (lineLength > 0) {
            line.push_back(' ');
            ++lineLength;
        }
        line += word.text;
        lineLength += word.length;
    }
    if (lineLength > 0)
        lines.push_back(std::move(line));
    if (lines.empty())
        lines.emplace_back();
    return lines;
}

} // namespace cardputer_hub::core
