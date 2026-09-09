#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "connectivity/hid/hid_transport.h"

namespace cardputer_hub::connectivity {

enum class NativeUsbAdapterResult : std::uint8_t { Success, Unavailable, Busy, Error };
enum class NativeUsbInitializeResult : std::uint8_t {
    Initialized,
    AlreadyInitialized,
    AdapterError
};
enum class NativeUsbEventType : std::uint8_t {
    Mounted,
    Unmounted,
    Suspended,
    Resumed,
    EndpointReadinessChanged,
    AdapterError,
};

struct NativeUsbEvent {
    NativeUsbEventType type = NativeUsbEventType::AdapterError;
    std::uint32_t lifecycle = 0;
    bool endpointReady = false;
};

enum class NativeUsbPollStatus : std::uint8_t { NoEvent, Event, AdapterError };

struct NativeUsbPollResult {
    NativeUsbPollStatus status = NativeUsbPollStatus::NoEvent;
    NativeUsbEvent event{};
};

class NativeUsbEventQueue {
  public:
    static constexpr std::size_t capacity = 8;

    bool push(const NativeUsbEvent& event) noexcept;
    NativeUsbPollResult pop() noexcept;

  private:
    std::array<NativeUsbEvent, capacity> events_{};
    std::atomic<std::size_t> readIndex_{0};
    std::atomic<std::size_t> writeIndex_{0};
    std::atomic<bool> overflowed_{false};
};

class INativeUsbAdapter {
  public:
    virtual ~INativeUsbAdapter() = default;

    virtual NativeUsbAdapterResult initialize(std::uint32_t lifecycle) = 0;
    virtual NativeUsbPollResult pollEvent() = 0;
    virtual NativeUsbAdapterResult sendHidReport(const HidReport& report) = 0;
};

class NativeUsbHidService final : public IHidTransport {
  public:
    explicit NativeUsbHidService(INativeUsbAdapter& adapter) noexcept;

    NativeUsbInitializeResult initialize();
    void update();

    HidTransportState state() const noexcept override;
    HidSendResult send(const HidReport& report) override;
    HidSendResult releaseAll() override;

  private:
    enum class ReleaseStage : std::uint8_t { None, Keyboard, Consumer };

    void handleEvent(const NativeUsbEvent& event) noexcept;
    HidSendResult sendToAdapter(const HidReport& report);

    static constexpr std::size_t maxEventsPerUpdate = NativeUsbEventQueue::capacity;
    static constexpr std::uint32_t lifecycle = 1;

    INativeUsbAdapter& adapter_;
    ReleaseStage releaseStage_ = ReleaseStage::None;
    bool initialized_ = false;
    bool mounted_ = false;
    bool suspended_ = false;
    bool endpointReady_ = false;
    bool busy_ = false;
    bool error_ = false;
};

} // namespace cardputer_hub::connectivity
