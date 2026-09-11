#pragma once

#include <cstdint>
#include <optional>

namespace cardputer_hub::hardware::bluetooth_detail {

enum class PeerEventKind { Connected, Data, Disconnected };
enum class PeerEventDisposition { AnnounceConnection, Forward, Ignore, Reject };

// One controller connection. The caller serializes access and emits the
// connection announcement before forwarding data from the first peer event.
class BlePeerEventOrder {
  public:
    PeerEventDisposition observe(std::uint16_t handle, PeerEventKind kind) {
        if (kind == PeerEventKind::Disconnected) {
            if (!active_.has_value()) {
                return PeerEventDisposition::Ignore;
            }
            if (*active_ != handle) {
                return PeerEventDisposition::Reject;
            }
            active_.reset();
            return PeerEventDisposition::Forward;
        }
        if (active_.has_value() && *active_ != handle) {
            return PeerEventDisposition::Reject;
        }
        if (!active_.has_value()) {
            active_ = handle;
            return PeerEventDisposition::AnnounceConnection;
        }
        return kind == PeerEventKind::Connected ? PeerEventDisposition::Ignore
                                                : PeerEventDisposition::Forward;
    }

    std::optional<std::uint16_t> active() const { return active_; }
    void reset() { active_.reset(); }

  private:
    std::optional<std::uint16_t> active_;
};

} // namespace cardputer_hub::hardware::bluetooth_detail
