#include "apps/nfc/nfc_graphics.h"

#include "core/display/contextual_footer.h"
#include "core/display/palette.h"
#include "core/display/screen_header.h"
#include "core/display/text_layout.h"

#include <algorithm>

namespace cardputer_hub::apps {
using namespace core;

namespace {

constexpr std::int32_t marginX = 6;
constexpr std::int32_t rightEdge = 234;
constexpr std::int32_t firstLineY = 26;
constexpr std::int32_t messageTitleY = 44;
constexpr std::int32_t messageBodyY = 64;
constexpr std::int32_t lineHeight = 12;
constexpr std::int32_t editorY = 54;
constexpr std::int32_t editorLimitY = 76;

void drawHeader(IDisplayAdapter& display, const std::string& title, const std::string& status,
                RgbColor statusColor) {
    display.clear(palette::bone);
    drawScreenHeader(display, title, status, statusColor);
}

} // namespace

std::vector<std::string> nfcDescriptionLines(const services::InventoryRecord& record) {
    std::vector<std::string> lines;
    if (record.description.empty())
        return lines;
    std::size_t start = 0;
    while (start <= record.description.size()) {
        auto end = record.description.find('\n', start);
        if (end == std::string::npos)
            end = record.description.size();
        const auto wrapped = wrapText(
            std::string_view(record.description).substr(start, end - start), nfcTextColumns);
        lines.insert(lines.end(), wrapped.begin(), wrapped.end());
        start = end + 1;
    }
    return lines;
}

std::size_t nfcPageCount(std::size_t lineCount) {
    return std::max<std::size_t>(1, (lineCount + nfcDescriptionLinesPerPage - 1) /
                                        nfcDescriptionLinesPerPage);
}

void drawNfcMessage(IDisplayAdapter& display, const std::string& headerStatus,
                    const NfcMessage& message) {
    drawHeader(display, "NFC", headerStatus, palette::ordinal);
    const TextStyle title{message.alert ? palette::vermilion : palette::ink, palette::bone,
                          systemTextScale};
    const TextStyle body{palette::ordinal, palette::bone, systemTextScale};
    const auto shownTitle = fitSystemText(message.title, rightEdge - marginX);
    display.drawText({centeredTextX(shownTitle.c_str(), 0, 240), messageTitleY}, shownTitle.c_str(),
                     title);
    std::int32_t y = messageBodyY;
    for (const auto& line : message.lines) {
        const auto shown = fitSystemText(line, rightEdge - marginX);
        display.drawText({centeredTextX(shown.c_str(), 0, 240), y}, shown.c_str(), body);
        y += lineHeight;
    }
    if (!message.footerLeft.empty() || !message.footerRight.empty())
        drawContextualFooter(display, message.footerLeft.c_str(), message.footerRight.c_str());
}

void drawNfcRecord(IDisplayAdapter& display, const services::InventoryRecord& record,
                   std::size_t page, bool registered, bool erasable, bool detached) {
    const auto lines = nfcDescriptionLines(record);
    const auto pages = nfcPageCount(lines.size());
    const auto shownPage = std::min(page, pages - 1);
    std::string status =
        pages > 1 ? std::to_string(shownPage + 1) + "/" + std::to_string(pages) : std::string();
    if (registered)
        status = status.empty() ? "SAVED" : "SAVED " + status;
    drawHeader(display, record.name, status, registered ? palette::leaf : palette::ordinal);
    const TextStyle ink{palette::ink, palette::bone, systemTextScale};
    const TextStyle quiet{palette::ordinal, palette::bone, systemTextScale};
    if (detached)
        drawContextualFooter(display, "ESC  CLOSE");
    else if (erasable)
        drawContextualFooter(display, "FN+DEL  ERASE");
    if (lines.empty()) {
        display.drawText({centeredTextX("NO DESCRIPTION YET", 0, 240), 54}, "NO DESCRIPTION YET",
                         ink);
        constexpr char hint[] = "WRITE IT IN MAC COMPANION";
        display.drawText({centeredTextX(hint, 0, 240), 70}, hint, quiet);
        return;
    }
    const auto first = shownPage * nfcDescriptionLinesPerPage;
    const auto last = std::min(lines.size(), first + nfcDescriptionLinesPerPage);
    std::int32_t y = firstLineY;
    for (auto index = first; index < last; ++index) {
        display.drawText({marginX, y}, lines[index].c_str(), ink);
        y += lineHeight;
    }
}

void drawNfcNameEntry(IDisplayAdapter& display, const std::string& title, const std::string& draft,
                      std::size_t maxLength) {
    drawHeader(display, title, {}, palette::ordinal);
    const TextStyle quiet{palette::ordinal, palette::bone, systemTextScale};
    const TextStyle ink{palette::ink, palette::bone, systemTextScale};
    display.drawText({marginX, 30}, "SHORT NAME FOR THE CONTAINER", quiet);
    // The draft scrolls so the cursor stays visible.
    auto shown = draft + "_";
    const auto columns = systemTextMaxCharacters(rightEdge - marginX);
    if (shown.size() > columns)
        shown = shown.substr(shown.size() - columns);
    display.drawText({marginX, editorY}, shown.c_str(), ink);
    display.fillRectangle({marginX, editorY + 12}, rightEdge - marginX, 1, palette::ordinal);
    const auto limit = std::to_string(draft.size()) + "/" + std::to_string(maxLength);
    const auto remaining = std::to_string(maxLength - std::min(maxLength, draft.size())) + " LEFT";
    display.drawText({marginX, editorLimitY}, remaining.c_str(), quiet);
    display.drawText({rightAlignedTextX(limit.c_str(), rightEdge), editorLimitY}, limit.c_str(),
                     quiet);
    display.drawText({marginX, 92}, "DESCRIBE IT ON THE MAC", quiet);
    drawContextualFooter(display, "ESC  CANCEL", draft.empty() ? "" : "ENTER  SAVE");
}

} // namespace cardputer_hub::apps
