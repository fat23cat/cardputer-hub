import CoreWLAN
import Darwin
import Foundation
import IOKit
import IOKit.ps
import SystemConfiguration
import CompanionCore

/// Answers SYSTEM_DETAILS for the visible Cardputer page. A request returns the
/// group's cached sample at once and starts a refresh on a background queue,
/// so BLE and the menu bar never wait for process scans or IORegistry reads.
/// Everything below `collect` runs only on that queue.
final class MacSystemDetailsCollector: SystemDetailsCollecting {
    /// A cached group older than this is not reported (the page shows `--`).
    static let cacheLifetime: TimeInterval = 5

    private let queue = DispatchQueue(label: "CardputerCompanion.SystemDetails", qos: .utility)
    private let lock = NSLock()
    private var cache: [SystemDetailsGroup: (sample: SystemDetailsSample, time: TimeInterval)] = [:]
    private var refreshing: Set<SystemDetailsGroup> = []

    private let clusters = MacSystemDetailsCollector.cpuClusters()
    private var cpuPrevious: (ticks: [CPUTimeTicks], time: TimeInterval)?
    private let apps = TopAppsTracker(logicalCPUs: ProcessInfo.processInfo.activeProcessorCount)
    /// Executable paths by pid and start time; a reused pid gets a new entry.
    private var paths: [ProcessKey: String] = [:]
    private var disk = CounterRate()
    private let probes = NetworkProbes()

    private struct ProcessKey: Hashable {
        let pid: pid_t
        let start: UInt64
    }

    func collect(_ group: SystemDetailsGroup) -> SystemDetailsSample? {
        let now = ProcessInfo.processInfo.systemUptime
        lock.lock()
        let cached = cache[group]
        let start = refreshing.insert(group).inserted
        lock.unlock()
        if start {
            queue.async { [weak self] in self?.refresh(group) }
        }
        guard let cached, now - cached.time <= Self.cacheLifetime else { return nil }
        return cached.sample
    }

    private func refresh(_ group: SystemDetailsGroup) {
        let sample = read(group, now: ProcessInfo.processInfo.systemUptime)
        let time = ProcessInfo.processInfo.systemUptime
        lock.lock()
        cache[group] = (sample, time)
        refreshing.remove(group)
        lock.unlock()
    }

    private func read(_ group: SystemDetailsGroup, now: TimeInterval) -> SystemDetailsSample {
        var sample = SystemDetailsSample(group: group)
        switch group {
        case .cpu:
            (sample.performancePercent, sample.efficiencyPercent) = clusterUsage(now: now)
            sample.gpuPercent = gpuUtilization()
            sample.loadCenti = loadAverage()
            sample.apps = apps.sample(processTimes(), at: now)
        case .power:
            let battery = MacSystemSources.registryValues(
                "AppleSmartBattery",
                keys: ["PowerTelemetryData", "ExternalConnected", "Amperage", "Voltage",
                       "BatteryData", "DesignCapacity", "NominalChargeCapacity",
                       "AppleRawMaxCapacity", "CycleCount"]).first
            sample.systemDrawDeciwatts = battery.flatMap(Self.systemDraw)
            sample.adapterWatts = adapterWatts()
            sample.healthPercent = battery.flatMap(Self.health)
            sample.cycleCount = (battery?["CycleCount"] as? NSNumber).map { UInt16(clamping: $0.intValue) }
            sample.peripheral = lowestPeripheral()
        case .network:
            let primary = MacSystemSources.primaryNetwork()
            let rtt = probes.results(router: primary.router, now: now)
            sample.internetRttMs = rtt.internet
            sample.routerRttMs = rtt.router
            if let wifi = CWWiFiClient.shared().interface(), wifi.powerOn(), wifi.rssiValue() != 0 {
                sample.wifiRssiDbm = Int8(clamping: wifi.rssiValue())
                let rate = wifi.transmitRate()
                sample.wifiLinkMbps = rate > 0 ? UInt16(clamping: Int(rate.rounded())) : nil
            }
        case .memory:
            sample.memorySplit = memorySplit()
            sample.swapUsedMiB = swapUsed()
            sample.ssd = ssdSpace()
            let counters = diskCounters()
            sample.diskRates = counters.flatMap { disk.sample(read: $0.read, write: $0.write, at: now) }
        }
        return sample
    }

