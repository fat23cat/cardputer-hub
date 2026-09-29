import Foundation

public enum MemoryPressure: UInt8 { case normal = 1, warning = 2, critical = 3 }
public enum ThermalState: UInt8 { case normal = 1, fair = 2, serious = 3, critical = 4 }
public enum PowerSource: UInt8 { case battery = 1, charging = 2, acPower = 3 }

public struct CPUTimeTicks {
    public let user: UInt64
    public let system: UInt64
    public let idle: UInt64
    public let nice: UInt64

    public init(user: UInt64, system: UInt64, idle: UInt64, nice: UInt64) {
        self.user = user
        self.system = system
        self.idle = idle
        self.nice = nice
    }

    public static func usagePercent(from previous: Self, to current: Self) -> UInt8? {
        guard current.user >= previous.user, current.system >= previous.system,
              current.idle >= previous.idle, current.nice >= previous.nice else { return nil }
        let busy = (current.user - previous.user) + (current.system - previous.system) +
                   (current.nice - previous.nice)
        let total = busy + (current.idle - previous.idle)
        guard total > 0 else { return nil }
        return UInt8(min(100, busy * 100 / total))
    }
}

public enum SystemMemoryUsage {
    public static func estimate(totalBytes: UInt64, pageBytes: UInt64,
                                freePages: UInt64, fileBackedPages: UInt64)
        -> (usedMiB: UInt32, totalMiB: UInt32)? {
        guard totalBytes > 0, pageBytes > 0 else { return nil }
        let totalPages = totalBytes / pageBytes
        let free = min(freePages, totalPages)
        let cached = min(fileBackedPages, totalPages - free)
        let usedBytes = totalBytes - (free + cached) * pageBytes
        let mib: UInt64 = 1_048_576
        return (UInt32(min(UInt64(UInt32.max), usedBytes / mib)),
                UInt32(min(UInt64(UInt32.max), totalBytes / mib)))
    }
}

public struct SystemMetricsSample: Equatable {
    public var cpuPercent: UInt8?
    public var memory: (usedMiB: UInt32, totalMiB: UInt32)?
    public var memoryPressure: MemoryPressure?
    public var diskUsedPercent: UInt8?
    public var batteryPercent: UInt8?
    public var thermalState: ThermalState?
    public var network: (downloadKiBps: UInt32, uploadKiBps: UInt32)?
    public var powerSource: PowerSource?
    /// Minutes to full while charging, to empty on battery.
    public var batteryMinutes: UInt16?

    public init() {}

    public static let payloadSize = 26

    public static func == (lhs: Self, rhs: Self) -> Bool {
        lhs.cpuPercent == rhs.cpuPercent &&
            lhs.memory?.usedMiB == rhs.memory?.usedMiB &&
            lhs.memory?.totalMiB == rhs.memory?.totalMiB &&
            lhs.memoryPressure == rhs.memoryPressure &&
            lhs.diskUsedPercent == rhs.diskUsedPercent &&
            lhs.batteryPercent == rhs.batteryPercent &&
            lhs.thermalState == rhs.thermalState &&
            lhs.network?.downloadKiBps == rhs.network?.downloadKiBps &&
            lhs.network?.uploadKiBps == rhs.network?.uploadKiBps &&
            lhs.powerSource == rhs.powerSource &&
            lhs.batteryMinutes == rhs.batteryMinutes
    }

    /// Validity (2), CPU %, RAM used/total MiB, pressure, disk %, battery %,
    /// thermal, download/upload KiB/s, power source, battery minutes.
    public func encode() -> [UInt8]? {
        if let cpuPercent, cpuPercent > 100 { return nil }
        if let memory, memory.totalMiB == 0 || memory.usedMiB > memory.totalMiB { return nil }
        if let diskUsedPercent, diskUsedPercent > 100 { return nil }
        if let batteryPercent, batteryPercent > 100 { return nil }
        var bytes = Array(repeating: UInt8(0), count: Self.payloadSize)
        var flags: UInt16 = 0
        if let cpuPercent { flags |= 1; bytes[2] = cpuPercent }
        if let memory {
            flags |= 2
            Self.put32(memory.usedMiB, into: &bytes, at: 3)
            Self.put32(memory.totalMiB, into: &bytes, at: 7)
        }
        if let memoryPressure { flags |= 4; bytes[11] = memoryPressure.rawValue }
        if let diskUsedPercent { flags |= 8; bytes[12] = diskUsedPercent }
        if let batteryPercent { flags |= 16; bytes[13] = batteryPercent }
        if let network {
            flags |= 32
            Self.put32(network.downloadKiBps, into: &bytes, at: 15)
            Self.put32(network.uploadKiBps, into: &bytes, at: 19)
        }
        if let thermalState { flags |= 64; bytes[14] = thermalState.rawValue }
        if let powerSource { flags |= 128; bytes[23] = powerSource.rawValue }
        if let batteryMinutes {
            flags |= 256
            bytes[24] = UInt8(truncatingIfNeeded: batteryMinutes)
            bytes[25] = UInt8(truncatingIfNeeded: batteryMinutes >> 8)
        }
        bytes[0] = UInt8(truncatingIfNeeded: flags)
        bytes[1] = UInt8(truncatingIfNeeded: flags >> 8)
        return bytes
    }

    public static func decode(_ bytes: [UInt8]) -> Self? {
        guard bytes.count == payloadSize else { return nil }
        let flags = UInt16(bytes[0]) | (UInt16(bytes[1]) << 8)
        guard flags & ~UInt16(0x1ff) == 0 else { return nil }
        var value = Self()
        if flags & 1 != 0 { value.cpuPercent = bytes[2] }
        if flags & 2 != 0 { value.memory = (get32(bytes, at: 3), get32(bytes, at: 7)) }
        if flags & 4 != 0 { value.memoryPressure = MemoryPressure(rawValue: bytes[11]); if value.memoryPressure == nil { return nil } }
        if flags & 8 != 0 { value.diskUsedPercent = bytes[12] }
        if flags & 16 != 0 { value.batteryPercent = bytes[13] }
        if flags & 32 != 0 { value.network = (get32(bytes, at: 15), get32(bytes, at: 19)) }
        if flags & 64 != 0 { value.thermalState = ThermalState(rawValue: bytes[14]); if value.thermalState == nil { return nil } }
        if flags & 128 != 0 { value.powerSource = PowerSource(rawValue: bytes[23]); if value.powerSource == nil { return nil } }
        if flags & 256 != 0 { value.batteryMinutes = UInt16(bytes[24]) | (UInt16(bytes[25]) << 8) }
        return value.encode() == nil ? nil : value
    }

    private static func put32(_ value: UInt32, into bytes: inout [UInt8], at offset: Int) {
        for index in 0..<4 { bytes[offset + index] = UInt8(truncatingIfNeeded: value >> (index * 8)) }
    }

    private static func get32(_ bytes: [UInt8], at offset: Int) -> UInt32 {
        (0..<4).reduce(UInt32(0)) { $0 | (UInt32(bytes[offset + $1]) << ($1 * 8)) }
    }
}

public protocol SystemMetricsCollecting {
    func collect() -> SystemMetricsSample?
}
