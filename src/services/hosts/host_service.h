#pragma once

#include "core/actions/action_bus.h"
#include "services/configuration/configuration_service.h"

namespace cardputer_hub::services {

enum class HostResult {
    Success,
    InvalidInput,
    HostSelectionRequired,
    StorageError,
    BluetoothError,
    MissingBond,
    CapacityReached
};

class HostService final : public core::IActionHandler {
  public:
    HostService(connectivity::BluetoothService& bluetooth, ConfigurationService& configuration,
                core::Logger* logger = nullptr)
        : bluetooth_(bluetooth), configuration_(configuration), logger_(logger) {}
    HostResult start();
    void update(std::chrono::milliseconds elapsed);
    HostResult selectHost(std::uint32_t id);
    HostResult setEnabled(bool enabled);
    HostResult renameHost(std::uint32_t id, const std::string& name);
    HostResult startPairing();
    HostResult cancelPairing();
    HostResult deleteHost(std::uint32_t id);
    core::ActionHandlingResult handle(const core::Action& action) override;
    const HostConfiguration& settings() const noexcept { return configuration_.value(); }
    HostResult lastResult() const noexcept { return lastResult_; }
    bool pairing() const noexcept { return pairing_; }
    connectivity::BluetoothPairingState pairingState() const { return bluetooth_.pairingState(); }
    std::optional<connectivity::BluetoothPairingChallenge> pairingChallenge() const {
        return challenge_;
    }
    connectivity::BluetoothState bluetoothState() const { return bluetooth_.state(); }
    connectivity::HidTransportState hidState() const { return bluetooth_.hidTransport().state(); }

  private:
    HostResult fail(HostResult result, const char* reason = nullptr);
    HostResult ensureReady();
    HostResult apply();
    HostResult initializeIdle();
    HostResult reconcile(HostConfiguration& value);
    HostResult save(const HostConfiguration& value);
    connectivity::BluetoothService& bluetooth_;
    ConfigurationService& configuration_;
    core::Logger* logger_;
    HostResult lastResult_ = HostResult::Success;
    bool ready_ = false;
    bool pairing_ = false;
    std::optional<connectivity::BluetoothPairingChallenge> challenge_;
    std::optional<connectivity::BluetoothPeerHandle> challengePeer_;
};
} // namespace cardputer_hub::services
