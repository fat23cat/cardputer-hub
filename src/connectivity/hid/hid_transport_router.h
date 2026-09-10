#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "connectivity/bluetooth/bluetooth_hid_target.h"
#include "connectivity/hid/hid_transport.h"

namespace cardputer_hub::connectivity {

enum class HidTransportKind : std::uint8_t { None, Ble, Usb };

struct HidTransactionFrame {
    HidReport report;
    std::chrono::milliseconds dwell{0};
};

struct HidTransaction {
    static constexpr std::size_t maximumFrameCount = 16;

    std::vector<HidTransactionFrame> frames;
};

enum class HidRouteResult : std::uint8_t {
    Accepted,
    Busy,
    NoReadyTransport,
    InvalidTransaction,
    TransportError,
};

enum class HidRouterState : std::uint8_t { Idle, Dispatching, Handover, Unavailable, Error };

bool isValidHidTransaction(const HidTransaction& transaction) noexcept;

class HidTransportRouter {
  public:
    HidTransportRouter(IUsbHidTransport& usb, IBluetoothHidTarget& bleTarget) noexcept;

    void setBleTarget(std::optional<BluetoothBondReference> target);
    HidRouteResult route(const HidTransaction& transaction);
    void cancel();
    void update(std::chrono::milliseconds elapsed);

    HidRouterState state() const noexcept;
    HidTransportKind activeTransport() const noexcept;
    HidTransportKind activeTransactionTransport() const noexcept;

  private:
    struct ActiveTransaction {
        HidTransaction transaction;
        HidTransportKind transport = HidTransportKind::None;
        std::size_t frameIndex = 0;
        std::chrono::milliseconds elapsedSinceFrame{0};
        bool waitingToSend = true;
        bool hasSentFrame = false;
    };

    static bool isReady(const IHidTransport& transport) noexcept;
    HidTransportKind selectTransport() const noexcept;
    IHidTransport& transport(HidTransportKind kind) noexcept;
    const IHidTransport& transport(HidTransportKind kind) const noexcept;
    void refreshIdleState() noexcept;
    void applyBleTarget();
    HidRouteResult sendCurrentFrame();
    void advance(std::chrono::milliseconds elapsed);
    void beginCleanup(bool error);
    void retryCleanup();
    void finishCleanup();
    bool boundTransportDisconnected() const noexcept;
    bool usbNeedsHandover() const noexcept;
    bool usbHasTakenOwnership() const noexcept;

    IUsbHidTransport& usb_;
    IBluetoothHidTarget& bleTargetControl_;
    std::optional<BluetoothBondReference> desiredBleTarget_;
    std::optional<ActiveTransaction> activeTransaction_;
    HidRouterState state_ = HidRouterState::Unavailable;
    bool cleanupPending_ = false;
    bool cleanupError_ = false;
    bool applyTargetAfterCleanup_ = false;
};

} // namespace cardputer_hub::connectivity
