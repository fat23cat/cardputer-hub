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

private final class FakeCursorHTTP: CursorHTTPTransport {
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
    func respond(status: Int, object: [String: Any]? = nil) {
        lock.lock()
        let url = storedRequests.last!.url!
        let callback = completion
        lock.unlock()
        let response = HTTPURLResponse(url: url, statusCode: status,
                                       httpVersion: nil, headerFields: nil)!
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
    let http = FakeCursorHTTP()
    let provider = CursorUsageProvider(credentials: FakeCursorCredentials(.value(token)), http: http)
    var sample: AiUsageProviderSnapshot?
    provider.refresh { value, _ in sample = value }
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
    let http = FakeCursorHTTP()
    let credentials = FakeCursorCredentials(.absent)
    let provider = CursorUsageProvider(credentials: credentials, http: http)
    var absent = false
    provider.refresh { _, missing in absent = missing }
    expect(absent)
    expect(http.requests.isEmpty)

    credentials.result = .value("not-a-jwt")
    var failed = false
    provider.refresh { _, missing in failed = !missing }
    expect(failed)
    expect(http.requests.isEmpty)

    credentials.result = .value(fakeJWT(sub: nil))
    provider.refresh { _, missing in failed = !missing }
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

func codexExecutableDiscoveryQueriesLoginShellOnceAndCachesValidResult() {
    var shellCalls = 0
    let locator = CodexExecutableLocator(environment: { ["PATH": "/missing"] },
        home: "/Users/test", isExecutable: { $0 == "/login/bin/codex" },
        loginShellPath: { shellCalls += 1; return "/login/bin/codex" })
    expect(locator.find() == "/login/bin/codex")
    expect(locator.find() == "/login/bin/codex")
    expect(shellCalls == 1)

    var absentCalls = 0
    let absent = CodexExecutableLocator(environment: { [:] }, home: "/Users/test",
        isExecutable: { _ in false },
        loginShellPath: { absentCalls += 1; return nil })
    expect(absent.find() == nil)
    expect(absent.find() == nil)
    expect(absentCalls == 1)
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
    provider.refresh { _, _ in lock.lock(); completed += 1; lock.unlock() }
    expect(waitUntil { factory.at(0)?.latest("initialize") != nil })
    guard let first = factory.at(0) else { return }
    first.emit(["id": 1, "result": [:]])
    expect(waitUntil { first.latest("account/read") != nil })
    first.emit(["id": 2, "result": ["account": ["planType": "plus"]]])
    expect(waitUntil { first.latest("account/rateLimits/read") != nil })
    let staleOutput = first.onData
    first.exit()
    expect(waitUntil { lock.lock(); defer { lock.unlock() }; return completed == 1 })

    provider.refresh { sample, _ in
        lock.lock(); completed += 1; finalSample = sample; lock.unlock()
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
    absentProvider.refresh { _, absent in
        absentLock.lock(); absentResult = absent; absentLock.unlock()
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
    provider.refresh { value, _ in
        lock.lock(); sample = value; completed = true; lock.unlock()
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
    private var callbacks: [((AiUsageProviderSnapshot?, Bool) -> Void)] = []
    func refresh(_ done: @escaping (AiUsageProviderSnapshot?, Bool) -> Void) {
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
        lock.lock()
        let callback = callbacks.removeFirst()
        lock.unlock()
        callback(sample, absent)
    }
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
    let http = FakeCursorHTTP()
    let cursor = CursorUsageProvider(credentials: FakeCursorCredentials(.value(
        fakeJWT(sub: "auth0|user_123"))), http: http)
    let collector = AiUsageCollector(codex: codex, cursor: cursor)
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
    expect(waitUntil { collector.snapshot()?.providers.first?.freshness == .stale })
    expect(collector.snapshot()?.providers.first?.provider == .cursor)
    collector.stop()
}

@main enum CompanionProvidersCheck {
    static func main() {
        providerPackageBoundary()
        cursorBuildsVerifiedEnterpriseCookie()
        cursorOmitsAbsentCredentialAndRejectsMalformedJWT()
        codexExecutableDiscoveryChecksDirectPathsBeforeLoginShell()
        codexExecutableDiscoveryQueriesLoginShellOnceAndCachesValidResult()
        codexRestartsAfterExitAndIgnoresOldProcessOutput()
        codexProviderHandlesAbsenceAndRealBusinessLimits()
        collectorPublishesHomeAndWorkProviderSets()
        cursorUnauthorizedRefreshKeepsPreviousSampleStale()
        if failures > 0 { exit(1) }
        print("companion provider checks passed")
    }
}
