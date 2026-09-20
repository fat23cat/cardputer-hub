#pragma once

#include "core/display/display_adapter.h"
#include "services/pomodoro/pomodoro_service.h"

#include <cstdint>

namespace cardputer_hub::apps {

inline constexpr char pomodoroAppId[] = "pomodoro";
inline constexpr std::int32_t pomodoroLcdSegments = 24;

[[nodiscard]] const char* pomodoroPhaseLabel(services::PomodoroPhase phase) noexcept;
[[nodiscard]] std::uint8_t
pomodoroCycleDisplay(const services::PomodoroSnapshot& snapshot) noexcept;
[[nodiscard]] std::uint8_t
pomodoroLcdFilledSegments(const services::PomodoroSnapshot& snapshot) noexcept;
void formatPomodoroRemaining(const services::PomodoroSnapshot& snapshot, char (&text)[6]) noexcept;

void drawPomodoroScreen(core::IDisplayAdapter& display, const services::PomodoroSnapshot& snapshot);

} // namespace cardputer_hub::apps
