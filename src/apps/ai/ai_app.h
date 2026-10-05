#pragma once

#include "apps/ai_agent_status/ai_agent_status_app.h"
#include "apps/ai_usage/ai_usage_app.h"

namespace cardputer_hub::apps {

class AiApp final : public IMiniApp {
  public:
    AiApp(services::AiUsageService& usage, services::AiUsageIndicatorController& usageIndicator,
          services::AiAgentStatusService& status,
          services::AiAgentStatusIndicatorController& statusIndicator,
          core::IDisplayAdapter& display);
    void onActivate() override;
    void onDeactivate() override;
    void update(const core::InputEvents& input, std::chrono::milliseconds elapsed) override;
    bool handleBack() override;

  private:
    enum class Page : std::uint8_t { Status, Usage };

    // Keep the existing 240x135 content coordinates and text sizes. Only vertical
    // positions and rectangle heights are fitted below the shared navigation.
    // Details use the full display. The shell groups both views and header into
    // one frame, so child beginFrame/endFrame calls are intentionally no-ops.
    class ContentDisplay final : public core::IDisplayAdapter {
      public:
        explicit ContentDisplay(AiApp& owner) : owner_(owner) {}
        void clear(core::RgbColor color) override;
        void fillRectangle(core::PixelPosition position, std::int32_t width, std::int32_t height,
                           core::RgbColor color) override;
        void drawText(core::PixelPosition position, const char* text,
                      core::TextStyle style) override;

      private:
        int mapY(int y) const;
        AiApp& owner_;
    };

    bool hasHeader() const;
    bool needsAttention() const;
    void drawHeader();
    IMiniApp& currentView();

    services::AiAgentStatusService& status_;
    core::IDisplayAdapter& display_;
    ContentDisplay contentDisplay_;
    AiUsageApp usageView_;
    AiAgentStatusApp statusView_;
    Page page_ = Page::Status;
    bool active_ = false;
    bool headerDrawn_ = false;
    bool attentionDrawn_ = false;
};

} // namespace cardputer_hub::apps
