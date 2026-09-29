import Foundation

public enum SystemDetailsGroup: UInt8, CaseIterable {
    case cpu = 1, power = 2, network = 3, memory = 4
}

public struct TopApp: Equatable {
    public var name: String
    public var percent: UInt8
    public init(name: String, percent: UInt8) { self.name = name; self.percent = percent }
}

public struct PeripheralBattery: Equatable {
    public var name: String
    public var percent: UInt8
    public init(name: String, percent: UInt8) { self.name = name; self.percent = percent }
}

public struct MemorySplit: Equatable {
    public var appMiB: UInt32
    public var wiredMiB: UInt32
    public var compressedMiB: UInt32
    public init(appMiB: UInt32, wiredMiB: UInt32, compressedMiB: UInt32) {
        self.appMiB = appMiB; self.wiredMiB = wiredMiB; self.compressedMiB = compressedMiB
    }
}

public struct SsdSpace: Equatable {
    public var freeGB: UInt16
    public var totalGB: UInt16
    public init(freeGB: UInt16, totalGB: UInt16) { self.freeGB = freeGB; self.totalGB = totalGB }
}

public struct DiskRates: Equatable {
    public var readKiBps: UInt32
    public var writeKiBps: UInt32
    public init(readKiBps: UInt32, writeKiBps: UInt32) { self.readKiBps = readKiBps; self.writeKiBps = writeKiBps }
}

/// One SYSTEM_DETAILS group (protocol v6). Only the fields of `group` are
/// encoded; nil fields are sent with their validity bit clear.
public struct SystemDetailsSample: Equatable {
    public static let maxApps = 4
    public static let maxAppName = 20
    public static let maxPeripheralName = 16

    public var group: SystemDetailsGroup
    public var performancePercent: UInt8?
    public var efficiencyPercent: UInt8?
    public var gpuPercent: UInt8?
    public var loadCenti: UInt16?
    public var apps: [TopApp]?
    public var systemDrawDeciwatts: UInt16?
    public var adapterWatts: UInt8?
    public var healthPercent: UInt8?
    public var cycleCount: UInt16?
    public var peripheral: PeripheralBattery?
    public var internetRttMs: UInt16?
    public var routerRttMs: UInt16?
    public var wifiRssiDbm: Int8?
    public var wifiLinkMbps: UInt16?
    public var vpnActive: Bool?
    public var memorySplit: MemorySplit?
    public var swapUsedMiB: UInt32?
    public var ssd: SsdSpace?
    public var diskRates: DiskRates?

    public init(group: SystemDetailsGroup) { self.group = group }

