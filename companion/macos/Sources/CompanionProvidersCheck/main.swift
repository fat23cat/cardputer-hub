@testable import CompanionProviders
import CompanionCore
import Foundation

private var failures = 0
private func expect(_ condition: @autoclosure () -> Bool,
                    file: StaticString = #fileID, line: UInt = #line) {
    if !condition() {
        fputs("FAIL \(file):\(line)\n", stderr)
        failures += 1
    }
}

func providerPackageBoundary() {
    expect(AiUsageCollector().snapshot() != nil)
}

private final class FakeCursorCredentials: CursorCredentialReading {
    var result: CursorCredentialResult
    init(_ result: CursorCredentialResult) { self.result = result }
    func read() -> CursorCredentialResult { result }
}

private final class FakeHTTP: AiUsageHTTPTransport {
    private let lock = NSLock()
    private var storedRequests: [URLRequest] = []
    private var completion: ((Data?, HTTPURLResponse?, Error?) -> Void)?
    var requests: [URLRequest] {
        lock.lock(); defer { lock.unlock() }
        return storedRequests
    }
    func get(_ request: URLRequest,
             completion: @escaping (Data?, HTTPURLResponse?, Error?) -> Void) {
        lock.lock()
        storedRequests.append(request)
        self.completion = completion
        lock.unlock()
    }
    func stop() {}
    func respond(status: Int, object: [String: Any]? = nil, headers: [String: String]? = nil) {
        lock.lock()
        let url = storedRequests.last!.url!
        let callback = completion
        lock.unlock()
        let response = HTTPURLResponse(url: url, statusCode: status,
                                       httpVersion: nil, headerFields: headers)!
        let data = object.flatMap { try? JSONSerialization.data(withJSONObject: $0) }
        callback?(data, response, nil)
    }
}

private func fakeJWT(sub: String?) -> String {
    let payload = sub.map { ["sub": $0] } ?? [:]
    let encoded = try! JSONSerialization.data(withJSONObject: payload).base64EncodedString()
        .replacingOccurrences(of: "+", with: "-")
        .replacingOccurrences(of: "/", with: "_")
        .replacingOccurrences(of: "=", with: "")
    return "HEADER.\(encoded).SIGNATURE"
}

func cursorBuildsVerifiedEnterpriseCookie() {
    let token = fakeJWT(sub: "auth0|user_123")
    let http = FakeHTTP()
    let provider = CursorUsageProvider(credentials: FakeCursorCredentials(.value(token)), http: http)
    var sample: AiUsageProviderSnapshot?
    provider.refresh { sample = $0.sample }
    expect(http.requests.count == 1)
    expect(http.requests.first?.value(forHTTPHeaderField: "Cookie") ==
            "WorkosCursorSessionToken=user_123%3A%3A\(token)")
    expect(http.requests.first?.url?.absoluteString == "https://cursor.com/api/usage-summary")
    http.respond(status: 200, object: ["membershipType": "enterprise",
        "billingCycleEnd": "2026-10-01T00:00:00.000Z",
        "individualUsage": ["overall": ["enabled": true, "used": 9458,
                                          "limit": 255000, "remaining": 245542]]])
    expect(sample?.plan == .enterprise)
    expect(sample?.metrics.first?.used == 9458)
    expect(sample?.metrics.first?.limit == 255000)
}

func cursorOmitsAbsentCredentialAndRejectsMalformedJWT() {
    let http = FakeHTTP()
    let credentials = FakeCursorCredentials(.absent)
    let provider = CursorUsageProvider(credentials: credentials, http: http)
    var absent = false
    provider.refresh { absent = $0 == .absent }
    expect(absent)
    expect(http.requests.isEmpty)

    credentials.result = .value("not-a-jwt")
    var failed = false
    provider.refresh { failed = $0 == .failed }
    expect(failed)
    expect(http.requests.isEmpty)

    credentials.result = .value(fakeJWT(sub: nil))
    provider.refresh { failed = $0 == .failed }
    expect(failed)
    expect(http.requests.isEmpty)
}

func codexExecutableDiscoveryChecksDirectPathsBeforeLoginShell() {
    let cases: [(String, String, String)] = [
        ("/custom/bin", "/custom/bin/codex", "/custom/bin/codex"),
        ("/missing", "/Users/test/.local/bin/codex", "/Users/test/.local/bin/codex"),
        ("/missing", "/opt/homebrew/bin/codex", "/opt/homebrew/bin/codex"),
    ]
    for (path, executable, expected) in cases {
        var shellCalls = 0
        let locator = CodexExecutableLocator(environment: { ["PATH": path] },
            home: "/Users/test", isExecutable: { $0 == executable },
            loginShellPath: { shellCalls += 1; return "/login/bin/codex" })
        expect(locator.find() == expected)
        expect(shellCalls == 0)
    }
}

func codexExecutableDiscoveryCachesSuccessAndRetriesNegativeLoginShellResult() {
    var shellCalls = 0
    let locator = CodexExecutableLocator(environment: { ["PATH": "/missing"] },
        home: "/Users/test", isExecutable: { $0 == "/login/bin/codex" },
        loginShellPath: { shellCalls += 1; return "/login/bin/codex" })
    expect(locator.find() == "/login/bin/codex")
    expect(locator.find() == "/login/bin/codex")
    expect(shellCalls == 1)

    var absentCalls = 0
    var installed = false
    let absent = CodexExecutableLocator(environment: { [:] }, home: "/Users/test",
        isExecutable: { installed && $0 == "/login/bin/codex" },
        loginShellPath: { absentCalls += 1; return installed ? "/login/bin/codex" : nil })
    expect(absent.find() == nil)
    installed = true
    expect(absent.find() == "/login/bin/codex")
    expect(absentCalls == 2)
}

