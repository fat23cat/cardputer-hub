import Foundation
import Security

/// Claude Code and Cursor store credentials in the file-based login Keychain.
/// Its ACL prompts need the legacy interaction switch, not just an LAContext.
final class LegacyKeychainRead {
    static let shared = LegacyKeychainRead()
    private let read: (CFDictionary, UnsafeMutablePointer<CFTypeRef?>?) -> OSStatus
    private let getInteraction: () -> (OSStatus, Bool)
    private let setInteraction: (Bool) -> OSStatus
    private let condition = NSCondition()
    private var interactiveReads = 0
    private var silentReadActive = false

    init(read: @escaping (CFDictionary, UnsafeMutablePointer<CFTypeRef?>?) -> OSStatus = SecItemCopyMatching,
         getInteraction: @escaping () -> (OSStatus, Bool) = {
             var allowed: DarwinBoolean = false
             let status = SecKeychainGetUserInteractionAllowed(&allowed)
             return (status, allowed.boolValue)
         },
         setInteraction: @escaping (Bool) -> OSStatus = SecKeychainSetUserInteractionAllowed) {
        self.read = read
        self.getInteraction = getInteraction
        self.setInteraction = setInteraction
    }

    func copyMatching(_ query: CFDictionary, allowInteraction: Bool,
                      result: UnsafeMutablePointer<CFTypeRef?>?) -> OSStatus {
        // The legacy switch is process-wide. Interactive reads may run together
        // (an open Claude prompt must not hold up Cursor), but a silent read
        // owns the switch exclusively until the previous state is restored.
        condition.lock()
        while silentReadActive || (!allowInteraction && interactiveReads > 0) {
            condition.wait()
        }
        if allowInteraction { interactiveReads += 1 } else { silentReadActive = true }
        condition.unlock()
        defer {
            condition.lock()
            if allowInteraction { interactiveReads -= 1 } else { silentReadActive = false }
            condition.broadcast()
            condition.unlock()
        }
        if allowInteraction { return read(query, result) }
        let (status, allowed) = getInteraction()
        // Never fall through to a potentially interactive read if disabling
        // the UI failed. Restore the original policy on all read outcomes.
        guard status == errSecSuccess, setInteraction(false) == errSecSuccess
        else { return errSecInteractionNotAllowed }
        defer { _ = setInteraction(allowed) }
        return read(query, result)
    }
}
