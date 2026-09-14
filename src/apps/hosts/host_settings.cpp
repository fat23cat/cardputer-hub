#include "apps/hosts/host_settings.h"

#include "apps/hosts/assets/micro5_digits.h"
#include "core/display/palette.h"
#include <algorithm>
#include <cstdio>
#include <string_view>

namespace cardputer_hub::apps {
using namespace core;
namespace {
const char* label(services::HostConnectionStatus value) {
    switch (value) {
    case services::HostConnectionStatus::Off:
        return "OFF";
    case services::HostConnectionStatus::Connecting:
        return "CONNECTING";
    case services::HostConnectionStatus::Securing:
        return "SECURING";
    case services::HostConnectionStatus::Ready:
        return "READY";
    case services::HostConnectionStatus::Pairing:
        return "PAIRING";
    case services::HostConnectionStatus::Error:
        return "ERROR";
    }
    return "ERROR";
}
const char* error(services::HostResult result) {
    switch (result) {
    case services::HostResult::Success:
        return "";
    case services::HostResult::InvalidInput:
        return "Check input";
    case services::HostResult::HostSelectionRequired:
        return "Select host, then Enter";
    case services::HostResult::StorageError:
        return "Settings error - BLE stopped";
    case services::HostResult::BluetoothError:
        return "Bluetooth error - retry";
    case services::HostResult::MissingBond:
        return "Pair missing - select another";
    case services::HostResult::CapacityReached:
        return "Host list is full";
    }
    return "Error";
}
std::string code(std::uint32_t value) {
    char buffer[7]{};
    std::snprintf(buffer, sizeof(buffer), "%06lu", static_cast<unsigned long>(value % 1000000));
    return buffer;
}
} // namespace

HostSettings::HostSettings(services::HostService& hosts, ActionBus& actions,
                           IDisplayAdapter& display)
    : hosts_(hosts), actions_(actions), display_(display) {
    for (const auto* id : {"hosts.up", "hosts.down", "hosts.confirm", "hosts.back", "hosts.rename",
                           "hosts.text", "hosts.erase"}) {
        (void)actions_.registerHandler(id, *this);
    }
}

void HostSettings::dispatch(const char* id, std::vector<ActionParameter> parameters) {
    (void)actions_.dispatch({id, "host-settings", std::move(parameters)});
}

void HostSettings::activate() {
    listFrame_.reset();
    listRenderState_.reset();
    modalRenderState_.reset();
}

bool HostSettings::modal() const {
    return renaming_ || deleting_ || detailHost_.has_value() || hosts_.status().pairing;
}

void HostSettings::update(const InputEvents& input) {
    const auto hostStatus = hosts_.status();
    const auto challenge = hostStatus.pairingPrompt;
    const auto generation = challenge ? challenge->generation : 0;
    if (generation != generation_) {
        generation_ = generation;
        entry_.clear();
    }
    for (const auto& event : input) {
        const auto currentStatus = hosts_.status();
        if (event.modifiers.ctrl || event.modifiers.alt || event.modifiers.option)
            continue;
        if (event.type == InputEventType::NamedKey) {
            switch (event.namedKey) {
            case NamedKey::Up:
                dispatch("hosts.up");
                break;
            case NamedKey::Down:
                dispatch("hosts.down");
                break;
            case NamedKey::Enter:
                dispatch("hosts.confirm");
                break;
            case NamedKey::Escape:
                dispatch("hosts.back");
                break;
            case NamedKey::Backspace:
                dispatch("hosts.erase");
                break;
            default:
                break;
            }
        } else if (!renaming_ && !event.modifiers.shift && event.character == '`') {
            dispatch("hosts.back");
        } else if (!renaming_ && !currentStatus.pairing && !event.modifiers.shift &&
                   (event.character == ';' || event.character == '.')) {
            dispatch(event.character == ';' ? "hosts.up" : "hosts.down");
        } else if (!renaming_ && !currentStatus.pairing &&
                   (event.character == 'r' || event.character == 'R')) {
            dispatch("hosts.rename");
        } else
            dispatch("hosts.text", {{"text", std::string(1, event.character)}});
    }
    render();
}

ActionHandlingResult HostSettings::handle(const Action& action) {
    const auto hostStatus = hosts_.status();
    const auto count = hosts_.settings().hosts.size() + 2;
    if (deleting_) {
        if (action.id == "hosts.back")
            deleting_ = false;
        else if (action.id == "hosts.confirm") {
            dispatch("host.delete", {{"id", static_cast<std::int32_t>(*detailHost_)}});
            if (hosts_.status().lastResult == services::HostResult::Success) {
                deleting_ = false;
                detailHost_.reset();
                focus_ = std::min(focus_, hosts_.settings().hosts.size() + 1);
                activate();
            }
        }
        return ActionHandlingResult::Handled;
    }
    if (action.id == "hosts.back") {
        if (renaming_) {
            renaming_ = false;
            entry_.clear();
        } else if (hostStatus.pairing)
            dispatch("host.cancel-pairing");
        else if (detailHost_) {
            detailHost_.reset();
            activate();
        } else
            dispatch("ui.back");
    } else if (action.id == "hosts.text") {
        const auto* value = action.findParameter("text");
        const auto* text = value ? std::get_if<std::string>(value) : nullptr;
        if (!text || text->size() != 1)
            return ActionHandlingResult::Rejected;
        const auto c = text->front();
        const auto challenge = hostStatus.pairingPrompt;
        if (renaming_ && entry_.size() < services::ConfigurationService::maximumNameLength &&
            c >= 32 && c <= 126)
            entry_ += c;
        else if (challenge && challenge->type == services::HostPairingPromptType::EnterPasskey &&
                 entry_.size() < 6 && c >= '0' && c <= '9')
            entry_ += c;
    } else if (action.id == "hosts.erase") {
        if (!entry_.empty())
            entry_.pop_back();
    } else if (action.id == "hosts.confirm") {
        if (renaming_) {
            if (focus_ >= 2 && focus_ < count) {
                dispatch("host.rename",
                         {{"id", static_cast<std::int32_t>(hosts_.settings().hosts[focus_ - 2].id)},
                          {"name", entry_}});
                if (hosts_.status().lastResult == services::HostResult::Success) {
                    renaming_ = false;
                    entry_.clear();
                }
            }
        } else if (hostStatus.pairing) {
            const auto challenge = hostStatus.pairingPrompt;
            if (challenge && challenge->type != services::HostPairingPromptType::DisplayPasskey &&
                (challenge->type != services::HostPairingPromptType::EnterPasskey ||
                 entry_.size() == 6)) {
                dispatch("host.pair-response",
                         {{"generation", static_cast<std::int32_t>(challenge->generation)},
                          {"accepted", true},
                          {"passkey",
                           challenge->type == services::HostPairingPromptType::EnterPasskey ? entry_
                                                                                            : ""}});
            }
        } else if (detailHost_) {
            if (detailFocus_ == 0) {
                dispatch("host.select", {{"id", static_cast<std::int32_t>(*detailHost_)}});
                if (hosts_.status().lastResult == services::HostResult::Success) {
                    detailHost_.reset();
                    activate();
                }
            } else if (detailFocus_ == 1) {
                dispatch("hosts.rename");
            } else {
                deleting_ = true;
            }
        } else if (focus_ == 0) {
            dispatch("host.bluetooth", {{"enabled", !hostStatus.connectionEnabled}});
            if (hosts_.status().lastResult == services::HostResult::HostSelectionRequired)
                focus_ = hosts_.settings().hosts.empty() ? 1 : 2;
        } else if (focus_ == 1)
            dispatch("host.pair");
        else if (focus_ < count) {
            detailHost_ = hosts_.settings().hosts[focus_ - 2].id;
            detailFocus_ = 0;
            activate();
        }
    } else if (!renaming_ && !hostStatus.pairing) {
        auto& cursor = detailHost_ ? detailFocus_ : focus_;
        const auto limit = detailHost_ ? 3 : count;
        if (action.id == "hosts.up" && cursor > 0)
            --cursor;
        else if (action.id == "hosts.down" && cursor + 1 < limit)
            ++cursor;
        else if (action.id == "hosts.rename" && focus_ >= 2 && focus_ < count) {
            renaming_ = true;
            entry_ = hosts_.settings().hosts[focus_ - 2].name;
        }
    }
    return ActionHandlingResult::Handled;
}

void HostSettings::digits(const std::string& value) {
    const auto width = static_cast<int>(value.size()) * 24;
    int left = (240 - width) / 2;
    for (const auto c : value) {
        if (c >= '0' && c <= '9') {
            constexpr std::string_view sheetCharacters = "1234560789";
            const auto& glyph = micro5_digits::kGlyphs[0][sheetCharacters.find(c)];
            for (int y = 0; y < micro5_digits::kHeight; ++y) {
                for (int x = 0; x < micro5_digits::kWidth; ++x) {
                    if (glyph[y * micro5_digits::kStride + x / 8] & (0x80U >> (x % 8))) {
                        display_.fillRectangle({left + x, 52 + y}, 1, 1, palette::ink);
                    }
                }
            }
        }
        left += 24;
    }
}

void HostSettings::renderList() {
    const auto& settings = hosts_.settings();
    const auto hostStatus = hosts_.status();
    const auto start = focus_ >= 4 ? focus_ - 3 : 0;
    ListFrame next;
    next.focusedRow = focus_ - start;
    next.status = label(hostStatus.connection);
    next.error = error(hostStatus.lastResult);
    if (hostStatus.lastResult == services::HostResult::HostSelectionRequired &&
        settings.hosts.empty())
        next.error = "Add device, then Enter";
    for (std::size_t slot = 0; slot < next.labels.size(); ++slot) {
        const auto row = start + slot;
        if (row < settings.hosts.size() + 2) {
            next.ordinals[slot] = (row + 1 < 10 ? "0" : "") + std::to_string(row + 1);
        }
        if (row == 0) {
            next.labels[slot] = "Bluetooth";
            next.values[slot] = hostStatus.connectionEnabled ? "ON" : "OFF";
        } else if (row == 1) {
            next.labels[slot] = "Add device";
        } else if (row - 2 < settings.hosts.size()) {
            const auto& host = settings.hosts[row - 2];
            next.labels[slot] = host.name;
            if (settings.activeHost == host.id)
                next.values[slot] = "SELECTED";
        }
    }
    if (detailHost_) {
        next.labels = {"Connect", "Rename", "Delete", ""};
        next.values = {};
        next.ordinals = {"01", "02", "03", ""};
        next.focusedRow = detailFocus_;
        const auto host = std::find_if(settings.hosts.begin(), settings.hosts.end(),
                                       [&](const auto& item) { return item.id == *detailHost_; });
        if (host != settings.hosts.end())
            next.caption = host->name;
    }
    const bool full = !listFrame_;
    const TextStyle normal{palette::ink, palette::bone, 1};
    if (full) {
        display_.clear(palette::bone);
        display_.drawText({6, 6}, detailHost_ ? "HOST" : "BLUETOOTH", normal);
        display_.fillRectangle({6, 20}, 228, 1, palette::ink);
    }
    if (full || next.status != listFrame_->status) {
        if (!full)
            display_.fillRectangle({174, 6}, 60, 8, palette::bone);
        display_.drawText({174, 6}, next.status.c_str(), normal);
    }
    for (std::size_t slot = 0; slot < next.labels.size(); ++slot) {
        const bool focused = slot == next.focusedRow;
        if (!full && next.labels[slot] == listFrame_->labels[slot] &&
            next.values[slot] == listFrame_->values[slot] &&
            next.ordinals[slot] == listFrame_->ordinals[slot] &&
            focused == (slot == listFrame_->focusedRow))
            continue;
        const auto y = 27 + static_cast<int>(slot) * 18;
        // Erase the old row, including any longer previous label or selection.
        const auto background = focused ? palette::ink : palette::bone;
        display_.fillRectangle({6, y - 3}, 228, 16, background);
        if (!next.labels[slot].empty()) {
            const auto style = focused ? TextStyle{palette::bone, palette::ink, 1} : normal;
            display_.drawText({10, y}, next.ordinals[slot].c_str(),
                              focused ? style : TextStyle{palette::ordinal, palette::bone, 1});
            display_.drawText({30, y}, next.labels[slot].c_str(), style);
            if (!next.values[slot].empty()) {
                display_.drawText({234 - static_cast<int>(next.values[slot].size()) * 6, y},
                                  next.values[slot].c_str(), style);
            }
        }
    }
    if (full || next.caption != listFrame_->caption) {
        if (!full)
            display_.fillRectangle({6, 95}, 228, 8, palette::bone);
        display_.drawText({6, 95}, next.caption.c_str(), normal);
    }
    if (full || next.error != listFrame_->error) {
        if (!full)
            display_.fillRectangle({6, 105}, 228, 8, palette::bone);
        display_.drawText({6, 105}, next.error.c_str(), {palette::vermilion, palette::bone, 1});
    }
    listFrame_ = std::move(next);
}

void HostSettings::render() {
    const auto hostStatus = hosts_.status();
    const int depth = renaming_ || deleting_ ? 2 : hostStatus.pairing || detailHost_ ? 1 : 0;
    if (previousViewDepth_ && depth != *previousViewDepth_) {
        display_.beginTransition(depth < *previousViewDepth_ ? SlideDirection::Backward
                                                             : SlideDirection::Forward);
    }
    previousViewDepth_ = depth;
    const auto challenge = hostStatus.pairingPrompt;
    const auto& settings = hosts_.settings();
    if (!hostStatus.pairing && !renaming_ && !deleting_) {
        modalRenderState_.reset();
        const auto sameHosts = [&] {
            if (!listRenderState_ || listRenderState_->hosts.size() != settings.hosts.size())
                return false;
            for (std::size_t index = 0; index < settings.hosts.size(); ++index) {
                if (listRenderState_->hosts[index].id != settings.hosts[index].id ||
                    listRenderState_->hosts[index].name != settings.hosts[index].name)
                    return false;
            }
            return true;
        }();
        const bool unchanged = listRenderState_ && listFrame_ &&
                               listRenderState_->focus == focus_ &&
                               listRenderState_->detailHost == detailHost_ &&
                               listRenderState_->detailFocus == detailFocus_ &&
                               listRenderState_->status == hostStatus.connection &&
                               listRenderState_->result == hostStatus.lastResult &&
                               listRenderState_->activeHost == settings.activeHost && sameHosts;
        if (unchanged)
            return;
        HostListRenderState next;
        next.focus = focus_;
        next.detailHost = detailHost_;
        next.detailFocus = detailFocus_;
        next.status = hostStatus.connection;
        next.result = hostStatus.lastResult;
        next.activeHost = settings.activeHost;
        next.hosts.reserve(settings.hosts.size());
        for (const auto& host : settings.hosts)
            next.hosts.push_back({host.id, host.name});
        listRenderState_ = std::move(next);
        renderList();
        return;
    }
    listRenderState_.reset();
    const auto kind = deleting_            ? ModalKind::Delete
                      : hostStatus.pairing ? ModalKind::Pairing
                                           : ModalKind::Rename;
    std::string_view hostName;
    if (deleting_ && focus_ >= 2 && focus_ - 2 < settings.hosts.size())
        hostName = settings.hosts[focus_ - 2].name;
    const auto samePrompt = [&] {
        if (!modalRenderState_)
            return false;
        if (challenge.has_value() != modalRenderState_->pairingPrompt.has_value())
            return false;
        return !challenge ||
               (modalRenderState_->pairingPrompt->generation == challenge->generation &&
                modalRenderState_->pairingPrompt->type == challenge->type &&
                modalRenderState_->pairingPrompt->value == challenge->value);
    }();
    if (modalRenderState_ && modalRenderState_->kind == kind &&
        modalRenderState_->status == hostStatus.connection &&
        modalRenderState_->result == hostStatus.lastResult &&
        modalRenderState_->pairingPhase == hostStatus.pairingPhase && samePrompt &&
        modalRenderState_->entry == entry_ && modalRenderState_->hostName == hostName)
        return;
    ModalRenderState next;
    next.kind = kind;
    next.status = hostStatus.connection;
    next.result = hostStatus.lastResult;
    next.pairingPhase = hostStatus.pairingPhase;
    if (challenge)
        next.pairingPrompt =
            PairingPromptRenderState{challenge->generation, challenge->type, challenge->value};
    next.entry = entry_;
    next.hostName = hostName;
    modalRenderState_ = std::move(next);
    listFrame_.reset();
    display_.clear(palette::bone);
    const TextStyle normal{palette::ink, palette::bone, 1};
    display_.drawText({6, 6},
                      deleting_            ? "DELETE HOST"
                      : hostStatus.pairing ? "ADD DEVICE"
                      : renaming_          ? "HOST NAME"
                                           : "BLUETOOTH",
                      normal);
    display_.drawText({174, 6}, label(hostStatus.connection), normal);
    display_.fillRectangle({6, 20}, 228, 1, palette::ink);
    std::string footer;
    if (deleting_) {
        display_.drawText({6, 35}, "Delete this host and its pairing?", normal);
        display_.drawText({6, 53}, hosts_.settings().hosts[focus_ - 2].name.c_str(), normal);
    } else if (hostStatus.pairing) {
        if (!challenge && hostStatus.pairingPhase == services::HostPairingPhase::Securing) {
            display_.drawText({6, 35}, "Securing connection", normal);
            display_.drawText({6, 51}, "Please wait for the code or READY", normal);
        } else if (!challenge) {
            display_.drawText({6, 35}, "Choose Cardputer Hub", normal);
            display_.drawText({6, 51}, "in computer Bluetooth settings", normal);
            display_.drawText({6, 80}, "Pairing window: 2 minutes", normal);
        } else if (challenge->type == services::HostPairingPromptType::DisplayPasskey) {
            display_.drawText({6, 32}, "Enter this code on the computer", normal);
            digits(code(challenge->value.value_or(0)));
        } else if (challenge->type == services::HostPairingPromptType::ConfirmComparison) {
            display_.drawText({6, 32}, "Does the computer show this code?", normal);
            digits(code(challenge->value.value_or(0)));
            footer = "Enter Yes";
        } else {
            display_.drawText({6, 32}, "Type the code from the computer", normal);
            digits(entry_);
            if (entry_.size() == 6)
                footer = "Enter Apply";
        }
    } else if (renaming_) {
        display_.drawText({6, 35}, "Name (up to 24 characters)", normal);
        display_.drawText({6, 61}, entry_.c_str(), normal);
    }
    display_.drawText({6, 105}, error(hostStatus.lastResult),
                      {palette::vermilion, palette::bone, 1});
    if (!footer.empty()) {
        display_.fillRectangle({6, 118}, 228, 1, palette::ink);
        display_.drawText({6, 123}, footer.c_str(), normal);
    }
}
} // namespace cardputer_hub::apps