    public func encode() -> [UInt8]? {
        var bytes: [UInt8] = [1, group.rawValue, 0, 0]
        var flags: UInt16 = 0
        func bit(_ index: UInt16, _ present: Bool) { if present { flags |= 1 << index } }
        switch group {
        case .cpu:
            for value in [performancePercent, efficiencyPercent, gpuPercent] {
                guard (value ?? 0) <= 100 else { return nil }
            }
            bit(0, performancePercent != nil); bit(1, efficiencyPercent != nil)
            bit(2, gpuPercent != nil); bit(3, loadCenti != nil); bit(4, apps != nil)
            bytes += [performancePercent ?? 0, efficiencyPercent ?? 0, gpuPercent ?? 0]
            Self.put16(loadCenti ?? 0, into: &bytes)
            let list = apps ?? []
            guard list.count <= Self.maxApps else { return nil }
            bytes.append(UInt8(list.count))
            for app in list {
                let name = Array(app.name.utf8)
                guard app.percent <= 100, !name.isEmpty, name.count <= Self.maxAppName,
                      Self.printable(name) else { return nil }
                bytes += [app.percent, UInt8(name.count)] + name
            }
        case .power:
            guard (healthPercent ?? 0) <= 100 else { return nil }
            bit(0, systemDrawDeciwatts != nil); bit(1, adapterWatts != nil)
            bit(2, healthPercent != nil); bit(3, cycleCount != nil); bit(4, peripheral != nil)
            Self.put16(systemDrawDeciwatts ?? 0, into: &bytes)
            bytes += [adapterWatts ?? 0, healthPercent ?? 0]
            Self.put16(cycleCount ?? 0, into: &bytes)
            if let peripheral {
                let name = Array(peripheral.name.utf8)
                guard peripheral.percent <= 100, !name.isEmpty,
                      name.count <= Self.maxPeripheralName, Self.printable(name) else { return nil }
                bytes += [peripheral.percent, UInt8(name.count)] + name
            } else {
                bytes += [0, 0]
            }
        case .network:
            bit(0, internetRttMs != nil); bit(1, routerRttMs != nil); bit(2, wifiRssiDbm != nil)
            bit(3, wifiLinkMbps != nil); bit(4, vpnActive != nil)
            Self.put16(internetRttMs ?? 0, into: &bytes)
            Self.put16(routerRttMs ?? 0, into: &bytes)
            bytes.append(UInt8(bitPattern: wifiRssiDbm ?? 0))
            Self.put16(wifiLinkMbps ?? 0, into: &bytes)
            bytes.append(vpnActive == true ? 1 : 0)
        case .memory:
            if let ssd { guard ssd.totalGB > 0, ssd.freeGB <= ssd.totalGB else { return nil } }
            bit(0, memorySplit != nil); bit(1, swapUsedMiB != nil)
            bit(2, ssd != nil); bit(3, diskRates != nil)
            Self.put32(memorySplit?.appMiB ?? 0, into: &bytes)
            Self.put32(memorySplit?.wiredMiB ?? 0, into: &bytes)
            Self.put32(memorySplit?.compressedMiB ?? 0, into: &bytes)
            Self.put32(swapUsedMiB ?? 0, into: &bytes)
            Self.put16(ssd?.freeGB ?? 0, into: &bytes)
            Self.put16(ssd?.totalGB ?? 0, into: &bytes)
            Self.put32(diskRates?.readKiBps ?? 0, into: &bytes)
            Self.put32(diskRates?.writeKiBps ?? 0, into: &bytes)
        }
        bytes[2] = UInt8(truncatingIfNeeded: flags)
        bytes[3] = UInt8(truncatingIfNeeded: flags >> 8)
        return bytes.count <= CompanionConstants.maxPayloadSize ? bytes : nil
    }

    public static func decode(_ bytes: [UInt8]) -> Self? {
        guard bytes.count >= 4, bytes[0] == 1, let group = SystemDetailsGroup(rawValue: bytes[1])
        else { return nil }
        let flags = get16(bytes, at: 2)
        guard flags & ~(group == .memory ? UInt16(0x0f) : 0x1f) == 0 else { return nil }
        func has(_ index: UInt16) -> Bool { flags & (1 << index) != 0 }
        var value = Self(group: group)
        var pos = 4
        switch group {
        case .cpu:
            guard bytes.count >= pos + 6 else { return nil }
            if has(0) { value.performancePercent = bytes[pos] }
            if has(1) { value.efficiencyPercent = bytes[pos + 1] }
            if has(2) { value.gpuPercent = bytes[pos + 2] }
            if has(3) { value.loadCenti = get16(bytes, at: pos + 3) }
            let count = Int(bytes[pos + 5]); pos += 6
            guard count <= maxApps, has(4) || count == 0 else { return nil }
            var apps: [TopApp] = []
            for _ in 0..<count {
                guard pos + 2 <= bytes.count else { return nil }
                let percent = bytes[pos], length = Int(bytes[pos + 1]); pos += 2
                guard length > 0, pos + length <= bytes.count,
                      let name = String(bytes: bytes[pos..<(pos + length)], encoding: .ascii)
                else { return nil }
                apps.append(TopApp(name: name, percent: percent)); pos += length
            }
            if has(4) { value.apps = apps }
        case .power:
            guard bytes.count >= pos + 8 else { return nil }
            if has(0) { value.systemDrawDeciwatts = get16(bytes, at: pos) }
            if has(1) { value.adapterWatts = bytes[pos + 2] }
            if has(2) { value.healthPercent = bytes[pos + 3] }
            if has(3) { value.cycleCount = get16(bytes, at: pos + 4) }
            let percent = bytes[pos + 6], length = Int(bytes[pos + 7]); pos += 8
            guard pos + length <= bytes.count, has(4) == (length > 0) else { return nil }
            if has(4) {
                guard let name = String(bytes: bytes[pos..<(pos + length)], encoding: .ascii)
                else { return nil }
                value.peripheral = PeripheralBattery(name: name, percent: percent)
            }
            pos += length
        case .network:
            guard bytes.count >= pos + 8, bytes[pos + 7] <= 1 else { return nil }
            if has(0) { value.internetRttMs = get16(bytes, at: pos) }
            if has(1) { value.routerRttMs = get16(bytes, at: pos + 2) }
            if has(2) { value.wifiRssiDbm = Int8(bitPattern: bytes[pos + 4]) }
            if has(3) { value.wifiLinkMbps = get16(bytes, at: pos + 5) }
            if has(4) { value.vpnActive = bytes[pos + 7] == 1 }
            pos += 8
        case .memory:
            guard bytes.count >= pos + 28 else { return nil }
            if has(0) {
                value.memorySplit = MemorySplit(appMiB: get32(bytes, at: pos),
                                                wiredMiB: get32(bytes, at: pos + 4),
                                                compressedMiB: get32(bytes, at: pos + 8))
            }
            if has(1) { value.swapUsedMiB = get32(bytes, at: pos + 12) }
            if has(2) { value.ssd = SsdSpace(freeGB: get16(bytes, at: pos + 16), totalGB: get16(bytes, at: pos + 18)) }
            if has(3) { value.diskRates = DiskRates(readKiBps: get32(bytes, at: pos + 20), writeKiBps: get32(bytes, at: pos + 24)) }
            pos += 28
        }
        guard pos == bytes.count, value.encode() == bytes else { return nil }
        return value
    }

