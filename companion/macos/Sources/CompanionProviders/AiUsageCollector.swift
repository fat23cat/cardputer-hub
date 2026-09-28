import AppKit
import CompanionCore
import Foundation

public final class AiUsageCollector: AiUsageCollecting {
    private let lock = NSLock()
    private let codex: AiUsageProviderRefreshing
    private let cursor: AiUsageProviderRefreshing
    private let scheduleRetry: (TimeInterval, @escaping () -> Void) -> Void
    private let now: () -> Date
    private var cached = AiUsageSnapshot()
    private var timer: Timer?
    private var wakeObserver: NSObjectProtocol?
    private var refreshSerial = DispatchQueue(label: "org.cardputer.companion.ai-collector")
    private var pending = 0
    private var samples: [AiProviderId: AiUsageProviderSnapshot] = [:]
    private var freshAt: [AiProviderId: Date] = [:]
    private var refreshId: UInt64 = 0
    private var retryId: UInt64 = 0
    private var retryDelay: TimeInterval = 1
    private var refreshRequested = false
    private var cycleFailed = false
    private var running = false

    public convenience init() {
        self.init(codex: CodexUsageProvider(), cursor: CursorUsageProvider())
    }

    init(codex: AiUsageProviderRefreshing, cursor: AiUsageProviderRefreshing,
         now: @escaping () -> Date = Date.init,
         scheduleRetry: @escaping (TimeInterval, @escaping () -> Void) -> Void = { delay, work in
             DispatchQueue.global(qos: .utility).asyncAfter(deadline: .now() + delay,
                                                             execute: work)
         }) {
        self.codex = codex
        self.cursor = cursor
        self.now = now
        self.scheduleRetry = scheduleRetry
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
        timer = Timer.scheduledTimer(withTimeInterval: 60, repeats: true) { [weak self] _ in
            self?.refresh()
        }
        wakeObserver = NSWorkspace.shared.notificationCenter.addObserver(
            forName: NSWorkspace.didWakeNotification, object: nil, queue: .main
        ) { [weak self] _ in self?.refresh() }
    }

    public func stop() {
        timer?.invalidate(); timer = nil
        if let wakeObserver { NSWorkspace.shared.notificationCenter.removeObserver(wakeObserver) }
        wakeObserver = nil
        refreshSerial.sync {
            running = false; samples.removeAll(); freshAt.removeAll(); pending = 0
            refreshRequested = false; cycleFailed = false; retryDelay = 1
            refreshId &+= 1; retryId &+= 1
        }
        codex.stop(); cursor.stop()
        lock.lock(); cached = AiUsageSnapshot(); lock.unlock()
    }

    public func snapshot() -> AiUsageSnapshot? {
        lock.lock(); defer { lock.unlock() }
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
        pending = 2
        cycleFailed = false
        codex.refresh { [weak self] sample, absent in
            self?.accept(.codex, sample: sample, absent: absent, id: id)
        }
        cursor.refresh { [weak self] sample, absent in
            self?.accept(.cursor, sample: sample, absent: absent, id: id)
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
        let providers = [AiProviderId.codex, .cursor].compactMap { samples[$0] }
        lock.lock()
        let next = AiUsageSnapshot(generation: cached.generation &+ 1,
                                   state: .forCache(pending: pending,
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

    private func accept(_ provider: AiProviderId, sample: AiUsageProviderSnapshot?,
                        absent: Bool, id: UInt64) {
        refreshSerial.async {
            guard self.running, self.refreshId == id else { return }
            if absent {
                self.samples.removeValue(forKey: provider)
                self.freshAt.removeValue(forKey: provider)
            } else if let sample {
                self.samples[provider] = sample
                self.freshAt[provider] = self.now()
            } else if var prior = self.samples[provider] {
                let age = self.freshAt[provider].map { self.now().timeIntervalSince($0) }
                if prior.freshness != .stale {
                    prior.freshness = age.map { $0 < 90 } == true ? .fresh : .stale
                }
                self.samples[provider] = prior
            }
            if sample == nil && !absent { self.cycleFailed = true }
            self.pending -= 1
            self.publishCache()
            if self.pending == 0 { self.finishRefresh() }
        }
    }
}
