#include "apps/mac_control/mac_control_app.h"

#include "core/display/palette.h"
#include "services/host_control/host_control_service.h"

#include <algorithm>
#include <string>

namespace cardputer_hub::apps {
using namespace core;
using namespace services;

namespace {
bool isPlain(const InputEvent& event) {
    return !event.modifiers.ctrl && !event.modifiers.alt && !event.modifiers.option;
}

bool isLeft(const InputEvent& event) {
    return (event.type == InputEventType::NamedKey && event.namedKey == NamedKey::Left) ||
           (event.type == InputEventType::PrintableCharacter && event.character == ',' &&
            !event.modifiers.shift);
}

bool isRight(const InputEvent& event) {
    return (event.type == InputEventType::NamedKey && event.namedKey == NamedKey::Right) ||
           (event.type == InputEventType::PrintableCharacter && event.character == '/' &&
            !event.modifiers.shift);
}

} // namespace

MacControlApp::MacControlApp(ActionBus& actions, HostControlService& hostControl,
                             IDisplayAdapter& display, std::vector<MacControlPage> pages)
    : actions_(actions), hostControl_(hostControl), display_(display), pages_(std::move(pages)) {
    if (pages_.empty())
        pages_ = productionMacControlPages();
}

const MacControlPage& MacControlApp::currentPage() const {
    return pages_[std::min(pageIndex_, pages_.size() - 1)];
}

void MacControlApp::resetLocalState() {
    view_ = MacControlView::Grid;
    sourceSlot_ = 0;
    source_ = {};
    takeover_ = {};
    phaseElapsed_ = {};
    commandGeneration_ = 0;
    failure_ = HostControlFailure::None;
    frame_.reset();
}

void MacControlApp::onActivate() {
    resetLocalState();
    pageIndex_ = 0;
}

void MacControlApp::onDeactivate() { resetLocalState(); }

void MacControlApp::update(const InputEvents& input, std::chrono::milliseconds elapsed) {
    for (const auto& event : input)
        handle(event);
    observeCommand();
    advanceAnimation(elapsed);
    render();
}

void MacControlApp::handle(const InputEvent& event) {
    if (!isPlain(event) || view_ != MacControlView::Grid)
        return;
    if (isLeft(event)) {
        turnPage(-1);
        return;
    }
    if (isRight(event)) {
        turnPage(1);
        return;
    }
    if (event.type != InputEventType::PrintableCharacter)
        return;
    const auto slot = macControlSlotForKey(event.character);
    if (slot != 0)
        activateSlot(slot);
}

bool MacControlApp::turnPage(int delta) {
    if (delta < 0 && !macControlCanMovePrevious(pageIndex_))
        return false;
    if (delta > 0 && !macControlCanMoveNext(pageIndex_, pages_.size()))
        return false;
    pageIndex_ += static_cast<std::size_t>(delta > 0 ? 1 : -1);
    display_.beginTransition(delta > 0 ? SlideDirection::Forward : SlideDirection::Backward);
    frame_.reset();
    return true;
}

void MacControlApp::activateSlot(std::uint8_t slot) {
    if (hostControl_.status().state == HostControlCommandState::Pending)
        return;
    const auto* binding = macControlBindingAt(currentPage(), slot);
    if (binding == nullptr)
        return;
    const auto result =
        actions_.dispatch({hostAppActivateActionId,
                           macControlAppId,
                           {{hostAppActivateBundleParameter, std::string(binding->bundleId)}}});
    if (result != DispatchResult::Handled)
        return;
    sourceSlot_ = slot;
    source_ = macControlTileRect(slot);
    takeover_ = source_;
    phaseElapsed_ = {};
    commandGeneration_ = hostControl_.status().generation;
    failure_ = HostControlFailure::None;
    view_ = MacControlView::PendingTakeover;
}

void MacControlApp::observeCommand() {
    if (view_ != MacControlView::PendingTakeover)
        return;
    const auto status = hostControl_.status();
    if (status.generation != commandGeneration_ ||
        status.state == HostControlCommandState::Pending ||
        status.state == HostControlCommandState::Idle)
        return;
    if (status.failure == HostControlFailure::Unavailable) {
        resetLocalState();
        return;
    }
    failure_ = status.failure;
    view_ = status.state == HostControlCommandState::Succeeded ? MacControlView::SuccessTakeover
                                                               : MacControlView::FailureTakeover;
    phaseElapsed_ = {};
}

void MacControlApp::advanceAnimation(std::chrono::milliseconds elapsed) {
    if (view_ != MacControlView::SuccessTakeover && view_ != MacControlView::FailureTakeover)
        return;
    phaseElapsed_ += std::max(elapsed, std::chrono::milliseconds(0));
    const auto hold = view_ == MacControlView::SuccessTakeover ? macControlSuccessHoldMs
                                                               : macControlFailureHoldMs;
    if (phaseElapsed_.count() >= hold)
        resetLocalState();
}

RgbColor MacControlApp::takeoverSurface() const {
    return view_ == MacControlView::SuccessTakeover ? palette::leaf : palette::vermilion;
}

void MacControlApp::render() {
    const auto* binding =
        sourceSlot_ == 0 ? nullptr : macControlBindingAt(currentPage(), sourceSlot_);
    Frame next{view_, pageIndex_, takeover_, hostControl_.status().state, failure_};
    if (frame_ && frame_->view == next.view && frame_->page == next.page &&
        frame_->takeover.origin.x == next.takeover.origin.x &&
        frame_->takeover.origin.y == next.takeover.origin.y &&
        frame_->takeover.width == next.takeover.width &&
        frame_->takeover.height == next.takeover.height && frame_->command == next.command &&
        frame_->failure == next.failure)
        return;

    drawMacControlGrid(display_, currentPage());
    if (view_ == MacControlView::SuccessTakeover || view_ == MacControlView::FailureTakeover)
        drawMacControlTakeover(display_, takeover_, takeoverSurface(), sourceSlot_, binding);
    frame_ = next;
}

} // namespace cardputer_hub::apps
