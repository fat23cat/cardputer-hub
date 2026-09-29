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
constexpr std::int32_t ruleY = 57;
constexpr std::int32_t firstRowY = 60;
constexpr std::int32_t rowPitch = 18;
constexpr float largeTextScale = 2.0f;

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

void drawBolt(core::IDisplayAdapter& display, std::int32_t x, std::int32_t y) {
    display.fillRectangle({x + 3, y}, 3, 4, core::palette::blue);
    display.fillRectangle({x + 1, y + 4}, 6, 2, core::palette::blue);
    display.fillRectangle({x + 1, y + 6}, 3, 4, core::palette::blue);
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

MacStatusRegion row(int index, std::string label, std::string value,
                    core::RgbColor color = core::palette::ink) {
    return textRegion(firstRowY + index * rowPitch, 16, std::move(label), std::move(value), color);
}

MacStatusRegion header(const char* title, MacStatusPage page) {
    char counter[8]{};
    std::snprintf(counter, sizeof(counter), "%u/%u", unsigned(page) + 1U,
                  unsigned(macStatusPageCount));
    const std::string name = title;
    const std::string pageText = counter;
    MacStatusRegion region{0, 0, 240, 16, name + pageText, {}};
    region.draw = [name, pageText](core::IDisplayAdapter& display) {
        display.drawText({left, 3}, name.c_str(), textStyle());
        drawRight(display, 3, pageText, core::palette::ordinal);
        display.fillRectangle({0, 15}, 240, 1, core::palette::pale);
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
    // Compact labels keep the widest line (all 100%) clear of LOAD.
    std::string cores = "P" + percentValue(opt(details.performancePercent)) + " E" +
                        percentValue(opt(details.efficiencyPercent)) + " GPU" +
                        percentValue(opt(details.gpuPercent));
    std::string load = "LOAD --";
    if (detail && details.loadCenti)
        load = "LOAD " + tenths(*details.loadCenti, 100);
    regions.push_back(textRegion(42, 14, cores, load));
    if (!detail || !details.appsAvailable || details.appCount == 0) {
        // No app reached 1% of the machine: say so instead of a blank list.
        regions.push_back(row(0, "APPS", detail && details.appsAvailable ? "IDLE" : "--"));
        for (int index = 1; index < 4; ++index)
            regions.push_back(row(index, "", ""));
        return;
    }
    for (int index = 0; index < 4; ++index) {
        if (index >= details.appCount) {
            regions.push_back(row(index, "", ""));
            continue;
        }
        const auto& app = details.apps[static_cast<std::size_t>(index)];
        char ordinal[4]{};
        std::snprintf(ordinal, sizeof(ordinal), "%02d", index + 1);
        const std::string value = percentValue(app.percent);
        const auto nameWidth = right - core::systemTextWidth(value.c_str()) - 8 - 30;
        const std::string name = core::fitSystemText(upper(app.name), nameWidth);
        const std::string number = ordinal;
        const auto y = firstRowY + index * rowPitch;
        MacStatusRegion region{0, y, 240, 16, number + name + value, {}};
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
            time = "LEFT " + duration(snapshot.batteryMinutes);
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
    std::string health = detail ? percentValue(details.healthPercent) : "--";
    if (detail && details.cycleCount)
        health += "  " + std::to_string(*details.cycleCount) + " CYC";
    regions.push_back(row(0, "SYSTEM DRAW", draw));
    regions.push_back(row(1, "ADAPTER", adapter));
    regions.push_back(row(2, "HEALTH", health));
    if (detail && details.peripheralPercent) {
        const bool low = *details.peripheralPercent <= 20;
        const auto value =
            (low ? std::string("LOW ") : std::string()) + percentValue(details.peripheralPercent);
        regions.push_back(row(3, core::fitSystemText(upper(details.peripheralName), 150), value,
                              low ? core::palette::vermilion : core::palette::ink));
    } else {
        regions.push_back(row(3, "", ""));
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
    std::string wifi = "--";
    if (detail && details.wifiRssiDbm) {
        wifi = std::to_string(*details.wifiRssiDbm) + " DBM";
        if (details.wifiLinkMbps)
            wifi += " " + std::to_string(*details.wifiLinkMbps) + " MBPS";
    }
    std::string vpn = "--";
    auto vpnColor = core::palette::ink;
    if (detail && details.vpnActive) {
        vpn = *details.vpnActive ? "ON" : "OFF";
        if (*details.vpnActive)
            vpnColor = core::palette::blue;
    }
    regions.push_back(row(0, "INTERNET", milliseconds(details.internetRttMs)));
    regions.push_back(row(1, "ROUTER", milliseconds(details.routerRttMs)));
    regions.push_back(row(2, "WI-FI", wifi));
    regions.push_back(row(3, "VPN", vpn, vpnColor));
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
    const auto part = [&](const char* name, std::uint32_t mib) {
        return std::string(name) + " " + (split ? tenths(mib, 1024) : std::string("--"));
    };
    const std::array<std::string, 3> legend{part("APP", details.appMiB),
                                            part("WIRED", details.wiredMiB),
                                            part("COMP", details.compressedMiB)};
    MacStatusRegion key{0, 42, 240, 13, legend[0] + legend[1] + legend[2], {}};
    key.draw = [legend](core::IDisplayAdapter& display) {
        const std::int32_t xs[] = {left, 76, 152};
        const core::RgbColor colors[] = {core::palette::blue, core::palette::ink,
                                         core::palette::ordinal};
        for (std::size_t i = 0; i < legend.size(); ++i) {
            display.fillRectangle({xs[i], 46}, 5, 5, colors[i]);
            display.drawText({xs[i] + 9, 44}, legend[i].c_str(), textStyle());
        }
    };
    regions.push_back(std::move(key));
    std::string ssd = "--";
    if (detail && details.ssdAvailable)
        ssd =
            std::to_string(details.ssdFreeGB) + " G / " + std::to_string(details.ssdTotalGB) + " G";
    const bool rates = detail && details.diskRatesAvailable;
    regions.push_back(
        row(0, "SWAP", detail && details.swapUsedMiB ? mebibytes(*details.swapUsedMiB) : "--"));
    regions.push_back(row(1, "SSD FREE", ssd));
    regions.push_back(row(2, "DISK READ", rate(rates, details.diskReadKiBps)));
    regions.push_back(row(3, "DISK WRITE", rate(rates, details.diskWriteKiBps)));
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
        if (value.labels[index].find("CRIT") != std::string::npos ||
            value.labels[index].find("SERIOUS") != std::string::npos)
            color = palette::vermilion;
        else if (value.labels[index].find("NORMAL") != std::string::npos)
            color = palette::leaf;
    }
    display.drawText({textX, y + 1}, value.labels[index].c_str(),
                     {color, palette::bone, core::systemTextScale});
    if (index == 3 && value.charging)
        drawBolt(display, x + 100, y + 1);
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
        regions.push_back(header("CPU / TOP APPS", page));
        addCpuPage(regions, snapshot, details, history);
        break;
    case MacStatusPage::Power:
        regions.push_back(header("POWER", page));
        addPowerPage(regions, snapshot, details);
        break;
    case MacStatusPage::Network:
        regions.push_back(header("NETWORK", page));
        addNetworkPage(regions, snapshot, details, history);
        break;
    case MacStatusPage::Memory:
        regions.push_back(header("MEMORY / DISK", page));
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