private final class FakeCodexTransport: CodexJSONLTransport {
    var onData: ((Data) -> Void)?
    var onExit: (() -> Void)?
    private let lock = NSLock()
    private var running = false
    private var messages: [[String: Any]] = []

    var isRunning: Bool {
        lock.lock(); defer { lock.unlock() }
        return running
    }
    func start(path: String) -> Bool {
        lock.lock(); running = true; lock.unlock()
        return true
    }
    func send(_ data: Data) {
        guard let object = try? JSONSerialization.jsonObject(with: data) as? [String: Any]
        else { return }
        lock.lock(); messages.append(object); lock.unlock()
    }
    func stop() {
        lock.lock(); running = false; lock.unlock()
    }
    func latest(_ method: String) -> [String: Any]? {
        lock.lock(); defer { lock.unlock() }
        return messages.last { $0["method"] as? String == method }
    }
    func emit(_ object: [String: Any]) {
        let data = try! JSONSerialization.data(withJSONObject: object) + Data([10])
        onData?(data)
    }
    func exit() {
        lock.lock(); running = false; lock.unlock()
        onExit?()
    }
}

private final class FakeCodexFactory {
    private let lock = NSLock()
    private var transports: [FakeCodexTransport] = []
    func make() -> CodexJSONLTransport {
        let transport = FakeCodexTransport()
        lock.lock(); transports.append(transport); lock.unlock()
        return transport
    }
    func at(_ index: Int) -> FakeCodexTransport? {
        lock.lock(); defer { lock.unlock() }
        return index < transports.count ? transports[index] : nil
    }
}

private func waitUntil(_ condition: () -> Bool) -> Bool {
    let deadline = Date().addingTimeInterval(2)
    while Date() < deadline {
        if condition() { return true }
        Thread.sleep(forTimeInterval: 0.005)
    }
    return condition()
}

func codexRestartsAfterExitAndIgnoresOldProcessOutput() {
    let locator = CodexExecutableLocator(environment: { ["PATH": "/fake"] }, home: "/none",
        isExecutable: { $0 == "/fake/codex" }, loginShellPath: { nil })
    let factory = FakeCodexFactory()
    let provider = CodexUsageProvider(locator: locator, transportFactory: { factory.make() })
    let lock = NSLock()
    var completed = 0
    var finalSample: AiUsageProviderSnapshot?
    provider.refresh { _ in lock.lock(); completed += 1; lock.unlock() }
    expect(waitUntil { factory.at(0)?.latest("initialize") != nil })
    guard let first = factory.at(0) else { return }
    first.emit(["id": 1, "result": [:]])
    expect(waitUntil { first.latest("account/read") != nil })
    first.emit(["id": 2, "result": ["account": ["planType": "plus"]]])
    expect(waitUntil { first.latest("account/rateLimits/read") != nil })
    let staleOutput = first.onData
    first.exit()
    expect(waitUntil { lock.lock(); defer { lock.unlock() }; return completed == 1 })

    provider.refresh { outcome in
        lock.lock(); completed += 1; finalSample = outcome.sample; lock.unlock()
    }
    expect(waitUntil { factory.at(1)?.latest("initialize") != nil })
    guard let second = factory.at(1) else { return }
    second.emit(["id": 1, "result": [:]])
    expect(waitUntil { second.latest("account/read") != nil })
    second.emit(["id": 2, "result": ["account": ["planType": "plus"]]])
    expect(waitUntil { second.latest("account/rateLimits/read") != nil })
    guard let requestId = second.latest("account/rateLimits/read")?["id"] as? Int else {
        expect(false); return
    }
    let response: [String: Any] = ["id": requestId, "result": ["rateLimits": [
        "planType": "plus", "primary": ["usedPercent": 37,
            "windowDurationMins": 300, "resetsAt": 1790812800]]]]
    staleOutput?(try! JSONSerialization.data(withJSONObject: response) + Data([10]))
    Thread.sleep(forTimeInterval: 0.02)
    lock.lock(); let before = completed; lock.unlock()
    expect(before == 1)
    second.emit(response)
    expect(waitUntil { lock.lock(); defer { lock.unlock() }; return completed == 2 })
    lock.lock(); let sample = finalSample; lock.unlock()
    expect(sample?.provider == .codex)
    expect(sample?.metrics.first?.remainingPercent == 63)
    provider.stop()
}

func codexProviderHandlesAbsenceAndRealBusinessLimits() {
    let missing = CodexExecutableLocator(environment: { [:] }, home: "/none",
        isExecutable: { _ in false }, loginShellPath: { nil })
    let absentFactory = FakeCodexFactory()
    let absentProvider = CodexUsageProvider(locator: missing,
        transportFactory: { absentFactory.make() })
    let absentLock = NSLock()
    var absentResult: Bool?
    absentProvider.refresh { outcome in
        absentLock.lock(); absentResult = outcome == .absent; absentLock.unlock()
    }
    expect(waitUntil { absentLock.lock(); defer { absentLock.unlock() }; return absentResult != nil })
    absentLock.lock(); let wasAbsent = absentResult; absentLock.unlock()
    expect(wasAbsent == true)
    expect(absentFactory.at(0) == nil)
    absentProvider.stop()

    let locator = CodexExecutableLocator(environment: { ["PATH": "/fake"] }, home: "/none",
        isExecutable: { $0 == "/fake/codex" }, loginShellPath: { nil })
    let factory = FakeCodexFactory()
    let provider = CodexUsageProvider(locator: locator, transportFactory: { factory.make() })
    let lock = NSLock()
    var sample: AiUsageProviderSnapshot?
    var completed = false
    provider.refresh { outcome in
        lock.lock(); sample = outcome.sample; completed = true; lock.unlock()
    }
    expect(waitUntil { factory.at(0)?.latest("initialize") != nil })
    guard let transport = factory.at(0) else { return }
    transport.onData?(Data("{malformed\n".utf8))
    transport.emit(["id": 1, "result": [:]])
    expect(waitUntil { transport.latest("account/read") != nil })
    transport.emit(["id": 2, "result": ["account": ["planType": "business"]]])
    expect(waitUntil { transport.latest("account/rateLimits/read") != nil })
    guard let id = transport.latest("account/rateLimits/read")?["id"] as? Int else {
        expect(false); return
    }
    let business: [String: Any] = ["rateLimits": ["planType": "business",
        "individualLimit": ["limit": "20000", "used": "19765.35930800438",
                            "remainingPercent": 1, "resetsAt": 1790812800]]]
    transport.emit(["id": id + 10, "result": business])
    Thread.sleep(forTimeInterval: 0.02)
    lock.lock(); let premature = completed; lock.unlock()
    expect(!premature)
    transport.emit(["id": id, "result": business])
    expect(waitUntil { lock.lock(); defer { lock.unlock() }; return completed })
    lock.lock(); let value = sample; lock.unlock()
    expect(value?.plan == .business)
    expect(value?.metrics.first?.used == 19765)
    expect(value?.metrics.first?.remaining == 235)
    provider.stop()
}

