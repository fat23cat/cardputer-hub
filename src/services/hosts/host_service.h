#pragma once

#include <cstdint>
#include <optional>
#include <string>

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

enum class HostConnectionStatus : std::uint8_t {
    Off,
    Connecting,
    Securing,
    Ready,
    Pairing,
    Error,
};

enum class HostPairingPhase : std::uint8_t {
    Inactive,
    Discoverable,
    Prompting,
    Securing,
};

enum class HostPairingPromptType : std::uint8_t {
    None,
    DisplayPasskey,
    EnterPasskey,
    ConfirmComparison,
};

struct HostPairingPrompt {
    std::uint32_t generation = 0;
    HostPairingPromptType type = HostPairingPromptType::None;
    std::optional<std::uint32_t> value;
};

struct HostStatusSnapshot {
    HostConnectionStatus connection = HostConnectionStatus::Off;
    std::optional<std::uint32_t> activeHostId;
    std::string activeHostName;
    HostResult lastResult = HostResult::Success;
    bool connectionEnabled = false;
    bool pairing = false;
    HostPairingPhase pairingPhase = HostPairingPhase::Inactive;
    std::optional<HostPairingPrompt> pairingPrompt;
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
    HostResult setHostPlatform(std::uint32_t id, std::optional<HostPlatformId> platform);
    HostResult setHostCapability(std::uint32_t id, const HostCapabilityId& capability,
                                 bool enabled);
    HostResult setHostMappingTemplate(std::uint32_t id,
                                      std::optional<HostMappingTemplateId> mappingTemplate);
    HostResult startPairing();
    HostResult cancelPairing();
    HostResult deleteHost(std::uint32_t id);
    core::ActionHandlingResult handle(const core::Action& action) override;
    const HostConfiguration& settings() const noexcept { return configuration_.value().host; }
    HostStatusSnapshot status() const;

  private:
    HostResult fail(HostResult result, const char* reason = nullptr);
    HostResult ensureReady();
    HostResult ensureMetadataReady();
    HostResult apply();
    HostResult initializeIdle();
    HostResult reconcile(SystemConfiguration& value);
    HostResult save(const SystemConfiguration& value);
    HostResult saveMetadata(const SystemConfiguration& value);
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
