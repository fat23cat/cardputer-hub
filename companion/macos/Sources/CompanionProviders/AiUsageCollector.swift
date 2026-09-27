import AppKit
import CompanionCore
import Foundation

public final class AiUsageCollector: AiUsageCollecting {
    private let lock = NSLock()
    private let codex: AiUsageProviderRefreshing
    private let cursor: AiUsageProviderRefreshing
    private var cached = AiUsageSnapshot()
    private var timer: Timer?
    private var wakeObserver: NSObjectProtocol?
    private var refreshSerial = DispatchQueue(label: "org.cardputer.companion.ai-collector")
    private var pending = 0
    private var samples: [AiProviderId: AiUsageProviderSnapshot] = [:]
    private var refreshId: UInt64 = 0
    private var running = false

    public convenience init() {
        self.init(codex: CodexUsageProvider(), cursor: CursorUsageProvider())
    }

    init(codex: AiUsageProviderRefreshing, cursor: AiUsageProviderRefreshing) {
        self.codex = codex
        self.cursor = cursor
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
        refreshSerial.sync { running = false; samples.removeAll(); pending = 0 }
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
            guard self.running, self.pending == 0 else { return }
            self.refreshId += 1
            let id = self.refreshId
            self.pending = 2
            self.codex.refresh { [weak self] sample, absent in
                self?.accept(.codex, sample: sample, absent: absent, id: id)
            }
            self.cursor.refresh { [weak self] sample, absent in
                self?.accept(.cursor, sample: sample, absent: absent, id: id)
            }
        }
    }

    private func accept(_ provider: AiProviderId, sample: AiUsageProviderSnapshot?,
                        absent: Bool, id: UInt64) {
        refreshSerial.async {
            guard self.running, self.refreshId == id else { return }
            if absent {
                self.samples.removeValue(forKey: provider)
            } else if let sample {
                self.samples[provider] = sample
            } else if var prior = self.samples[provider] {
                prior.freshness = .stale
                self.samples[provider] = prior
            }
            self.pending -= 1
            let providers = [AiProviderId.codex, .cursor].compactMap { self.samples[$0] }
            self.lock.lock()
            let next = AiUsageSnapshot(generation: self.cached.generation &+ 1,
                                       state: .forCache(pending: self.pending,
                                                        hasUsableProvider: !providers.isEmpty),
                                       providers: providers)
            if self.cached.state != next.state || self.cached.providers != next.providers {
                self.cached = next
            }
            self.lock.unlock()
        }
    }
}