private final class FakeUsageProvider: AiUsageProviderRefreshing {
    private let lock = NSLock()
    private var callbacks: [(AiUsageRefreshOutcome) -> Void] = []
    func refresh(_ done: @escaping (AiUsageRefreshOutcome) -> Void) {
        lock.lock(); callbacks.append(done); lock.unlock()
    }
    func stop() {
        lock.lock(); callbacks.removeAll(); lock.unlock()
    }
    var pending: Int {
        lock.lock(); defer { lock.unlock() }
        return callbacks.count
    }
    func complete(_ sample: AiUsageProviderSnapshot?, absent: Bool = false) {
        complete(absent ? .absent : sample.map { .sample($0) } ?? .failed)
    }
    func complete(_ outcome: AiUsageRefreshOutcome) {
        lock.lock()
        let callback = callbacks.removeFirst()
        lock.unlock()
        callback(outcome)
    }
}

private final class FakeRetryScheduler {
    private let lock = NSLock()
    private var actions: [() -> Void] = []
    private var intervals: [TimeInterval] = []
    var delays: [TimeInterval] {
        lock.lock(); defer { lock.unlock() }
        return intervals
    }
    func schedule(_ delay: TimeInterval, _ action: @escaping () -> Void) {
        lock.lock(); intervals.append(delay); actions.append(action); lock.unlock()
    }
    func fireNext() {
        lock.lock()
        let action = actions.removeFirst()
        lock.unlock()
        action()
    }
}

private final class FakePeriodicScheduler {
    private let lock = NSLock()
    private var action: (() -> Void)?
    private var scheduledInterval: TimeInterval?
    var interval: TimeInterval? {
        lock.lock(); defer { lock.unlock() }
        return scheduledInterval
    }
    func schedule(_ interval: TimeInterval, _ action: @escaping () -> Void) -> () -> Void {
        lock.lock()
        scheduledInterval = interval
        self.action = action
        lock.unlock()
        return { [weak self] in
            guard let self else { return }
            self.lock.lock()
            self.action = nil
            self.lock.unlock()
        }
    }
    func fire() {
        lock.lock(); let action = self.action; lock.unlock()
        action?()
    }
}

func collectorExpiresCachedFreshnessWhileRefreshIsPending() {
    let codex = FakeUsageProvider()
    let cursor = FakeUsageProvider()
    var clockNow = Date(timeIntervalSince1970: 1000)
    let collector = AiUsageCollector(codex: codex, cursor: cursor, now: { clockNow })
    let sample = AiUsageNormalization.codex(["planType": "plus",
        "primary": ["usedPercent": 37, "windowDurationMins": 300]])!
    collector.start()
    expect(waitUntil { codex.pending == 1 && cursor.pending == 1 })
    codex.complete(sample)
    cursor.complete(nil, absent: true)
    expect(waitUntil { collector.snapshot()?.providers.first?.freshness == .fresh })
    collector.refresh()
    expect(waitUntil { codex.pending == 1 && cursor.pending == 1 })
    clockNow = Date(timeIntervalSince1970: 1091)
    expect(collector.snapshot()?.providers.first?.freshness == .stale)
    codex.complete(sample)
    cursor.complete(nil, absent: true)
    expect(waitUntil { collector.snapshot()?.providers.first?.freshness == .fresh })
    collector.stop()
}

func collectorRefreshesEveryThirtySecondsOutsideMainRunLoop() {
    let codex = FakeUsageProvider()
    let cursor = FakeUsageProvider()
    let scheduler = FakePeriodicScheduler()
    let collector = AiUsageCollector(codex: codex, cursor: cursor,
                                     schedulePeriodic: scheduler.schedule)
    collector.start()
    expect(waitUntil { codex.pending == 1 && cursor.pending == 1 })
    expect(scheduler.interval == 30)
    codex.complete(nil, absent: true)
    cursor.complete(nil, absent: true)
    scheduler.fire()
    expect(waitUntil { codex.pending == 1 && cursor.pending == 1 })
    codex.complete(nil, absent: true)
    cursor.complete(nil, absent: true)
    collector.stop()
    scheduler.fire()
    Thread.sleep(forTimeInterval: 0.02)
    expect(codex.pending == 0 && cursor.pending == 0)
}

func collectorCoalescesRefreshRequestedDuringAnActiveCycle() {
    let codex = FakeUsageProvider()
    let cursor = FakeUsageProvider()
    let collector = AiUsageCollector(codex: codex, cursor: cursor)
    collector.start()
    expect(waitUntil { codex.pending == 1 && cursor.pending == 1 })
    collector.refresh()
    collector.refresh()
    codex.complete(nil, absent: true)
    cursor.complete(nil, absent: true)
    expect(waitUntil { codex.pending == 1 && cursor.pending == 1 })
    codex.complete(nil, absent: true)
    cursor.complete(nil, absent: true)
    expect(waitUntil { collector.snapshot()?.state == .ready })
    expect(codex.pending == 0 && cursor.pending == 0)
    collector.stop()
}

