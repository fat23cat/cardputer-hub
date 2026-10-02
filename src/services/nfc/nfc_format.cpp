#include "services/nfc/nfc_format.h"

namespace cardputer_hub::services {
namespace {

constexpr char hexDigits[] = "0123456789ABCDEF";

void appendByte(std::string& text, std::uint8_t value) {
    text.push_back(hexDigits[value >> 4]);
    text.push_back(hexDigits[value & 0x0F]);
}

} // namespace

const char* nfcTechnologyName(core::NfcTechnology technology) noexcept {
    switch (technology) {
    case core::NfcTechnology::NfcA:
        return "NFC-A";
    case core::NfcTechnology::NfcB:
        return "NFC-B";
    case core::NfcTechnology::NfcF:
        return "NFC-F";
    case core::NfcTechnology::NfcV:
        return "NFC-V";
    }
    return "NFC";
}

const char* nfcCardTypeName(core::NfcCardType type) noexcept {
    switch (type) {
    case core::NfcCardType::MifareClassicMini:
        return "MIFARE CLASSIC MINI";
    case core::NfcCardType::MifareClassic1K:
        return "MIFARE CLASSIC 1K";
    case core::NfcCardType::MifareClassic2K:
        return "MIFARE CLASSIC 2K";
    case core::NfcCardType::MifareClassic4K:
        return "MIFARE CLASSIC 4K";
    case core::NfcCardType::MifareUltralight:
        return "MIFARE ULTRALIGHT";
    case core::NfcCardType::MifareUltralightEv1:
        return "MIFARE ULTRALIGHT EV1";
    case core::NfcCardType::MifareUltralightNano:
        return "MIFARE ULTRALIGHT NANO";
    case core::NfcCardType::MifareUltralightC:
        return "MIFARE ULTRALIGHT C";
    case core::NfcCardType::Ntag203:
        return "NTAG 203";
    case core::NfcCardType::Ntag210:
        return "NTAG 210";
    case core::NfcCardType::Ntag212:
        return "NTAG 212";
    case core::NfcCardType::Ntag213:
        return "NTAG 213";
    case core::NfcCardType::Ntag215:
        return "NTAG 215";
    case core::NfcCardType::Ntag216:
        return "NTAG 216";
    case core::NfcCardType::Ntag4xx:
        return "NTAG 4XX";
    case core::NfcCardType::St25ta:
        return "ST25TA";
    case core::NfcCardType::MifarePlus:
        return "MIFARE PLUS";
    case core::NfcCardType::MifareDesfire:
        return "MIFARE DESFIRE";
    case core::NfcCardType::Iso14443Type4:
        return "ISO 14443-4";
    case core::NfcCardType::Iso18092:
        return "ISO 18092";
    case core::NfcCardType::Iso14443B:
        return "ISO 14443-B";
    case core::NfcCardType::Felica:
        return "FELICA";
    case core::NfcCardType::Iso15693:
        return "ISO 15693";
    case core::NfcCardType::Unknown:
        break;
    }
    return "UNKNOWN CARD";
}

std::string nfcHexSpaced(const std::uint8_t* bytes, std::size_t count) {
    std::string text;
    text.reserve(count * 3);
    for (std::size_t index = 0; index < count; ++index) {
        if (index != 0)
            text.push_back(' ');
        appendByte(text, bytes[index]);
    }
    return text;
}

std::string nfcHexSpaced(const std::vector<std::uint8_t>& bytes) {
    return nfcHexSpaced(bytes.data(), bytes.size());
}

std::string nfcHexCompact(const std::uint8_t* bytes, std::size_t count) {
    std::string text;
    text.reserve(count * 2);
    for (std::size_t index = 0; index < count; ++index)
        appendByte(text, bytes[index]);
    return text;
}

std::string nfcAtqaText(std::uint16_t atqa) {
    const std::uint8_t bytes[] = {static_cast<std::uint8_t>(atqa >> 8),
                                  static_cast<std::uint8_t>(atqa & 0xFF)};
    return nfcHexSpaced(bytes, sizeof(bytes));
}

std::string nfcByteText(std::uint8_t value) {
    std::string text;
    appendByte(text, value);
    return text;
}

} // namespace cardputer_hub::services
