#pragma once

#include "core/display/display_adapter.h"
#include "services/indicator/indicator_service.h"
#include "services/microphone/microphone_service.h"

namespace cardputer_hub::apps {

[[nodiscard]] core::RgbColor soundReactiveColor(float level) noexcept;
[[nodiscard]] float soundReactiveSpeed(float level) noexcept;
[[nodiscard]] services::IndicatorFrame soundReactiveLedFrame(float phase, float level,
                                                             core::RgbColor color) noexcept;
void drawSoundReactiveScreen(core::IDisplayAdapter& display, float phase, float level,
                             core::RgbColor color);
void drawSoundReactiveDiagnostics(core::IDisplayAdapter& display,
                                  const services::MicrophoneSnapshot& snapshot);
void drawSoundReactiveFailure(core::IDisplayAdapter& display);

} // namespace cardputer_hub::apps