func collectorRetriesProviderFailuresWithBoundedBackoff() {
    let codex = FakeUsageProvider()
    let cursor = FakeUsageProvider()
    let scheduler = FakeRetryScheduler()
    let collector = AiUsageCollector(codex: codex, cursor: cursor,
                                     scheduleRetry: scheduler.schedule)
    collector.start()
    expect(waitUntil { codex.pending == 1 && cursor.pending == 1 })
    for (index, delay) in [1.0, 2, 4, 8, 16, 30, 30].enumerated() {
        codex.complete(nil)
        cursor.complete(nil, absent: true)
        expect(waitUntil { scheduler.delays.count == index + 1 })
        expect(scheduler.delays.last == delay)
        scheduler.fireNext()
        expect(waitUntil { codex.pending == 1 && cursor.pending == 1 })
    }
    let recovered = AiUsageNormalization.codex(["planType": "plus",
        "primary": ["usedPercent": 37, "windowDurationMins": 300]])!
    codex.complete(recovered)
    cursor.complete(nil, absent: true)
    expect(waitUntil { collector.snapshot()?.providers.first?.freshness == .fresh })
    collector.refresh()
    expect(waitUntil { codex.pending == 1 && cursor.pending == 1 })
    codex.complete(nil)
    cursor.complete(nil, absent: true)
    expect(waitUntil { scheduler.delays.count == 8 })
    expect(scheduler.delays.last == 1)
    collector.stop()
    scheduler.fireNext()
    Thread.sleep(forTimeInterval: 0.02)
    expect(codex.pending == 0 && cursor.pending == 0)
}

func collectorRetriesCodexProcessExitBetweenNormalRefreshes() {
    let locator = CodexExecutableLocator(environment: { ["PATH": "/fake"] }, home: "/none",
        isExecutable: { $0 == "/fake/codex" }, loginShellPath: { nil })
    let factory = FakeCodexFactory()
    let codex = CodexUsageProvider(locator: locator, transportFactory: { factory.make() })
    let cursor = FakeUsageProvider()
    let scheduler = FakeRetryScheduler()
    let collector = AiUsageCollector(codex: codex, cursor: cursor,
                                     scheduleRetry: scheduler.schedule)
    collector.start()
    expect(waitUntil { factory.at(0)?.latest("initialize") != nil && cursor.pending == 1 })
    guard let first = factory.at(0) else { collector.stop(); return }
    first.emit(["id": 1, "result": [:]])
    expect(waitUntil { first.latest("account/read") != nil })
    first.emit(["id": 2, "result": ["account": ["planType": "plus"]]])
    expect(waitUntil { first.latest("account/rateLimits/read") != nil })
    guard let requestId = first.latest("account/rateLimits/read")?["id"] as? Int else {
        collector.stop(); return
    }
    first.emit(["id": requestId, "result": ["rateLimits": ["planType": "plus",
        "primary": ["usedPercent": 37, "windowDurationMins": 300]]]])
    cursor.complete(nil, absent: true)
    expect(waitUntil { collector.snapshot()?.providers.first?.freshness == .fresh })
    first.exit()
    expect(waitUntil { scheduler.delays == [1] })
    expect(collector.snapshot()?.providers.first?.freshness == .stale)
    guard scheduler.delays == [1] else { collector.stop(); return }
    scheduler.fireNext()
    expect(waitUntil { factory.at(1)?.latest("initialize") != nil })
    collector.stop()
}

func collectorPublishesHomeAndWorkProviderSets() {
    let business = AiUsageNormalization.codex(["planType": "business",
        "individualLimit": ["limit": "20000", "used": "19765.35930800438",
                            "remainingPercent": 1, "resetsAt": 1790812800]])!
    let cursor = AiUsageNormalization.cursor(["membershipType": "enterprise",
        "billingCycleEnd": "2026-10-01T00:00:00.000Z",
        "individualUsage": ["overall": ["enabled": true, "used": 9458,
                                          "limit": 255000, "remaining": 245542]]])!
    let codexFake = FakeUsageProvider()
    let cursorFake = FakeUsageProvider()
    let work = AiUsageCollector(codex: codexFake, cursor: cursorFake)
    work.start()
    expect(waitUntil { codexFake.pending == 1 && cursorFake.pending == 1 })
    codexFake.complete(business)
    cursorFake.complete(cursor)
    expect(waitUntil { work.snapshot()?.providers.count == 2 })
    expect(work.snapshot()?.providers.map(\.provider) == [.codex, .cursor])
    expect(work.snapshot()?.providers[0].plan == .business)
    expect(work.snapshot()?.providers[1].plan == .enterprise)
    work.stop()
    expect(work.snapshot()?.providers.isEmpty == true)

    let plus = AiUsageNormalization.codex(["planType": "plus",
        "primary": ["usedPercent": 37, "windowDurationMins": 300],
        "secondary": ["usedPercent": 62, "windowDurationMins": 10080]])!
    let homeCodex = FakeUsageProvider()
    let homeCursor = FakeUsageProvider()
    let home = AiUsageCollector(codex: homeCodex, cursor: homeCursor)
    home.start()
    expect(waitUntil { homeCodex.pending == 1 && homeCursor.pending == 1 })
    homeCodex.complete(plus)
    homeCursor.complete(nil, absent: true)
    expect(waitUntil { home.snapshot()?.state == .ready &&
        home.snapshot()?.providers.count == 1 })
    expect(home.snapshot()?.providers[0].provider == .codex)
    expect(home.snapshot()?.providers[0].metrics.map(\.kind) == [.fiveHour, .week])
    home.stop()
}

