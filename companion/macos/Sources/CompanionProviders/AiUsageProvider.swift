import CompanionCore
import Foundation

enum AiUsageRefreshOutcome: Equatable {
    case sample(AiUsageProviderSnapshot)
    /// The provider is not installed or signed in; omit it.
    case absent
    /// The refresh failed; retry with backoff.
    case failed
    /// The source cannot be read now without anything failing (for example, its
    /// credential owner is not running). Keep any prior sample, marked stale.
    case unavailable
    /// The source waits for the user (a Keychain prompt). Like `unavailable`,
    /// but discovery stays open while nothing else can be shown.
    case waiting

    var sample: AiUsageProviderSnapshot? {
        if case .sample(let value) = self { return value }
        return nil
    }
}

protocol AiUsageProviderRefreshing: AnyObject {
    func refresh(_ done: @escaping (AiUsageRefreshOutcome) -> Void)
    func setRecoveryHandler(_ handler: @escaping () -> Void)
    func stop()
}

extension AiUsageProviderRefreshing {
    func setRecoveryHandler(_ handler: @escaping () -> Void) {}
}

/// Calls a refresh completion at most once, whichever of the read timeout and
/// the result comes first.
final class RefreshCompletion {
    private let lock = NSLock()
    private var done: ((AiUsageRefreshOutcome) -> Void)?
    init(_ done: @escaping (AiUsageRefreshOutcome) -> Void) { self.done = done }
    var finished: Bool { lock.lock(); defer { lock.unlock() }; return done == nil }
    func callAsFunction(_ outcome: AiUsageRefreshOutcome) {
        lock.lock(); let callback = done; done = nil; lock.unlock()
        callback?(outcome)
    }
}