    // MARK: CPU

    /// Cluster type ("P" or "E") for each logical CPU, from the IORegistry.
    private static func cpuClusters() -> [Character] {
        var result: [Int: Character] = [:]
        for properties in MacSystemSources.registryValues("IOPlatformDevice",
                                                          keys: ["logical-cpu-id", "cluster-type"]) {
            guard let id = (properties["logical-cpu-id"] as? NSNumber)?.intValue,
                  let data = properties["cluster-type"] as? Data,
                  let first = data.first.map({ Character(UnicodeScalar($0)) }),
                  first == "P" || first == "E" else { continue }
            result[id] = first
        }
        let count = ProcessInfo.processInfo.processorCount
        guard result.count == count else { return [] }
        return (0..<count).compactMap { result[$0] }
    }

    private func clusterUsage(now: TimeInterval) -> (UInt8?, UInt8?) {
        var cpuCount: natural_t = 0
        var info: processor_info_array_t?
        var infoCount: mach_msg_type_number_t = 0
        guard host_processor_info(mach_host_self(), PROCESSOR_CPU_LOAD_INFO, &cpuCount, &info,
                                  &infoCount) == KERN_SUCCESS, let info else { return (nil, nil) }
        defer {
            vm_deallocate(mach_task_self_, vm_address_t(UInt(bitPattern: info)),
                          vm_size_t(Int(infoCount) * MemoryLayout<integer_t>.stride))
        }
        let states = Int(CPU_STATE_MAX)
        let ticks = (0..<Int(cpuCount)).map { cpu -> CPUTimeTicks in
            let base = cpu * states
            return CPUTimeTicks(user: UInt64(UInt32(bitPattern: info[base + Int(CPU_STATE_USER)])),
                                system: UInt64(UInt32(bitPattern: info[base + Int(CPU_STATE_SYSTEM)])),
                                idle: UInt64(UInt32(bitPattern: info[base + Int(CPU_STATE_IDLE)])),
                                nice: UInt64(UInt32(bitPattern: info[base + Int(CPU_STATE_NICE)])))
        }
        defer { cpuPrevious = (ticks, now) }
        guard let previous = cpuPrevious, now - previous.time <= TopAppsTracker.baselineLimit,
              !clusters.isEmpty else { return (nil, nil) }
        let usage = ClusterUsage.percents(previous: previous.ticks, current: ticks, clusters: clusters)
        return (usage.performance, usage.efficiency)
    }

    private func gpuUtilization() -> UInt8? {
        for properties in MacSystemSources.registryValues("IOAccelerator", keys: ["PerformanceStatistics"]) {
            if let statistics = properties["PerformanceStatistics"] as? [String: Any],
               let value = (statistics["Device Utilization %"] as? NSNumber)?.intValue {
                return UInt8(clamping: min(100, max(0, value)))
            }
        }
        return nil
    }

    private func loadAverage() -> UInt16? {
        var loads = [Double](repeating: 0, count: 1)
        guard getloadavg(&loads, 1) == 1 else { return nil }
        return UInt16(clamping: Int((loads[0] * 100).rounded()))
    }