    private static func printable(_ bytes: [UInt8]) -> Bool { bytes.allSatisfy { (0x20...0x7e).contains($0) } }

    private static func put16(_ value: UInt16, into bytes: inout [UInt8]) {
        bytes += [UInt8(truncatingIfNeeded: value), UInt8(truncatingIfNeeded: value >> 8)]
    }

    private static func put32(_ value: UInt32, into bytes: inout [UInt8]) {
        for index in 0..<4 { bytes.append(UInt8(truncatingIfNeeded: value >> (index * 8))) }
    }

    private static func get16(_ bytes: [UInt8], at offset: Int) -> UInt16 {
        UInt16(bytes[offset]) | (UInt16(bytes[offset + 1]) << 8)
    }

    private static func get32(_ bytes: [UInt8], at offset: Int) -> UInt32 {
        (0..<4).reduce(UInt32(0)) { $0 | (UInt32(bytes[offset + $1]) << ($1 * 8)) }
    }
}

public protocol SystemDetailsCollecting {
    func collect(_ group: SystemDetailsGroup) -> SystemDetailsSample?
}

public enum DisplayName {
    /// Printable ASCII for the Cardputer font: transliterated to Latin,
    /// without diacritics, other characters dropped, at most `limit` bytes.
    public static func sanitize(_ value: String, limit: Int) -> String {
        let latin = value.applyingTransform(.toLatin, reverse: false) ?? value
        let plain = latin.applyingTransform(.stripDiacritics, reverse: false) ?? latin
        let ascii = plain.unicodeScalars.filter { (0x20...0x7e).contains($0.value) }
        let collapsed = String(String.UnicodeScalarView(ascii))
            .split(separator: " ", omittingEmptySubsequences: true).joined(separator: " ")
        let trimmed = String(collapsed.prefix(limit)).trimmingCharacters(in: .whitespaces)
        return trimmed.isEmpty ? "APP" : trimmed
    }

    /// The outermost `.app` bundle in `path`, so helpers count as their app.
    public static func appName(forExecutable path: String) -> String {
        let components = path.split(separator: "/")
        if let bundle = components.first(where: { $0.hasSuffix(".app") }) {
            return String(bundle.dropLast(4))
        }
        return components.last.map(String.init) ?? ""
    }
}

/// CPU time of one process, in nanoseconds since it started.
public struct ProcessCPUTime: Equatable {
    public var pid: Int32
    public var path: String
    public var cpuNanoseconds: UInt64
    public init(pid: Int32, path: String, cpuNanoseconds: UInt64) {
        self.pid = pid; self.path = path; self.cpuNanoseconds = cpuNanoseconds
    }
}

