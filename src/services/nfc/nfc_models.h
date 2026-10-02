#pragma once

#include <cstdint>
#include <vector>

namespace cardputer_hub::services {

inline constexpr char nfcReaderCapabilityId[] = "NFC_READER";

// What the inspected NFC Forum Type 2 data area holds. Only NTAG213/215/216
// are inspected; every other card is Unsupported without any data access.
enum class NfcTagContent : std::uint8_t {
    // No tag, or its data area is still being read.
    None,
    Unsupported,
    // No NDEF capability container: the tag is not NDEF formatted.
    NotFormatted,
    // Formatted and provably empty: a Terminator TLV, optionally after one
    // empty NDEF TLV and NULL bytes, or a whole data area of nothing else.
    Blank,
    // One NDEF message, held in full in `message`.
    Message,
    // Anything that is not provably blank and not one short NDEF message: a
    // proprietary or control TLV area without a message, a message longer than
    // the inspected bytes, data after an empty NDEF TLV, or malformed TLVs.
    OtherData,
    // The tag stayed in the field but its data area could not be read.
    ReadFailed,
};

struct NfcTagInspection {
    NfcTagContent content = NfcTagContent::None;
    // False when the capability container or static lock bytes deny writes.
    bool writable = false;
    // NDEF data area size declared by the capability container, in bytes.
    std::uint16_t capacity = 0;
    // The bytes inspected so far decide nothing yet: read more of the data
    // area. Never published by NfcService.
    bool incomplete = false;
    // Lock or Memory Control TLVs describe reserved areas: never erased.
    bool reserved = false;
    std::vector<std::uint8_t> message;
    // Every byte read from page 2 to decide the content. Two inspections of
    // the same unchanged tag read the same bytes, so an erase confirmed for one
    // inspection is never applied to different content.
    std::vector<std::uint8_t> raw;
};

enum class NfcWriteState : std::uint8_t {
    None,
    Writing,
    Verifying,
    // Every page was written and read back identical.
    Succeeded,
    // The tag stayed but refused a page or read back different data.
    Failed,
    // The tag left the field, or another tag arrived, before verification.
    Interrupted,
    // Refused before touching the tag: no blank writable tag in this session,
    // or the message does not fit.
    Refused,
};

} // namespace cardputer_hub::services