func cursorUnauthorizedRefreshKeepsPreviousSampleStale() {
    let codex = FakeUsageProvider()
    let http = FakeHTTP()
    let timeLock = NSLock()
    var clockNow = Date(timeIntervalSince1970: 1000)
    let clock = { () -> Date in
        timeLock.lock(); defer { timeLock.unlock() }
        return clockNow
    }
    let cursor = CursorUsageProvider(credentials: FakeCursorCredentials(.value(
        fakeJWT(sub: "auth0|user_123"))), http: http)
    let collector = AiUsageCollector(codex: codex, cursor: cursor, now: clock)
    collector.start()
    expect(waitUntil { codex.pending == 1 && http.requests.count == 1 })
    codex.complete(nil, absent: true)
    http.respond(status: 200, object: ["membershipType": "enterprise",
        "individualUsage": ["overall": ["enabled": true, "used": 9458,
                                          "limit": 255000, "remaining": 245542]]])
    expect(waitUntil { collector.snapshot()?.providers.first?.freshness == .fresh })

    collector.refresh()
    expect(waitUntil { codex.pending == 1 && http.requests.count == 2 })
    codex.complete(nil, absent: true)
    http.respond(status: 401)
    collector.refresh()
    expect(waitUntil { codex.pending == 1 && http.requests.count == 3 })
    expect(collector.snapshot()?.providers.first?.freshness == .fresh)
    timeLock.lock(); clockNow = Date(timeIntervalSince1970: 1091); timeLock.unlock()
    codex.complete(nil, absent: true)
    http.respond(status: 401)
    expect(waitUntil { collector.snapshot()?.providers.first?.freshness == .stale })
    expect(collector.snapshot()?.providers.first?.provider == .cursor)
    collector.refresh()
    expect(waitUntil { codex.pending == 1 && http.requests.count == 4 })
    codex.complete(nil, absent: true)
    http.respond(status: 200, object: ["membershipType": "enterprise",
        "individualUsage": ["overall": ["enabled": true, "used": 9458,
                                          "limit": 255000, "remaining": 245542]]])
    expect(waitUntil { collector.snapshot()?.providers.first?.freshness == .fresh })
    collector.stop()
}

private final class FakeClaudeCredentials: ClaudeCredentialReading {
    private let lock = NSLock()
    private var storedResult: ClaudeCredentialResult
    private var storedReads = 0
    init(_ result: ClaudeCredentialResult) { storedResult = result }
    var result: ClaudeCredentialResult {
        get { lock.lock(); defer { lock.unlock() }; return storedResult }
        set { lock.lock(); storedResult = newValue; lock.unlock() }
    }
    var reads: Int { lock.lock(); defer { lock.unlock() }; return storedReads }
    func read() -> ClaudeCredentialResult {
        lock.lock(); defer { lock.unlock() }
        storedReads += 1
        return storedResult
    }
}

private final class RefreshResult {
    private let lock = NSLock()
    private var value: AiUsageRefreshOutcome?
    var outcome: AiUsageRefreshOutcome? { lock.lock(); defer { lock.unlock() }; return value }
    var sample: AiUsageProviderSnapshot? { outcome?.sample }
    var done: Bool { outcome != nil }
    func set(_ outcome: AiUsageRefreshOutcome) { lock.lock(); value = outcome; lock.unlock() }
}

private final class TestClock {
    private let lock = NSLock()
    private var value = Date(timeIntervalSince1970: 10_000)
    var now: Date {
        get { lock.lock(); defer { lock.unlock() }; return value }
        set { lock.lock(); value = newValue; lock.unlock() }
    }
}

private final class BlockingClaudeCredentials: ClaudeCredentialReading {
    let release = DispatchSemaphore(value: 0)
    private let lock = NSLock()
    private var storedReads = 0
    var reads: Int { lock.lock(); defer { lock.unlock() }; return storedReads }
    func read() -> ClaudeCredentialResult {
        lock.lock(); storedReads += 1; lock.unlock()
        release.wait()
        return claudeCredential()
    }
}

private func claudeCredential(token: String = "claude-token",
                              expiresAt: Date? = Date(timeIntervalSince1970: 13_600))
    -> ClaudeCredentialResult {
    .value(ClaudeCredential(accessToken: token, subscriptionType: "pro", expiresAt: expiresAt))
}
private let claudeUsage: [String: Any] = ["limits": [
    ["kind": "session", "percent": 8, "resets_at": "1970-01-01T03:00:00Z", "scope": NSNull()],
    ["kind": "weekly_all", "percent": 1, "resets_at": "1970-01-02T00:00:00Z", "scope": NSNull()],
]]

private func claudeProvider(_ credentials: ClaudeCredentialReading, _ http: FakeHTTP,
                            _ clock: TestClock, readTimeout: TimeInterval = 2)
    -> ClaudeUsageProvider {
    ClaudeUsageProvider(credentials: credentials, http: http, now: { clock.now },
                        readTimeout: readTimeout)
}

private func refreshed(_ provider: ClaudeUsageProvider) -> RefreshResult {
    let result = RefreshResult()
    provider.refresh { result.set($0) }
    return result
}

func claudeBuildsOAuthUsageRequest() {
    let http = FakeHTTP()
    let provider = claudeProvider(FakeClaudeCredentials(claudeCredential()), http, TestClock())
    let result = refreshed(provider)
    expect(waitUntil { http.requests.count == 1 })
    let request = http.requests.first
    expect(request?.url?.absoluteString == "https://api.anthropic.com/api/oauth/usage")
    expect(request?.value(forHTTPHeaderField: "Authorization") == "Bearer claude-token")
    expect(request?.value(forHTTPHeaderField: "anthropic-beta") == "oauth-2025-04-20")
    http.respond(status: 200, object: claudeUsage)
    expect(waitUntil { result.done })
    expect(result.sample?.provider == .claude && result.sample?.plan == .pro)
    expect(result.sample?.metrics.map(\.remainingPercent) == [92, 99])
}