    private func processTimes() -> [ProcessCPUTime] {
        let capacity = proc_listallpids(nil, 0)
        guard capacity > 0 else { return [] }
        var pids = [pid_t](repeating: 0, count: Int(capacity) + 64)
        let count = proc_listallpids(&pids, Int32(pids.count * MemoryLayout<pid_t>.size))
        guard count > 0 else { return [] }
        var timebase = mach_timebase_info_data_t()
        mach_timebase_info(&timebase)
        var result: [ProcessCPUTime] = []
        result.reserveCapacity(Int(count))
        var path = [CChar](repeating: 0, count: Int(MAXPATHLEN) * 4)
        var seen: [ProcessKey: String] = [:]
        for pid in pids.prefix(Int(count)) where pid > 0 {
            var usage = rusage_info_v2()
            let status = withUnsafeMutablePointer(to: &usage) {
                $0.withMemoryRebound(to: rusage_info_t?.self, capacity: 1) {
                    proc_pid_rusage(pid, RUSAGE_INFO_V2, $0)
                }
            }
            // Processes of other users are not readable without root; skip them.
            guard status == 0 else { continue }
            let key = ProcessKey(pid: pid, start: usage.ri_proc_start_abstime)
            let executable: String
            if let known = paths[key] {
                executable = known
            } else {
                guard proc_pidpath(pid, &path, UInt32(path.count)) > 0 else { continue }
                executable = String(cString: path)
            }
            seen[key] = executable
            let ticks = usage.ri_user_time + usage.ri_system_time
            let nanoseconds = ticks.multipliedReportingOverflow(by: UInt64(timebase.numer))
            guard !nanoseconds.overflow, timebase.denom > 0 else { continue }
            result.append(ProcessCPUTime(pid: pid, path: executable,
                                         cpuNanoseconds: nanoseconds.partialValue / UInt64(timebase.denom)))
        }
        paths = seen
        return result
    }

    // MARK: Power

    private static func systemDraw(_ battery: [String: Any]) -> UInt16? {
        if let telemetry = battery["PowerTelemetryData"] as? [String: Any],
           let milliwatts = (telemetry["SystemLoad"] as? NSNumber)?.int64Value, milliwatts > 0 {
            return UInt16(clamping: (milliwatts + 50) / 100)
        }
        guard (battery["ExternalConnected"] as? Bool) != true,
              let amperage = (battery["Amperage"] as? NSNumber)?.int64Value,
              let voltage = (battery["Voltage"] as? NSNumber)?.int64Value,
              amperage < 0, voltage > 0 else { return nil }
        return UInt16(clamping: (-amperage * voltage + 50_000) / 100_000)
    }

    private static func health(_ battery: [String: Any]) -> UInt8? {
        let data = battery["BatteryData"] as? [String: Any] ?? [:]
        func number(_ key: String) -> Int? {
            ((data[key] ?? battery[key]) as? NSNumber)?.intValue
        }
        guard let design = number("DesignCapacity"), design > 0,
              let nominal = number("NominalChargeCapacity") ?? number("AppleRawMaxCapacity"),
              nominal > 0 else { return nil }
        return UInt8(clamping: min(100, (nominal * 100 + design / 2) / design))
    }

    private func adapterWatts() -> UInt8? {
        guard let details = IOPSCopyExternalPowerAdapterDetails()?.takeRetainedValue() as? [String: Any],
              let watts = (details[kIOPSPowerAdapterWattsKey] as? NSNumber)?.intValue, watts > 0
        else { return nil }
        return UInt8(clamping: watts)
    }

    private func lowestPeripheral() -> PeripheralBattery? {
        MacSystemSources.registryValues("AppleDeviceManagementHIDEventService",
                                        keys: ["BatteryPercent", "Product"])
            .compactMap { properties -> PeripheralBattery? in
                guard let percent = (properties["BatteryPercent"] as? NSNumber)?.intValue,
                      (0...100).contains(percent),
                      let product = properties["Product"] as? String else { return nil }
                return PeripheralBattery(
                    name: DisplayName.sanitize(product, limit: SystemDetailsSample.maxPeripheralName),
                    percent: UInt8(percent))
            }
            .min { $0.percent < $1.percent }
    }

    // MARK: Memory and disk

    private func memorySplit() -> MemorySplit? {
        guard let stats = MacSystemSources.vmStatistics() else { return nil }
        let page = UInt64(vm_kernel_page_size)
        func mib(_ pages: UInt64) -> UInt32 { UInt32(clamping: pages * page / 1_048_576) }
        let anonymous = UInt64(stats.internal_page_count)
        let purgeable = min(UInt64(stats.purgeable_count), anonymous)
        return MemorySplit(appMiB: mib(anonymous - purgeable), wiredMiB: mib(UInt64(stats.wire_count)),
                           compressedMiB: mib(UInt64(stats.compressor_page_count)))
    }

