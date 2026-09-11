#pragma once

#include <array>
#include <optional>

#include "core/display/display_adapter.h"
#include "core/input/input_event.h"
#include "services/hosts/host_service.h"

namespace cardputer_hub::apps {

class HostSettings final : public core::IActionHandler {
  public:
    HostSettings(services::HostService& hosts, core::ActionBus& actions,
                 core::IDisplayAdapter& display);
    void update(const core::InputEvents& input);
    void activate();
    bool modal() const {
        return renaming_ || deleting_ || detailHost_.has_value() || hosts_.pairing();
    }
    core::ActionHandlingResult handle(const core::Action& action) override;

  private:
    void dispatch(const char* id, std::vector<core::ActionParameter> parameters = {});
    void render();
    void renderList();
    struct ListFrame {
        std::array<std::string, 4> labels;
        std::array<std::string, 4> values;
        std::array<std::string, 4> ordinals;
        std::size_t focusedRow = 0;
        std::string caption;
        std::string status;
        std::string error;
        std::string footer;
    };
    std::optional<ListFrame> listFrame_;
    void digits(const std::string& value);
    services::HostService& hosts_;
    core::ActionBus& actions_;
    core::IDisplayAdapter& display_;
    std::size_t focus_ = 0;
    bool renaming_ = false;
    bool deleting_ = false;
    std::optional<std::uint32_t> detailHost_;
    std::size_t detailFocus_ = 0;
    std::string entry_;
    std::uint32_t generation_ = 0;
    std::string previousFrame_;
    std::optional<int> previousViewDepth_;
};
} // namespace cardputer_hub::apps
