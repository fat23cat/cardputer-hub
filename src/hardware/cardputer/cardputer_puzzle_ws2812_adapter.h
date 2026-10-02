#pragma once

#include "core/indicator/led_adapter.h"

#include <cstdint>

namespace cardputer_hub::hardware {

inline constexpr int puzzleLedGpio = 2;

enum class PuzzleRotation : std::uint8_t {
    Deg0,
    Deg90,
    Deg180,
    Deg270,
};

// Unit Puzzle is column-major, bottom-to-top in the documented upright
// orientation. Matches Codex Microputer ADV `puzzle_renderer::wire_index`.
[[nodiscard]] constexpr std::uint8_t
puzzleWireIndex(std::uint8_t x, std::uint8_t y,
                PuzzleRotation rotation = PuzzleRotation::Deg0) noexcept {
    int panelX = x;
    int panelY = y;
    switch (rotation) {
    case PuzzleRotation::Deg90:
        panelX = core::ledMatrixWidth - 1 - y;
        panelY = x;
        break;
    case PuzzleRotation::Deg180:
        panelX = core::ledMatrixWidth - 1 - x;
        panelY = core::ledMatrixWidth - 1 - y;
        break;
    case PuzzleRotation::Deg270:
        panelX = y;
        panelY = core::ledMatrixWidth - 1 - x;
        break;
    case PuzzleRotation::Deg0:
        break;
    }
    return static_cast<std::uint8_t>((core::ledMatrixWidth - 1 - panelY) +
                                     panelX * core::ledMatrixWidth);
}

[[nodiscard]] constexpr std::uint8_t puzzlePhysicalIndex(std::uint8_t logicalIndex) noexcept {
    if (logicalIndex >= core::ledMatrixPixelCount)
        return 0;
    return puzzleWireIndex(static_cast<std::uint8_t>(logicalIndex % core::ledMatrixWidth),
                           static_cast<std::uint8_t>(logicalIndex / core::ledMatrixWidth));
}

class IPuzzleLedBackend {
  public:
    virtual ~IPuzzleLedBackend() = default;
    virtual void quietLine() = 0;
    virtual bool open() = 0;
    virtual bool writeMappedFrame(const core::LedHardwareFrame& frame) = 0;
    virtual void close() = 0;
};

class PuzzleWs2812Adapter final : public core::ILEDAdapter {
  public:
    explicit PuzzleWs2812Adapter(IPuzzleLedBackend& backend);
    ~PuzzleWs2812Adapter() override;

    PuzzleWs2812Adapter(const PuzzleWs2812Adapter&) = delete;
    PuzzleWs2812Adapter& operator=(const PuzzleWs2812Adapter&) = delete;

    bool begin();
    void writeFrame(const core::LedHardwareFrame& frame) override;

    // Gives the data pin up for the rest of the session. Unit Puzzle and the NFC
    // Unit share Grove port A, so the firmware inhibits the Puzzle when it finds
    // an NFC reader there. An inhibited adapter never touches the backend again,
    // not even to quiet the line: the pin now belongs to another device. Call it
    // before begin() whenever possible; a running strip is released once.
    void inhibit();
    [[nodiscard]] bool inhibited() const noexcept { return inhibited_; }

  private:
    void disable();

    IPuzzleLedBackend& backend_;
    bool ready_ = false;
    bool inhibited_ = false;
};

class EspPuzzleLedBackend final : public IPuzzleLedBackend {
  public:
    EspPuzzleLedBackend() = default;
    ~EspPuzzleLedBackend() override;

    EspPuzzleLedBackend(const EspPuzzleLedBackend&) = delete;
    EspPuzzleLedBackend& operator=(const EspPuzzleLedBackend&) = delete;

    void quietLine() override;
    bool open() override;
    bool writeMappedFrame(const core::LedHardwareFrame& frame) override;
    void close() override;

  private:
    void* strip_ = nullptr;
};

inline PuzzleWs2812Adapter::PuzzleWs2812Adapter(IPuzzleLedBackend& backend) : backend_(backend) {}

inline PuzzleWs2812Adapter::~PuzzleWs2812Adapter() {
    if (!inhibited_)
        disable();
}

inline void PuzzleWs2812Adapter::inhibit() {
    if (inhibited_)
        return;
    if (ready_)
        disable();
    inhibited_ = true;
}

inline void PuzzleWs2812Adapter::disable() {
    backend_.close();
    ready_ = false;
}

inline bool PuzzleWs2812Adapter::begin() {
    if (inhibited_)
        return false;
    if (ready_)
        return true;
    backend_.quietLine();
    if (!backend_.open()) {
        backend_.quietLine();
        ready_ = false;
        return false;
    }
    ready_ = true;
    return true;
}

inline void PuzzleWs2812Adapter::writeFrame(const core::LedHardwareFrame& frame) {
    if (inhibited_)
        return;
    if (!ready_ && !begin())
        return;
    if (!backend_.writeMappedFrame(frame))
        disable();
}

} // namespace cardputer_hub::hardware
