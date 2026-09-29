#pragma once

#include "apps/mac_status/mac_status_graphics.h"
#include "apps/runtime/mini_app.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace cardputer_hub::apps {

// All view state is allocated in onActivate() and released in onDeactivate(),
// so a closed MAC STATUS holds only its two references and a pointer.
class MacStatusApp final : public IMiniApp {
  public:
    MacStatusApp(services::MacStatusService& service, core::IDisplayAdapter& display)
        : service_(service), display_(display) {}
    void onActivate() override;
    void onDeactivate() override;
    void update(const core::InputEvents& input, std::chrono::milliseconds) override;
    MacStatusPage page() const noexcept { return view_ ? view_->page : MacStatusPage::Overview; }

  private:
    struct View {
        MacStatusPage page = MacStatusPage::Overview;
        bool pagesVisible = false;
        bool drawn = false;
        std::optional<MacStatusPresentation> frame;
        std::uint32_t historyGeneration = 0;
        std::vector<std::string> regionKeys;
        // Inputs of the last detail layout; an unchanged page is not rebuilt.
        std::array<std::uint32_t, 3> detailInputs{};
    };

    void turnPage(int delta);
    void showPage(MacStatusPage page);
    void renderOverview(const services::MacStatusHistory& history);
    void renderDetailPage(const services::MacStatusHistory& history,
                          const services::MacStatusDetails& details);

    services::MacStatusService& service_;
    core::IDisplayAdapter& display_;
    std::unique_ptr<View> view_;
};

} // namespace cardputer_hub::apps
