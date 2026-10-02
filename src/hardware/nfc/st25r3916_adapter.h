#pragma once

#include "core/logging/logger.h"
#include "core/nfc/nfc_reader.h"

#include <memory>

namespace cardputer_hub::hardware {

// M5Stack Unit NFC (ST25R3916) on Grove port A, driven through the pinned
// M5Unit-NFC library. This is the only code that includes that library: its
// types never cross this header. Besides detection it reads NFC Forum Type 2
// pages and writes single pages inside an NTAG213/215/216 user area; it has no
// card emulation, MIFARE Classic authentication or key handling.
//
// Grove port A (SCL G1, SDA G2) is also the Unit Puzzle data pin. The adapter
// claims M5.Ex_I2C only when a reader answers at the Unit NFC address and
// releases it again otherwise, so a missing reader leaves the pins untouched.
class St25r3916Adapter final : public core::INfcReader {
  public:
    explicit St25r3916Adapter(core::Logger* logger = nullptr);
    ~St25r3916Adapter() override;

    St25r3916Adapter(const St25r3916Adapter&) = delete;
    St25r3916Adapter& operator=(const St25r3916Adapter&) = delete;

    core::NfcReaderInitResult initialize() override;
    void shutdown() override;
    void setFieldEnabled(bool enabled) override;
    core::NfcDetection detect() override;
    core::NfcPresence presence(core::NfcActivationId activation) override;
    core::NfcPageReadResult readPages(core::NfcActivationId activation,
                                      std::uint16_t firstPage) override;
    core::NfcPageWriteResult writePage(core::NfcActivationId activation, std::uint16_t page,
                                       const core::NfcType2Page& data) override;

  private:
    struct Impl;

    void log(core::LogLevel level, const char* message) const;

    core::Logger* logger_;
    std::unique_ptr<Impl> impl_;
    // Monotonic across re-initialization, so the service can order activations.
    core::NfcActivationId lastActivation_ = 0;
};

} // namespace cardputer_hub::hardware
