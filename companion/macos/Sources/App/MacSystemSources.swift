import Darwin
import Foundation
import IOKit
import SystemConfiguration

/// macOS readers shared by the overview and detail collectors.
enum MacSystemSources {
    static func vmStatistics() -> vm_statistics64_data_t? {
        var count = mach_msg_type_number_t(MemoryLayout<vm_statistics64_data_t>.size / MemoryLayout<integer_t>.size)
        var stats = vm_statistics64_data_t()
        let result = withUnsafeMutablePointer(to: &stats) { pointer in
            pointer.withMemoryRebound(to: integer_t.self, capacity: Int(count)) {
                host_statistics64(mach_host_self(), HOST_VM_INFO64, $0, &count)
            }
        }
        return result == KERN_SUCCESS ? stats : nil
    }

    /// The primary interface (IPv4 first, then IPv6) and the IPv4 router.
    static func primaryNetwork() -> (interface: String?, router: String?) {
        guard let store = SCDynamicStoreCreate(nil, "CardputerCompanion" as CFString, nil, nil) else {
            return (nil, nil)
        }
        let v4 = SCDynamicStoreCopyValue(store, "State:/Network/Global/IPv4" as CFString) as? [String: Any]
        let v6 = SCDynamicStoreCopyValue(store, "State:/Network/Global/IPv6" as CFString) as? [String: Any]
        let key = kSCDynamicStorePropNetPrimaryInterface as String
        return ((v4?[key] ?? v6?[key]) as? String, v4?["Router"] as? String)
    }

    /// Only the named properties of every matching IORegistry service; copying
    /// whole property dictionaries is much more expensive.
    static func registryValues(_ className: String, keys: [String]) -> [[String: Any]] {
        var iterator: io_iterator_t = 0
        guard IOServiceGetMatchingServices(kIOMainPortDefault, IOServiceMatching(className), &iterator) ==
            KERN_SUCCESS else { return [] }
        defer { IOObjectRelease(iterator) }
        var entries: [[String: Any]] = []
        while case let service = IOIteratorNext(iterator), service != 0 {
            defer { IOObjectRelease(service) }
            var values: [String: Any] = [:]
            for key in keys {
                if let value = IORegistryEntryCreateCFProperty(service, key as CFString, kCFAllocatorDefault, 0)?
                    .takeRetainedValue() {
                    values[key] = value
                }
            }
            if !values.isEmpty { entries.append(values) }
        }
        return entries
    }
}