func claudeOmitsAbsentCredential() {
    let http = FakeHTTP()
    let result = refreshed(claudeProvider(FakeClaudeCredentials(.absent), http, TestClock()))
    expect(waitUntil { result.done })
    expect(result.outcome == .absent)
    expect(http.requests.isEmpty)
}

func claudeCachesTokenAndRechecksKeychain() {
    let http = FakeHTTP()
    let clock = TestClock()
    let credentials = FakeClaudeCredentials(claudeCredential())
    let provider = claudeProvider(credentials, http, clock)
    let first = refreshed(provider)
    expect(waitUntil { http.requests.count == 1 })
    http.respond(status: 200, object: claudeUsage)
    expect(waitUntil { first.done })
    // The recheck runs beside a request with the cached token; a sign-in to
    // another account replaces it on a following refresh.
    clock.now = Date(timeIntervalSince1970: 10_300)
    credentials.result = claudeCredential(token: "switched")
    let second = refreshed(provider)
    expect(waitUntil { http.requests.count == 2 })
    expect(http.requests.last?.value(forHTTPHeaderField: "Authorization") == "Bearer claude-token")
    http.respond(status: 200, object: claudeUsage)
    expect(waitUntil { second.done && credentials.reads == 2 })
    Thread.sleep(forTimeInterval: 0.02)
    clock.now = Date(timeIntervalSince1970: 10_600)
    let third = refreshed(provider)
    expect(waitUntil { http.requests.count == 3 })
    expect(http.requests.last?.value(forHTTPHeaderField: "Authorization") == "Bearer switched")
    http.respond(status: 200, object: claudeUsage)
    expect(waitUntil { third.done && credentials.reads == 3 })
    Thread.sleep(forTimeInterval: 0.02)
    // An expired token is read again before any request.
    clock.now = Date(timeIntervalSince1970: 13_600)
    credentials.result = claudeCredential(token: "renewed", expiresAt: Date(timeIntervalSince1970: 40_000))
    _ = refreshed(provider)
    expect(waitUntil { http.requests.count == 4 })
    expect(credentials.reads == 4)
    expect(http.requests.last?.value(forHTTPHeaderField: "Authorization") == "Bearer renewed")
    http.respond(status: 200, object: claudeUsage)
    // Signing out removes the token after the next recheck.
    clock.now = Date(timeIntervalSince1970: 14_000)
    credentials.result = .absent
    expect(waitUntil {
        let result = refreshed(provider)
        return waitUntil { result.done } && result.outcome == .absent
    })
}

func claudeRecheckKeepsTokenWhenKeychainFails() {
    let http = FakeHTTP()
    let clock = TestClock()
    let credentials = FakeClaudeCredentials(claudeCredential())
    let provider = claudeProvider(credentials, http, clock)
    _ = refreshed(provider)
    expect(waitUntil { http.requests.count == 1 })
    http.respond(status: 200, object: claudeUsage)
    clock.now = Date(timeIntervalSince1970: 10_300)
    credentials.result = .failed
    let result = refreshed(provider)
    expect(waitUntil { http.requests.count == 2 })
    http.respond(status: 200, object: claudeUsage)
    expect(waitUntil { result.done })
    expect(result.sample?.provider == .claude)
    expect(waitUntil { credentials.reads == 2 })
    Thread.sleep(forTimeInterval: 0.02)
    clock.now = Date(timeIntervalSince1970: 10_600)
    _ = refreshed(provider)
    expect(waitUntil { http.requests.count == 3 })
    expect(http.requests.last?.value(forHTTPHeaderField: "Authorization") == "Bearer claude-token")
}

func claudeThrottlesRequestsAndServesAgedSample() {
    let http = FakeHTTP()
    let clock = TestClock()
    let provider = claudeProvider(FakeClaudeCredentials(claudeCredential()), http, clock)
    let first = refreshed(provider)
    expect(waitUntil { http.requests.count == 1 })
    http.respond(status: 200, object: claudeUsage)
    expect(waitUntil { first.done })
    clock.now = Date(timeIntervalSince1970: 10_030)
    let cached = refreshed(provider)
    expect(waitUntil { cached.done })
    expect(http.requests.count == 1)
    expect(cached.sample?.metrics.map(\.remainingPercent) == [92, 99])
    expect(cached.sample?.metrics.first?.resetRemainingSeconds == 10_800 - 10_030)
    clock.now = Date(timeIntervalSince1970: 10_300)
    _ = refreshed(provider)
    expect(waitUntil { http.requests.count == 2 })
}

func claudeRateLimitBacksOffWithoutFailure() {
    let http = FakeHTTP()
    let clock = TestClock()
    let provider = claudeProvider(FakeClaudeCredentials(claudeCredential(
        expiresAt: Date(timeIntervalSince1970: 90_000))), http, clock)
    let first = refreshed(provider)
    expect(waitUntil { http.requests.count == 1 })
    http.respond(status: 200, object: claudeUsage)
    expect(waitUntil { first.done })

    // Retry-After wins over the first one-minute pause; the sample stays current.
    clock.now = Date(timeIntervalSince1970: 10_100)
    let limited = refreshed(provider)
    expect(waitUntil { http.requests.count == 2 })
    http.respond(status: 429, headers: ["Retry-After": "600"])
    expect(waitUntil { limited.done })
    expect(limited.sample?.provider == .claude)
    clock.now = Date(timeIntervalSince1970: 10_200)
    let old = refreshed(provider)
    expect(waitUntil { old.done })
    expect(old.outcome == .unavailable && http.requests.count == 2)

    // Without Retry-After the pause doubles to two minutes.
    clock.now = Date(timeIntervalSince1970: 10_701)
    let again = refreshed(provider)
    expect(waitUntil { http.requests.count == 3 })
    http.respond(status: 429)
    expect(waitUntil { again.done })
    expect(again.outcome == .unavailable)
    clock.now = Date(timeIntervalSince1970: 10_800)
    _ = refreshed(provider)
    Thread.sleep(forTimeInterval: 0.02)
    expect(http.requests.count == 3)
    clock.now = Date(timeIntervalSince1970: 10_822)
    let recovered = refreshed(provider)
    expect(waitUntil { http.requests.count == 4 })
    http.respond(status: 200, object: claudeUsage)
    expect(waitUntil { recovered.done })
    expect(recovered.sample?.provider == .claude)
}

