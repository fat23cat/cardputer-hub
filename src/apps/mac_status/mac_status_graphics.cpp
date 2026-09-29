#include "apps/mac_status/mac_status_graphics.h"

#include "core/display/palette.h"
#include "core/display/text_layout.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace cardputer_hub::apps {
namespace {
using services::MacStatusFreshness;
using services::MacStatusHistory;

constexpr std::int32_t left = 8;
constexpr std::int32_t right = 232;
constexpr float largeTextScale = 2.0f;

// Label/value rows. Pages with four rows use the roomy grid; pages that need
// five or six rows use the compact one (15 px keeps a 5 px gap under 10 px text).
struct RowGrid {
    std::int32_t top;
    std::int32_t pitch;
};
constexpr RowGrid roomyRows{60, 18};

std::string percent(const char* prefix, bool available, unsigned value) {
    if (!available)
        return std::string(prefix) + " --";
    char text[24]{};
    std::snprintf(text, sizeof(text), "%s %u%%", prefix, value);
    return text;
}

std::string percentValue(const std::optional<std::uint8_t>& value) {
    if (!value)
        return "--";
    char text[8]{};
    std::snprintf(text, sizeof(text), "%u%%", unsigned(*value));
    return text;
}

std::string tenths(std::uint64_t value, std::uint64_t unit) {
    const auto scaled = (value * 10U + unit / 2U) / unit;
    char text[24]{};
    std::snprintf(text, sizeof(text), "%llu.%llu", static_cast<unsigned long long>(scaled / 10U),
                  static_cast<unsigned long long>(scaled % 10U));
    return text;
}

std::string memory(bool available, std::uint32_t used, std::uint32_t total) {
    if (!available)
        return "RAM --";
    char text[24]{};
    const auto totalGB = (total + 512U) / 1024U;
    std::snprintf(text, sizeof(text), "RAM %s/%luG", tenths(used, 1024).c_str(),
                  static_cast<unsigned long>(totalGB));
    return text;
}

std::string mebibytes(std::uint32_t mib) {
    if (mib < 1024U) {
        char text[16]{};
        std::snprintf(text, sizeof(text), "%lu M", static_cast<unsigned long>(mib));
        return text;
    }
    return tenths(mib, 1024) + " G";
}

std::string rate(bool available, std::uint32_t kib) {
    if (!available)
        return "--";
    char text[20]{};
    if (kib < 1024)
        std::snprintf(text, sizeof(text), "%lu KB/s", static_cast<unsigned long>(kib));
    else if (kib < 1024U * 1024U)
        std::snprintf(text, sizeof(text), "%s MB/s", tenths(kib, 1024).c_str());
    else
        std::snprintf(text, sizeof(text), "%lu GB/s",
                      static_cast<unsigned long>(kib / (1024U * 1024U)));
    return text;
}

// Battery time as H:MM; macOS never reports more than a few days.
// Both disk rates in one unit so a single row can carry them.
std::string megabytes(std::uint32_t kib) {
    if (kib < 10U * 1024U)
        return tenths(kib, 1024);
    return std::to_string((kib + 512U) / 1024U);
}

std::string wifiSignal(std::int8_t dbm) {
    if (dbm >= -60)
        return "STRONG";
    if (dbm >= -70)
        return "GOOD";
    if (dbm >= -80)
        return "WEAK";
    return "VERY WEAK";
}

std::string duration(std::uint16_t minutes) {
    if (minutes / 60U > 99U)
        return "--";
    char text[8]{};
    std::snprintf(text, sizeof(text), "%u:%02u", unsigned(minutes / 60U), unsigned(minutes % 60U));
    return text;
}

std::string upper(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return value;
}

core::TextStyle textStyle(core::RgbColor color = core::palette::ink,
                          float scale = core::systemTextScale) {
    return {color, core::palette::bone, scale};
}

void drawRight(core::IDisplayAdapter& display, std::int32_t y, const std::string& text,
               core::RgbColor color = core::palette::ink) {
    display.drawText({core::rightAlignedTextX(text.c_str(), right), y}, text.c_str(),
                     textStyle(color));
}

void drawArrow(core::IDisplayAdapter& display, std::int32_t x, std::int32_t y, bool down) {
    // Direction arrows have stable geometry even if the font lacks arrow glyphs.
    if (down) {
        display.fillRectangle({x, y + 2}, 2, 8, core::palette::blue);
        display.fillRectangle({x - 2, y + 8}, 6, 2, core::palette::blue);
        display.fillRectangle({x - 1, y + 10}, 4, 2, core::palette::blue);
    } else {
        display.fillRectangle({x, y + 4}, 2, 8, core::palette::blue);
        display.fillRectangle({x - 2, y + 4}, 6, 2, core::palette::blue);
        display.fillRectangle({x - 1, y + 2}, 4, 2, core::palette::blue);
    }
}

// A round 6×6 Leaf dot marks a charging battery.
void drawChargingDot(core::IDisplayAdapter& display, std::int32_t x, std::int32_t y) {
    display.fillRectangle({x + 1, y}, 4, 6, core::palette::leaf);
    display.fillRectangle({x, y + 1}, 6, 4, core::palette::leaf);
}

enum class Series : std::uint8_t { Cpu, Download, Upload };

bool sampleValid(const MacStatusHistory::Sample& sample, Series series) {
    return series == Series::Cpu ? sample.cpuValid : sample.networkValid;
}

std::uint32_t sampleValue(const MacStatusHistory::Sample& sample, Series series) {
    switch (series) {
    case Series::Cpu:
        return sample.cpu;
    case Series::Download:
        return sample.downloadKiBps;
    case Series::Upload:
        return sample.uploadKiBps;
    }
    return 0;
}

std::uint32_t seriesScale(const MacStatusHistory& history, Series series) {
    if (series == Series::Cpu)
        return 100;
    std::uint32_t maximum = 64;
    for (std::size_t i = 0; i < history.size(); ++i)
        if (sampleValid(history.at(i), series))
            maximum = std::max(maximum, sampleValue(history.at(i), series));
    return maximum;
}

// A 60-second line that fills from the right edge; gaps break the line.
void drawSparkline(core::IDisplayAdapter& display, const MacStatusHistory& history, Series series,
                   std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height) {
    display.fillRectangle({x, y + height - 1}, width, 1, core::palette::pale);
    const auto count = history.size();
    if (count == 0)
        return;
    const auto scale = seriesScale(history, series);
    const auto span = static_cast<std::int32_t>(MacStatusHistory::capacity - 1);
    const auto column = [&](std::size_t index) {
        const auto slot = static_cast<std::int32_t>(MacStatusHistory::capacity - count + index);
        return x + slot * (width - 2) / span;
    };
    const auto row = [&](std::size_t index) {
        const auto value = std::min(sampleValue(history.at(index), series), scale);
        const auto range = static_cast<std::uint64_t>(height - 2);
        return y + height - 2 -
               static_cast<std::int32_t>((value * range + scale / 2U) / std::uint64_t(scale));
    };
    for (std::size_t i = 0; i < count; ++i) {
        if (!sampleValid(history.at(i), series))
            continue;
        const bool previous = i > 0 && sampleValid(history.at(i - 1), series);
        const bool next = i + 1 < count && sampleValid(history.at(i + 1), series);
        if (!previous && !next) {
            display.fillRectangle({column(i), row(i)}, 2, 2, core::palette::blue);
            continue;
        }
        if (!previous)
            continue;
        const auto x0 = column(i - 1);
        const auto x1 = column(i);
        const auto y0 = row(i - 1);
        const auto y1 = row(i);
        auto last = y0;
        for (auto c = x0; c <= x1; ++c) {
            const auto yc = x1 == x0 ? y1 : y0 + (y1 - y0) * (c - x0) / (x1 - x0);
            const auto top = std::min(last, yc);
            display.fillRectangle({c, top}, 2, std::max(last, yc) - top + 2, core::palette::blue);
            last = yc;
        }
    }
}

std::string sparkKey(const MacStatusHistory& history) {
    return std::to_string(history.generation());
}

// The left text is shortened so that at least one glyph of space separates it
// from the right-aligned value.
MacStatusRegion textRegion(std::int32_t y, std::int32_t height, std::string leftText,
                           std::string rightText, core::RgbColor rightColor = core::palette::ink) {
    if (!rightText.empty())
        leftText = core::fitSystemText(leftText, core::rightAlignedTextX(rightText.c_str(), right) -
                                                     left - core::systemTextWidth(" "));
    MacStatusRegion region{0, y, 240, height, leftText + "|" + rightText, {}};
    region.draw = [y, leftText, rightText, rightColor](core::IDisplayAdapter& display) {
        if (!leftText.empty())
            display.drawText({left, y + 2}, leftText.c_str(), textStyle());
        if (!rightText.empty())
            drawRight(display, y + 2, rightText, rightColor);
    };
    return region;
}

MacStatusRegion row(RowGrid grid, int index, std::string label, std::string value,
                    core::RgbColor color = core::palette::ink) {
    return textRegion(grid.top + index * grid.pitch, grid.pitch - 2, std::move(label),
                      std::move(value), color);
}

// A row whose label starts with a colour swatch that keys a bar above it.
MacStatusRegion swatchRow(RowGrid grid, int index, core::RgbColor swatch, std::string label,
                          std::string value) {
    const auto y = grid.top + index * grid.pitch;
    MacStatusRegion region{0, y, 240, grid.pitch - 2, label + "|" + value, {}};
    region.draw = [y, swatch, label, value](core::IDisplayAdapter& display) {
        display.fillRectangle({left, y + 4}, 5, 5, swatch);
        display.drawText({left + 9, y + 2}, label.c_str(), textStyle());
        drawRight(display, y + 2, value);
    };
    return region;
}

// `ruleY` draws the separator under the page's summary; 0 means none.
MacStatusRegion header(const char* title, MacStatusPage page, std::int32_t ruleY) {
    char counter[8]{};
    std::snprintf(counter, sizeof(counter), "%u/%u", unsigned(page) + 1U,
                  unsigned(macStatusPageCount));
    const std::string name = title;
    const std::string pageText = counter;
    MacStatusRegion region{0, 0, 240, 16, name + pageText, {}};
    region.draw = [name, pageText, ruleY](core::IDisplayAdapter& display) {
        display.drawText({left, 3}, name.c_str(), textStyle());
        drawRight(display, 3, pageText, core::palette::ordinal);
        display.fillRectangle({0, 15}, 240, 1, core::palette::pale);
        if (ruleY > 0)
            display.fillRectangle({0, ruleY}, 240, 1, core::palette::pale);
    };
    return region;
}

MacStatusRegion largeValue(std::int32_t width, std::string text) {
    MacStatusRegion region{0, 17, width, 24, "L" + text, {}};
    region.draw = [text](core::IDisplayAdapter& display) {
        display.drawText({left, 21}, text.c_str(), textStyle(core::palette::ink, largeTextScale));
    };
    return region;
}

void addCpuPage(std::vector<MacStatusRegion>& regions, const services::MacStatusSnapshot& snapshot,
                const services::MacStatusDetails& details, const MacStatusHistory& history) {
    const bool fresh = snapshot.freshness == MacStatusFreshness::Fresh;
    const bool detail = details.freshness == MacStatusFreshness::Fresh;
    regions.push_back(largeValue(70, fresh && snapshot.cpuAvailable
                                         ? percentValue(snapshot.cpuPercent)
                                         : std::string("--")));
    MacStatusRegion spark{72, 17, 168, 24, "S" + sparkKey(history), {}};
    spark.draw = [&history](core::IDisplayAdapter& display) {
        drawSparkline(display, history, Series::Cpu, 72, 19, 160, 21);
    };
    regions.push_back(std::move(spark));
    const auto opt = [&](const auto& value) { return detail ? value : std::nullopt; };
    regions.push_back(textRegion(42, 14, "CORES",
                                 "FAST " + percentValue(opt(details.performancePercent)) +
                                     "  EFF " + percentValue(opt(details.efficiencyPercent))));
    regions.push_back(textRegion(57, 14, "GPU", percentValue(opt(details.gpuPercent))));
    constexpr RowGrid apps{75, 15};
    if (!detail || !details.appsAvailable || details.appCount == 0) {
        // No app reached 1% of the machine: say so instead of a blank list.
        regions.push_back(row(apps, 0, "APPS", detail && details.appsAvailable ? "IDLE" : "--"));
        for (int index = 1; index < 4; ++index)
            regions.push_back(row(apps, index, "", ""));
        return;
    }
    for (int index = 0; index < 4; ++index) {
        if (index >= details.appCount) {
            regions.push_back(row(apps, index, "", ""));
            continue;
        }
        const auto& app = details.apps[static_cast<std::size_t>(index)];
        char ordinal[4]{};
        std::snprintf(ordinal, sizeof(ordinal), "%02d", index + 1);
        const std::string value = percentValue(app.percent);
        const auto nameWidth = right - core::systemTextWidth(value.c_str()) - 8 - 30;
        const std::string name = core::fitSystemText(upper(app.name), nameWidth);
        const std::string number = ordinal;
        const auto y = apps.top + index * apps.pitch;
        MacStatusRegion region{0, y, 240, apps.pitch - 2, number + name + value, {}};
        region.draw = [y, number, name, value](core::IDisplayAdapter& display) {
            display.drawText({left, y + 2}, number.c_str(), textStyle(core::palette::ordinal));
            display.drawText({30, y + 2}, name.c_str(), textStyle());
            drawRight(display, y + 2, value);
        };
        regions.push_back(std::move(region));
    }
}

void addPowerPage(std::vector<MacStatusRegion>& regions,
                  const services::MacStatusSnapshot& snapshot,
                  const services::MacStatusDetails& details) {
    using services::MacPowerSource;
    const bool fresh = snapshot.freshness == MacStatusFreshness::Fresh;
    const bool battery = fresh && snapshot.batteryAvailable;
    if (battery) {
        regions.push_back(largeValue(110, percentValue(snapshot.batteryPercent)));
    } else {
        const std::string text = fresh ? "NO BATTERY" : "--";
        MacStatusRegion region{0, 17, 110, 24, "L" + text, {}};
        region.draw = [text](core::IDisplayAdapter& display) {
            display.drawText({left, 25}, text.c_str(), textStyle());
        };
        regions.push_back(std::move(region));
    }
    const auto source = fresh ? snapshot.powerSource : MacPowerSource::Unknown;
    std::string state;
    std::string time;
    auto stateColor = core::palette::ink;
    const bool minutes = fresh && snapshot.batteryMinutesAvailable;
    switch (source) {
    case MacPowerSource::Charging:
        state = "CHARGING";
        stateColor = core::palette::blue;
        if (minutes)
            time = "FULL IN " + duration(snapshot.batteryMinutes);
        break;
    case MacPowerSource::Battery:
        state = "ON BATTERY";
        if (minutes)
            time = "EMPTY IN " + duration(snapshot.batteryMinutes);
        break;
    case MacPowerSource::AcPower:
        state = "ON AC";
        break;
    case MacPowerSource::Unknown:
        break;
    }
    MacStatusRegion status{110, 17, 130, 26, state + "|" + time, {}};
    status.draw = [state, time, stateColor](core::IDisplayAdapter& display) {
        if (!state.empty())
            drawRight(display, 19, state, stateColor);
        if (!time.empty())
            drawRight(display, 31, time);
    };
    regions.push_back(std::move(status));
    const int filled = battery ? (snapshot.batteryPercent + 2) / 5 : 0;
    MacStatusRegion meter{0, 44, 240, 10, "M" + std::to_string(battery ? filled : -1), {}};
    meter.draw = [filled](core::IDisplayAdapter& display) {
        for (int segment = 0; segment < 20; ++segment) {
            const auto x0 = left + segment * 224 / 20;
            const auto x1 = left + (segment + 1) * 224 / 20;
            display.fillRectangle({x0, 46}, x1 - x0 - 2, 7,
                                  segment < filled ? core::palette::blue : core::palette::pale);
        }
    };
    regions.push_back(std::move(meter));
    const bool detail = details.freshness == MacStatusFreshness::Fresh;
    std::string draw = "--";
    if (detail && details.systemDrawDeciwatts)
        draw = tenths(*details.systemDrawDeciwatts, 10) + " W";
    std::string adapter = "--";
    if (detail && details.adapterWatts)
        adapter = std::to_string(*details.adapterWatts) + " W";
    constexpr RowGrid rows{59, 15};
    regions.push_back(row(rows, 0, "POWER USE", draw));
    regions.push_back(row(rows, 1, "CHARGER", adapter));
    regions.push_back(
        row(rows, 2, "BATTERY HEALTH", detail ? percentValue(details.healthPercent) : "--"));
    regions.push_back(row(rows, 3, "CHARGE CYCLES",
                          detail && details.cycleCount ? std::to_string(*details.cycleCount)
                                                       : std::string("--")));
    if (detail && details.peripheralPercent) {
        const bool low = *details.peripheralPercent <= 20;
        const auto value =
            (low ? std::string("LOW ") : std::string()) + percentValue(details.peripheralPercent);
        regions.push_back(row(rows, 4, core::fitSystemText(upper(details.peripheralName), 150),
                              value, low ? core::palette::vermilion : core::palette::ink));
    } else {
        regions.push_back(row(rows, 4, "", ""));
    }
}

void addNetworkPage(std::vector<MacStatusRegion>& regions,
                    const services::MacStatusSnapshot& snapshot,
                    const services::MacStatusDetails& details, const MacStatusHistory& history) {
    const bool network =
        snapshot.freshness == MacStatusFreshness::Fresh && snapshot.networkAvailable;
    const auto lane = [&](std::int32_t y, bool down) {
        const auto text = rate(network, down ? snapshot.downloadKiBps : snapshot.uploadKiBps);
        MacStatusRegion region{0, y, 240, 17, text + "S" + sparkKey(history), {}};
        region.draw = [&history, y, down, text](core::IDisplayAdapter& display) {
            drawArrow(display, left + 3, y + 1, down);
            display.drawText({22, y + 3}, text.c_str(), textStyle());
            drawSparkline(display, history, down ? Series::Download : Series::Upload, 112, y + 1,
                          120, 15);
        };
        return region;
    };
    regions.push_back(lane(17, true));
    regions.push_back(lane(36, false));
    const bool detail = details.freshness == MacStatusFreshness::Fresh;
    const auto milliseconds = [&](const std::optional<std::uint16_t>& value) {
        return detail && value ? std::to_string(*value) + " MS" : std::string("--");
    };
    regions.push_back(row(roomyRows, 0, "INTERNET PING", milliseconds(details.internetRttMs)));
    regions.push_back(row(roomyRows, 1, "ROUTER PING", milliseconds(details.routerRttMs)));
    regions.push_back(
        row(roomyRows, 2, "WI-FI SIGNAL",
            detail && details.wifiRssiDbm ? wifiSignal(*details.wifiRssiDbm) : std::string("--")));
    regions.push_back(row(roomyRows, 3, "WI-FI SPEED",
                          detail && details.wifiLinkMbps
                              ? std::to_string(*details.wifiLinkMbps) + " MBIT/S"
                              : std::string("--")));
}

void addMemoryPage(std::vector<MacStatusRegion>& regions,
                   const services::MacStatusSnapshot& snapshot,
                   const services::MacStatusDetails& details) {
    const bool fresh = snapshot.freshness == MacStatusFreshness::Fresh;
    const bool detail = details.freshness == MacStatusFreshness::Fresh;
    std::string ram = "--";
    if (fresh && snapshot.memoryAvailable)
        ram = tenths(snapshot.memoryUsedMiB, 1024) + " / " +
              std::to_string((snapshot.memoryTotalMiB + 512U) / 1024U) + " G";
    regions.push_back(textRegion(17, 13, "RAM", ram));
    const bool split = detail && details.memorySplitAvailable;
    std::uint64_t total =
        split ? std::uint64_t(details.appMiB) + details.wiredMiB + details.compressedMiB : 0;
    if (fresh && snapshot.memoryAvailable)
        total = std::max<std::uint64_t>(total, snapshot.memoryTotalMiB);
    std::array<std::int32_t, 3> widths{};
    if (split && total > 0) {
        const std::array<std::uint64_t, 3> parts{details.appMiB, details.wiredMiB,
                                                 details.compressedMiB};
        for (std::size_t i = 0; i < parts.size(); ++i)
            widths[i] = static_cast<std::int32_t>(parts[i] * 224U / total);
    }
    MacStatusRegion bar{0,
                        31,
                        240,
                        10,
                        "B" + std::to_string(widths[0]) + "," + std::to_string(widths[1]) + "," +
                            std::to_string(widths[2]),
                        {}};
    bar.draw = [widths](core::IDisplayAdapter& display) {
        display.fillRectangle({left, 32}, 224, 8, core::palette::pale);
        std::int32_t x = left;
        const core::RgbColor colors[] = {core::palette::blue, core::palette::ink,
                                         core::palette::ordinal};
        for (std::size_t i = 0; i < widths.size(); ++i) {
            if (widths[i] > 0)
                display.fillRectangle({x, 32}, widths[i], 8, colors[i]);
            x += widths[i];
        }
    };
    regions.push_back(std::move(bar));
    const auto gigabytes = [&](std::uint32_t mib) {
        return split ? tenths(mib, 1024) + " G" : std::string("--");
    };
    constexpr RowGrid rows{41, 15};
    regions.push_back(swatchRow(rows, 0, core::palette::blue, "APPS", gigabytes(details.appMiB)));
    regions.push_back(swatchRow(rows, 1, core::palette::ink, "MACOS", gigabytes(details.wiredMiB)));
    regions.push_back(
        swatchRow(rows, 2, core::palette::ordinal, "COMPRESSED", gigabytes(details.compressedMiB)));
    std::string ssd = "--";
    if (detail && details.ssdAvailable)
        ssd =
            std::to_string(details.ssdFreeGB) + " G / " + std::to_string(details.ssdTotalGB) + " G";
    std::string disk = "--";
    if (detail && details.diskRatesAvailable)
        disk = "READ " + megabytes(details.diskReadKiBps) + " WRITE " +
               megabytes(details.diskWriteKiBps) + " MB/s";
    regions.push_back(row(rows, 3, "SWAPPED TO DISK",
                          detail && details.swapUsedMiB ? mebibytes(*details.swapUsedMiB) : "--"));
    regions.push_back(row(rows, 4, "DISK FREE", ssd));
    regions.push_back(row(rows, 5, "DISK", disk));
}
} // namespace

