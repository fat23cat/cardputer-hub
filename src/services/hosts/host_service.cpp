#include "services/hosts/host_service.h"

#include <algorithm>
#include <limits>

namespace cardputer_hub::services {
using namespace connectivity;
namespace {
const BluetoothDeviceConfig deviceConfig{"Cardputer Hub"};

auto findHost(const HostConfiguration& config, std::uint32_t id) {
    return std::find_if(config.hosts.begin(), config.hosts.end(),
                        [id](const auto& host) { return host.id == id; });
}

template <typename T> const T* parameter(const core::Action& action, const char* name) {
    const auto* value = action.findParameter(name);
    return value ? std::get_if<T>(value) : nullptr;
}
} // namespace

HostResult HostService::fail(HostResult result, const char* reason) {
    if (logger_ && reason)
        logger_->error("hosts", reason);
    pairing_ = false;
    challenge_.reset();
    challengePeer_.reset();
    if (bluetooth_.disable() == BluetoothDisableResult::AdapterError)
        result = HostResult::BluetoothError;
    return lastResult_ = result;
}

HostResult HostService::initializeIdle() {
    if (bluetooth_.disable() == BluetoothDisableResult::AdapterError ||
        bluetooth_.enable(deviceConfig, BluetoothStartup::Idle) ==
            BluetoothEnableResult::AdapterError) {
        return fail(HostResult::BluetoothError);
    }
    return HostResult::Success;
}

HostResult HostService::save(const HostConfiguration& value) {
    const auto result = configuration_.save(value);
    if (result == ConfigurationResult::InvalidData)
        return lastResult_ = HostResult::InvalidInput;
    if (result == ConfigurationResult::StorageError)
        return fail(HostResult::StorageError);
    return lastResult_ = HostResult::Success;
}

HostResult HostService::reconcile(HostConfiguration& value) {
    const auto registry = bluetooth_.bonds();
    if (registry.status != BluetoothBondListStatus::Success)
        return fail(HostResult::BluetoothError, "bond registry unavailable during import");
    for (const auto& bond : registry.bonds) {
        if (std::any_of(value.hosts.begin(), value.hosts.end(),
                        [&](const auto& host) { return host.bond == bond; }))
            continue;
        if (value.hosts.size() >= BluetoothService::maximumBondCount ||
            value.nextHostId >=
                static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())) {
            return fail(HostResult::CapacityReached);
        }
        const auto id = value.nextHostId++;
        value.hosts.push_back({id, "Host " + std::to_string(id), bond});
    }
    return HostResult::Success;
}

HostResult HostService::start() {
    if (ready_)
        return lastResult_;
    if (ensureReady() != HostResult::Success)
        return lastResult_;
    return apply();
}

HostResult HostService::ensureReady() {
    if (ready_)
        return HostResult::Success;
    const auto loaded = configuration_.load();
    if (loaded != ConfigurationResult::Success)
        return fail(HostResult::StorageError);
    if (initializeIdle() != HostResult::Success)
        return lastResult_;
    auto value = settings();
    if (reconcile(value) != HostResult::Success)
        return lastResult_;
    if (value.hosts.size() != settings().hosts.size() && save(value) != HostResult::Success)
        return lastResult_;
    // Recovery prepares profiles only; the requested action decides whether to advertise.
    if (bluetooth_.disable() == BluetoothDisableResult::AdapterError)
        return fail(HostResult::BluetoothError);
    ready_ = true;
    return HostResult::Success;
}

HostResult HostService::apply() {
    pairing_ = false;
    challenge_.reset();
    challengePeer_.reset();
    if (!settings().bluetoothEnabled) {
        return bluetooth_.disable() == BluetoothDisableResult::AdapterError
                   ? fail(HostResult::BluetoothError)
                   : lastResult_ = HostResult::Success;
    }
    if (!settings().activeHost)
        return fail(HostResult::InvalidInput);
    const auto host = findHost(settings(), *settings().activeHost);
    if (host == settings().hosts.end())
        return fail(HostResult::InvalidInput);
    if (initializeIdle() != HostResult::Success)
        return lastResult_;
    const auto registry = bluetooth_.bonds();
    if (registry.status != BluetoothBondListStatus::Success)
        return fail(HostResult::BluetoothError, "bond registry unavailable during host activation");
    if (std::find(registry.bonds.begin(), registry.bonds.end(), host->bond) ==
        registry.bonds.end()) {
        return fail(HostResult::MissingBond, "selected host bond missing");
    }
    const auto selected = bluetooth_.selectBond(host->bond);
    if (selected != BluetoothBondSelectionResult::Selected &&
        selected != BluetoothBondSelectionResult::AlreadySelected) {
        return fail(HostResult::BluetoothError, "bond selection failed");
    }
    if (bluetooth_.advertise() == BluetoothAdvertisingResult::AdapterError)
        return fail(HostResult::BluetoothError, "advertising request failed");
    return lastResult_ = HostResult::Success;
}