    private func swapUsed() -> UInt32? {
        var usage = xsw_usage()
        var size = MemoryLayout<xsw_usage>.size
        guard sysctlbyname("vm.swapusage", &usage, &size, nil, 0) == 0 else { return nil }
        return UInt32(clamping: usage.xsu_used / 1_048_576)
    }

    /// Decimal gigabytes, as Finder shows them.
    private func ssdSpace() -> SsdSpace? {
        guard let values = try? URL(fileURLWithPath: "/").resourceValues(
            forKeys: [.volumeAvailableCapacityForImportantUsageKey, .volumeTotalCapacityKey]),
            let available = values.volumeAvailableCapacityForImportantUsage,
            let total = values.volumeTotalCapacity, total > 0 else { return nil }
        let totalGB = UInt16(clamping: Int64(total) / 1_000_000_000)
        let freeGB = UInt16(clamping: min(Int64(totalGB), max(0, available / 1_000_000_000)))
        return totalGB > 0 ? SsdSpace(freeGB: freeGB, totalGB: totalGB) : nil
    }

    private func diskCounters() -> (read: UInt64, write: UInt64)? {
        var read: UInt64 = 0, write: UInt64 = 0, found = false
        for properties in MacSystemSources.registryValues("IOBlockStorageDriver", keys: ["Statistics"]) {
            guard let statistics = properties["Statistics"] as? [String: Any] else { continue }
            read += (statistics["Bytes (Read)"] as? NSNumber)?.uint64Value ?? 0
            write += (statistics["Bytes (Write)"] as? NSNumber)?.uint64Value ?? 0
            found = true
        }
        return found ? (read, write) : nil
    }
}

/// Internet and router round-trip times, measured on a private queue at most
/// every five seconds while the NETWORK page keeps asking.
private final class NetworkProbes {
    private static let internetHost = "1.1.1.1"
    private static let resultLifetime: TimeInterval = 15
    private let queue = DispatchQueue(label: "CardputerCompanion.NetworkProbes")
    private let lock = NSLock()
    private var schedule = ProbeSchedule()
    private var internet: (value: UInt16?, time: TimeInterval)?
    /// Keyed by the router it measured, so a network change never shows the old router.
    private var router: (address: String, value: UInt16?, time: TimeInterval)?

    func results(router address: String?, now: TimeInterval) -> (internet: UInt16?, router: UInt16?) {
        lock.lock()
        schedule.noteRequest(at: now)
        let start = schedule.shouldProbe(at: now)
        func current(_ result: (value: UInt16?, time: TimeInterval)?) -> UInt16? {
            guard let result, now - result.time <= Self.resultLifetime else { return nil }
            return result.value
        }
        let routerResult = router.flatMap { $0.address == address ? ($0.value, $0.time) : nil }
        let values = (current(internet), current(routerResult))
        lock.unlock()
        if start {
            queue.async { [weak self] in self?.probe(router: address) }
        }
        return values
    }

    private func probe(router address: String?) {
        let internetValue = Self.echo(Self.internetHost)
        // Many home routers drop ICMP but accept a TCP connection to their admin page.
        let routerValue = address.flatMap { Self.echo($0) ?? Self.connect($0, port: 80) ?? Self.connect($0, port: 443) }
        let now = ProcessInfo.processInfo.systemUptime
        lock.lock()
        internet = (internetValue, now)
        router = address.map { ($0, routerValue, now) }
        lock.unlock()
    }

    private static func milliseconds(since start: UInt64) -> UInt16 {
        UInt16(clamping: (DispatchTime.now().uptimeNanoseconds - start + 500_000) / 1_000_000)
    }

