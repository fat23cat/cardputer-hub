#pragma once

#include "connectivity/bluetooth/bluetooth_service.h"

namespace cardputer_hub::hardware {

class Esp32BluetoothAdapter final : public connectivity::IBluetoothAdapter {
  public:
    Esp32BluetoothAdapter() noexcept = default;
    ~Esp32BluetoothAdapter() override;

    Esp32BluetoothAdapter(const Esp32BluetoothAdapter&) = delete;
    Esp32BluetoothAdapter& operator=(const Esp32BluetoothAdapter&) = delete;

    connectivity::BluetoothAdapterResult
    initialize(const connectivity::BluetoothDeviceConfig& config, std::uint32_t lifecycle) override;
    connectivity::BluetoothAdapterResult shutdown() override;
    connectivity::BluetoothAdvertisingResult startAdvertising(std::uint32_t lifecycle) override;
    connectivity::BluetoothAdapterResult requestAdvertisingStop() override;
    connectivity::BluetoothAdapterResult
    disconnectPeer(connectivity::BluetoothPeerHandle peer) override;
    connectivity::BluetoothPollResult pollEvent() override;
    connectivity::BluetoothBondQueryResult
    bondState(connectivity::BluetoothPeerHandle peer) override;
    connectivity::BluetoothAdapterResult
    beginPairing(connectivity::BluetoothPeerHandle peer) override;
    connectivity::BluetoothAdapterResult
    respondToPairing(connectivity::BluetoothPeerHandle peer,
                     connectivity::BluetoothPairingChallengeType type, bool accepted,
                     std::optional<std::uint32_t> passkey) override;
    connectivity::BluetoothBondListResult bonds() override;
    connectivity::BluetoothBondReferenceResult
    bondReference(connectivity::BluetoothPeerHandle peer) override;
    connectivity::BluetoothAdapterResult
    deleteBond(const connectivity::BluetoothBondReference& reference) override;
    connectivity::BluetoothAdapterResult
    deleteBondForPeer(connectivity::BluetoothPeerHandle peer) override;
};

} // namespace cardputer_hub::hardware
