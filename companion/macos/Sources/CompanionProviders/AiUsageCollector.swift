import AppKit
import CompanionCore
import Foundation

public final class AiUsageCollector: AiUsageCollecting {
    private let lock = NSLock()
    // Snapshot order; the wire encoding sends at most the first two providers.
    private let providers: [(AiProviderId, AiUsageProviderRefreshing)]
    private let scheduleRetry: (TimeInterval, @escaping () -> Void) -> Void
    private let schedulePeriodic: (TimeInterval, @escaping () -> Void) -> (() -> Void)
    private let now: () -> Date
    private var cached = AiUsageSnapshot()
    private var cachedFreshAt: [AiProviderId: Date] = [:]
    private var cancelPeriodic: (() -> Void)?
    private var wakeObserver: NSObjectProtocol?
    private var refreshSerial = DispatchQueue(label: "org.cardputer.companion.ai-collector")
    private var pending = 0
    private var samples: [AiProviderId: AiUsageProviderSnapshot] = [:]
    private var waiting: Set<AiProviderId> = []
    private var freshAt: [AiProviderId: Date] = [:]
    private var refreshId: UInt64 = 0
    private var retryId: UInt64 = 0
    private var retryDelay: TimeInterval = 1
    private var refreshRequested = false
    private var cycleFailed = false
    private var running = false

    public convenience init() {
        self.init(codex: CodexUsageProvider(), cursor: CursorUsageProvider(),
                  claude: ClaudeUsageProvider())
    }

    init(codex: AiUsageProviderRefreshing, cursor: AiUsageProviderRefreshing,
         claude: AiUsageProviderRefreshing = AbsentUsageProvider(),
         now: @escaping () -> Date = Date.init,
         scheduleRetry: @escaping (TimeInterval, @escaping () -> Void) -> Void = { delay, work in
             DispatchQueue.global(qos: .utility).asyncAfter(deadline: .now() + delay,
                                                             execute: work)
         },
         schedulePeriodic: @escaping (TimeInterval, @escaping () -> Void) -> (() -> Void) = {
             interval, work in
             let timer = DispatchSource.makeTimerSource(queue: .global(qos: .utility))
             timer.schedule(deadline: .now() + interval, repeating: interval)
             timer.setEventHandler(handler: work)
             timer.resume()
             return { timer.cancel() }
         }) {
        providers = [(.codex, codex), (.cursor, cursor), (.claude, claude)]
        self.now = now
        self.scheduleRetry = scheduleRetry
        self.schedulePeriodic = schedulePeriodic
        codex.setRecoveryHandler { [weak self] in self?.providerExited(.codex) }
    }

    public func start() {
        let shouldStart = refreshSerial.sync { () -> Bool in
            guard !running else { return false }
            running = true
            return true
        }
        guard shouldStart else { return }
        refresh()
        cancelPeriodic = schedulePeriodic(30) { [weak self] in
            self?.refresh()
        }
        wakeObserver = NSWorkspace.shared.notificationCenter.addObserver(
            forName: NSWorkspace.didWakeNotification, object: nil, queue: .main
        ) { [weak self] _ in self?.refresh() }
    }

    public func stop() {
        cancelPeriodic?(); cancelPeriodic = nil
        if let wakeObserver { NSWorkspace.shared.notificationCenter.removeObserver(wakeObserver) }
        wakeObserver = nil
        refreshSerial.sync {
            running = false; samples.removeAll(); freshAt.removeAll(); waiting.removeAll()
            pending = 0
            refreshRequested = false; cycleFailed = false; retryDelay = 1
            refreshId &+= 1; retryId &+= 1
        }
        providers.forEach { $0.1.stop() }
        lock.lock(); cached = AiUsageSnapshot(); cachedFreshAt.removeAll(); lock.unlock()
    }