    private static func address(_ host: String, port: UInt16 = 0) -> sockaddr_in? {
        var address = sockaddr_in()
        address.sin_len = UInt8(MemoryLayout<sockaddr_in>.size)
        address.sin_family = sa_family_t(AF_INET)
        address.sin_port = port.bigEndian
        return inet_pton(AF_INET, host, &address.sin_addr) == 1 ? address : nil
    }

    /// Unprivileged ICMP echo; macOS allows SOCK_DGRAM ICMP sockets without root.
    private static func echo(_ host: String) -> UInt16? {
        guard var target = address(host) else { return nil }
        let socketFD = socket(AF_INET, SOCK_DGRAM, IPPROTO_ICMP)
        guard socketFD >= 0 else { return nil }
        defer { close(socketFD) }
        var timeout = timeval(tv_sec: 1, tv_usec: 0)
        setsockopt(socketFD, SOL_SOCKET, SO_RCVTIMEO, &timeout, socklen_t(MemoryLayout<timeval>.size))
        let identifier = UInt16(truncatingIfNeeded: getpid())
        let sequence = UInt16.random(in: 1...UInt16.max)
        var packet: [UInt8] = [8, 0, 0, 0, UInt8(identifier >> 8), UInt8(identifier & 0xFF),
                               UInt8(sequence >> 8), UInt8(sequence & 0xFF)] + [UInt8](repeating: 0, count: 8)
        var sum: UInt32 = 0
        for index in stride(from: 0, to: packet.count, by: 2) {
            sum += UInt32(packet[index]) << 8 | UInt32(packet[index + 1])
        }
        while sum >> 16 != 0 { sum = (sum & 0xFFFF) + (sum >> 16) }
        let checksum = ~UInt16(sum)
        packet[2] = UInt8(checksum >> 8); packet[3] = UInt8(checksum & 0xFF)
        let start = DispatchTime.now().uptimeNanoseconds
        let sent = withUnsafePointer(to: &target) {
            $0.withMemoryRebound(to: sockaddr.self, capacity: 1) {
                sendto(socketFD, packet, packet.count, 0, $0, socklen_t(MemoryLayout<sockaddr_in>.size))
            }
        }
        guard sent == packet.count else { return nil }
        var buffer = [UInt8](repeating: 0, count: 256)
        while DispatchTime.now().uptimeNanoseconds - start < 1_000_000_000 {
            let received = recv(socketFD, &buffer, buffer.count, 0)
            guard received > 0 else { return nil }
            // The reply includes the IPv4 header.
            let offset = Int(buffer[0] & 0x0F) * 4
            guard received >= offset + 8, buffer[offset] == 0,
                  UInt16(buffer[offset + 6]) << 8 | UInt16(buffer[offset + 7]) == sequence else { continue }
            return milliseconds(since: start)
        }
        return nil
    }

    /// Time to a TCP answer; a refused connection still proves the host replied.
    private static func connect(_ host: String, port: UInt16) -> UInt16? {
        guard var target = address(host, port: port) else { return nil }
        let socketFD = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)
        guard socketFD >= 0 else { return nil }
        defer { close(socketFD) }
        _ = fcntl(socketFD, F_SETFL, fcntl(socketFD, F_GETFL) | O_NONBLOCK)
        let start = DispatchTime.now().uptimeNanoseconds
        let result = withUnsafePointer(to: &target) {
            $0.withMemoryRebound(to: sockaddr.self, capacity: 1) {
                Darwin.connect(socketFD, $0, socklen_t(MemoryLayout<sockaddr_in>.size))
            }
        }
        if result != 0 {
            guard errno == EINPROGRESS else { return errno == ECONNREFUSED ? milliseconds(since: start) : nil }
            var descriptor = pollfd(fd: socketFD, events: Int16(POLLOUT), revents: 0)
            guard poll(&descriptor, 1, 500) == 1 else { return nil }
            var error: Int32 = 0
            var length = socklen_t(MemoryLayout<Int32>.size)
            getsockopt(socketFD, SOL_SOCKET, SO_ERROR, &error, &length)
            guard error == 0 || error == ECONNREFUSED else { return nil }
        }
        return milliseconds(since: start)
    }
}
