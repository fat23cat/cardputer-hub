#pragma once
#include "core/actions/action_bus.h"
#include "core/indicator/led_canvas_types.h"
#include <array>
#include <chrono>
#include <cstdint>
#include <optional>

namespace cardputer_hub::services {
inline constexpr char ledCanvasHoldActionId[] = "led-canvas.set-hold";
inline constexpr char ledCanvasClearActionId[] = "led-canvas.clear";
using core::LedCanvasBatch;
using core::LedCanvasFade;
using core::ledCanvasFadeIds;
using core::LedCanvasMode;
using core::ledCanvasModeIds;
using core::ledCanvasModeNames;
using core::LedCanvasPlayback;
using core::LedCanvasPoint;
using core::LedCanvasSettings;
struct LedCanvasStatus {
    bool open = false;
    std::uint32_t revision = 0, clearGeneration = 0;
    std::size_t fingers = 0, queued = 0;
    bool recent = false;
    LedCanvasSettings settings;
};
class LedCanvasService final : public core::IActionHandler {
  public:
    void setActive(bool active);
    void endPointers();
    bool submit(const LedCanvasBatch& batch);
    bool setSettings(const LedCanvasSettings& settings);
    void clear();
    [[nodiscard]] LedCanvasStatus status() const;
    // Advance once per loop, then drain with elapsed=0. Playback is globally ordered.
    [[nodiscard]] std::optional<LedCanvasPlayback> takeDue(std::chrono::milliseconds elapsed);
    [[nodiscard]] std::uint32_t takeEnded();
    core::ActionHandlingResult handle(const core::Action& action) override;

  private:
    struct Client {
        bool used = false;
        std::uint32_t id = 0, seq = 0;
        std::uint64_t touched = 0;
        std::uint64_t lastScheduled = 0;
    };
    struct Pointer {
        bool down = false;
        bool used = false;
        std::uint64_t touched = 0;
    };
    std::array<Client, 4> clients_{};
    std::array<Pointer, 20> pointers_{};
    std::array<LedCanvasPlayback, 256> queue_{};
    std::size_t count_ = 0;
    std::uint64_t clock_ = 0, lastPoint_ = 0, useCounter_ = 0;
    std::uint32_t revision_ = 0, clearGeneration_ = 0, ended_ = 0;
    LedCanvasSettings settings_;
    bool active_ = false, seenPoint_ = false;
};
} // namespace cardputer_hub::services
