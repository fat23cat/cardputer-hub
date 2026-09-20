#include "services/indicator/indicator_service.h"

#include <algorithm>

namespace cardputer_hub::services {
namespace {
bool sameColor(core::RgbColor left, core::RgbColor right) noexcept {
    return left.red == right.red && left.green == right.green && left.blue == right.blue;
}

bool sameFrame(const core::LedHardwareFrame& left, const core::LedHardwareFrame& right) noexcept {
    for (std::size_t i = 0; i < core::ledMatrixPixelCount; ++i) {
        if (!sameColor(left.pixels[i], right.pixels[i]))
            return false;
    }
    return true;
}

std::uint8_t scaleChannel(std::uint8_t channel, std::uint8_t percent) noexcept {
    return static_cast<std::uint8_t>((static_cast<unsigned>(channel) * percent + 50U) / 100U);
}
} // namespace

IndicatorClaim::IndicatorClaim(IndicatorService* service, std::uint32_t token) noexcept
    : service_(service), token_(token) {}

IndicatorClaim::IndicatorClaim(IndicatorClaim&& other) noexcept
    : service_(other.service_), token_(other.token_) {
    other.service_ = nullptr;
    other.token_ = 0;
}

IndicatorClaim& IndicatorClaim::operator=(IndicatorClaim&& other) noexcept {
    if (this == &other)
        return *this;
    release();
    service_ = other.service_;
    token_ = other.token_;
    other.service_ = nullptr;
    other.token_ = 0;
    return *this;
}

IndicatorClaim::~IndicatorClaim() { release(); }

void IndicatorClaim::setFrame(const IndicatorFrame& frame) {
    if (service_ != nullptr)
        service_->setFrame(token_, frame);
}

void IndicatorClaim::release() {
    if (service_ == nullptr)
        return;
    service_->release(token_);
    service_ = nullptr;
    token_ = 0;
}

IndicatorService::IndicatorService(core::ILEDAdapter& adapter) : adapter_(adapter) {}

std::uint8_t IndicatorService::brightnessPercentFor(std::string_view owner) noexcept {
    if (owner == pomodoroIndicatorOwner)
        return pomodoroBrightnessPercent;
    return 100;
}

core::LedHardwareFrame IndicatorService::hardwareFrame(const IndicatorFrame& frame,
                                                       std::uint8_t brightnessPercent) {
    core::LedHardwareFrame hardware{};
    for (std::size_t i = 0; i < core::ledMatrixPixelCount; ++i) {
        hardware.pixels[i] = {scaleChannel(frame.pixels[i].red, brightnessPercent),
                              scaleChannel(frame.pixels[i].green, brightnessPercent),
                              scaleChannel(frame.pixels[i].blue, brightnessPercent)};
    }
    return hardware;
}

IndicatorClaim IndicatorService::acquire(std::string_view owner, IndicatorPriority priority) {
    if (owner.empty())
        return {};
    const auto token = nextToken_++;
    if (nextToken_ == 0)
        nextToken_ = 1;
    records_.push_back({token, std::string(owner), priority, {}, false});
    dirty_ = true;
    return IndicatorClaim(this, token);
}

IndicatorService::Record* IndicatorService::find(std::uint32_t token) noexcept {
    const auto it = std::find_if(records_.begin(), records_.end(),
                                 [token](const Record& record) { return record.token == token; });
    return it == records_.end() ? nullptr : &*it;
}

void IndicatorService::setFrame(std::uint32_t token, const IndicatorFrame& frame) {
    auto* record = find(token);
    if (record == nullptr)
        return;
    record->frame = frame;
    record->hasFrame = true;
    dirty_ = true;
}

void IndicatorService::release(std::uint32_t token) {
    const auto before = records_.size();
    records_.erase(std::remove_if(records_.begin(), records_.end(),
                                  [token](const Record& record) { return record.token == token; }),
                   records_.end());
    if (records_.size() != before)
        dirty_ = true;
}

void IndicatorService::resolve() {
    const Record* selected = nullptr;
    for (const auto& record : records_) {
        if (!record.hasFrame)
            continue;
        if (selected == nullptr || record.priority > selected->priority ||
            (record.priority == selected->priority && record.token > selected->token))
            selected = &record;
    }
    if (selected == nullptr) {
        resolved_ = {};
        return;
    }
    resolved_.hasFrame = true;
    resolved_.owner = selected->owner;
    resolved_.priority = selected->priority;
    resolved_.frame = selected->frame;
    resolved_.brightnessPercent = brightnessPercentFor(selected->owner);
}

void IndicatorService::update() {
    if (!dirty_)
        return;
    resolve();
    core::LedHardwareFrame hardware{};
    if (resolved_.hasFrame)
        hardware = hardwareFrame(resolved_.frame, resolved_.brightnessPercent);
    if (haveLastHardware_ && sameFrame(hardware, lastHardware_)) {
        dirty_ = false;
        return;
    }
    adapter_.writeFrame(hardware);
    lastHardware_ = hardware;
    haveLastHardware_ = true;
    ++adapterWrites_;
    dirty_ = false;
}

} // namespace cardputer_hub::services
