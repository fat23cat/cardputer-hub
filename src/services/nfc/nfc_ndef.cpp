#include "services/nfc/nfc_ndef.h"

#include <algorithm>

namespace cardputer_hub::services {
namespace {

// Capability container (page 3) and TLV values from the NFC Forum Type 2 Tag
// specification.
constexpr std::uint8_t ccMagic = 0xE1;
constexpr std::uint8_t tlvNull = 0x00;
constexpr std::uint8_t tlvLockControl = 0x01;
constexpr std::uint8_t tlvMemoryControl = 0x02;
constexpr std::uint8_t tlvNdef = 0x03;
constexpr std::uint8_t tlvTerminator = 0xFE;
constexpr std::uint8_t tlvLongLength = 0xFF;
constexpr std::size_t headerBytes = 8;

// NDEF record header flags.
constexpr std::uint8_t recordMessageBegin = 0x80;
constexpr std::uint8_t recordMessageEnd = 0x40;
constexpr std::uint8_t recordChunked = 0x20;
constexpr std::uint8_t recordShort = 0x10;
constexpr std::uint8_t recordIdPresent = 0x08;
constexpr std::uint8_t recordTnfMask = 0x07;
constexpr std::uint8_t tnfWellKnown = 0x01;
constexpr std::uint8_t textUtf16 = 0x80;
constexpr std::uint8_t textReserved = 0x40;
constexpr std::uint8_t textLanguageMask = 0x3F;

struct TlvLength {
    std::size_t value = 0;
    std::size_t header = 0;
};

// The length field of the TLV at `position`, or nullopt when it leaves `limit`.
std::optional<TlvLength> tlvLength(const std::uint8_t* data, std::size_t position,
                                   std::size_t limit) {
    if (position + 1 >= limit)
        return std::nullopt;
    if (data[position + 1] != tlvLongLength)
        return TlvLength{data[position + 1], 2};
    if (position + 3 >= limit)
        return std::nullopt;
    return TlvLength{static_cast<std::size_t>(data[position + 2]) << 8U | data[position + 3], 4};
}

} // namespace

NfcTagInspection inspectType2Window(const std::uint8_t* window, std::size_t size) {
    NfcTagInspection inspection;
    if (window != nullptr)
        inspection.raw.assign(window, window + size);
    if (window == nullptr || size < headerBytes) {
        inspection.content = NfcTagContent::ReadFailed;
        return inspection;
    }
    const std::uint8_t* cc = window + 4;
    if (cc[0] != ccMagic || (cc[1] >> 4U) != 1 || (cc[3] >> 4U) != 0) {
        inspection.content = NfcTagContent::NotFormatted;
        return inspection;
    }
    inspection.capacity = static_cast<std::uint16_t>(cc[2] * 8U);
    // Any static lock bit makes the tag read-only for this firmware: it never
    // writes around locked pages.
    inspection.writable = (cc[3] & 0x0FU) == 0 && window[2] == 0 && window[3] == 0;

    const std::uint8_t* data = window + headerBytes;
    const bool complete = size - headerBytes >= inspection.capacity;
    const std::size_t limit = std::min<std::size_t>(size - headerBytes, inspection.capacity);
    const auto decide = [&inspection](NfcTagContent content) {
        inspection.content = content;
        return inspection;
    };
    const auto undecided = [&]() {
        inspection.incomplete = true;
        return inspection;
    };
    // Control TLVs and data after an NDEF TLV must not be overwritten by a
    // write from the first user page.
    bool control = false;
    bool emptyMessage = false;
    std::size_t position = 0;
    while (position < limit) {
        switch (data[position]) {
        case tlvNull:
            ++position;
            continue;
        case tlvTerminator:
            return decide(!inspection.message.empty() ? NfcTagContent::Message
                          : control                   ? NfcTagContent::OtherData
                                                      : NfcTagContent::Blank);
        case tlvLockControl:
        case tlvMemoryControl: {
            if (emptyMessage || !inspection.message.empty()) {
                inspection.reserved = true;
                return decide(NfcTagContent::OtherData);
            }
            const auto length = tlvLength(data, position, limit);
            if (!length || position + length->header + length->value > limit)
                return complete ? decide(NfcTagContent::OtherData) : undecided();
            control = true;
            inspection.reserved = true;
            position += length->header + length->value;
            continue;
        }
        case tlvNdef: {
            if (emptyMessage || !inspection.message.empty()) {
                inspection.reserved = true;
                return decide(NfcTagContent::OtherData);
            }
            const auto length = tlvLength(data, position, limit);
            if (!length)
                return complete ? decide(NfcTagContent::OtherData) : undecided();
            if (length->value == 0) {
                emptyMessage = true;
                position += length->header;
                continue;
            }
            if (position + length->header + length->value > limit)
                return complete ? decide(NfcTagContent::OtherData) : undecided();
            const auto* first = data + position + length->header;
            inspection.message.assign(first, first + length->value);
            position += length->header + length->value;
            continue;
        }
        default:
            if (emptyMessage || !inspection.message.empty())
                inspection.reserved = true;
            return decide(NfcTagContent::OtherData);
        }
    }
    if (!complete)
        return undecided();
    // A message without a Terminator can fill the declared data area.
    return decide(!inspection.message.empty() ? NfcTagContent::Message
                  : control                   ? NfcTagContent::OtherData
                                              : NfcTagContent::Blank);
}

std::optional<std::vector<std::uint8_t>>
encodeType2NdefArea(const std::vector<std::uint8_t>& message, std::size_t capacity) {
    if (message.empty() || message.size() > 0xFFFE)
        return std::nullopt;
    std::vector<std::uint8_t> area{tlvNdef};
    if (message.size() < tlvLongLength) {
        area.push_back(static_cast<std::uint8_t>(message.size()));
    } else {
        area.push_back(tlvLongLength);
        area.push_back(static_cast<std::uint8_t>(message.size() >> 8U));
        area.push_back(static_cast<std::uint8_t>(message.size() & 0xFFU));
    }
    area.insert(area.end(), message.begin(), message.end());
    area.push_back(tlvTerminator);
    while (area.size() % 4 != 0)
        area.push_back(0);
    if (area.size() > capacity)
        return std::nullopt;
    return area;
}

std::vector<std::uint8_t> encodeNdefTextMessage(std::string_view text, std::string_view language) {
    std::vector<std::uint8_t> payload;
    payload.push_back(static_cast<std::uint8_t>(language.size() & textLanguageMask));
    payload.insert(payload.end(), language.begin(), language.end());
    payload.insert(payload.end(), text.begin(), text.end());

    std::vector<std::uint8_t> message;
    const bool shortRecord = payload.size() <= 0xFF;
    message.push_back(static_cast<std::uint8_t>(recordMessageBegin | recordMessageEnd |
                                                (shortRecord ? recordShort : 0) | tnfWellKnown));
    message.push_back(1);
    if (shortRecord) {
        message.push_back(static_cast<std::uint8_t>(payload.size()));
    } else {
        for (int shift = 24; shift >= 0; shift -= 8)
            message.push_back(static_cast<std::uint8_t>(payload.size() >> shift));
    }
    message.push_back('T');
    message.insert(message.end(), payload.begin(), payload.end());
    return message;
}

std::optional<std::string> decodeNdefTextMessage(const std::vector<std::uint8_t>& message) {
    if (message.size() < 4)
        return std::nullopt;
    const auto header = message[0];
    if ((header & recordMessageBegin) == 0 || (header & recordMessageEnd) == 0 ||
        (header & recordChunked) != 0 || (header & recordTnfMask) != tnfWellKnown)
        return std::nullopt;
    const std::size_t typeLength = message[1];
    std::size_t position = 2;
    std::size_t payloadLength = 0;
    if ((header & recordShort) != 0) {
        payloadLength = message[position++];
    } else {
        if (position + 4 > message.size())
            return std::nullopt;
        for (int index = 0; index < 4; ++index)
            payloadLength = payloadLength << 8U | message[position++];
    }
    std::size_t idLength = 0;
    if ((header & recordIdPresent) != 0) {
        if (position >= message.size())
            return std::nullopt;
        idLength = message[position++];
    }
    if (typeLength != 1 || position >= message.size() || message[position] != 'T')
        return std::nullopt;
    position += typeLength + idLength;
    if (position >= message.size() || message.size() - position != payloadLength)
        return std::nullopt;
    const auto status = message[position];
    if ((status & (textUtf16 | textReserved)) != 0)
        return std::nullopt;
    const std::size_t languageLength = status & textLanguageMask;
    if (1 + languageLength > payloadLength)
        return std::nullopt;
    const auto text = position + 1 + languageLength;
    return std::string(message.begin() + static_cast<std::ptrdiff_t>(text), message.end());
}

} // namespace cardputer_hub::services