HostResult HostService::selectHost(std::uint32_t id) {
    if (ensureReady() != HostResult::Success)
        return lastResult_;
    if (findHost(settings(), id) == settings().hosts.end())
        return lastResult_ = HostResult::InvalidInput;
    // Quiesce output before persisting intent or exposing another target.
    if (bluetooth_.disable() == BluetoothDisableResult::AdapterError)
        return fail(HostResult::BluetoothError);
    auto value = settings();
    value.activeHost = id;
    value.bluetoothEnabled = true;
    if (save(value) != HostResult::Success)
        return lastResult_;
    return apply();
}

HostResult HostService::setEnabled(bool enabled) {
    if (ensureReady() != HostResult::Success)
        return lastResult_;
    if (enabled && !settings().activeHost)
        return lastResult_ = HostResult::HostSelectionRequired;
    if (bluetooth_.disable() == BluetoothDisableResult::AdapterError)
        return fail(HostResult::BluetoothError);
    auto value = settings();
    value.bluetoothEnabled = enabled;
    if (save(value) != HostResult::Success)
        return lastResult_;
    return apply();
}

HostResult HostService::renameHost(std::uint32_t id, const std::string& name) {
    if (ensureReady() != HostResult::Success)
        return lastResult_;
    auto value = settings();
    const auto host = std::find_if(value.hosts.begin(), value.hosts.end(),
                                   [id](const auto& h) { return h.id == id; });
    if (host == value.hosts.end())
        return lastResult_ = HostResult::InvalidInput;
    host->name = name;
    return save(value);
}

HostResult HostService::startPairing() {
    if (ensureReady() != HostResult::Success)
        return lastResult_;
    if (pairing_)
        return HostResult::Success;
    if (settings().hosts.size() >= BluetoothService::maximumBondCount)
        return lastResult_ = HostResult::CapacityReached;
    if (initializeIdle() != HostResult::Success)
        return lastResult_;
    if (bluetooth_.selectBond(std::nullopt) == BluetoothBondSelectionResult::AdapterError)
        return fail(HostResult::BluetoothError);
    const auto opened = bluetooth_.openPairing();
    if (opened != BluetoothPairingOpenResult::Opened) {
        return fail(opened == BluetoothPairingOpenResult::CapacityReached
                        ? HostResult::CapacityReached
                        : HostResult::BluetoothError);
    }
    pairing_ = true;
    if (bluetooth_.advertise() == BluetoothAdvertisingResult::AdapterError)
        return fail(HostResult::BluetoothError, "advertising request failed");
    return lastResult_ = HostResult::Success;
}

HostResult HostService::cancelPairing() {
    if (ensureReady() != HostResult::Success)
        return lastResult_;
    return apply(); // Shutdown is the barrier; the saved target is unchanged.
}

HostResult HostService::deleteHost(std::uint32_t id) {
    if (ensureReady() != HostResult::Success)
        return lastResult_;
    if (pairing_ || findHost(settings(), id) == settings().hosts.end())
        return lastResult_ = HostResult::InvalidInput;
    auto value = settings();
    const auto reference = findHost(value, id)->bond;
    const bool selected = value.activeHost == id;
    const bool needsIdle = selected || bluetooth_.state() == BluetoothState::Disabled ||
                           bluetooth_.state() == BluetoothState::Error;
    if (selected) {
        if (bluetooth_.disable() == BluetoothDisableResult::AdapterError)
            return fail(HostResult::BluetoothError);
        value.bluetoothEnabled = false;
    }
    // Verify persistence before deleting keys; selected-host deletion persists Off.
    if (save(value) != HostResult::Success)
        return lastResult_;
    if (needsIdle && initializeIdle() != HostResult::Success)
        return lastResult_;
    auto removed = bluetooth_.removeBond(reference);
    if (removed == BluetoothBondRemovalResult::Pending) {
        bluetooth_.update(std::chrono::milliseconds::zero());
        removed = bluetooth_.lastBondRemovalResult();
    }
    if (removed != BluetoothBondRemovalResult::Removed &&
        removed != BluetoothBondRemovalResult::NotFound)
        return fail(HostResult::BluetoothError, "deleting host bond failed");
    value.hosts.erase(std::remove_if(value.hosts.begin(), value.hosts.end(),
                                     [id](const auto& host) { return host.id == id; }),
                      value.hosts.end());
    if (selected)
        value.activeHost.reset();
    if (save(value) != HostResult::Success)
        return lastResult_;
    return needsIdle ? apply() : lastResult_ = HostResult::Success;
}

