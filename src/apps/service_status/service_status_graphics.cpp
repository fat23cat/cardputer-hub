#include "apps/service_status/service_status_graphics.h"

#include "core/display/palette.h"
#include "core/display/text_layout.h"

#include <algorithm>

namespace cardputer_hub::apps {
using namespace core;
using connectivity::ServiceStatusLevel;
using services::StatusProblem;

namespace {
constexpr std::int32_t marginX = 6;
constexpr std::int32_t headerY = 6;
constexpr std::int32_t ruleY = 20;
constexpr std::int32_t firstRowY = 27;
constexpr std::int32_t rowPitch = 18;
constexpr std::int32_t captionY = 101;
constexpr std::int32_t markerSize = 7;
constexpr std::int32_t markerGap = 5;
constexpr char title[] = "SERVICES HEALTH";

std::optional<RgbColor> markerOf(ServiceStatusLevel level) {
    switch (level) {
    case ServiceStatusLevel::Operational:
        return palette::leaf;
    case ServiceStatusLevel::Maintenance:
        return palette::blue;
    case ServiceStatusLevel::Minor:
    case ServiceStatusLevel::Major:
    case ServiceStatusLevel::Critical:
        return palette::vermilion;
    case ServiceStatusLevel::Unknown:
        break;
    }
    return std::nullopt;
}

// One line for the whole list: no connection, or the pages that failed.
std::string caption(const services::ServiceStatusSnapshot& snapshot) {
    std::size_t offline = 0;
    std::size_t failed = 0;
    const services::ServiceStatusEntry* failure = nullptr;
    std::size_t failureIndex = 0;
    for (std::size_t index = 0; index < snapshot.entries.size(); ++index) {
        const auto& entry = snapshot.entries[index];
        if (entry.problem == StatusProblem::NoConnection) {
            ++offline;
        } else if (entry.problem != StatusProblem::None) {
            ++failed;
            failure = &entry;
            failureIndex = index;
        }
    }
    if (offline == snapshot.entries.size())
        return "NO WI-FI OR COMPANION";
    if (failed > 1)
        return std::to_string(failed) + " PAGES FAILED";
    if (failed == 1)
        return std::string(services::statusSources[failureIndex].name) +
               (failure->problem == StatusProblem::Unreadable ? " NOT READABLE" : " UNREACHABLE");
    return "";
}

void drawRow(IDisplayAdapter& display, std::size_t index, const ServiceStatusRowFrame& row,
             bool full) {
    const auto y = firstRowY + static_cast<std::int32_t>(index) * rowPitch;
    const TextStyle style{palette::ink, palette::bone, systemTextScale};
    if (!full)
        display.fillRectangle({marginX, y - 3}, 240 - 2 * marginX, 16, palette::bone);
    char ordinal[3] = {'0', static_cast<char>('1' + index), '\0'};
    display.drawText({10, y}, ordinal, {palette::ordinal, palette::bone, systemTextScale});
    display.drawText({30, y}, services::statusSources[index].name, style);
    const auto valueX = rightAlignedTextX(row.value.c_str());
    display.drawText({valueX, y}, row.value.c_str(), style);
    if (row.marker)
        display.fillRectangle({valueX - markerGap - markerSize, y + 1}, markerSize, markerSize,
                              *row.marker);
}

void drawLine(IDisplayAdapter& display, std::int32_t y, const std::string& text, RgbColor color,
              bool erase) {
    if (erase)
        display.fillRectangle({marginX, y}, 240 - 2 * marginX, systemTextHeight(), palette::bone);
    const auto shown = fitSystemText(text, 240 - 2 * marginX);
    display.drawText({marginX, y}, shown.c_str(), {color, palette::bone, systemTextScale});
}
} // namespace

const char* serviceStatusValue(const services::ServiceStatusEntry& entry) {
    switch (entry.level) {
    case ServiceStatusLevel::Operational:
        return "OK";
    case ServiceStatusLevel::Maintenance:
        return "MAINT";
    case ServiceStatusLevel::Minor:
        return "MINOR";
    case ServiceStatusLevel::Major:
        return "MAJOR";
    case ServiceStatusLevel::Critical:
        return "CRITICAL";
    case ServiceStatusLevel::Unknown:
        break;
    }
    if (!entry.checked || entry.problem == StatusProblem::NoConnection)
        return "--";
    return "ERROR";
}

std::string serviceStatusHeader(const services::ServiceStatusSnapshot& snapshot) {
    if (snapshot.checking)
        return "CHECKING";
    const auto seconds =
        std::chrono::duration_cast<std::chrono::seconds>(snapshot.sinceRound).count();
    return std::to_string(seconds / 10 * 10) + " SEC AGO";
}

ServiceStatusFrame serviceStatusFrame(const services::ServiceStatusSnapshot& snapshot) {
    ServiceStatusFrame frame;
    frame.status = serviceStatusHeader(snapshot);
    for (std::size_t index = 0; index < frame.rows.size(); ++index) {
        const auto& entry = snapshot.entries[index];
        frame.rows[index].value = serviceStatusValue(entry);
        frame.rows[index].marker = markerOf(entry.level);
    }
    frame.caption = caption(snapshot);
    return frame;
}

void drawServiceStatus(IDisplayAdapter& display, const ServiceStatusFrame& next,
                       const ServiceStatusFrame* previous) {
    const bool full = previous == nullptr;
    const TextStyle normal{palette::ink, palette::bone, systemTextScale};
    if (full) {
        display.clear(palette::bone);
        display.drawText({marginX, headerY}, title, normal);
        display.fillRectangle({marginX, ruleY}, 240 - 2 * marginX, 1, palette::ink);
    }
    if (full || next.status != previous->status) {
        if (!full) {
            const auto left = std::min(rightAlignedTextX(previous->status.c_str()),
                                       rightAlignedTextX(next.status.c_str()));
            display.fillRectangle({left, headerY}, headerStatusRight - left, systemTextHeight(),
                                  palette::bone);
        }
        display.drawText({rightAlignedTextX(next.status.c_str()), headerY}, next.status.c_str(),
                         {palette::ordinal, palette::bone, systemTextScale});
    }
    for (std::size_t index = 0; index < next.rows.size(); ++index) {
        if (!full && next.rows[index] == previous->rows[index])
            continue;
        drawRow(display, index, next.rows[index], full);
    }
    if (full || next.caption != previous->caption)
        drawLine(display, captionY, next.caption, palette::ink, !full);
}

} // namespace cardputer_hub::apps
