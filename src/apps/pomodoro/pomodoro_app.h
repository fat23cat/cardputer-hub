#pragma once

#include "apps/pomodoro/pomodoro_graphics.h"
#include "apps/runtime/mini_app.h"
#include "core/display/display_adapter.h"
#include "services/pomodoro/pomodoro_service.h"

#include <optional>

namespace cardputer_hub::apps {

class PomodoroApp final : public IMiniApp {
  public:
    PomodoroApp(services::PomodoroService& pomodoro, core::IDisplayAdapter& display);

    void onActivate() override;
    void onDeactivate() override;
    void update(const core::InputEvents& input, std::chrono::milliseconds elapsed) override;

  private:
    void handle(const core::InputEvent& event);
    void render();

    services::PomodoroService& pomodoro_;
    core::IDisplayAdapter& display_;
    std::optional<services::PomodoroSnapshot> frame_;
};

} // namespace cardputer_hub::apps
