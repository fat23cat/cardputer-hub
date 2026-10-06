#pragma once

#include "core/display/display_adapter.h"
#include "services/inventory/inventory_record.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace cardputer_hub::apps {

inline constexpr char nfcAppId[] = "nfc";
// Description lines between the header rule and the footer.
inline constexpr std::size_t nfcDescriptionLinesPerPage = 7;
// Keep 31 columns within the 6-pixel margins at the shared system text scale.
inline constexpr std::size_t nfcTextColumns = 31;

// A state screen: a title, up to three body lines and an optional footer.
struct NfcMessage {
    std::string title;
    std::vector<std::string> lines;
    // Left: cancel or back; right: confirm.
    std::string footerLeft;
    std::string footerRight;
    // A failure is drawn with the alert colour.
    bool alert = false;
};

// The description wrapped to the display: each line break starts a new line,
// and long lines wrap at word boundaries. UTF-8 throughout.
[[nodiscard]] std::vector<std::string> nfcDescriptionLines(const services::InventoryRecord& record);
[[nodiscard]] std::size_t nfcPageCount(std::size_t lineCount);

void drawNfcMessage(core::IDisplayAdapter& display, const std::string& headerStatus,
                    const NfcMessage& message);
// One page of a known container: its name, a page counter and description
// lines, with the erase hint when the tag can be erased.
void drawNfcRecord(core::IDisplayAdapter& display, const services::InventoryRecord& record,
                   std::size_t page, bool registered, bool erasable, bool detached = false);
// The Cardputer name editor: draft with cursor and the visible length limit.
void drawNfcNameEntry(core::IDisplayAdapter& display, const std::string& title,
                      const std::string& draft, std::size_t maxLength);

} // namespace cardputer_hub::apps
