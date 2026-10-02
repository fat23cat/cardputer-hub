#pragma once

#include "core/capabilities/capability_registry.h"
#include "core/logging/logger.h"
#include "core/nfc/nfc_reader.h"
#include "services/nfc/nfc_models.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace cardputer_hub::services {

enum class NfcServiceState : std::uint8_t {
    // No reader is present, or it was lost and has not returned yet.
    Unavailable,
    Initializing,
    // Reader ready, no tag in the field.
    Idle,
    // A tag is present and its data area is being read.
    Reading,
    // A tag is present and its content is published.
    Ready,
    // Writing an NDEF message to the present tag, then reading it back.
    Writing,
    // A reader answered but failed, or a ready reader stopped responding.
    Error,
};

// Read-only view for the Mini App and InventoryService. `generation` changes
// whenever anything here changes, so a settled view repaints nothing.
struct NfcStatus {
    NfcServiceState state = NfcServiceState::Unavailable;
    bool scanning = false;
    std::uint32_t generation = 0;
    // Identity of the current tag session; 0 while no tag is present.
    std::uint32_t session = 0;
    std::optional<core::NfcCardInfo> card;
    NfcTagInspection tag;
    // The last write and the session it belongs to. A write never outlives its
    // session: removal or replacement reports Interrupted for the old session.
    NfcWriteState write = NfcWriteState::None;
    std::uint32_t writeSession = 0;
};

struct NfcServiceConfig {
    std::chrono::milliseconds pollInterval{150};
    std::chrono::milliseconds presenceInterval{300};
    std::chrono::milliseconds retryInterval{2000};
    // Results from an earlier tag tolerated before the reader is reset.
    std::uint8_t maxStaleResults = 3;
};

// Owns the NFC reader lifecycle, tag sessions and Type 2 page operations.
//
// The service performs at most one reader operation per update(), so reading
// or writing a tag never stalls the main loop. A Mini App only states whether
// it wants scanning; it never drives the reader. The RF field is on only while
// scanning is requested. Only NTAG213/215/216 data areas are read, and only a
// blank, writable one is ever written.
class NfcService {
  public:
    NfcService(core::INfcReader& reader, core::CapabilityRegistry& capabilities,
               core::Logger* logger = nullptr, NfcServiceConfig config = {});

    // Initializes the reader once. Returns true when it is ready. A reader that
    // does not answer at start is not probed again: its bus pins may belong to
    // another device.
    bool start();
    void update(std::chrono::milliseconds elapsed);

    void startScanning();
    void stopScanning();

    // Writes one NDEF message to the tag of `session` and verifies it by reading
    // it back. Accepted only while that session's tag is published as Blank and
    // writable and the message fits; returns false otherwise (write Refused).
    bool writeMessage(std::uint32_t session, const std::vector<std::uint8_t>& message);
    // Empties the NDEF area of the tag of `session` (an empty NDEF TLV and a
    // Terminator on the first user page) and verifies it. Accepted only for an
    // erasable tag (see isErasable) whose inspected bytes are exactly
    // `expectedRaw`, so the caller erases only what it has seen and confirmed;
    // returns false otherwise.
    bool eraseTag(std::uint32_t session, const std::vector<std::uint8_t>& expectedRaw);
    // A writable, NDEF-formatted NTAG holding a message or other data, without
    // control TLVs: what eraseTag accepts.
    [[nodiscard]] static bool isErasable(const NfcTagInspection& tag) noexcept;

    [[nodiscard]] const NfcStatus& status() const noexcept { return status_; }
    [[nodiscard]] NfcServiceState state() const noexcept { return status_.state; }
    // True once a reader answered on its bus, even if it later failed.
    [[nodiscard]] bool readerPresent() const noexcept { return readerPresent_; }

  private:
    struct Session {
        std::uint32_t id = 0;
        core::NfcActivationId activation = 0;
        std::optional<core::NfcType2UserArea> userArea;
        std::vector<std::uint8_t> window;
        std::uint8_t readRetries = 0;
        std::uint8_t staleResults = 0;
        // Write plan: (page, data) in order, then verification of `area`.
        std::vector<std::pair<std::uint16_t, core::NfcType2Page>> writes;
        std::size_t writeIndex = 0;
        std::uint8_t writeRetries = 0;
        std::vector<std::uint8_t> area;
        std::vector<std::uint8_t> readBack;
        bool verifying = false;
    };

    void initializeReader(bool initial);
    void touch();
    void setState(NfcServiceState state);
    [[nodiscard]] bool readerReady() const noexcept;

    void poll();
    void checkPresence();
    void readStep();
    void writeStep();
    void verifyStep();
    void beginInspection();
    void beginWrite(std::uint32_t session, std::vector<std::uint8_t> area);
    bool refuseWrite(std::uint32_t session);
    void finishWrite(NfcWriteState result);
    // After a failed operation: true when the tag is still there; otherwise the
    // session (or the reader) has already been ended.
    bool confirmPresent();
    bool acceptActivation(core::NfcActivationId activation);
    void endSession();
    void handleReaderLost();
    void interruptWrite();
    void clearTagStatus();
    void log(core::LogLevel level, const char* message) const;

    core::INfcReader& reader_;
    core::CapabilityRegistry& capabilities_;
    core::Logger* logger_;
    NfcServiceConfig config_;

    NfcStatus status_;
    std::optional<Session> session_;
    std::uint32_t nextSession_ = 1;
    bool started_ = false;
    bool scanning_ = false;
    bool readerPresent_ = false;
    bool retryAllowed_ = false;
    bool capabilityPublished_ = false;
    std::chrono::milliseconds sincePoll_{0};
    std::chrono::milliseconds sincePresence_{0};
    std::chrono::milliseconds sinceRetry_{0};
};

} // namespace cardputer_hub::services