MacStatusPresentation formatMacStatus(const services::MacStatusSnapshot& snapshot) {
    MacStatusPresentation result{};
    const bool fresh = snapshot.freshness == services::MacStatusFreshness::Fresh;
    result.labels[0] = percent("CPU", fresh && snapshot.cpuAvailable, snapshot.cpuPercent);
    result.labels[1] =
        memory(fresh && snapshot.memoryAvailable, snapshot.memoryUsedMiB, snapshot.memoryTotalMiB);
    result.labels[2] = percent("DISK", fresh && snapshot.diskAvailable, snapshot.diskUsedPercent);
    if (fresh && snapshot.diskAvailable)
        result.labels[2] += " USED";
    result.labels[3] = percent("BAT", fresh && snapshot.batteryAvailable, snapshot.batteryPercent);
    if (fresh && snapshot.batteryAvailable) {
        using services::MacPowerSource;
        const bool timed = snapshot.powerSource == MacPowerSource::Charging ||
                           snapshot.powerSource == MacPowerSource::Battery;
        if (timed && snapshot.batteryMinutesAvailable)
            result.labels[3] += " " + duration(snapshot.batteryMinutes);
        else if (snapshot.powerSource == MacPowerSource::AcPower)
            result.labels[3] += " AC";
        result.charging = snapshot.powerSource == MacPowerSource::Charging;
    }
    result.labels[4] = rate(fresh && snapshot.networkAvailable, snapshot.downloadKiBps);
    result.labels[5] = rate(fresh && snapshot.networkAvailable, snapshot.uploadKiBps);
    // Severity: 1 is healthy (Leaf), 2 needs attention (Vermilion), 0 is neutral.
    const auto pressure = fresh ? snapshot.memoryPressure : services::MacMemoryPressure::Unknown;
    switch (pressure) {
    case services::MacMemoryPressure::Normal:
        result.labels[6] = "MEMORY OK";
        result.severity[0] = 1;
        break;
    case services::MacMemoryPressure::Warning:
        result.labels[6] = "MEMORY TIGHT";
        break;
    case services::MacMemoryPressure::Critical:
        result.labels[6] = "MEMORY CRITICAL";
        result.severity[0] = 2;
        break;
    default:
        result.labels[6] = "MEMORY --";
        break;
    }
    const auto thermal = fresh ? snapshot.thermalState : services::MacThermalState::Unknown;
    switch (thermal) {
    case services::MacThermalState::Normal:
        result.labels[7] = "TEMP OK";
        result.severity[1] = 1;
        break;
    case services::MacThermalState::Fair:
        result.labels[7] = "TEMP WARM";
        break;
    case services::MacThermalState::Serious:
        result.labels[7] = "TEMP HOT";
        result.severity[1] = 2;
        break;
    case services::MacThermalState::Critical:
        result.labels[7] = "TEMP CRITICAL";
        result.severity[1] = 2;
        break;
    default:
        result.labels[7] = "TEMP --";
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
                         int index, const services::MacStatusHistory& history) {
    using namespace core;
    const int x = index % 2 == 0 ? 8 : 124;
    const int y = index < 2 ? 10 : index < 4 ? 47 : index < 6 ? 86 : 113;
    const int height = index < 4 ? 31 : index < 6 ? 18 : 17;
    display.fillRectangle({x, y}, 108, height, palette::bone);
    int textX = x;
    if (index == 4 || index == 5) {
        drawArrow(display, x + 3, y, index == 4);
        textX += 14;
    }
    auto color = palette::ink;
    if (index >= 6) {
        const auto severity = value.severity[static_cast<std::size_t>(index - 6)];
        if (severity == 2)
            color = palette::vermilion;
        else if (severity == 1)
            color = palette::leaf;
    }
    display.drawText({textX, y + 1}, value.labels[index].c_str(),
                     {color, palette::bone, core::systemTextScale});
    if (index == 3 && value.charging)
        drawChargingDot(display, x + 101, y + 3);
    if (index == 0) {
        drawSparkline(display, history, Series::Cpu, x, y + 16, 100, 14);
    } else if (index < 4) {
        display.fillRectangle({x, y + 22}, 100, 6, palette::pale);
        if (value.bars[index] >= 0)
            display.fillRectangle({x, y + 22}, value.bars[index], 6, palette::blue);
    }
}

void drawMacStatusPageDots(core::IDisplayAdapter& display, MacStatusPage page) {
    for (std::uint8_t index = 0; index < macStatusPageCount; ++index)
        display.fillRectangle({108 + index * 6, 131}, 3, 3,
                              index == static_cast<std::uint8_t>(page) ? core::palette::ink
                                                                       : core::palette::pale);
}

std::vector<MacStatusRegion> layoutMacStatusPage(MacStatusPage page,
                                                 const services::MacStatusSnapshot& snapshot,
                                                 const services::MacStatusDetails& details,
                                                 const services::MacStatusHistory& history) {
    std::vector<MacStatusRegion> regions;
    switch (page) {
    case MacStatusPage::Overview:
        break;
    case MacStatusPage::Cpu:
        regions.push_back(header("CPU / TOP APPS", page, 73));
        addCpuPage(regions, snapshot, details, history);
        break;
    case MacStatusPage::Power:
        regions.push_back(header("POWER", page, 57));
        addPowerPage(regions, snapshot, details);
        break;
    case MacStatusPage::Network:
        regions.push_back(header("NETWORK", page, 57));
        addNetworkPage(regions, snapshot, details, history);
        break;
    case MacStatusPage::Memory:
        regions.push_back(header("MEMORY / DISK", page, 0));
        addMemoryPage(regions, snapshot, details);
        break;
    }
    return regions;
}

services::MacDetailGroup macStatusDetailGroup(MacStatusPage page) {
    switch (page) {
    case MacStatusPage::Cpu:
        return services::MacDetailGroup::Cpu;
    case MacStatusPage::Power:
        return services::MacDetailGroup::Power;
    case MacStatusPage::Network:
        return services::MacDetailGroup::Network;
    case MacStatusPage::Memory:
        return services::MacDetailGroup::Memory;
    case MacStatusPage::Overview:
        break;
    }
    return services::MacDetailGroup::None;
}

} // namespace cardputer_hub::apps
