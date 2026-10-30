import CompanionCore
import Foundation

final class CodexUsageProvider: AiUsageProviderRefreshing {
    private let queue = DispatchQueue(label: "org.cardputer.companion.codex-usage")
    private let locator: ExecutableLocator
    private let transportFactory: () -> CodexJSONLTransport
    private var transport: CodexJSONLTransport?
    private var generation: UInt64 = 0
    private var buffer = Data()
    private var nextId = 3
    private var activeId: Int?
    private var completion: ((AiUsageRefreshOutcome) -> Void)?
    private var accountPlan: String?
    private var initialized = false
    /// Whether the last account/read found a signed-in account.
    private var signedIn = false
    private var timeout: DispatchWorkItem?
    private var watchdogId: UInt64 = 0
    private var recoveryHandler: (() -> Void)?

    init(locator: ExecutableLocator = ExecutableLocator(name: "codex"),
         transportFactory: @escaping () -> CodexJSONLTransport = {
             ProcessCodexJSONLTransport()
         }) {
        self.locator = locator
        self.transportFactory = transportFactory
    }

    func refresh(_ done: @escaping (AiUsageRefreshOutcome) -> Void) {
        queue.async {
            guard self.completion == nil else { return }
            self.completion = done
            if self.transport?.isRunning == true && self.initialized {
                // A signed-out account is checked again in the same process.
                if self.signedIn { self.requestLimits() } else { self.readAccount() }
            } else {
                self.start()
            }
        }
    }

    func stop() {
        queue.sync {
            self.resetTransport()
            self.completion = nil
        }
    }

    func setRecoveryHandler(_ handler: @escaping () -> Void) {
        queue.async { self.recoveryHandler = handler }
    }

    private func start() {
        resetTransport()
        guard let path = locator.find() else { finish(nil, absent: true); return }
        let current = generation
        let transport = transportFactory()
        self.transport = transport
        transport.onData = { [weak self] data in
            self?.queue.async { [weak self] in
                guard let self, self.generation == current else { return }
                self.receive(data)
            }
        }
        transport.onExit = { [weak self] in
            self?.queue.async { [weak self] in
                guard let self, self.generation == current else { return }
                self.recover()
            }
        }
        guard transport.start(path: path) else { recover(); return }
        guard send(["method": "initialize", "id": 1, "params": [
            "clientInfo": ["name": "cardputer_hub", "title": "Cardputer Hub", "version": "1.0"]]])
        else { return }
        scheduleTimeout()
    }

    private func resetTransport() {
        timeout?.cancel(); timeout = nil
        watchdogId &+= 1
        generation &+= 1
        let previous = transport
        transport = nil
        previous?.onData = nil
        previous?.onExit = nil
        previous?.stop()
        buffer.removeAll()
        initialized = false
        signedIn = false
        activeId = nil
        accountPlan = nil
    }

    /// Sends one JSONL message. A closed input is a provider failure: the
    /// transport is reset and the refresh reports failed.
    @discardableResult
    private func send(_ message: [String: Any]) -> Bool {
        guard let transport,
              let data = try? JSONSerialization.data(withJSONObject: message) else { return false }
        guard transport.send(data + Data([10])) else { recover(); return false }
        return true
    }

    private func receive(_ data: Data) {
        if data.isEmpty { return }
        buffer.append(data)
        if buffer.count > 64 * 1024 { recover(); return }
        while let newline = buffer.firstIndex(of: 10) {
            let line = Data(buffer.prefix(upTo: newline))
            buffer.removeSubrange(...newline)
            guard let object = try? JSONSerialization.jsonObject(with: line) as? [String: Any],
                  let id = object["id"] as? Int else { continue }
            handle(object, id: id)
        }
    }

    private func handle(_ object: [String: Any], id: Int) {
        guard object["error"] == nil else { recover(); return }
        if id == 1 {
            guard object["result"] != nil else { recover(); return }
            guard send(["method": "initialized", "params": [:]]) else { return }
            readAccount()
        } else if id == 2 {
            let result = object["result"] as? [String: Any]
            initialized = true
            // `account: null` means Codex is installed but nobody is signed in;
            // asking for limits would only fail and restart the process.
            guard let account = result?["account"] as? [String: Any] else {
                signedIn = false
                finish(nil, absent: true)
                return
            }
            signedIn = true
            accountPlan = account["planType"] as? String
            requestLimits()
        } else if id == activeId {
            activeId = nil
            let result = object["result"] as? [String: Any]
            switch result.map({ AiUsageNormalization.codexReading($0, accountPlan: accountPlan) }) {
            case .sample(let sample)?:
                finish(sample)
            case .unavailable?:
                // A backend restriction is unavailable, not a retryable transport failure.
                finish(nil, unavailable: true)
            case .invalid?, nil:
                finish(nil)
            }
        }
    }

    private func readAccount() {
        guard send(["method": "account/read", "id": 2, "params": ["refreshToken": false]])
        else { return }
        scheduleTimeout()
    }

    private func requestLimits() {
        guard activeId == nil else { return }
        let id = nextId; nextId += 1; activeId = id
        guard send(["method": "account/rateLimits/read", "id": id]) else { return }
        scheduleTimeout()
    }

    private func scheduleTimeout() {
        timeout?.cancel()
        watchdogId &+= 1
        let id = watchdogId
        let current = generation
        let task = DispatchWorkItem { [weak self] in
            guard let self, self.generation == current, self.watchdogId == id else { return }
            self.recover()
        }
        timeout = task
        queue.asyncAfter(deadline: .now() + 12, execute: task)
    }

    private func recover() {
        let wasRefreshing = completion != nil
        resetTransport()
        finish(nil)
        if !wasRefreshing { recoveryHandler?() }
    }

    private func finish(_ result: AiUsageProviderSnapshot?, absent: Bool = false,
                        unavailable: Bool = false) {
        timeout?.cancel(); timeout = nil
        watchdogId &+= 1
        let done = completion; completion = nil
        if unavailable {
            done?(.unavailable)
        } else {
            done?(absent ? .absent : result.map { .sample($0) } ?? .failed)
        }
    }
}
