#include "apps/network/wifi_settings.h"

#include "core/display/contextual_footer.h"
#include "core/display/palette.h"
#include "core/display/text_layout.h"

#include <algorithm>
#include <string>

namespace cardputer_hub::apps {
using namespace core;

namespace {
constexpr std::size_t editorColumns = 39;
}

bool WiFiSettings::TextDraft::append(char c) {
    if (c < 32 || c > 126 || value.size() >= maxLength)
        return false;
    value.push_back(c);
    return true;
}

void WiFiSettings::TextDraft::erase() {
    if (!value.empty())
        value.pop_back();
}

void WiFiSettings::TextDraft::clear() { value.clear(); }

std::string WiFiSettings::TextDraft::masked() const { return std::string(value.size(), '*'); }

WiFiSettings::WiFiSettings(services::NetworkService& network, ActionBus& actions,
                           IDisplayAdapter& display)
    : network_(network), actions_(actions), display_(display) {}

void WiFiSettings::dispatch(const char* id, std::vector<ActionParameter> parameters) {
    (void)actions_.dispatch({id, "wifi-settings", std::move(parameters)});
}

void WiFiSettings::resetFocus(bool configured) {
    focusedKind_ = configured ? RowKind::Toggle : RowKind::Configure;
}

void WiFiSettings::activate() {
    view_ = View::Status;
    resetFocus(network_.status().configured);
    ssidDraft_.clear();
    passphraseDraft_.clear();
    revealRemaining_ = {};
    frame_.reset();
}

bool WiFiSettings::modal() const { return view_ != View::Status; }

std::string WiFiSettings::fitSsid(const std::string& ssid) const {
    constexpr std::size_t maximum = 16;
    if (ssid.size() <= maximum)
        return ssid;
    return ssid.substr(0, maximum - 3) + "...";
}

std::string WiFiSettings::visibleEditor(const std::string& text) const {
    auto shown = text;
    shown.push_back('_');
    if (shown.size() <= editorColumns)
        return shown;
    return shown.substr(shown.size() - editorColumns);
}

std::string WiFiSettings::passphrasePreview() const {
    auto shown = passphraseDraft_.masked();
    if (revealRemaining_ > std::chrono::milliseconds{0} && !passphraseDraft_.value.empty())
        shown.back() = passphraseDraft_.value.back();
    return shown;
}

const char* WiFiSettings::headerStatus(const services::WifiStatusSnapshot& status) const {
    if (!status.configured)
        return "NOT CONFIGURED";
    if (!status.enabled)
        return "OFF";
    switch (status.connection) {
    case services::WifiConnectionStatus::Connecting:
        return "CONNECTING";
    case services::WifiConnectionStatus::Connected:
        return "CONNECTED";
    case services::WifiConnectionStatus::Error:
        return "ERROR";
    case services::WifiConnectionStatus::Off:
        return "OFF";
    }
    return "OFF";
}

const char* WiFiSettings::errorText(services::NetworkResult result) const {
    switch (result) {
    case services::NetworkResult::Success:
        return "";
    case services::NetworkResult::InvalidInput:
        return "Check network settings";
    case services::NetworkResult::NotConfigured:
        return "Configure a network first";
    case services::NetworkResult::StorageError:
        return "Settings could not be saved";
    case services::NetworkResult::ConnectivityError:
        return "Wi-Fi connection failed";
    }
    return "";
}

std::vector<WiFiSettings::Row>
WiFiSettings::rows(const services::WifiStatusSnapshot& status) const {
    std::vector<Row> next;
    if (!status.configured) {
        next.push_back({RowKind::Configure, "Configure network", {}});
        return next;
    }
    next.push_back({RowKind::Toggle, "Wi-Fi", status.enabled ? "ON" : "OFF"});
    next.push_back({RowKind::Network, "Network", fitSsid(status.ssid)});
    if (status.connection == services::WifiConnectionStatus::Connected) {
        std::string rssi = "--";
        if (status.signalStrengthDbm) {
            rssi = std::to_string(*status.signalStrengthDbm) + " dBm";
        }
        next.push_back({RowKind::Signal, "Signal", std::move(rssi)});
    }
    next.push_back({RowKind::Change, "Change network", {}});
    next.push_back({RowKind::Forget, "Forget network", {}});
    return next;
}

std::size_t WiFiSettings::resolveFocus(const std::vector<Row>& visible) {
    for (std::size_t index = 0; index < visible.size(); ++index) {
        if (visible[index].kind == focusedKind_)
            return index;
    }
    if (!visible.empty())
        focusedKind_ = visible.front().kind;
    return 0;
}

void WiFiSettings::cancelEditor() {
    ssidDraft_.clear();
    passphraseDraft_.clear();
    revealRemaining_ = {};
    view_ = View::Status;
    resetFocus(network_.status().configured);
}

void WiFiSettings::submitConfiguration() {
    dispatch("network.configure",
             {{"ssid", ssidDraft_.value}, {"passphrase", passphraseDraft_.value}});
    const auto result = network_.status().lastResult;
    if (result == services::NetworkResult::Success ||
        result == services::NetworkResult::ConnectivityError)
        cancelEditor();
}

void WiFiSettings::handleEditor(const InputEvent& event) {
    if (event.modifiers.ctrl || event.modifiers.alt || event.modifiers.option)
        return;
    if (isPlainEscape(event)) {
        cancelEditor();
        return;
    }
    if (event.type == InputEventType::NamedKey) {
        switch (event.namedKey) {
        case NamedKey::Backspace:
            if (view_ == View::Ssid)
                ssidDraft_.erase();
            else if (view_ == View::Passphrase) {
                passphraseDraft_.erase();
                revealRemaining_ = {};
            }
            break;
        case NamedKey::Enter:
            if (view_ == View::Ssid) {
                if (!ssidDraft_.value.empty())
                    view_ = View::Passphrase;
            } else if (view_ == View::Passphrase)
                submitConfiguration();
            else if (view_ == View::Forget) {
                dispatch("network.forget");
                view_ = View::Status;
                resetFocus(false);
            }
            break;
        default:
            break;
        }
        return;
    }
    if (view_ == View::Forget)
        return;
    if (view_ == View::Ssid)
        ssidDraft_.append(event.character);
    else if (view_ == View::Passphrase && passphraseDraft_.append(event.character))
        revealRemaining_ = passphraseRevealDuration;
}

void WiFiSettings::handleStatus(const InputEvent& event,
                                const services::WifiStatusSnapshot& status) {
    const bool plain = !event.modifiers.ctrl && !event.modifiers.alt && !event.modifiers.option;
    if (!plain)
        return;
    const auto visible = rows(status);
    if (visible.empty())
        return;
    auto focus = resolveFocus(visible);
    const bool up = (event.type == InputEventType::NamedKey && event.namedKey == NamedKey::Up) ||
                    (event.type == InputEventType::PrintableCharacter && event.character == ';' &&
                     !event.modifiers.shift);
    const bool down =
        (event.type == InputEventType::NamedKey && event.namedKey == NamedKey::Down) ||
        (event.type == InputEventType::PrintableCharacter && event.character == '.' &&
         !event.modifiers.shift);
    const bool escape = isPlainEscape(event);
    if (up && focus > 0)
        focusedKind_ = visible[focus - 1].kind;
    else if (down && focus + 1 < visible.size())
        focusedKind_ = visible[focus + 1].kind;
    else if (escape)
        dispatch("ui.back");
    else if (event.type == InputEventType::NamedKey && event.namedKey == NamedKey::Enter) {
        switch (visible[resolveFocus(visible)].kind) {
        case RowKind::Toggle:
            dispatch("network.set-enabled", {{"enabled", !status.enabled}});
            break;
        case RowKind::Configure:
        case RowKind::Change:
            ssidDraft_.clear();
            passphraseDraft_.clear();
            revealRemaining_ = {};
            view_ = View::Ssid;
            break;
        case RowKind::Forget:
            view_ = View::Forget;
            break;
        case RowKind::Network:
        case RowKind::Signal:
            break;
        }
    }
}

void WiFiSettings::update(const InputEvents& input, std::chrono::milliseconds elapsed) {
    for (const auto& event : input) {
        if (event.modifiers.ctrl || event.modifiers.alt || event.modifiers.option)
            continue;
        if (view_ == View::Status)
            handleStatus(event, network_.status());
        else
            handleEditor(event);
    }
    if (view_ != View::Passphrase)
        revealRemaining_ = {};
    else if (revealRemaining_ > std::chrono::milliseconds{0}) {
        const auto step = std::max(elapsed, std::chrono::milliseconds{0});
        if (step >= revealRemaining_)
            revealRemaining_ = {};
        else
            revealRemaining_ -= step;
    }
    render();
}

void WiFiSettings::renderStatus(const services::WifiStatusSnapshot& status, bool full) {
    const auto visible = rows(status);
    const auto focus = static_cast<std::uint8_t>(resolveFocus(visible));
    const TextStyle normal{palette::ink, palette::bone, 1};
    const TextStyle selected{palette::bone, palette::ink, 1};
    const TextStyle quiet{palette::ordinal, palette::bone, 1};
    if (full) {
        display_.clear(palette::bone);
        display_.drawText({6, 6}, "WIFI", normal);
        const auto* label = headerStatus(status);
        display_.drawText({rightAlignedTextX(label), 6}, label, normal);
        display_.fillRectangle({6, 20}, 228, 1, palette::ink);
    } else if (frame_ &&
               (frame_->configured != status.configured || frame_->enabled != status.enabled ||
                frame_->connection != status.connection)) {
        display_.fillRectangle({90, 6}, 144, 8, palette::bone);
        const auto* label = headerStatus(status);
        display_.drawText({rightAlignedTextX(label), 6}, label, normal);
    }
    for (std::uint8_t index = 0; index < 5; ++index) {
        const auto y = 24 + static_cast<std::int32_t>(index) * 18;
        const bool present = index < visible.size();
        const bool focusChanged = frame_ && ((frame_->focus == index) != (focus == index));
        const bool contentChanged = !frame_ || index >= visible.size()
                                        ? present != (frame_ && index < 5 && !frame_->ssid.empty())
                                        : false;
        bool rowChanged = full || !frame_;
        if (!rowChanged && present) {
            rowChanged =
                focusChanged || frame_->focus == index ||
                (visible[index].kind == RowKind::Toggle && frame_->enabled != status.enabled) ||
                (visible[index].kind == RowKind::Network && frame_->ssid != status.ssid) ||
                (visible[index].kind == RowKind::Signal &&
                 frame_->rssi != status.signalStrengthDbm) ||
                frame_->configured != status.configured || frame_->connection != status.connection;
        } else if (!rowChanged)
            rowChanged = focusChanged || contentChanged ||
                         frame_->configured != status.configured ||
                         frame_->connection != status.connection;
        if (!rowChanged)
            continue;
        display_.fillRectangle({6, y}, 228, 16,
                               present && focus == index ? palette::ink : palette::bone);
        if (!present)
            continue;
        char ordinal[3]{'0', static_cast<char>('1' + index), '\0'};
        const auto style = focus == index ? selected : normal;
        display_.drawText({10, y + 3}, ordinal, focus == index ? style : quiet);
        display_.drawText({30, y + 3}, visible[index].label, style);
        if (!visible[index].value.empty())
            display_.drawText(
                {230 - static_cast<std::int32_t>(visible[index].value.size()) * 6, y + 3},
                visible[index].value.c_str(), style);
    }
    const auto* error = errorText(status.lastResult);
    if (full || !frame_ || frame_->lastResult != status.lastResult) {
        if (!full)
            display_.fillRectangle({6, 118}, 228, 8, palette::bone);
        if (*error != '\0')
            display_.drawText({6, 118}, error, {palette::vermilion, palette::bone, 1});
    }
}

void WiFiSettings::renderEditor(bool full) {
    const TextStyle normal{palette::ink, palette::bone, 1};
    if (!full && frame_ && frame_->view == view_ && frame_->ssidDraft == ssidDraft_.value &&
        frame_->passphraseLength == passphraseDraft_.value.size() &&
        frame_->passphraseRevealed == (revealRemaining_ > std::chrono::milliseconds{0}) &&
        frame_->lastResult == network_.status().lastResult)
        return;
    display_.clear(palette::bone);
    const char* title = view_ == View::Forget ? "FORGET NETWORK?"
                        : view_ == View::Ssid ? "NETWORK NAME"
                                              : "PASSWORD";
    display_.drawText({6, 6}, title, normal);
    display_.fillRectangle({6, 20}, 228, 1, palette::ink);
    if (view_ == View::Forget) {
        display_.drawText({6, 40}, fitSsid(network_.status().ssid).c_str(), normal);
    } else {
        const auto shown =
            visibleEditor(view_ == View::Ssid ? ssidDraft_.value : passphrasePreview());
        display_.drawText({6, 40}, shown.c_str(), normal);
        const auto* error = errorText(network_.status().lastResult);
        if (*error != '\0')
            display_.drawText({6, 80}, error, {palette::vermilion, palette::bone, 1});
    }
    const char* confirm = view_ == View::Forget ? "ENTER FORGET"
                          : view_ == View::Ssid ? (ssidDraft_.value.empty() ? "" : "ENTER NEXT")
                                                : "ENTER CONNECT";
    drawContextualFooter(display_, "ESC CANCEL", confirm);
}

void WiFiSettings::render() {
    const auto status = network_.status();
    const auto visible = rows(status);
    StatusFrame next;
    next.view = view_;
    next.focus = static_cast<std::uint8_t>(resolveFocus(visible));
    next.focusedKind = focusedKind_;
    next.configured = status.configured;
    next.enabled = status.enabled;
    next.connection = status.connection;
    next.ssid = status.ssid;
    next.rssi = status.signalStrengthDbm;
    next.lastResult = status.lastResult;
    next.ssidDraft = ssidDraft_.value;
    next.passphraseLength = passphraseDraft_.value.size();
    next.passphraseRevealed = revealRemaining_ > std::chrono::milliseconds{0};
    if (frame_ && frame_->view == next.view && frame_->focus == next.focus &&
        frame_->focusedKind == next.focusedKind && frame_->configured == next.configured &&
        frame_->enabled == next.enabled && frame_->connection == next.connection &&
        frame_->ssid == next.ssid && frame_->rssi == next.rssi &&
        frame_->lastResult == next.lastResult && frame_->ssidDraft == next.ssidDraft &&
        frame_->passphraseLength == next.passphraseLength &&
        frame_->passphraseRevealed == next.passphraseRevealed)
        return;
    const bool full = !frame_ || frame_->view != view_ || frame_->configured != next.configured ||
                      frame_->enabled != next.enabled || frame_->connection != next.connection;
    if (view_ == View::Status)
        renderStatus(status, full);
    else
        renderEditor(full);
    frame_ = std::move(next);
}

} // namespace cardputer_hub::apps
