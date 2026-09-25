#include "apps/mac_status/mac_status_graphics.h"

#include "core/display/palette.h"

#include <algorithm>
#include <cstdio>

namespace cardputer_hub::apps {
namespace {
std::string percent(const char* prefix, bool available, unsigned value) {
    if (!available)
        return std::string(prefix) + " --";
    char text[24]{};
    std::snprintf(text, sizeof(text), "%s %u%%", prefix, value);
    return text;
}

std::string memory(bool available, std::uint32_t used, std::uint32_t total) {
    if (!available)
        return "RAM --";
    char text[24]{};
    const auto tenth = (std::uint64_t(used) * 10U + 512U) / 1024U;
    const auto totalGiB = (total + 512U) / 1024U;
    std::snprintf(
        text, sizeof(text), "RAM %llu.%llu/%luG", static_cast<unsigned long long>(tenth / 10),
        static_cast<unsigned long long>(tenth % 10), static_cast<unsigned long>(totalGiB));
    return text;
}

std::string rate(bool available, std::uint32_t kib) {
    if (!available)
        return "--";
    char text[20]{};
    if (kib < 1024)
        std::snprintf(text, sizeof(text), "%lu KB/s", static_cast<unsigned long>(kib));
    else if (kib < 1024U * 1024U) {
        const auto tenths = (kib * 10U + 512U) / 1024U;
        std::snprintf(text, sizeof(text), "%lu.%lu MB/s", static_cast<unsigned long>(tenths / 10U),
                      static_cast<unsigned long>(tenths % 10U));
    } else
        std::snprintf(text, sizeof(text), "%lu GB/s",
                      static_cast<unsigned long>(kib / (1024U * 1024U)));
    return text;
}
} // namespace

MacStatusPresentation formatMacStatus(const services::MacStatusSnapshot& snapshot) {
    MacStatusPresentation result{};
    const bool fresh = snapshot.freshness == services::MacStatusFreshness::Fresh;
    result.labels[0] = percent("CPU", fresh && snapshot.cpuAvailable, snapshot.cpuPercent);
    result.labels[1] =
        memory(fresh && snapshot.memoryAvailable, snapshot.memoryUsedMiB, snapshot.memoryTotalMiB);
    result.labels[2] = percent("SSD", fresh && snapshot.diskAvailable, snapshot.diskUsedPercent);
    result.labels[3] = percent("BAT", fresh && snapshot.batteryAvailable, snapshot.batteryPercent);
    result.labels[4] = rate(fresh && snapshot.networkAvailable, snapshot.downloadKiBps);
    result.labels[5] = rate(fresh && snapshot.networkAvailable, snapshot.uploadKiBps);
    const auto pressure = fresh ? snapshot.memoryPressure : services::MacMemoryPressure::Unknown;
    switch (pressure) {
    case services::MacMemoryPressure::Normal:
        result.labels[6] = "PRESS NORMAL";
        break;
    case services::MacMemoryPressure::Warning:
        result.labels[6] = "PRESS WARN";
        break;
    case services::MacMemoryPressure::Critical:
        result.labels[6] = "PRESS CRIT";
        break;
    default:
        result.labels[6] = "PRESS --";
        break;
    }
    const auto thermal = fresh ? snapshot.thermalState : services::MacThermalState::Unknown;
    switch (thermal) {
    case services::MacThermalState::Normal:
        result.labels[7] = "THERM NORMAL";
        break;
    case services::MacThermalState::Fair:
        result.labels[7] = "THERM FAIR";
        break;
    case services::MacThermalState::Serious:
        result.labels[7] = "THERM SERIOUS";
        break;
    case services::MacThermalState::Critical:
        result.labels[7] = "THERM CRIT";
        break;
    default:
        result.labels[7] = "THERM --";
        break;
    }
    result.bars[0] = fresh && snapshot.cpuAvailable ? snapshot.cpuPercent : -1;
    result.bars[1] =
        fresh && snapshot.memoryAvailable && snapshot.memoryTotalMiB
            ? static_cast<int>(std::min<std::uint64_t>(100, std::uint64_t(snapshot.memoryUsedMiB) *
                                                                100 / snapshot.memoryTotalMiB))
            : -1;
    result.bars[2] = fresh && snapshot.diskAvailable ? snapshot.diskUsedPercent : -1;
    result.bars[3] = fresh && snapshot.batteryAvailable ? snapshot.batteryPercent : -1;
    return result;
}

void drawMacStatusMetric(core::IDisplayAdapter& display, const MacStatusPresentation& value,
                         int index) {
    using namespace core;
    const int x = index % 2 == 0 ? 8 : 124;
    const int y = index < 2 ? 10 : index < 4 ? 47 : index < 6 ? 86 : 113;
    const int height = index < 4 ? 31 : index < 6 ? 18 : 17;
    display.fillRectangle({x, y}, 108, height, palette::bone);
    int textX = x;
    if (index == 4 || index == 5) {
        // Direction arrows have stable geometry even if the font lacks arrow glyphs.
        const int arrowX = x + 3;
        if (index == 4) {
            display.fillRectangle({arrowX, y + 2}, 2, 8, palette::blue);
            display.fillRectangle({arrowX - 2, y + 8}, 6, 2, palette::blue);
            display.fillRectangle({arrowX - 1, y + 10}, 4, 2, palette::blue);
        } else {
            display.fillRectangle({arrowX, y + 4}, 2, 8, palette::blue);
            display.fillRectangle({arrowX - 2, y + 4}, 6, 2, palette::blue);
            display.fillRectangle({arrowX - 1, y + 2}, 4, 2, palette::blue);
        }
        textX += 14;
    }
    auto color = palette::ink;
    if (index >= 6) {
        if (value.labels[index].find("CRIT") != std::string::npos ||
            value.labels[index].find("SERIOUS") != std::string::npos)
            color = palette::vermilion;
        else if (value.labels[index].find("NORMAL") != std::string::npos)
            color = palette::leaf;
    }
    display.drawText({textX, y + 1}, value.labels[index].c_str(), {color, palette::bone, 1});
    if (index < 4) {
        display.fillRectangle({x, y + 22}, 100, 6, palette::pale);
        if (value.bars[index] >= 0)
            display.fillRectangle({x, y + 22}, value.bars[index], 6, palette::blue);
    }
}

} // namespace cardputer_hub::apps
