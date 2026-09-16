#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/display/display_adapter.h"
#include "core/input/input_event.h"
#include "services/network/network_service.h"

namespace cardputer_hub::apps {

class WiFiSettings {
  public:
    static constexpr std::chrono::milliseconds passphraseRevealDuration{2000};

    WiFiSettings(services::NetworkService& network, core::ActionBus& actions,
                 core::IDisplayAdapter& display);
    void update(const core::InputEvents& input, std::chrono::milliseconds elapsed = {});
    void activate();
    bool modal() const;

  private:
    enum class View : std::uint8_t { Status, Ssid, Passphrase, Forget };
    enum class RowKind : std::uint8_t { Toggle, Network, Signal, Configure, Change, Forget };

    struct Row {
        RowKind kind = RowKind::Configure;
        const char* label = "";
        std::string value;
    };

    struct StatusFrame {
        View view = View::Status;
        std::uint8_t focus = 0;
        RowKind focusedKind = RowKind::Configure;
        bool configured = false;
        bool enabled = false;
        services::WifiConnectionStatus connection = services::WifiConnectionStatus::Off;
        std::string ssid;
        std::optional<std::int32_t> rssi;
        services::NetworkResult lastResult = services::NetworkResult::Success;
        std::string ssidDraft;
        std::size_t passphraseLength = 0;
        bool passphraseRevealed = false;
    };

    struct TextDraft {
        std::string value;
        std::size_t maxLength = 0;
        bool append(char c);
        void erase();
        void clear();
        std::string masked() const;
    };

    void dispatch(const char* id, std::vector<core::ActionParameter> parameters = {});
    std::vector<Row> rows(const services::WifiStatusSnapshot& status) const;
    std::size_t resolveFocus(const std::vector<Row>& visible);
    void resetFocus(bool configured);
    std::string fitSsid(const std::string& ssid) const;
    std::string visibleEditor(const std::string& text) const;
    std::string passphrasePreview() const;
    const char* headerStatus(const services::WifiStatusSnapshot& status) const;
    const char* errorText(services::NetworkResult result) const;
    void handleStatus(const core::InputEvent& event, const services::WifiStatusSnapshot& status);
    void handleEditor(const core::InputEvent& event);
    void submitConfiguration();
    void cancelEditor();
    void render();
    void renderStatus(const services::WifiStatusSnapshot& status, bool full);
    void renderEditor(bool full);

    services::NetworkService& network_;
    core::ActionBus& actions_;
    core::IDisplayAdapter& display_;
    View view_ = View::Status;
    RowKind focusedKind_ = RowKind::Configure;
    TextDraft ssidDraft_{{}, services::NetworkService::maximumSsidLength};
    TextDraft passphraseDraft_{{}, services::NetworkService::maximumPassphraseLength};
    std::chrono::milliseconds revealRemaining_{0};
    std::optional<StatusFrame> frame_;
};

} // namespace cardputer_hub::apps