/// Ranks apps by CPU use between two samples. Processes that the user may
/// not read are simply absent from the samples.
public final class TopAppsTracker {
    public static let baselineLimit: TimeInterval = 10
    private var previous: [Int32: ProcessCPUTime] = [:]
    private var previousTime: TimeInterval?
    private let logicalCPUs: Int

    public init(logicalCPUs: Int) { self.logicalCPUs = max(1, logicalCPUs) }

    public func sample(_ processes: [ProcessCPUTime], at time: TimeInterval) -> [TopApp]? {
        defer {
            previous = Dictionary(processes.map { ($0.pid, $0) }, uniquingKeysWith: { _, last in last })
            previousTime = time
        }
        guard let previousTime, time > previousTime, time - previousTime <= Self.baselineLimit
        else { return nil }
        let window = (time - previousTime) * 1_000_000_000 * Double(logicalCPUs)
        var totals: [String: UInt64] = [:]
        for process in processes {
            guard let earlier = previous[process.pid], earlier.path == process.path,
                  process.cpuNanoseconds >= earlier.cpuNanoseconds else { continue }
            let name = DisplayName.appName(forExecutable: process.path)
            guard !name.isEmpty else { continue }
            totals[name, default: 0] += process.cpuNanoseconds - earlier.cpuNanoseconds
        }
        return totals
            .map { name, nanoseconds in
                TopApp(name: DisplayName.sanitize(name, limit: SystemDetailsSample.maxAppName),
                       percent: UInt8(min(100, (Double(nanoseconds) / window * 100).rounded())))
            }
            .filter { $0.percent > 0 }
            .sorted { $0.percent != $1.percent ? $0.percent > $1.percent : $0.name < $1.name }
            .prefix(SystemDetailsSample.maxApps)
            .map { $0 }
    }
}

/// Performance and efficiency cluster load from per-CPU tick deltas.
public enum ClusterUsage {
    public static func percents(previous: [CPUTimeTicks], current: [CPUTimeTicks],
                                clusters: [Character]) -> (performance: UInt8?, efficiency: UInt8?) {
        guard previous.count == current.count, current.count == clusters.count else { return (nil, nil) }
        func usage(_ kind: Character) -> UInt8? {
            var busy: UInt64 = 0, total: UInt64 = 0
            for index in current.indices where clusters[index] == kind {
                let before = previous[index], after = current[index]
                guard after.user >= before.user, after.system >= before.system,
                      after.idle >= before.idle, after.nice >= before.nice else { return nil }
                let working = (after.user - before.user) + (after.system - before.system) +
                    (after.nice - before.nice)
                busy += working
                total += working + (after.idle - before.idle)
            }
            return total > 0 ? UInt8(min(100, busy * 100 / total)) : nil
        }
        return (usage("P"), usage("E"))
    }
}

/// A byte counter turned into KiB/s; a baseline older than ten seconds is discarded.
public struct CounterRate {
    private var previous: (read: UInt64, write: UInt64, time: TimeInterval)?
    public init() {}

    public mutating func sample(read: UInt64, write: UInt64, at time: TimeInterval) -> DiskRates? {
        defer { previous = (read, write, time) }
        guard let previous, time > previous.time, time - previous.time <= 10,
              read >= previous.read, write >= previous.write else { return nil }
        let seconds = time - previous.time
        func kib(_ delta: UInt64) -> UInt32 { UInt32(min(Double(UInt32.max), Double(delta) / seconds / 1024)) }
        return DiskRates(readKiBps: kib(read - previous.read), writeKiBps: kib(write - previous.write))
    }
}

/// Network probes run only while the NETWORK page is being read.
public struct ProbeSchedule {
    public static let interval: TimeInterval = 5
    public static let idleLimit: TimeInterval = 10
    private var lastRequest: TimeInterval?
    private var lastProbe: TimeInterval?
    public init() {}

    public mutating func noteRequest(at time: TimeInterval) { lastRequest = time }

    /// True when a probe should start now; records the start.
    public mutating func shouldProbe(at time: TimeInterval) -> Bool {
        guard let lastRequest, time - lastRequest <= Self.idleLimit else { return false }
        if let lastProbe, time - lastProbe < Self.interval { return false }
        lastProbe = time
        return true
    }
}
