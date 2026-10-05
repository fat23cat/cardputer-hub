#include "apps/ai_agent_status/ai_agent_status_app.h"

#include "core/display/palette.h"
#include "core/display/text_layout.h"

namespace cardputer_hub::apps {
namespace {
using connectivity::AgentState;
using connectivity::AiProvider;
using namespace core;

constexpr int screenWidth = 240;
constexpr int screenHeight = 135;
// Bone line between plates, so equal neighbouring states stay apart.
constexpr int plateGap = 2;
constexpr float plateTextScale = 2.0f;
constexpr int textLeft = 10;
constexpr int textRight = 230;

std::size_t indexOf(AiProvider application) { return static_cast<std::size_t>(application) - 1; }

const char* applicationName(AiProvider application) {
    switch (application) {
    case AiProvider::Codex:
        return "CODEX";
    case AiProvider::Claude:
        return "CLAUDE";
    case AiProvider::Cursor:
        return "CURSOR";
    }
    return "";
}

const char* stateLabel(AgentState state) {
    switch (state) {
    case AgentState::Working:
        return "WORKING";
    case AgentState::NeedsYou:
        return "NEEDS YOU";
    case AgentState::Done:
    case AgentState::DoneEarlier:
        return "DONE";
    case AgentState::Unknown:
        break;
    }
    return "--";
}

// The state surface and its text color.
TextStyle plateStyle(AgentState state) {
    switch (state) {
    case AgentState::Working:
        return {palette::bone, palette::blue, plateTextScale};
    case AgentState::NeedsYou:
        return {palette::bone, palette::vermilion, plateTextScale};
    case AgentState::Done:
        return {palette::ink, palette::leaf, plateTextScale};
    case AgentState::DoneEarlier:
        return {palette::ink, palette::lightNeutral, plateTextScale};
    case AgentState::Unknown:
        break;
    }
    return {palette::ink, palette::pale, plateTextScale};
}

int plateTop(int index, int count) { return index * screenHeight / count; }

void centered(IDisplayAdapter& display, int y, const char* text, RgbColor color, float scale) {
    display.drawText({centeredTextX(text, 0, screenWidth, scale), y}, text,
                     {color, palette::bone, scale});
}
} // namespace

void AiAgentStatusApp::onActivate() {
    plates_ = {};
    plateCount_ = 0;
    layout_ = Layout::None;
    shown_ = {};
    status_.startMonitoring();
    indicator_.activate();
}

void AiAgentStatusApp::onDeactivate() {
    indicator_.deactivate();
    status_.stopMonitoring();
    plates_ = {};
    layout_ = Layout::None;
}

void AiAgentStatusApp::drawLayout(Layout layout, const connectivity::CompanionAgentStatus& plates) {
    display_.clear(palette::bone);
    layout_ = layout;
    shown_ = plates;
    plates_ = {};
    plateCount_ = 0;
    if (layout == Layout::Checking) {
        centered(display_, 58, "CHECKING AI", palette::ink, plateTextScale);
        return;
    }
    if (layout == Layout::NoHooks) {
        centered(display_, 46, "NO AI HOOKS", palette::ink, plateTextScale);
        centered(display_, 76, "INSTALL IN COMPANION", palette::ordinal, systemTextScale);
        return;
    }
    for (const auto application : connectivity::agentStatusOrder)
        if (plates.installed(application))
            plates_[indexOf(application)].index = plateCount_++;
    for (const auto application : connectivity::agentStatusOrder)
        if (plates.installed(application))
            drawPlate(application, AgentState::Unknown);
}

void AiAgentStatusApp::drawPlate(AiProvider application, AgentState state) {
    auto& plate = plates_[indexOf(application)];
    const int top = plateTop(plate.index, plateCount_);
    const int bottom =
        plateTop(plate.index + 1, plateCount_) - (plate.index + 1 < plateCount_ ? plateGap : 0);
    const auto style = plateStyle(state);
    display_.fillRectangle({0, top}, screenWidth, bottom - top, style.background);
    const int textHeight = static_cast<int>(systemGlyphNativeHeight * plateTextScale);
    const int y = top + (bottom - top - textHeight) / 2;
    display_.drawText({textLeft, y}, applicationName(application), style);
    const char* label = stateLabel(state);
    display_.drawText({textRight - textWidth(label, plateTextScale), y}, label, style);
    plate.drawn = state;
}

void AiAgentStatusApp::update(const core::InputEvents&, std::chrono::milliseconds) {
    const auto& snapshot = status_.snapshot();
    if (layout_ != Layout::None && snapshot.generation == generation_)
        return;
    generation_ = snapshot.generation;
    const bool fresh = snapshot.freshness == services::AgentStatusFreshness::Fresh;
    // Plates follow the installed applications of the latest fresh answer; a
    // stale period keeps them and shows --.
    Layout next = layout_;
    if (fresh)
        next = snapshot.status.installedCount() == 0 ? Layout::NoHooks : Layout::Plates;
    else if (layout_ == Layout::None || snapshot.freshness == services::AgentStatusFreshness::Empty)
        next = Layout::Checking;
    if (next != layout_ || (fresh && next == Layout::Plates &&
                            snapshot.status.installedApplications != shown_.installedApplications))
        drawLayout(next, fresh ? snapshot.status : connectivity::CompanionAgentStatus{});
    if (layout_ != Layout::Plates)
        return;
    for (const auto application : connectivity::agentStatusOrder) {
        const auto& plate = plates_[indexOf(application)];
        if (plate.index < 0)
            continue;
        const auto state = fresh ? snapshot.status.state(application) : AgentState::Unknown;
        if (state != plate.drawn)
            drawPlate(application, state);
    }
}

} // namespace cardputer_hub::apps