void HostService::update(std::chrono::milliseconds elapsed) {
    if (!ready_)
        return;
    bluetooth_.update(elapsed);
    if (bluetooth_.state() == BluetoothState::Error) {
        fail(HostResult::BluetoothError);
        return;
    }
    if (!pairing_)
        return;
    if (challengePeer_ != bluetooth_.pairingPeer()) {
        challenge_.reset();
        challengePeer_.reset();
    }
    if (const auto challenge = bluetooth_.pairingChallenge()) {
        challenge_ = challenge;
        challengePeer_ = bluetooth_.pairingPeer();
        if (challenge->type == BluetoothPairingChallengeType::DisplayPasskey) {
            const auto result =
                bluetooth_.respondToPairing({challenge->generation, challenge->type, true, {}});
            if (result != BluetoothPairingResponseResult::Accepted) {
                fail(HostResult::BluetoothError);
                return;
            }
        }
    }
    if (bluetooth_.pairingState() == BluetoothPairingState::Succeeded) {
        const auto completed = bluetooth_.completedPairing();
        if (!completed) {
            fail(HostResult::BluetoothError);
            return;
        }
        auto value = settings();
        if (reconcile(value) != HostResult::Success)
            return;
        const auto host = std::find_if(value.hosts.begin(), value.hosts.end(),
                                       [&](const auto& h) { return h.bond == *completed; });
        if (host == value.hosts.end()) {
            fail(HostResult::BluetoothError);
            return;
        }
        value.activeHost = host->id;
        value.bluetoothEnabled = true;
        if (save(value) != HostResult::Success)
            return;
        if (bluetooth_.selectBond(*completed) != BluetoothBondSelectionResult::Selected) {
            fail(HostResult::BluetoothError);
            return;
        }
        pairing_ = false;
        challenge_.reset();
        challengePeer_.reset();
    } else if (bluetooth_.pairingState() == BluetoothPairingState::Closed ||
               bluetooth_.pairingState() == BluetoothPairingState::Error) {
        (void)apply();
    }
}

core::ActionHandlingResult HostService::handle(const core::Action& action) {
    HostResult result = HostResult::InvalidInput;
    if (action.id == "host.select") {
        if (const auto* id = parameter<std::int32_t>(action, "id"); id && *id > 0)
            result = selectHost(static_cast<std::uint32_t>(*id));
    } else if (action.id == "host.bluetooth") {
        if (const auto* enabled = parameter<bool>(action, "enabled"))
            result = setEnabled(*enabled);
    } else if (action.id == "host.rename") {
        const auto* id = parameter<std::int32_t>(action, "id");
        const auto* name = parameter<std::string>(action, "name");
        if (id && *id > 0 && name)
            result = renameHost(static_cast<std::uint32_t>(*id), *name);
    } else if (action.id == "host.pair")
        result = startPairing();
    else if (action.id == "host.cancel-pairing")
        result = cancelPairing();
    else if (action.id == "host.delete") {
        if (const auto* id = parameter<std::int32_t>(action, "id"); id && *id > 0)
            result = deleteHost(static_cast<std::uint32_t>(*id));
    } else if (action.id == "host.pair-response" && pairing_ && challenge_) {
        const auto* generation = parameter<std::int32_t>(action, "generation");
        const auto* accepted = parameter<bool>(action, "accepted");
        const auto* passkey = parameter<std::string>(action, "passkey");
        if (generation && accepted &&
            static_cast<std::uint32_t>(*generation) == challenge_->generation) {
            const auto response = bluetooth_.respondToPairing(
                {challenge_->generation, challenge_->type, *accepted, passkey ? *passkey : ""});
            if (response == BluetoothPairingResponseResult::Accepted ||
                response == BluetoothPairingResponseResult::Rejected) {
                challenge_.reset();
                challengePeer_.reset();
                result = HostResult::Success;
            }
        }
    }
    lastResult_ = result;
    return result == HostResult::Success ? core::ActionHandlingResult::Handled
                                         : core::ActionHandlingResult::Rejected;
}
} // namespace cardputer_hub::services
