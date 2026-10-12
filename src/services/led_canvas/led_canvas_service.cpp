#include "services/led_canvas/led_canvas_service.h"
#include <algorithm>
#include <utility>

namespace cardputer_hub::services {
void LedCanvasService::setActive(bool active) {
    if (active_ == active)
        return;
    active_ = active;
    endPointers();
    clients_ = {};
    seenPoint_ = false;
    if (active)
        ended_ = 0;
}
void LedCanvasService::endPointers() {
    for (std::size_t i = 0; i < pointers_.size(); ++i)
        ended_ |= std::uint32_t{1} << i;
    pointers_ = {};
    count_ = 0;
    for (auto& client : clients_)
        client.lastScheduled = 0;
}
bool LedCanvasService::submit(const LedCanvasBatch& batch) {
    if (!active_ || batch.count == 0 || batch.count > batch.points.size())
        return false;
    for (std::size_t i = 0; i < batch.count; ++i) {
        const auto& p = batch.points[i];
        if (p.slot > 4 || p.x > 2047 || p.y > 2047 ||
            (p.radius != 0 && (p.radius < 5 || p.radius > 29)) || p.flags > 3 ||
            (p.hasColor && (p.hue >= core::ledCanvasHueUnits || p.mixInk > 2)))
            return false;
    }
    std::size_t client = clients_.size();
    for (std::size_t i = 0; i < clients_.size(); ++i)
        if (clients_[i].used && clients_[i].id == batch.client)
            client = i;
    if (client != clients_.size()) {
        clients_[client].touched = ++useCounter_;
        if (batch.seq <= clients_[client].seq)
            return true;
    } else {
        client = 0;
        for (std::size_t i = 0; i < clients_.size(); ++i) {
            if (!clients_[i].used) {
                client = i;
                break;
            }
            if (clients_[i].touched < clients_[client].touched)
                client = i;
        }
        if (clients_[client].used) {
            for (std::size_t i = client * 5; i < client * 5 + 5; ++i) {
                ended_ |= std::uint32_t{1} << i;
                pointers_[i] = {};
            }
            std::size_t kept = 0;
            for (std::size_t i = 0; i < count_; ++i)
                if (queue_[i].pointer / 5 != client)
                    queue_[kept++] = queue_[i];
            count_ = kept;
        }
        clients_[client] = {true, batch.client, batch.seq, ++useCounter_, 0};
    }
    clients_[client].seq = batch.seq;
    // Continue this client's timeline when input accumulated behind an in-flight request.
    std::uint64_t time = std::max(clock_ + 100, clients_[client].lastScheduled);
    for (std::size_t i = 0; i < batch.count; ++i) {
        const auto& p = batch.points[i];
        time += p.dt;
        const auto pointer = static_cast<std::uint8_t>(client * 5 + p.slot);
        // A pending playback point defers expiry; otherwise silence ends at 500 ms.
        pointers_[pointer] = {(p.flags & 2) == 0, true, clock_};
        if (count_ == queue_.size()) {
            std::move(queue_.begin() + 1, queue_.end(), queue_.begin());
            --count_;
        }
        auto position = count_;
        while (position > 0 && queue_[position - 1].time > time) {
            queue_[position] = queue_[position - 1];
            --position;
        }
        queue_[position] = {p, pointer, time};
        ++count_;
    }
    clients_[client].lastScheduled = time;
    lastPoint_ = clock_;
    seenPoint_ = true;
    return true;
}
bool LedCanvasService::setSettings(const LedCanvasSettings& settings) {
    if (static_cast<unsigned>(settings.mode) >= ledCanvasModeIds.size() ||
        static_cast<unsigned>(settings.fade) >= ledCanvasFadeIds.size() || settings.ink > 6)
        return false;
    if (settings.mode != settings_.mode || settings.fade != settings_.fade ||
        settings.ink != settings_.ink || settings.hold != settings_.hold) {
        settings_ = settings;
        ++revision_;
    }
    return true;
}
void LedCanvasService::clear() {
    if (!active_)
        return;
    endPointers();
    ++clearGeneration_;
}
LedCanvasStatus LedCanvasService::status() const {
    std::size_t fingers = 0;
    for (const auto& p : pointers_)
        if (p.down)
            ++fingers;
    return {active_,  revision_, clearGeneration_,
            fingers,  count_,    seenPoint_ && clock_ - lastPoint_ < 3000,
            settings_};
}
std::optional<LedCanvasPlayback> LedCanvasService::takeDue(std::chrono::milliseconds elapsed) {
    if (elapsed.count() > 0)
        clock_ += static_cast<std::uint64_t>(elapsed.count());
    for (std::size_t i = 0; i < pointers_.size(); ++i) {
        auto& p = pointers_[i];
        const bool pending = std::any_of(queue_.begin(), queue_.begin() + count_,
                                         [i](const auto& point) { return point.pointer == i; });
        if (p.used && !pending && clock_ >= p.touched + 500) {
            p.down = false;
            p.used = false;
            ended_ |= std::uint32_t{1} << i;
        }
    }
    if (!count_ || queue_[0].time > clock_)
        return std::nullopt;
    const auto point = queue_[0];
    std::move(queue_.begin() + 1, queue_.begin() + count_, queue_.begin());
    --count_;
    return point;
}
std::uint32_t LedCanvasService::takeEnded() { return std::exchange(ended_, 0); }
core::ActionHandlingResult LedCanvasService::handle(const core::Action& action) {
    if (action.id == ledCanvasHoldActionId && action.parameters.size() == 1) {
        const auto* value = action.findParameter("hold");
        if (value && std::holds_alternative<bool>(*value)) {
            auto settings = settings_;
            settings.hold = std::get<bool>(*value);
            (void)setSettings(settings);
            return core::ActionHandlingResult::Handled;
        }
    }
    if (action.id == ledCanvasClearActionId && action.parameters.empty() && active_) {
        clear();
        return core::ActionHandlingResult::Handled;
    }
    return core::ActionHandlingResult::Rejected;
}
} // namespace cardputer_hub::services
