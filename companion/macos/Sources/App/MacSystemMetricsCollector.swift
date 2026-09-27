import Darwin
import Foundation
import IOKit.ps
import SystemConfiguration
import CompanionCore

final class MacSystemMetricsCollector: SystemMetricsCollecting {
    private var cpuPrevious: CPUTimeTicks?
    private var networkPrevious: (name: String, rx: UInt64, tx: UInt64, time: TimeInterval)?

    func collect() -> SystemMetricsSample? {
        var sample = SystemMetricsSample()
        sample.cpuPercent = cpu()
        sample.memory = memory()
        sample.memoryPressure = memoryPressure()
        sample.diskUsedPercent = disk()
        sample.batteryPercent = battery()
        sample.network = network()
        switch ProcessInfo.processInfo.thermalState {
        case .nominal: sample.thermalState = .normal
        case .fair: sample.thermalState = .fair
        case .serious: sample.thermalState = .serious
        case .critical: sample.thermalState = .critical
        @unknown default: sample.thermalState = nil
        }
        return sample
    }

    private func cpu() -> UInt8? {
        var count = mach_msg_type_number_t(MemoryLayout<host_cpu_load_info_data_t>.size / MemoryLayout<integer_t>.size)
        var load = host_cpu_load_info_data_t()
        let result = withUnsafeMutablePointer(to: &load) { pointer in
            pointer.withMemoryRebound(to: integer_t.self, capacity: Int(count)) {
                host_statistics(mach_host_self(), HOST_CPU_LOAD_INFO, $0, &count)
            }
        }
        guard result == KERN_SUCCESS else { return nil }
        let ticks = load.cpu_ticks
        let current = CPUTimeTicks(user: UInt64(ticks.0), system: UInt64(ticks.1),
                                   idle: UInt64(ticks.2), nice: UInt64(ticks.3))
        defer { cpuPrevious = current }
        guard let previous = cpuPrevious else { return nil }
        return CPUTimeTicks.usagePercent(from: previous, to: current)
    }

    private func memory() -> (usedMiB: UInt32, totalMiB: UInt32)? {
        var count = mach_msg_type_number_t(MemoryLayout<vm_statistics64_data_t>.size / MemoryLayout<integer_t>.size)
        var stats = vm_statistics64_data_t()
        let result = withUnsafeMutablePointer(to: &stats) { pointer in
            pointer.withMemoryRebound(to: integer_t.self, capacity: Int(count)) {
                host_statistics64(mach_host_self(), HOST_VM_INFO64, $0, &count)
            }
        }
        guard result == KERN_SUCCESS else { return nil }
        return SystemMemoryUsage.estimate(
            totalBytes: ProcessInfo.processInfo.physicalMemory,
            pageBytes: UInt64(vm_kernel_page_size),
            freePages: UInt64(stats.free_count),
            fileBackedPages: UInt64(stats.external_page_count))
    }

    private func memoryPressure() -> MemoryPressure? {
        var level: Int32 = 0
        var size = MemoryLayout<Int32>.size
        guard sysctlbyname("kern.memorystatus_vm_pressure_level", &level, &size, nil, 0) == 0 else { return nil }
        switch level {
        case 1: return .normal
        case 2: return .warning
        case 4: return .critical
        default: return nil
        }
    }

    private func disk() -> UInt8? {
        guard let attributes = try? FileManager.default.attributesOfFileSystem(forPath: "/"),
              let total = (attributes[.systemSize] as? NSNumber)?.uint64Value,
              let free = (attributes[.systemFreeSize] as? NSNumber)?.uint64Value,
              total > 0, free <= total else { return nil }
        return UInt8(min(100, (total - free) * 100 / total))
    }

    private func battery() -> UInt8? {
        guard let info = IOPSCopyPowerSourcesInfo()?.takeRetainedValue(),
              let sources = IOPSCopyPowerSourcesList(info)?.takeRetainedValue() as? [CFTypeRef] else { return nil }
        for source in sources {
            guard let description = IOPSGetPowerSourceDescription(info, source)?.takeUnretainedValue() as? [String: Any],
                  let type = description[kIOPSTypeKey] as? String,
                  type == kIOPSInternalBatteryType,
                  let present = description[kIOPSIsPresentKey] as? Bool, present,
                  let current = description[kIOPSCurrentCapacityKey] as? Int,
                  let maxCapacity = description[kIOPSMaxCapacityKey] as? Int,
                  maxCapacity > 0 else { continue }
            return UInt8(max(0, min(100, current * 100 / maxCapacity)))
        }
        return nil
    }

    private func network() -> (downloadKiBps: UInt32, uploadKiBps: UInt32)? {
        guard let store = SCDynamicStoreCreate(nil, "CardputerCompanion" as CFString, nil, nil),
              let name = ["State:/Network/Global/IPv4", "State:/Network/Global/IPv6"]
                .compactMap({ key -> String? in
                    let global = SCDynamicStoreCopyValue(store, key as CFString) as? [String: Any]
                    return global?[kSCDynamicStorePropNetPrimaryInterface as String] as? String
                }).first else {
            networkPrevious = nil
            return nil
        }
        var interfaces: UnsafeMutablePointer<ifaddrs>?
        guard getifaddrs(&interfaces) == 0, let head = interfaces else { return nil }
        defer { freeifaddrs(head) }
        var cursor: UnsafeMutablePointer<ifaddrs>? = head
        while let pointer = cursor {
            let item = pointer.pointee
            if String(cString: item.ifa_name) == name,
               let address = item.ifa_addr, address.pointee.sa_family == UInt8(AF_LINK),
               let data = item.ifa_data?.assumingMemoryBound(to: if_data.self) {
                let rx = UInt64(data.pointee.ifi_ibytes)
                let tx = UInt64(data.pointee.ifi_obytes)
                let now = ProcessInfo.processInfo.systemUptime
                defer { networkPrevious = (name, rx, tx, now) }
                guard let previous = networkPrevious, previous.name == name,
                      rx >= previous.rx, tx >= previous.tx,
                      now > previous.time, now - previous.time <= 10 else { return nil }
                let seconds = now - previous.time
                return (UInt32(min(Double(UInt32.max), Double(rx - previous.rx) / seconds / 1024)),
                        UInt32(min(Double(UInt32.max), Double(tx - previous.tx) / seconds / 1024)))
            }
            cursor = item.ifa_next
        }
        networkPrevious = nil
        return nil
    }
}