    public func snapshot() -> AiUsageSnapshot? {
        lock.lock(); defer { lock.unlock() }
        let currentTime = now()
        var expired = false
        for index in cached.providers.indices {
            let provider = cached.providers[index].provider
            if cached.providers[index].freshness == .fresh,
               let sampledAt = cachedFreshAt[provider],
               currentTime.timeIntervalSince(sampledAt) >= 90 {
                cached.providers[index].freshness = .stale
                expired = true
            }
            guard cached.providers[index].freshness == .stale else { continue }
            let aged = cached.providers[index].aged(now: currentTime)
            // Countdowns alone are not a new generation; a window that reset is.
            if aged.metrics.map(\.resetAt) != cached.providers[index].metrics.map(\.resetAt) {
                expired = true
            }
            cached.providers[index] = aged
        }
        if expired { cached.generation &+= 1 }
        return cached
    }

    func refresh() {
        refreshSerial.async { [weak self] in
            guard let self else { return }
            guard self.running else { return }
            if self.pending != 0 { self.refreshRequested = true; return }
            self.startRefresh()
        }
    }

    private func startRefresh() {
        retryId &+= 1
        refreshId &+= 1
        let id = refreshId
        pending = providers.count
        cycleFailed = false
        for (provider, source) in providers {
            source.refresh { [weak self] outcome in
                self?.accept(provider, outcome: outcome, id: id)
            }
        }
    }

    private func scheduleFailureRetry() {
        let delay = retryDelay
        retryDelay = min(retryDelay * 2, 30)
        let id = retryId
        scheduleRetry(delay) { [weak self] in
            self?.refreshSerial.async { [weak self] in
                guard let self, self.running, self.retryId == id else { return }
                if self.pending != 0 { self.refreshRequested = true; return }
                self.startRefresh()
            }
        }
    }

    private func providerExited(_ provider: AiProviderId) {
        refreshSerial.async { [weak self] in
            guard let self, self.running else { return }
            if var prior = self.samples[provider], prior.freshness != .stale {
                prior.freshness = .stale
                self.samples[provider] = prior
                self.publishCache()
            }
            if self.pending != 0 { self.refreshRequested = true; return }
            self.scheduleFailureRetry()
        }
    }

    private func publishCache() {
        let currentTime = now()
        let providers = self.providers.map(\.0).compactMap { provider -> AiUsageProviderSnapshot? in
            guard var sample = samples[provider] else { return nil }
            if sample.freshness == .fresh,
               let sampledAt = freshAt[provider],
               currentTime.timeIntervalSince(sampledAt) >= 90 {
                sample.freshness = .stale
            }
            return sample.freshness == .stale
                ? sample.aged(now: currentTime) : sample
        }
        lock.lock()
        cachedFreshAt = freshAt
        let next = AiUsageSnapshot(generation: cached.generation &+ 1,
                                   state: .forCache(pending: pending + waiting.count,
                                                    hasUsableProvider: !providers.isEmpty),
                                   providers: providers)
        if cached.state != next.state || cached.providers != next.providers {
            cached = next
        }
        lock.unlock()
    }

    private func finishRefresh() {
        if cycleFailed {
            if !refreshRequested { scheduleFailureRetry() }
        } else {
            retryDelay = 1
        }
        if refreshRequested {
            refreshRequested = false
            startRefresh()
        }
    }

    private func accept(_ provider: AiProviderId, outcome: AiUsageRefreshOutcome, id: UInt64) {
        refreshSerial.async {
            guard self.running, self.refreshId == id else { return }
            if outcome == .waiting { self.waiting.insert(provider) } else { self.waiting.remove(provider) }
            switch outcome {
            case .absent:
                self.samples.removeValue(forKey: provider)
                self.freshAt.removeValue(forKey: provider)
            case .sample(let sample):
                self.samples[provider] = sample
                self.freshAt[provider] = self.now()
            case .failed:
                self.cycleFailed = true
                if var prior = self.samples[provider], prior.freshness != .stale {
                    let age = self.freshAt[provider].map { self.now().timeIntervalSince($0) }
                    prior.freshness = age.map { $0 < 90 } == true ? .fresh : .stale
                    self.samples[provider] = prior
                }
            case .unavailable, .waiting:
                self.samples[provider]?.freshness = .stale
            }
            self.pending -= 1
            self.publishCache()
            if self.pending == 0 { self.finishRefresh() }
        }
    }
}

final class AbsentUsageProvider: AiUsageProviderRefreshing {
    func refresh(_ done: @escaping (AiUsageRefreshOutcome) -> Void) { done(.absent) }
    func stop() {}
}