func claudeKeychainPromptIsUnavailableNotFailure() {
    let http = FakeHTTP()
    let credentials = BlockingClaudeCredentials()
    let provider = claudeProvider(credentials, http, TestClock(), readTimeout: 0.05)
    let waiting = refreshed(provider)
    expect(waitUntil { waiting.done })
    expect(waiting.outcome == .waiting)
    let again = refreshed(provider)
    expect(waitUntil { again.done })
    expect(again.outcome == .waiting && credentials.reads == 1)
    credentials.release.signal()
    // Refreshes keep waiting until the read lands, then use its token.
    expect(waitUntil { _ = refreshed(provider); return !http.requests.isEmpty })
    Thread.sleep(forTimeInterval: 0.02)
    expect(http.requests.count == 1 && credentials.reads == 1)
}

func httpTransportCanBeReusedAfterStop() {
    let http = URLSessionAiUsageHTTP()
    http.stop()
    let lock = NSLock()
    var completed = false
    http.get(URLRequest(url: URL(fileURLWithPath: "/dev/null"))) { _, _, _ in
        lock.lock(); completed = true; lock.unlock()
    }
    expect(waitUntil { lock.lock(); defer { lock.unlock() }; return completed })
    http.stop()
}

func claudeExpiredTokenIsUnavailable() {
    let http = FakeHTTP()
    let clock = TestClock()
    let credentials = FakeClaudeCredentials(claudeCredential(expiresAt: clock.now))
    let provider = claudeProvider(credentials, http, clock)
    let expired = refreshed(provider)
    expect(waitUntil { expired.done })
    expect(expired.outcome == .unavailable && http.requests.isEmpty)

    credentials.result = claudeCredential(token: "revoked")
    let unauthorized = refreshed(provider)
    expect(waitUntil { http.requests.count == 1 })
    http.respond(status: 401)
    expect(waitUntil { unauthorized.done })
    expect(unauthorized.outcome == .failed)
    let reads = credentials.reads
    _ = refreshed(provider)
    expect(waitUntil { http.requests.count == 2 })
    expect(credentials.reads == reads + 1)

    credentials.result = .failed
    clock.now = Date(timeIntervalSince1970: 50_000)
    let failed = refreshed(provider)
    expect(waitUntil { failed.done })
    expect(failed.outcome == .failed && http.requests.count == 2)
}

func claudeDenialPausesKeychainReads() {
    let http = FakeHTTP()
    let clock = TestClock()
    let credentials = FakeClaudeCredentials(.denied)
    let provider = claudeProvider(credentials, http, clock)
    let first = refreshed(provider)
    expect(waitUntil { first.done })
    expect(first.outcome == .absent)
    credentials.result = claudeCredential(expiresAt: Date(timeIntervalSince1970: 20_000))
    let paused = refreshed(provider)
    expect(waitUntil { paused.done })
    expect(paused.outcome == .absent && credentials.reads == 1 && http.requests.isEmpty)
    clock.now = Date(timeIntervalSince1970: 13_601)
    _ = refreshed(provider)
    expect(waitUntil { http.requests.count == 1 })
    expect(credentials.reads == 2)
}

func collectorKeepsUnavailableSampleStaleWithoutRetry() {
    let codex = FakeUsageProvider()
    let cursor = FakeUsageProvider()
    let claude = FakeUsageProvider()
    let scheduler = FakeRetryScheduler()
    let clock = TestClock()
    let collector = AiUsageCollector(codex: codex, cursor: cursor, claude: claude,
                                     now: { clock.now }, scheduleRetry: scheduler.schedule)
    let sample = AiUsageNormalization.claude(claudeUsage, subscriptionType: "pro", now: clock.now)!
    collector.start()
    expect(waitUntil { claude.pending == 1 && codex.pending == 1 && cursor.pending == 1 })
    codex.complete(nil, absent: true)
    cursor.complete(nil, absent: true)
    claude.complete(sample)
    expect(waitUntil { collector.snapshot()?.providers.first?.freshness == .fresh })
    collector.refresh()
    expect(waitUntil { claude.pending == 1 && codex.pending == 1 && cursor.pending == 1 })
    codex.complete(nil, absent: true)
    cursor.complete(nil, absent: true)
    claude.complete(.unavailable)
    expect(waitUntil { collector.snapshot()?.providers.first?.freshness == .stale })
    expect(collector.snapshot()?.state == .ready)
    expect(collector.snapshot()?.providers.first?.metrics == sample.metrics)
    Thread.sleep(forTimeInterval: 0.02)
    expect(scheduler.delays.isEmpty)
    collector.stop()
}

