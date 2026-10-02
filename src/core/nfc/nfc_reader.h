#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace cardputer_hub::core {

// Project-owned NFC reader boundary. Nothing from the ST25R3916 vendor library
// appears here; the hardware adapter translates its own types into these.
// The only tag write is one NFC Forum Type 2 page inside an NTAG21x user area:
// there is no card emulation, no MIFARE Classic authentication and no key
// handling, and lock, configuration and OTP pages cannot be addressed.

enum class NfcTechnology : std::uint8_t { NfcA, NfcB, NfcF, NfcV };

enum class NfcCardType : std::uint8_t {
    Unknown,
    MifareClassicMini,
    MifareClassic1K,
    MifareClassic2K,
    MifareClassic4K,
    MifareUltralight,
    MifareUltralightEv1,
    MifareUltralightNano,
    MifareUltralightC,
    Ntag203,
    Ntag210,
    Ntag212,
    Ntag213,
    Ntag215,
    Ntag216,
    Ntag4xx,
    St25ta,
    MifarePlus,
    MifareDesfire,
    Iso14443Type4,
    Iso18092,
    Iso14443B,
    Felica,
    Iso15693,
};

// Identification only. Fields the reader could not determine stay absent and
// are never replaced by zero-filled placeholders. The UID is diagnostic: it is
// never a persistent key and never authorizes anything.
struct NfcCardInfo {
    NfcTechnology technology = NfcTechnology::NfcA;
    NfcCardType type = NfcCardType::Unknown;
    std::vector<std::uint8_t> uid;
    std::optional<std::uint16_t> atqa;
    std::optional<std::uint8_t> sak;
    std::optional<std::uint32_t> totalBytes;
    std::optional<std::uint32_t> userBytes;
};

// Reader-issued identity of one physical card activation. Every operation
// result echoes the activation it ran against, so a completion that belongs to
// an earlier card can be recognised and discarded.
using NfcActivationId = std::uint32_t;

enum class NfcReaderInitResult : std::uint8_t {
    Ready,
    // No reader answered on its bus. The adapter leaves the bus pins released.
    NotPresent,
    // A reader answered but could not be brought up.
    Failed,
};

enum class NfcDetectStatus : std::uint8_t { NoCard, CardActivated, ReaderLost };

struct NfcDetection {
    NfcDetectStatus status = NfcDetectStatus::NoCard;
    NfcActivationId activation = 0;
    NfcCardInfo card;
};

enum class NfcPresence : std::uint8_t { Present, Removed, ReaderLost };

inline constexpr std::size_t nfcType2PageBytes = 4;
// One Type 2 READ answers four consecutive pages.
inline constexpr std::size_t nfcType2ReadBytes = 16;
// Pages 0-3 hold the UID, static lock bytes and the capability container.
inline constexpr std::uint16_t nfcType2FirstUserPage = 4;
using NfcType2Page = std::array<std::uint8_t, nfcType2PageBytes>;

enum class NfcOperationStatus : std::uint8_t {
    Ok,
    // The tag refused the operation, for example a locked page.
    Rejected,
    // Communication failed; the tag may have left the field.
    Failed,
    ReaderLost,
};

struct NfcPageReadResult {
    NfcActivationId activation = 0;
    NfcOperationStatus status = NfcOperationStatus::Failed;
    std::array<std::uint8_t, nfcType2ReadBytes> data{};
};

struct NfcPageWriteResult {
    NfcActivationId activation = 0;
    NfcOperationStatus status = NfcOperationStatus::Failed;
};

// The user-memory pages of a supported NTAG21x, or nullopt for any other type.
struct NfcType2UserArea {
    std::uint16_t firstPage = nfcType2FirstUserPage;
    std::uint16_t pageCount = 0;
};

[[nodiscard]] inline std::optional<NfcType2UserArea> nfcNtagUserArea(NfcCardType type) noexcept {
    switch (type) {
    case NfcCardType::Ntag213:
        return NfcType2UserArea{nfcType2FirstUserPage, 36};
    case NfcCardType::Ntag215:
        return NfcType2UserArea{nfcType2FirstUserPage, 126};
    case NfcCardType::Ntag216:
        return NfcType2UserArea{nfcType2FirstUserPage, 222};
    default:
        break;
    }
    return std::nullopt;
}

class INfcReader {
  public:
    virtual ~INfcReader() = default;

    // Bring the reader up. Called once at start and again after a loss.
    virtual NfcReaderInitResult initialize() = 0;
    // Field off and bus released; initialize() may be called again.
    virtual void shutdown() = 0;
    // RF field control. The field is only on while a Mini App is scanning.
    virtual void setFieldEnabled(bool enabled) = 0;

    // Look for a card. At most one card is activated and identified per call.
    virtual NfcDetection detect() = 0;
    // Is the card of this activation still in the field?
    virtual NfcPresence presence(NfcActivationId activation) = 0;

    // NFC Forum Type 2 (NTAG21x only): four pages from `firstPage`.
    virtual NfcPageReadResult readPages(NfcActivationId activation, std::uint16_t firstPage) = 0;
    // One page inside the NTAG21x user area; any other page is refused without
    // reaching the tag.
    virtual NfcPageWriteResult writePage(NfcActivationId activation, std::uint16_t page,
                                         const NfcType2Page& data) = 0;
};

} // namespace cardputer_hub::core
