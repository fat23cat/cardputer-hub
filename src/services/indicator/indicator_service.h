#pragma once

#include "core/indicator/led_adapter.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace cardputer_hub::services {

inline constexpr char pomodoroIndicatorOwner[] = "pomodoro";
inline constexpr char ledGalleryIndicatorOwner[] = "led-gallery";
inline constexpr std::uint8_t defaultIndicatorBrightnessPercent = 3;

enum class IndicatorPriority : std::uint8_t {
    Idle = 0,
    Connection,
    BackgroundApplication,
    ForegroundApplication,
    Notification,
    Warning,
    Critical,
};

struct IndicatorFrame {
    std::array<core::RgbColor, core::ledMatrixPixelCount> pixels{};
};

struct IndicatorResolvedOutput {
    bool hasFrame = false;
    std::string owner;
    IndicatorPriority priority = IndicatorPriority::Idle;
    IndicatorFrame frame{};
    std::uint8_t brightnessPercent = 0;
};

class IndicatorService;

class IndicatorClaim {
  public:
    IndicatorClaim() = default;
    IndicatorClaim(IndicatorClaim&& other) noexcept;
    IndicatorClaim& operator=(IndicatorClaim&& other) noexcept;
    ~IndicatorClaim();

    IndicatorClaim(const IndicatorClaim&) = delete;
    IndicatorClaim& operator=(const IndicatorClaim&) = delete;

    [[nodiscard]] bool valid() const noexcept { return service_ != nullptr && token_ != 0; }
    void setFrame(const IndicatorFrame& frame);
    void release();

  private:
    friend class IndicatorService;
    IndicatorClaim(IndicatorService* service, std::uint32_t token) noexcept;

    IndicatorService* service_ = nullptr;
    std::uint32_t token_ = 0;
};

class IndicatorService {
  public:
    explicit IndicatorService(core::ILEDAdapter& adapter);

    [[nodiscard]] IndicatorClaim acquire(std::string_view owner, IndicatorPriority priority);
    void update();
    void setMaximumBrightnessPercent(std::uint8_t percent) noexcept;
    std::uint8_t maximumBrightnessPercent() const noexcept { return maximumBrightnessPercent_; }
    [[nodiscard]] IndicatorResolvedOutput resolved() const { return resolved_; }
    [[nodiscard]] int adapterWrites() const noexcept { return adapterWrites_; }

  private:
    friend class IndicatorClaim;
    struct Record {
        std::uint32_t token = 0;
        std::string owner;
        IndicatorPriority priority = IndicatorPriority::Idle;
        IndicatorFrame frame{};
        bool hasFrame = false;
    };

    void setFrame(std::uint32_t token, const IndicatorFrame& frame);
    void release(std::uint32_t token);
    void resolve();
    [[nodiscard]] Record* find(std::uint32_t token) noexcept;
    [[nodiscard]] static core::LedHardwareFrame hardwareFrame(const IndicatorFrame& frame,
                                                              std::uint8_t brightnessPercent);

    core::ILEDAdapter& adapter_;
    std::vector<Record> records_;
    std::uint32_t nextToken_ = 1;
    IndicatorResolvedOutput resolved_{};
    core::LedHardwareFrame lastHardware_{};
    bool haveLastHardware_ = false;
    int adapterWrites_ = 0;
    bool dirty_ = true;
    bool brightnessDirty_ = false;
    std::uint8_t maximumBrightnessPercent_ = defaultIndicatorBrightnessPercent;
};

} // namespace cardputer_hub::services