func collectorKeepsDiscoveringWhileWaitingForUser() {
    let codex = FakeUsageProvider()
    let cursor = FakeUsageProvider()
    let claude = FakeUsageProvider()
    let scheduler = FakeRetryScheduler()
    let collector = AiUsageCollector(codex: codex, cursor: cursor, claude: claude,
                                     scheduleRetry: scheduler.schedule)
    collector.start()
    expect(waitUntil { claude.pending == 1 && codex.pending == 1 && cursor.pending == 1 })
    codex.complete(nil, absent: true)
    cursor.complete(nil, absent: true)
    claude.complete(.waiting)
    expect(waitUntil { codex.pending == 0 && claude.pending == 0 })
    Thread.sleep(forTimeInterval: 0.02)
    expect(collector.snapshot()?.state == .discovering && scheduler.delays.isEmpty)
    collector.refresh()
    expect(waitUntil { claude.pending == 1 && codex.pending == 1 && cursor.pending == 1 })
    codex.complete(nil, absent: true)
    cursor.complete(nil, absent: true)
    claude.complete(AiUsageNormalization.claude(claudeUsage, subscriptionType: "pro")!)
    expect(waitUntil { collector.snapshot()?.state == .ready &&
        collector.snapshot()?.providers.count == 1 })
    collector.stop()
}

func collectorPublishesPersonalCodexAndClaude() {
    let plus = AiUsageNormalization.codex(["planType": "plus",
        "primary": ["usedPercent": 37, "windowDurationMins": 300],
        "secondary": ["usedPercent": 62, "windowDurationMins": 10080]])!
    let claude = AiUsageNormalization.claude(claudeUsage, subscriptionType: "pro")!
    let cursor = AiUsageNormalization.cursor(["membershipType": "enterprise",
        "individualUsage": ["overall": ["enabled": true, "used": 9458,
                                          "limit": 255000, "remaining": 245542]]])!
    let codexFake = FakeUsageProvider()
    let cursorFake = FakeUsageProvider()
    let claudeFake = FakeUsageProvider()
    let collector = AiUsageCollector(codex: codexFake, cursor: cursorFake, claude: claudeFake)
    collector.start()
    expect(waitUntil { codexFake.pending == 1 && cursorFake.pending == 1 &&
        claudeFake.pending == 1 })
    codexFake.complete(plus)
    cursorFake.complete(nil, absent: true)
    claudeFake.complete(claude)
    expect(waitUntil { collector.snapshot()?.state == .ready &&
        collector.snapshot()?.providers.count == 2 })
    expect(collector.snapshot()?.providers.map(\.provider) == [.codex, .claude])

    collector.refresh()
    expect(waitUntil { codexFake.pending == 1 && cursorFake.pending == 1 &&
        claudeFake.pending == 1 })
    codexFake.complete(plus)
    cursorFake.complete(cursor)
    claudeFake.complete(claude)
    expect(waitUntil { collector.snapshot()?.providers.map(\.provider) == [.codex, .cursor, .claude] })
    expect(collector.snapshot()?.encode().flatMap(AiUsageSnapshot.decode)?
        .providers.map(\.provider) == [.codex, .cursor])
    collector.stop()
}

func collectorShowsElapsedStaleWindowsAsReset() {
    let clock = TestClock()
    let codex = FakeUsageProvider()
    let cursor = FakeUsageProvider()
    let collector = AiUsageCollector(codex: codex, cursor: cursor, now: { clock.now })
    let plus = AiUsageNormalization.codex(["planType": "plus",
        "primary": ["usedPercent": 90, "windowDurationMins": 300, "resetsAt": 10_800]],
        now: clock.now)!
    collector.start()
    expect(waitUntil { codex.pending == 1 && cursor.pending == 1 })
    codex.complete(plus)
    cursor.complete(nil, absent: true)
    expect(waitUntil { collector.snapshot()?.providers.first?.metrics.first?.remainingPercent == 10 })
    clock.now = Date(timeIntervalSince1970: 10_801)
    let metric = collector.snapshot()?.providers.first?.metrics.first
    expect(collector.snapshot()?.providers.first?.freshness == .stale)
    expect(metric?.remainingPercent == 100 && metric?.resetAt == 0)
    collector.stop()
}

@main enum CompanionProvidersCheck {
    static func main() {
        providerPackageBoundary()
        cursorBuildsVerifiedEnterpriseCookie()
        cursorOmitsAbsentCredentialAndRejectsMalformedJWT()
        codexExecutableDiscoveryChecksDirectPathsBeforeLoginShell()
        codexExecutableDiscoveryCachesSuccessAndRetriesNegativeLoginShellResult()
        codexRestartsAfterExitAndIgnoresOldProcessOutput()
        codexProviderHandlesAbsenceAndRealBusinessLimits()
        collectorPublishesHomeAndWorkProviderSets()
        collectorRefreshesEveryThirtySecondsOutsideMainRunLoop()
        collectorExpiresCachedFreshnessWhileRefreshIsPending()
        collectorCoalescesRefreshRequestedDuringAnActiveCycle()
        collectorRetriesProviderFailuresWithBoundedBackoff()
        collectorRetriesCodexProcessExitBetweenNormalRefreshes()
        cursorUnauthorizedRefreshKeepsPreviousSampleStale()
        claudeBuildsOAuthUsageRequest()
        claudeOmitsAbsentCredential()
        claudeCachesTokenAndRechecksKeychain()
        claudeRecheckKeepsTokenWhenKeychainFails()
        claudeThrottlesRequestsAndServesAgedSample()
        claudeRateLimitBacksOffWithoutFailure()
        claudeKeychainPromptIsUnavailableNotFailure()
        httpTransportCanBeReusedAfterStop()
        collectorKeepsDiscoveringWhileWaitingForUser()
        claudeExpiredTokenIsUnavailable()
        claudeDenialPausesKeychainReads()
        collectorKeepsUnavailableSampleStaleWithoutRetry()
        collectorPublishesPersonalCodexAndClaude()
        collectorShowsElapsedStaleWindowsAsReset()
        if failures > 0 { exit(1) }
        print("companion provider checks passed")
    }
}
