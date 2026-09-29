import CompanionCore
import Foundation

@main
enum CompanionCoreCheck {
    static func main() {
        var failed = 0
        func expect(_ condition: Bool, _ name: String) {
            if condition { return }
            fputs("FAIL \(name)\n", stderr)
            failed += 1
        }

        let cpuPrevious = CPUTimeTicks(user: 100, system: 100, idle: 700, nice: 100)
        let cpuCurrent = CPUTimeTicks(user: 110, system: 120, idle: 760, nice: 110)
        expect(CPUTimeTicks.usagePercent(from: cpuPrevious, to: cpuCurrent) == 40,
               "CPU usage excludes idle ticks and includes nice ticks")
        let memory = SystemMemoryUsage.estimate(totalBytes: 16 * 1_073_741_824,
                                                pageBytes: 16_384,
                                                freePages: 6_400,
                                                fileBackedPages: 160_000)
        expect(memory?.usedMiB == 13_784 && memory?.totalMiB == 16_384,
               "RAM usage excludes file cache but retains inactive app memory")

        let displayNow = Date(timeIntervalSince1970: 10_000)
        expect(CompanionPresentation.connection(session: 0, phase: .idle, error: false) == .disconnected,
               "idle presentation is disconnected")
        expect(CompanionPresentation.connection(session: 0, phase: .handshaking, error: false) == .connecting,
               "handshake presentation is connecting")
        expect(CompanionPresentation.connection(session: 8, phase: .handshaking, error: false) == .connected,
               "negotiated session presentation is connected")
        expect(CompanionPresentation.connection(session: 0, phase: .idle, error: true) == .error,
               "failure presentation is error")
        expect(CompanionPresentation.connection(session: 0, phase: .cancelling, error: false) == .disconnected,
               "completed cancellation is disconnected")
        expect(CompanionPresentation.connection(session: 0, phase: .cancelling, error: false,
                                                waitingToConnect: true) == .connecting,
               "pending replacement connection is connecting")
        expect(CompanionPresentation.lastSeen(Date(timeIntervalSince1970: 9_999), now: displayNow) == "Just now",
               "last seen just now")
        expect(CompanionPresentation.lastSeen(Date(timeIntervalSince1970: 9_992), now: displayNow) == "8s ago",
               "last seen seconds")
        expect(CompanionPresentation.lastSeen(Date(timeIntervalSince1970: 9_760), now: displayNow) == "4m ago",
               "last seen minutes")
        expect(CompanionPresentation.lastSeen(Date(timeIntervalSince1970: 6_400), now: displayNow) == "1h ago",
               "last seen hours")
expect(CompanionPresentation.statusMetadata(connection: .connected, firmwareBuildId: "2026-09-29 abc1234",
                                                    lastMessageAt: Date(timeIntervalSince1970: 9_999),
                                                    now: displayNow) == "Cardputer 2026-09-29 abc1234 · just now",
               "connected status names the Cardputer build")
        expect(CompanionPresentation.statusMetadata(connection: .disconnected, firmwareBuildId: "2026-09-29 abc1234",
                                                    lastMessageAt: Date(timeIntervalSince1970: 9_999),
                                                    now: displayNow) == nil,
               "disconnected status hides stale build metadata")
        expect(CompanionPresentation.sessionDuration(Date(timeIntervalSince1970: 8_878), now: displayNow) == "18m 42s",
               "session duration")
        expect(CompanionPresentation.compatibilityNotice(.mismatch(firmwareBuildId: "2026-09-29 abc1234"),
                                                         companionBuildId: "2026-09-20 fff0000") ==
               "Update this Companion — Cardputer runs 2026-09-29 abc1234", "older Companion is named")
        expect(CompanionPresentation.compatibilityNotice(.mismatch(firmwareBuildId: "2026-09-20 abc1234"),
                                                         companionBuildId: "2026-09-29 fff0000") ==
               "Update Cardputer firmware (2026-09-20 abc1234)", "older firmware is named")
        expect(CompanionPresentation.compatibilityNotice(.mismatch(firmwareBuildId: "2026-09-29 abc1234"),
                                                         companionBuildId: "dev") ==
               "Rebuild firmware and Companion from one commit", "undated builds rebuild both")
        expect(CompanionPresentation.compatibilityNotice(.noAnswer, companionBuildId: "dev")?
                .hasPrefix("No answer") == true, "unanswered HELLO hints at old firmware")
        expect(CompanionPresentation.compatibilityNotice(.matched(firmwareBuildId: "x"), companionBuildId: "y") == nil,
               "matched builds need no notice")
        expect(BuildIdentity.buildId(info: [BuildIdentity.infoKey: "2026-09-29 abc1234+"]) == "2026-09-29 abc1234+" &&
               BuildIdentity.buildId(info: nil) == "dev" &&
               BuildIdentity.buildId(info: [BuildIdentity.infoKey: String(repeating: "a", count: 25)]) == "dev",
               "build id comes from Info.plist and is bounded")
        let loginService = FakeLoginRegistration()
        let login = StartAtLoginModel(service: loginService)
        expect(!login.enabled && !login.hasError, "login state reads service")
        login.setEnabled(true)
        expect(login.enabled && !login.hasError, "login enable reflects service")
        loginService.shouldFail = true
        login.setEnabled(false)
        expect(login.enabled && login.hasError, "login failure retains actual state")
        loginService.shouldFail = false
        login.setEnabled(false)
        expect(!login.enabled && !login.hasError, "login retry clears error")
        let externalService = FakeLoginRegistration()
        let externalLogin = StartAtLoginModel(service: externalService)
        externalService.shouldFail = true
        externalLogin.setEnabled(true)
        externalLogin.refresh()
        expect(!externalLogin.enabled && externalLogin.hasError,
               "login error remains while registration is still disabled")
        externalService.isEnabled = true
        externalLogin.refresh()
        expect(externalLogin.enabled && !externalLogin.hasError,
               "login error clears when external approval enables registration")

let hello = CompanionCodec.hello(CompanionHello(buildId: "2026-09-29 abc1234")).flatMap(CompanionCodec.encode)
        expect(hello == fixture("hello.bin"), "hello carries fingerprint and build id")
        var ack = CompanionEnvelope()
        ack.kind = .helloAck
        ack.session = 42
        ack.payload = CompanionHello(buildId: "2026-09-29 abc1234").payload!
        expect(CompanionCodec.encode(ack) == fixture("hello-ack.bin"), "accepted hello-ack fixture")
        var mismatchAck = CompanionEnvelope()
        mismatchAck.kind = .helloAck
        mismatchAck.status = .unsupported
        mismatchAck.payload = CompanionHello(fingerprint: [0xFF, 0xEE, 0xDD, 0xCC, 0xBB, 0xAA, 0x99, 0x88],
                                             buildId: "2026-09-29 abc1234").payload!
        expect(CompanionCodec.encode(mismatchAck) == fixture("hello-ack-mismatch.bin"), "mismatch hello-ack fixture")
        mismatchAck.session = 3
        expect(CompanionCodec.encode(mismatchAck) == nil, "a mismatch ack carries no session")
        expect(CompanionHello(buildId: "").payload == nil && CompanionHello(buildId: "café").payload == nil &&
               CompanionHello(buildId: String(repeating: "a", count: 25)).payload == nil,
               "build ids are 1-24 printable ASCII bytes")
        expect(CompanionCodec.isLegacyFrame(fixture("legacy-hello.bin")) &&
               CompanionCodec.decode(fixture("legacy-hello.bin")) == nil, "legacy frames are recognised, not decoded")
        expect(!CompanionCodec.isLegacyFrame(fixture("hello.bin")), "current frames are not legacy")

        var ping = CompanionEnvelope()
        ping.kind = .request
        ping.session = 42
        ping.requestId = 1
        ping.operation = .ping
        ping.payload = [1, 2, 3, 4]
        expect(CompanionCodec.encode(ping) == fixture("ping-request.bin"), "ping fixture")
        expect(CompanionCodec.decode(fixture("unknown-marker.bin")) == nil, "unknown marker")
        expect(CompanionCodec.decode(fixture("unknown-operation.bin")) == nil, "unknown operation")
        expect(CompanionCodec.decode(fixture("malformed-length.bin")) == nil, "malformed length")
        expect(CompanionCodec.decode(fixture("wrong-session.bin"))?.session == 99, "wrong session")

        let framed = CompanionFramer.encode(Array(UInt8(0)..<40), messageId: 3)
        expect(CompanionFramer.decode(framed ?? []) == Array(UInt8(0)..<40), "framer round trip")

        let reassembler = CompanionReassembler()
        expect(reassembler.ingest(framed?[0] ?? []) == nil, "partial reassembly")
        expect(reassembler.ingest(framed?[1] ?? []) == nil, "still partial")
        expect(reassembler.ingest(framed?[2] ?? []) == Array(UInt8(0)..<40), "reassembly complete")

        let abandoned = CompanionReassembler()
        expect(abandoned.ingest(framed?[0] ?? []) == nil, "timeout starts")
        abandoned.update(CompanionReassembler.timeout)
        expect(abandoned.ingest(framed?[0] ?? []) == nil, "fresh first chunk")
        expect(abandoned.ingest(framed?[1] ?? []) == nil, "fresh second chunk")
        expect(abandoned.ingest(framed?[2] ?? []) == Array(UInt8(0)..<40), "fresh reassembly")

        expect(CompanionCodec.readBundle([129] + Array(repeating: 65, count: 129)) == nil, "bundle over 128")
        expect(CompanionCodec.readBundle(CompanionCodec.bundlePayload(String(repeating: "a", count: 128))!) != nil,
               "bundle at 128")

        var engine = CompanionAttachEngine()
        expect(engine.lookup(companionCount: 0, hidCount: 0) == .retryLater, "attach waiting")
        expect(engine.lookup(companionCount: 2, hidCount: 0) == .ambiguous, "attach ambiguous companions")
        expect(engine.lookup(companionCount: 1, hidCount: 5) == .connectCompanion(0), "prefer companion uuid")
        expect(engine.lookup(companionCount: 1, hidCount: 5) == .idle, "held peripheral blocks lookup")
        expect(engine.servicesMissing() == .cancelCurrentAndRetry, "missing service clears hold")
        expect(engine.lookup(companionCount: 0, hidCount: 2) == .connectHid(0), "probe first hid")
        expect(engine.connected() == .discoverServices, "discover after hid connect")
        expect(engine.servicesMissing() == .cancelCurrentAndProbeHid(1), "probe next hid")
        expect(engine.connectFailed() == .cancelCurrentAndRetry, "last hid failure retries")
        engine = CompanionAttachEngine()
        expect(engine.lookup(companionCount: 0, hidCount: 2) == .connectHid(0), "hid connect fail setup")
        expect(engine.connectFailed() == .cancelCurrentAndProbeHid(1), "hid connect fail probes next")
        expect(engine.servicesFound() == .discoverCharacteristics, "confirmed cardputer on hid")
        expect(engine.characteristicsMissing() == .cancelCurrentAndRetry, "bad characteristics retry")
        engine = CompanionAttachEngine()
        _ = engine.lookup(companionCount: 1, hidCount: 0)
        expect(engine.notifyFailed() == .cancelCurrentAndRetry, "notify failure retries")
        engine = CompanionAttachEngine()
        _ = engine.lookup(companionCount: 1, hidCount: 0)
        expect(engine.handshakeTimeout() == .cancelCurrentAndRetry, "handshake timeout retries")

        let hidA = UUID()
        let hidB = UUID()
        var coordinator = CompanionAttachCoordinator()
        expect(coordinator.lookup(companionCount: 0, hidCount: 2) == .connectHid(0), "coordinator probes hid")
        expect(coordinator.beginConnect(hidA) == .proceed, "connect A proceeds")
        expect(coordinator.handleDidConnect(hidA) == .discoverServices, "connect A")
        expect(coordinator.handleDidDiscoverServices(hidA, hasCompanion: false, error: false) ==
                .cancelCurrentAndProbeHid(1), "A has no companion service")
        expect(coordinator.beginConnect(hidB) == .proceed, "B proceeds while A cancelling")
        expect(coordinator.handleDidDisconnect(hidA) == .idle, "late A disconnect ignored")
        expect(coordinator.currentId == hidB, "B remains current")
        expect(coordinator.handleDidFailToConnect(hidA) == .idle, "late A fail ignored")
        expect(coordinator.handleDidDiscoverServices(hidA, hasCompanion: true, error: false) == .idle,
               "late A discovery ignored")
        expect(coordinator.handleDidConnect(hidB) == .discoverServices, "B still connects")
        expect(coordinator.currentId == hidB, "B still current after stale A")

        var same = CompanionAttachCoordinator()
        let cardputer = UUID()
        expect(same.lookup(companionCount: 1, hidCount: 0) == .connectCompanion(0), "same-id setup")
        expect(same.beginConnect(cardputer) == .proceed, "attempt 1 proceeds")
        expect(same.handleDidConnect(cardputer) == .discoverServices, "attempt 1 connected")
        expect(same.handleDidDiscoverServices(cardputer, hasCompanion: false, error: false) ==
                .cancelCurrentAndRetry, "attempt 1 cancelled")
        expect(same.phase == .cancelling, "wait for previous cancellation")
        expect(same.beginConnect(cardputer) == .waitForCancellation, "attempt 2 waits")
        expect(same.handleDidConnect(cardputer) == .idle, "late attempt-1 connect ignored")
        expect(same.phase == .cancelling, "still cancelling after late connect")
        expect(same.handleDidDisconnect(cardputer) == .resumeConnect, "cancel complete resumes 2")
        expect(same.currentId == cardputer, "attempt 2 is current")
        expect(same.phase == .connecting, "attempt 2 connecting")
        expect(same.handleDidDisconnect(cardputer) == .cancelCurrentAndRetry, "real attempt-2 disconnect retries")

        var failing = CompanionAttachCoordinator()
        expect(failing.lookup(companionCount: 1, hidCount: 0) == .connectCompanion(0), "fail setup")
        expect(failing.beginConnect(cardputer) == .proceed, "fail attempt 1")
        expect(failing.handleDidConnect(cardputer) == .discoverServices, "fail attempt 1 connected")
        _ = failing.failAndRetry()
        expect(failing.beginConnect(cardputer) == .waitForCancellation, "fail attempt 2 waits")
        expect(failing.handleDidFailToConnect(cardputer) == .resumeConnect, "old fail completes cancel")
        expect(failing.handleDidFailToConnect(cardputer) == .cancelCurrentAndRetry, "real attempt-2 fail retries")
        expect(failing.updateCancellation(CompanionAttachCoordinator.cancellationTimeout) == .idle,
               "timeout without pending is idle")

        var timed = CompanionAttachCoordinator()
        expect(timed.lookup(companionCount: 1, hidCount: 0) == .connectCompanion(0), "timeout setup")
        expect(timed.beginConnect(cardputer) == .proceed, "timeout attempt 1")
        expect(timed.handleDidConnect(cardputer) == .discoverServices, "timeout attempt 1 connected")
        _ = timed.failAndRetry()
        expect(timed.beginConnect(cardputer) == .waitForCancellation, "timeout attempt 2 waits")
        expect(timed.updateCancellation(CompanionAttachCoordinator.cancellationTimeout) == .resetCentral,
               "timeout resets central instead of resuming")
        expect(timed.phase == .idle, "timeout leaves coordinator idle")
        expect(timed.currentId == nil, "timeout clears current id")
        expect(timed.handleDidDisconnect(cardputer) == .idle, "stale cancel disconnect ignored after reset")
        expect(timed.lookup(companionCount: 1, hidCount: 0) == .connectCompanion(0), "fresh lookup after timeout")
        expect(timed.beginConnect(cardputer) == .proceed, "attempt 2 proceeds after reset")
        expect(timed.handleDidConnect(cardputer) == .discoverServices, "attempt 2 connects")
        expect(timed.handleDidDisconnect(cardputer) == .cancelCurrentAndRetry,
               "real attempt-2 disconnect retries")

        var radio = CompanionAttachEngine()
        expect(radio.lookup(companionCount: 1, hidCount: 0) == .connectCompanion(0), "radio hold")
        expect(radio.lookup(companionCount: 1, hidCount: 0) == .idle, "held blocks lookup")
        expect(radio.handleRadioUnavailable() == .radioUnavailable, "radio down clears hold")
        expect(radio.lookup(companionCount: 1, hidCount: 0) == .connectCompanion(0), "lookup after radio down")
        expect(radio.handleSleepWake() == .cancelCurrentAndRetry, "wake while held retries")
        expect(radio.handleSleepWake() == .lookupNow, "wake idle lookups now")

        var radioCoord = CompanionAttachCoordinator()
        expect(radioCoord.lookup(companionCount: 1, hidCount: 0) == .connectCompanion(0), "coord radio setup")
        expect(radioCoord.beginConnect(cardputer) == .proceed, "coord radio connect")
        expect(radioCoord.handleDidConnect(cardputer) == .discoverServices, "coord radio connected")
        expect(radioCoord.handleRadioUnavailable() == .radioUnavailable, "coord radio down")
        expect(radioCoord.phase == .idle, "coord idle after radio down")
        expect(radioCoord.currentId == nil, "coord cleared after radio down")
        expect(radioCoord.lookup(companionCount: 1, hidCount: 0) == .connectCompanion(0), "coord lookup after radio")
        expect(radioCoord.handleDidDisconnect(cardputer) == .idle, "stale disconnect after radio down")

        var wake = CompanionAttachCoordinator()
        expect(wake.handleSleepWake() == .lookupNow, "wake idle lookups")
        expect(wake.lookup(companionCount: 1, hidCount: 0) == .connectCompanion(0), "wake setup")
        expect(wake.beginConnect(cardputer) == .proceed, "wake connect")
        expect(wake.handleDidConnect(cardputer) == .discoverServices, "wake connected")
        expect(wake.handleSleepWake() == .cancelCurrentAndRetry, "wake while attached retries")
        expect(wake.phase == .cancelling, "wake cancels current")
        expect(wake.lookup(companionCount: 1, hidCount: 0) == .connectCompanion(0), "wake lookup allowed")
        expect(wake.beginConnect(cardputer) == .waitForCancellation, "wake same-id waits")
        expect(wake.handleRadioUnavailable() == .radioUnavailable, "will-sleep drops attach")
        expect(wake.phase == .idle, "will-sleep idle")
        expect(wake.currentId == nil, "will-sleep clears current")
        expect(wake.lookup(companionCount: 1, hidCount: 0) == .connectCompanion(0),
               "lookup after will-sleep")

var helloActivate = CompanionCodec.hello(CompanionHello(buildId: "x"))!
        helloActivate.operation = .appActivate
        expect(CompanionCodec.encode(helloActivate) == nil, "hello+activate encode rejected")
        var helloWire = fixture("hello.bin")
        helloWire[5] = CompanionOperation.appActivate.rawValue
        expect(CompanionCodec.decode(helloWire) == nil, "hello+activate decode rejected")
        var capabilitiesWire = fixture("app-active-request.bin")
        capabilitiesWire[5] = 2
        expect(CompanionCodec.decode(capabilitiesWire) == nil, "CAPABILITIES no longer exists")
        var zeroRequestId = fixture("ping-request.bin")
        zeroRequestId[4] = 0
        expect(CompanionCodec.decode(zeroRequestId) == nil, "zero request id rejected")
        var shortPing = Array(fixture("ping-request.bin").prefix(CompanionConstants.envelopeSize + 3))
        shortPing[7] = 3
        expect(CompanionCodec.decode(shortPing) == nil, "short ping rejected")
        var activePayload = fixture("app-active-request.bin")
        activePayload[7] = 1
        activePayload.append(1)
        expect(CompanionCodec.decode(activePayload) == nil, "non-empty app.active request rejected")
        expect(CompanionCodec.readBundle([3, 0xE0, 0x80, 0xAF]) == nil, "overlong utf-8 bundle")
        expect(CompanionCodec.readBundle([3, 0xED, 0xA0, 0x80]) == nil, "surrogate utf-8 bundle")
        expect(CompanionCodec.readBundle([4, 0xF4, 0x90, 0x80, 0x80]) == nil, "too-large utf-8 bundle")

        let apps = FakeApplications()
        let session = CompanionSession(applications: apps)
        var sent: [[UInt8]] = []
        session.outgoing = { sent.append($0) }

func helloAck(session: UInt16, firmware: String = "2026-09-29 abc1234",
                      fingerprint: [UInt8] = ProtocolFingerprint.bytes) -> [UInt8] {
            var ack = CompanionEnvelope()
            ack.kind = .helloAck
            ack.session = session
            ack.status = session == 0 ? .unsupported : .ok
            ack.payload = CompanionHello(fingerprint: fingerprint, buildId: firmware).payload!
            return CompanionCodec.encode(ack)!
        }
        session.handle(helloAck(session: 7))
        expect(session.session == 0, "unsolicited hello-ack ignored")

        session.startHandshake()
        let sentHello = CompanionCodec.decode(sent[0])
        expect(sentHello?.kind == .hello &&
               CompanionHello.read(sentHello?.payload ?? [])?.fingerprint == ProtocolFingerprint.bytes,
               "hello carries this protocol's fingerprint")
        session.handle(helloAck(session: 7))
        expect(session.session == 7 && session.compatibility == .matched(firmwareBuildId: "2026-09-29 abc1234"),
               "matching ack starts the session")
        session.handle(helloAck(session: 9))
        expect(session.session == 7, "duplicate hello-ack ignored")

        var pingReq = CompanionEnvelope()
        pingReq.kind = .request
        pingReq.session = 7
        pingReq.requestId = 1
        pingReq.operation = .ping
        pingReq.payload = [9, 8, 7, 6]
        session.handle(CompanionCodec.encode(pingReq)!)
        expect(CompanionCodec.decode(sent.last!)?.payload == [9, 8, 7, 6], "ping echo")

        var stale = pingReq
        stale.session = 99
        let beforeStale = sent.count
        session.handle(CompanionCodec.encode(stale)!)
        expect(sent.count == beforeStale, "stale session ignored")
        expect(session.lastValidMessageAt != nil, "valid session keeps last-seen time")

        var activate = CompanionEnvelope()
        activate.kind = .request
        activate.session = 7
        activate.requestId = 4
        activate.operation = .appActivate
        activate.payload = CompanionCodec.bundlePayload("org.telegram.desktop")!
        session.handle(CompanionCodec.encode(activate)!)
        expect(apps.activated == ["org.telegram.desktop"], "activate running")
        apps.activateResult = .ok
        apps.running.remove("org.telegram.desktop")
        session.handle(CompanionCodec.encode(activate)!)
        expect(apps.launched == ["org.telegram.desktop"], "activate resolvable")
        apps.activateResult = .notFound
        session.handle(CompanionCodec.encode(activate)!)
        expect(CompanionCodec.decode(sent.last!)?.status == .notFound, "activate missing")

        let afterAck = sent.count
        apps.observer?("dev.zed.Zed")
        expect(sent.count == afterAck, "duplicate foreground suppressed")
        apps.observer?("com.apple.Safari")
        expect(CompanionCodec.decode(sent.last!)?.operation == .appActiveChanged, "foreground event")
        apps.observer?(nil)
        expect(CompanionCodec.decode(sent.last!)?.payload.isEmpty == true, "empty active-changed")

        session.reset()
        let afterReset = sent.count
        apps.observer?("com.apple.Mail")
        expect(sent.count == afterReset, "reset suppresses events")
        expect(session.session == 0, "reset clears session")
        expect(session.lastValidMessageAt == nil && session.sessionStartedAt == nil &&
               session.compatibility == .unknown, "reset clears presentation session details")

        let collector = FakeMetrics()
        var sessionClock = Date(timeIntervalSince1970: 100)
let v2 = CompanionSession(applications: FakeApplications(), metrics: collector,
                                  now: { sessionClock })
        var v2Sent: [[UInt8]] = []
        v2.outgoing = { v2Sent.append($0) }
        v2.startHandshake()
        v2.handle(helloAck(session: 21))
        expect(v2.sessionStartedAt == sessionClock && v2.lastValidMessageAt == sessionClock,
               "the accepted ack starts the session and last-seen clock")
        var v2Metrics = CompanionEnvelope()
        v2Metrics.kind = .request
        v2Metrics.session = 21
        v2Metrics.requestId = 2
        v2Metrics.operation = .systemMetrics
        sessionClock = Date(timeIntervalSince1970: 105)
        v2.handle(CompanionCodec.encode(v2Metrics)!)
        expect(collector.calls == 1, "metrics collected once")
        expect(v2.lastValidMessageAt == sessionClock, "valid request updates last seen")
        var oldSessionRequest = v2Metrics
        oldSessionRequest.session = 99
        sessionClock = Date(timeIntervalSince1970: 110)
        v2.handle(CompanionCodec.encode(oldSessionRequest)!)
        expect(v2.lastValidMessageAt == Date(timeIntervalSince1970: 105),
               "old-session traffic does not update last seen")
        let metricsResponse = CompanionCodec.decode(v2Sent.last!)
        expect(metricsResponse?.operation == .systemMetrics && metricsResponse?.status == .ok &&
               metricsResponse?.payload.count == SystemMetricsSample.payloadSize, "metrics response")
        var fixtureSample = SystemMetricsSample()
        fixtureSample.cpuPercent = 34
        fixtureSample.memory = (11500, 16384)
        fixtureSample.memoryPressure = .normal
        fixtureSample.diskUsedPercent = 63
        fixtureSample.batteryPercent = 82
        fixtureSample.thermalState = .fair
        fixtureSample.network = (12698, 1843)
        fixtureSample.powerSource = .charging
        fixtureSample.batteryMinutes = 102
        var fixtureResponse = CompanionEnvelope()
        fixtureResponse.kind = .response
        fixtureResponse.session = 42
        fixtureResponse.requestId = 5
        fixtureResponse.operation = .systemMetrics
        fixtureResponse.payload = fixtureSample.encode()!
        expect(CompanionCodec.encode(fixtureResponse) == fixture("system-metrics-response.bin"),
               "metrics fixture")
        expect(SystemMetricsSample.decode(fixtureResponse.payload) == fixtureSample, "metrics round trip")
        expect(SystemMetricsSample.decode(metricsResponse?.payload ?? [])?.cpuPercent == 34,
               "session metrics decode")
        var badPayload = fixtureResponse.payload
        badPayload[1] = 0x02
        expect(SystemMetricsSample.decode(badPayload) == nil, "unknown metrics validity bit rejected")
        badPayload = Array(fixtureResponse.payload.dropLast())
        expect(SystemMetricsSample.decode(badPayload) == nil, "truncated metrics rejected")

// SYSTEM_DETAILS.

        var cpuDetails = SystemDetailsSample(group: .cpu)
        cpuDetails.performancePercent = 61
        cpuDetails.efficiencyPercent = 18
        cpuDetails.gpuPercent = 27
        cpuDetails.apps = [TopApp(name: "Xcode", percent: 38), TopApp(name: "Google Chrome", percent: 21)]
        var powerDetails = SystemDetailsSample(group: .power)
        powerDetails.systemDrawDeciwatts = 142
        powerDetails.adapterWatts = 96
        powerDetails.healthPercent = 91
        powerDetails.cycleCount = 214
        powerDetails.peripheral = PeripheralBattery(name: "Magic Mouse", percent: 12)
        var networkDetails = SystemDetailsSample(group: .network)
        networkDetails.internetRttMs = 18
        networkDetails.routerRttMs = 3
        networkDetails.wifiRssiDbm = -54
        networkDetails.wifiLinkMbps = 866
        var memoryDetails = SystemDetailsSample(group: .memory)
        memoryDetails.memorySplit = MemorySplit(appMiB: 14438, wiredMiB: 3994, compressedMiB: 3482)
        memoryDetails.swapUsedMiB = 1229
        memoryDetails.ssd = SsdSpace(freeGB: 212, totalGB: 994)
        memoryDetails.diskRates = DiskRates(readKiBps: 348160, writeKiBps: 59392)
        for (sample, name) in [(cpuDetails, "cpu"), (powerDetails, "power"),
                               (networkDetails, "network"), (memoryDetails, "memory")] {
            var envelope = CompanionEnvelope()
            envelope.kind = .response; envelope.session = 42
            envelope.requestId = 8; envelope.operation = .systemDetails
            envelope.payload = sample.encode() ?? []
            let wire = fixture("system-details-response-\(name).bin")
            expect(CompanionCodec.encode(envelope) == wire, "\(name) details fixture")
            expect(CompanionCodec.decode(wire).flatMap { SystemDetailsSample.decode($0.payload) } == sample,
                   "\(name) details round trip")
        }
        var tooMany = cpuDetails
        tooMany.apps = Array(repeating: TopApp(name: "A", percent: 1), count: 5)
        expect(tooMany.encode() == nil, "at most four apps")
        var badName = cpuDetails
        badName.apps = [TopApp(name: "Телеграм", percent: 1)]
        expect(badName.encode() == nil, "names must be printable ASCII")
        var desktop = SystemDetailsSample(group: .power)
        desktop.adapterWatts = 65
        expect(SystemDetailsSample.decode(desktop.encode() ?? []) == desktop,
               "partial power details round trip")
        var detailsRequest = CompanionEnvelope()
        detailsRequest.kind = .request; detailsRequest.session = 42
        detailsRequest.requestId = 8; detailsRequest.operation = .systemDetails
        detailsRequest.payload = [1]
        expect(CompanionCodec.encode(detailsRequest) == fixture("system-details-request.bin"),
               "details request fixture")
        detailsRequest.payload = [5]
        expect(CompanionCodec.encode(detailsRequest) == nil, "unknown detail group rejected")
        detailsRequest.payload = [1]

        let detailCollector = FakeDetails([.cpu: cpuDetails])
        let v6 = CompanionSession(applications: FakeApplications(), metrics: FakeMetrics(),
                                  details: detailCollector)
        var v6Sent: [[UInt8]] = []
        v6.outgoing = { v6Sent.append($0) }
        v6.startHandshake()
        v6.handle(helloAck(session: 61))
        var v6Request = detailsRequest
        v6Request.session = 61
        v6.handle(CompanionCodec.encode(v6Request)!)
        let v6Response = CompanionCodec.decode(v6Sent.last!)
        expect(v6Response?.operation == .systemDetails && v6Response?.status == .ok &&
               SystemDetailsSample.decode(v6Response?.payload ?? []) == cpuDetails,
               "session answers the requested group")
        v6Request.requestId = 9
        v6Request.payload = [2]
        v6.handle(CompanionCodec.encode(v6Request)!)
        expect(CompanionCodec.decode(v6Sent.last!)?.status == .notAvailable,
               "uncollectable group is NOT_AVAILABLE")
        expect(detailCollector.requested == [.cpu, .power], "collector sees each requested group")
        var v6MetricsRequest = v6Request
        v6MetricsRequest.requestId = 10; v6MetricsRequest.operation = .systemMetrics
        v6MetricsRequest.payload = []
        v6.handle(CompanionCodec.encode(v6MetricsRequest)!)
        expect(CompanionCodec.decode(v6Sent.last!)?.payload.count == SystemMetricsSample.payloadSize,
               "session sends the single metrics layout")

        expect(CompanionFramer.payloadSize(maximumWriteLength: 20) == 17 &&
               CompanionFramer.payloadSize(maximumWriteLength: 182) == 179 &&
               CompanionFramer.payloadSize(maximumWriteLength: 512) == 256,
               "chunk payload follows the negotiated write length")
        expect(CompanionFramer.encode(Array(repeating: 1, count: 106), messageId: 3,
                                      maxPayload: CompanionFramer.payloadSize(maximumWriteLength: 182))?.count == 1,
               "a CPU details response fits one write at a 185-byte MTU")
        var livenessClock = Date(timeIntervalSince1970: 1_000)
        let liveness = CompanionSession(applications: FakeApplications(), now: { livenessClock })
        liveness.outgoing = { _ in }
        expect(!liveness.livenessExpired(at: livenessClock), "no session is never expired")
        liveness.startHandshake()
        liveness.handle(helloAck(session: 5))
        livenessClock = livenessClock.addingTimeInterval(12)
        expect(!liveness.livenessExpired(at: livenessClock), "twelve silent seconds are still alive")
        expect(liveness.livenessExpired(at: livenessClock.addingTimeInterval(1)),
               "a silent session expires after twelve seconds")
        var livenessPing = CompanionEnvelope()
        livenessPing.kind = .request; livenessPing.session = 5
        livenessPing.requestId = 1; livenessPing.operation = .ping; livenessPing.payload = [1, 2, 3, 4]
        livenessClock = livenessClock.addingTimeInterval(1)
        liveness.handle(CompanionCodec.encode(livenessPing)!)
        expect(!liveness.livenessExpired(at: livenessClock.addingTimeInterval(5)), "a ping keeps the session alive")
        expect(DisplayName.sanitize("Телеграм", limit: 20) == "Telegram", "Cyrillic is transliterated")
        expect(DisplayName.sanitize("Café  Crème", limit: 20) == "Cafe Creme", "diacritics are stripped")
        expect(DisplayName.sanitize("✓", limit: 20) == "APP", "empty names fall back")
        expect(DisplayName.sanitize("An Extremely Long Application Name", limit: 20).utf8.count <= 20,
               "names are truncated")
        let chromeHelper = "/Applications/Google Chrome.app/Contents/Frameworks/Google Chrome Framework.framework/Versions/1/Helpers/Google Chrome Helper.app/Contents/MacOS/Google Chrome Helper"
        expect(DisplayName.appName(forExecutable: chromeHelper) == "Google Chrome",
               "helpers roll up into the outermost app")
        expect(DisplayName.appName(forExecutable: "/usr/libexec/duetexpertd") == "duetexpertd",
               "plain executables keep their name")

        let tracker = TopAppsTracker(logicalCPUs: 2)
        let chromeMain = "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"
        let first = [ProcessCPUTime(pid: 1, path: chromeMain, cpuNanoseconds: 0),
                     ProcessCPUTime(pid: 2, path: chromeHelper, cpuNanoseconds: 0),
                     ProcessCPUTime(pid: 3, path: "/Applications/Zed.app/Contents/MacOS/zed", cpuNanoseconds: 0)]
        expect(tracker.sample(first, at: 100) == nil, "first process sample is only a baseline")
        let second = [ProcessCPUTime(pid: 1, path: chromeMain, cpuNanoseconds: 300_000_000),
                      ProcessCPUTime(pid: 2, path: chromeHelper, cpuNanoseconds: 500_000_000),
                      ProcessCPUTime(pid: 3, path: "/Applications/Zed.app/Contents/MacOS/zed", cpuNanoseconds: 200_000_000),
                      ProcessCPUTime(pid: 4, path: "/usr/bin/new", cpuNanoseconds: 900_000_000)]
        expect(tracker.sample(second, at: 101) == [TopApp(name: "Google Chrome", percent: 40),
                                                    TopApp(name: "Zed", percent: 10)],
               "top apps roll up helpers and skip processes without a baseline")
        expect(tracker.sample(second, at: 112) == nil, "process baseline expires after ten seconds")

        // Top apps are smoothed over about ten seconds and do not reorder for
        // small differences, so the Cardputer list does not jump every update.
        func topAppsRun(_ percents: [[String: Double]]) -> [[TopApp]?] {
            let smoothTracker = TopAppsTracker(logicalCPUs: 2)
            var cumulative: [String: UInt64] = [:]
            var names: [String] = []
            for step in percents { for name in step.keys where !names.contains(name) { names.append(name) } }
            func processes() -> [ProcessCPUTime] {
                names.enumerated().map { index, name in
                    ProcessCPUTime(pid: Int32(index + 1), path: "/Applications/\(name).app/Contents/MacOS/\(name)",
                                   cpuNanoseconds: cumulative[name, default: 0])
                }
            }
            var results: [[TopApp]?] = [smoothTracker.sample(processes(), at: 0)]
            for (index, step) in percents.enumerated() {
                // 1% of two CPUs over two seconds is 40 ms of CPU time.
                for (name, percent) in step { cumulative[name, default: 0] += UInt64(percent * 40_000_000) }
                results.append(smoothTracker.sample(processes(), at: Double(index + 1) * 2))
            }
            return results
        }
        let flapping = topAppsRun((0..<10).map { ["Steady": 20, "Flicker": $0 % 2 == 0 ? 4 : 0] })
        expect(flapping.dropFirst(2).allSatisfy { $0?.map(\.name) == ["Steady", "Flicker"] },
               "an app that briefly idles stays in the list")
        let close = topAppsRun((0..<10).map { $0 % 2 == 0 ? ["Alpha": 10, "Beta": 11] : ["Alpha": 11, "Beta": 10] })
        let closeOrders = Set(close.dropFirst().compactMap { $0?.map(\.name).joined(separator: ",") })
        expect(closeOrders.count == 1, "near-equal apps keep their order")
        let takeover = topAppsRun([["Old": 10, "New": 0], ["Old": 10, "New": 60], ["Old": 10, "New": 60]])
        expect(takeover.last??.first?.name == "New", "a clearly busier app moves up within one update")
        let spawned = topAppsRun([["Base": 5], ["Base": 5, "Burst": 30]])
        expect(spawned.last??.first == TopApp(name: "Burst", percent: 30),
               "a new busy app appears on its first sample")
        let fading = topAppsRun([["Gone": 3]] + Array(repeating: [:], count: 12))
        expect(fading.last == .some([]), "an idle app leaves the list once its average falls")

        var disk = CounterRate()
        expect(disk.sample(read: 0, write: 0, at: 10) == nil, "disk counters need a baseline")
        expect(disk.sample(read: 2048, write: 1024, at: 12) == DiskRates(readKiBps: 1, writeKiBps: 0),
               "disk rates in KiB/s")
        expect(disk.sample(read: 4096, write: 4096, at: 30) == nil, "disk baseline expires")

        let clusters = ClusterUsage.percents(
            previous: [CPUTimeTicks(user: 0, system: 0, idle: 0, nice: 0),
                       CPUTimeTicks(user: 0, system: 0, idle: 0, nice: 0)],
            current: [CPUTimeTicks(user: 10, system: 0, idle: 90, nice: 0),
                      CPUTimeTicks(user: 50, system: 10, idle: 40, nice: 0)],
            clusters: ["E", "P"])
        expect(clusters.efficiency == 10 && clusters.performance == 60, "cluster usage by type")

        var probes = ProbeSchedule()
        expect(!probes.shouldProbe(at: 0), "no probes before a network request")
        probes.noteRequest(at: 1)
        expect(probes.shouldProbe(at: 1) && !probes.shouldProbe(at: 3) && probes.shouldProbe(at: 6),
               "probes run at most every five seconds")
        expect(probes.shouldProbe(at: 11) && !probes.shouldProbe(at: 16.5),
               "probes stop ten seconds after the last network request")

        let plus: [String: Any] = ["rateLimits": ["planType": "plus",
            "primary": ["usedPercent": 80, "windowDurationMins": 10080, "resetsAt": 20000],
            "secondary": ["usedPercent": 37, "windowDurationMins": 300, "resetsAt": 12000]]]
        let plusSample = AiUsageNormalization.codex(plus, now: displayNow)
        expect(plusSample?.metrics.map(\.kind) == [.fiveHour, .week],
               "Codex windows identified by duration")
        expect(plusSample?.metrics.first?.remainingPercent == 63, "Codex shows remaining quota")
        var plusWithSpend = plus
        var plusLimits = plusWithSpend["rateLimits"] as! [String: Any]
        plusLimits["individualLimit"] = ["limit": 100, "used": 1,
                                         "remainingPercent": 99]
        plusWithSpend["rateLimits"] = plusLimits
        expect(AiUsageNormalization.codex(plusWithSpend)?.metrics.map(\.kind) ==
               [.fiveHour, .week], "Plus rolling limits win over optional spend limit")
        var plusWithResets = plus
        plusWithResets["rateLimitResetCredits"] = ["availableCount": 2, "credits": [
            ["status": "available", "title": "Full reset", "expiresAt": 90000],
            ["status": "used", "title": "Old reset", "expiresAt": 80000],
            ["status": "available", "title": "Later reset", "expiresAt": 120000]]]
        let resetSample = AiUsageNormalization.codex(plusWithResets, now: displayNow)
        expect(resetSample?.resetCredits?.availableCount == 2, "Plus reset count")
        expect(resetSample?.resetCredits?.credits.map(\.title) == ["FULL RESET", "LATER RESET"],
               "Only usable credits, sorted by expiry")
        let resetUsage = AiUsageSnapshot(generation: 10, state: .ready,
                                         providers: [resetSample!])
        let v4Payload = resetUsage.encode()
        expect(AiUsageSnapshot.decode(v4Payload ?? []) == resetUsage, "reset credits round trip")
        let fixtureCredit = AiResetCredit(title: "A", expiresAt: 1,
                                           expiresRemainingSeconds: 60)
        func resetPayload(available: UInt8, credits: [AiResetCredit],
                          provider: AiProviderId = .codex, plan: AiPlan = .plus) -> [UInt8]? {
            AiUsageSnapshot(state: .ready, providers: [
                AiUsageProviderSnapshot(provider: provider, plan: plan,
                    metrics: [resetSample!.metrics[0]],
                    resetCredits: AiResetCredits(availableCount: available, credits: credits))
            ]).encode()
        }
        expect(resetPayload(available: 0, credits: [fixtureCredit]) == nil,
               "v4 rejects zero available with a detail")
        expect(resetPayload(available: 1, credits: [fixtureCredit, fixtureCredit]) == nil,
               "v4 rejects more details than available")
        expect(resetPayload(available: 4, credits: Array(repeating: fixtureCredit, count: 4)) != nil &&
               resetPayload(available: 7, credits: Array(repeating: fixtureCredit, count: 4)) != nil,
               "v4 accepts bounded detail lists")
        expect(resetPayload(available: 0, credits: []) != nil,
               "v4 known zero is encodable")
        expect(resetPayload(available: 1, credits: [fixtureCredit], provider: .cursor) == nil &&
               resetPayload(available: 1, credits: [fixtureCredit], plan: .business) == nil,
               "v4 reset details are Plus only")
        if var invalid = resetPayload(available: 1, credits: [fixtureCredit]) {
            let availableOffset = 6 + 4 + 23 + 1
            let countOffset = availableOffset + 1
            let titleLengthOffset = countOffset + 1
            invalid[availableOffset] = 0
            expect(AiUsageSnapshot.decode(invalid) == nil, "v4 invalid available count")
            invalid[availableOffset] = 1
            invalid[countOffset] = 2
            expect(AiUsageSnapshot.decode(invalid) == nil, "v4 invalid detail count")
            invalid[countOffset] = 1
            invalid[titleLengthOffset + 1] = 0xff
            expect(AiUsageSnapshot.decode(invalid) == nil, "v4 invalid UTF-8 title")
            invalid[titleLengthOffset + 1] = 65
            invalid[titleLengthOffset] = 24
            expect(AiUsageSnapshot.decode(invalid) == nil, "v4 truncated title")
            invalid[titleLengthOffset] = 1
            expect(AiUsageSnapshot.decode(Array(invalid.dropLast())) == nil,
                   "v4 truncated expiry")
            expect(AiUsageSnapshot.decode(invalid + [0]) == nil, "v4 trailing byte")
            invalid[6] = AiProviderId.cursor.rawValue
            expect(AiUsageSnapshot.decode(invalid) == nil, "v4 Cursor reset payload")
            invalid[6] = AiProviderId.codex.rawValue
            invalid[7] = AiPlan.business.rawValue
            expect(AiUsageSnapshot.decode(invalid) == nil, "v4 Business reset payload")
        }
        plusWithResets["rateLimitResetCredits"] = ["availableCount": 0,
                                                     "credits": []]
        expect(AiUsageNormalization.codex(plusWithResets)?.resetCredits?.availableCount == 0,
               "known zero remains known")
        plusWithResets["rateLimitResetCredits"] = ["availableCount": 1, "credits": [
            ["status": "available", "title": "First reset", "expiresAt": 100],
            ["status": "available", "title": "Second reset", "expiresAt": 200]]]
        expect(AiUsageNormalization.codex(plusWithResets)?.resetCredits?.credits.count == 1,
               "details never exceed available count")
        plusWithResets["rateLimitResetCredits"] = ["availableCount": 1, "credits": [
            ["status": "available", "title": "Full reset", "expiresAt": 100]]]
        expect(AiUsageNormalization.codex(plusWithResets)?.resetCredits?.credits.first?.title ==
               "FULL RESET", "short title normalized on Mac")
        plusWithResets["rateLimitResetCredits"] = ["availableCount": 1, "credits": [
            ["status": "available", "title": "Full reset (Weekly + 5 hr)",
             "expiresAt": 100]]]
        expect(AiUsageNormalization.codex(plusWithResets)?.resetCredits?.credits.first?.title ==
               "FULL RESET", "reset detail uses a concise title")
        plusWithResets["rateLimitResetCredits"] = ["availableCount": 1, "credits": [
            ["status": "available", "title": "Additional Codex Rate Limit Reset",
             "expiresAt": 100]]]
        let longTitle = AiUsageNormalization.codex(plusWithResets)?
            .resetCredits?.credits.first?.title
        expect(longTitle?.hasPrefix("ADDITIONAL") == true &&
               longTitle?.hasSuffix("...") == true && (longTitle?.utf8.count ?? 25) <= 24,
               "long ASCII title keeps a bounded prefix")
        plusWithResets["rateLimitResetCredits"] = ["availableCount": 1, "credits": [
            ["status": "available", "title": String(repeating: "é", count: 20),
             "expiresAt": 100]]]
        let unicodeTitle = AiUsageNormalization.codex(plusWithResets)?
            .resetCredits?.credits.first?.title
        expect(unicodeTitle == "RESET CREDIT",
               "title outside the Cardputer font falls back")
        plusWithResets["rateLimitResetCredits"] = ["availableCount": 1, "credits": [
            ["status": "available", "title": "Reset\ncredit", "expiresAt": 100]]]
        expect(AiUsageNormalization.codex(plusWithResets)?.resetCredits?.credits.first?.title ==
               "RESET CREDIT", "control characters in title fall back")
        plusWithResets["rateLimitResetCredits"] = ["availableCount": "bad"]
        let malformedResets = AiUsageNormalization.codex(plusWithResets)
        expect(malformedResets?.metrics.count == 2 && malformedResets?.resetCredits == nil,
               "bad optional reset data keeps Plus limits")
        let business: [String: Any] = ["planType": "business",
            "individualLimit": ["limit": "20000", "used": "19765.35930800438",
                                "remainingPercent": 1, "resetsAt": 1790812800]]
        let businessSample = AiUsageNormalization.codex(business, now: displayNow)
        expect(businessSample?.provider == .codex && businessSample?.plan == .business &&
               businessSample?.metrics.first?.kind == .credits &&
               businessSample?.metrics.first?.limit == 20000 &&
               businessSample?.metrics.first?.used == 19765 &&
               businessSample?.metrics.first?.remaining == 235 &&
               businessSample?.metrics.first?.remainingPercent == 1,
               "real Business numeric strings and fractional credits normalized")
        let overflowingBusiness: [String: Any] = ["planType": "business",
            "individualLimit": ["limit": "4294967295.9", "used": "1",
                                "remainingPercent": 1]]
        expect(AiUsageNormalization.codex(overflowingBusiness) == nil,
               "rounded Business credits cannot overflow UInt32")
        let cursor: [String: Any] = ["membershipType": "enterprise",
            "billingCycleEnd": "2026-10-01T00:00:00Z",
            "individualUsage": ["overall": ["enabled": true, "used": 9458,
                                              "limit": 255000, "remaining": 245542]]]
        let cursorSample = AiUsageNormalization.cursor(cursor, now: displayNow)
        expect(cursorSample?.metrics.first?.unit == .cents &&
               cursorSample?.metrics.first?.remainingPercent == 96,
               "Cursor individual cents normalized")
        var fractionalCursor = cursor
        fractionalCursor["billingCycleEnd"] = "2026-10-01T00:00:00.000Z"
        expect(AiUsageNormalization.cursor(fractionalCursor, now: displayNow)?
            .metrics.first?.resetAt != 0, "Cursor fractional reset timestamp")

        let claude: [String: Any] = [
            "five_hour": ["utilization": 99.0, "resets_at": "1970-01-01T03:00:00.284624+00:00"],
            "seven_day": ["utilization": 99.0, "resets_at": "1970-01-02T00:00:00+00:00"],
            "seven_day_opus": NSNull(),
            "iguana_necktie": ["utilization": 0.0, "limit_dollars": 100],
            "limits": [
                ["kind": "session", "percent": 8, "resets_at": "1970-01-01T03:00:00.284624+00:00",
                 "scope": NSNull()],
                ["kind": "weekly_opus", "percent": 50, "resets_at": "1970-01-03T00:00:00Z",
                 "scope": "opus"],
                ["kind": "weekly_all", "percent": 1, "resets_at": "1970-01-02T00:00:00+00:00",
                 "scope": NSNull()],
            ]]
        let claudeSample = AiUsageNormalization.claude(claude, subscriptionType: "pro",
                                                       now: displayNow)
        expect(claudeSample?.provider == .claude && claudeSample?.plan == .pro,
               "Claude provider and plan")
        expect(claudeSample?.metrics.map(\.kind) == [.fiveHour, .week] &&
               claudeSample?.metrics.map(\.remainingPercent) == [92, 99],
               "Claude limits list wins over legacy windows")
        expect(claudeSample?.metrics.first?.resetAt == 10_800 &&
               claudeSample?.metrics.first?.resetRemainingSeconds == 800,
               "Claude fractional reset timestamp")
        var legacyClaude = claude
        legacyClaude["limits"] = nil
        legacyClaude["five_hour"] = ["utilization": 37.4, "resets_at": "1970-01-01T03:00:00Z"]
        legacyClaude["seven_day"] = NSNull()
        let legacySample = AiUsageNormalization.claude(legacyClaude, subscriptionType: "max",
                                                       now: displayNow)
        expect(legacySample?.plan == .max && legacySample?.metrics.map(\.kind) == [.fiveHour] &&
               legacySample?.metrics.first?.remainingPercent == 63,
               "Claude legacy five-hour fallback")
        expect(AiUsageNormalization.claude(["limits": []], subscriptionType: "pro") == nil,
               "Claude without rolling windows is not a sample")
        expect(AiUsageNormalization.claude(claude, subscriptionType: "team")?.plan == .unknown,
               "Claude unknown subscription keeps the account")
        let personal = AiUsageSnapshot(generation: 11, state: .ready,
                                       providers: [plusSample!, claudeSample!])
        let personalPayload = personal.encode()
        expect(AiUsageSnapshot.decode(personalPayload ?? []) == personal, "Codex and Claude round trip")
        let cursorForCap = AiUsageNormalization.cursor(["membershipType": "enterprise",
            "individualUsage": ["overall": ["enabled": true, "used": 1, "limit": 100]]])!
        let three = AiUsageSnapshot(state: .ready,
                                    providers: [plusSample!, cursorForCap, claudeSample!])
        expect(three.encode().flatMap(AiUsageSnapshot.decode)?
            .providers.map(\.provider) == [.codex, .cursor], "the wire carries the first two providers")
        let cursorAndClaude = AiUsageSnapshot(state: .ready, providers: [cursorForCap, claudeSample!])
        var elapsed = claudeSample!
        elapsed.freshness = .stale
        let reset = elapsed.aged(now: Date(timeIntervalSince1970: 20_000))
        expect(reset.metrics.first?.remainingPercent == 100 && reset.metrics.first?.used == 0 &&
               reset.metrics.first?.resetAt == 0, "elapsed window shows as reset")
        expect(reset.metrics.last?.remainingPercent == elapsed.metrics.last?.remainingPercent &&
               reset.metrics.last?.resetRemainingSeconds == 86_400 - 20_000,
               "pending window keeps usage and recounts its reset")
        let agedCursor = cursorSample!.aged(now: Date(timeIntervalSince1970: 4_000_000_000))
        expect(agedCursor.metrics.first?.remainingPercent == 96 &&
               agedCursor.metrics.first?.resetRemainingSeconds == 0,
               "spend metrics are not rolling windows")
        let agedResets = resetSample!.aged(now: Date(timeIntervalSince1970: 30_000))
        expect(agedResets.resetCredits?.credits.first?.expiresRemainingSeconds == 60_000,
               "reset-credit expiry recounts")
        expect(three.sentProviders.map(\.provider) == [.codex, .cursor] &&
               cursorAndClaude.sentProviders.map(\.provider) == [.cursor, .claude],
               "sent providers follow the two-provider cap")
        let usage = AiUsageSnapshot(generation: 9, state: .ready,
            providers: [plusSample!, cursorSample!])
        expect(AiUsageState.forCache(pending: 1, hasUsableProvider: true) == .ready,
               "cached provider remains ready while another refresh is pending")
        expect(AiUsageState.forCache(pending: 1, hasUsableProvider: false) == .discovering,
               "initial discovery remains visible without a provider")
        expect(AiUsageState.forCache(pending: 0, hasUsableProvider: false) == .ready,
               "empty discovery completes")
        let payload = usage.encode()
        expect(payload != nil && AiUsageSnapshot.decode(payload!) == usage,
               "AI usage bounded payload round trip")
        if var invalid = payload {
            invalid[6 + 4 + 14] = 101
            expect(AiUsageSnapshot.decode(invalid) == nil, "AI usage percent bound")
            expect(AiUsageSnapshot.decode(Array(invalid.dropLast())) == nil,
                   "AI usage truncation rejected")
        }
var aiFixture = CompanionEnvelope()
        aiFixture.kind = .response
        aiFixture.session = 42
        aiFixture.requestId = 7
        aiFixture.operation = .aiUsage
        aiFixture.payload = AiUsageSnapshot(generation: 9, state: .ready,
            providers: [AiUsageProviderSnapshot(provider: .claude, plan: .pro,
                metrics: [AiUsageMetric(kind: .fiveHour, unit: .percent, used: 8, limit: 100,
                                        remaining: 92, remainingPercent: 92,
                                        resetAt: 1780000000, resetRemainingSeconds: 3600)])])
            .encode()!
        expect(CompanionCodec.encode(aiFixture) == fixture("ai-usage-response.bin"), "AI usage fixture")
        let aiSession = CompanionSession(applications: FakeApplications(), aiUsage: FakeAiUsage(resetUsage))
        var aiSent: [[UInt8]] = []
        aiSession.outgoing = { aiSent.append($0) }
        aiSession.startHandshake()
        expect(aiSent[0] == CompanionCodec.encode(CompanionCodec.hello(CompanionHello(buildId: aiSession.buildId))!),
               "the session sends one HELLO with its build id")
        aiSession.handle(helloAck(session: 32))
        var aiRequest = CompanionEnvelope()
        aiRequest.kind = .request
        aiRequest.session = 32
        aiRequest.requestId = 8
        aiRequest.operation = .aiUsage
        aiSession.handle(CompanionCodec.encode(aiRequest)!)
        expect(CompanionCodec.decode(aiSent.last!)?.payload == v4Payload,
               "AI request reads the cached snapshot with reset details")

        let mismatched = CompanionSession(applications: FakeApplications(), metrics: FakeMetrics())
        var mismatchedSent: [[UInt8]] = []
        mismatched.outgoing = { mismatchedSent.append($0) }
        mismatched.startHandshake()
        mismatched.handle(helloAck(session: 0, firmware: "2026-09-20 fff0000",
                                   fingerprint: [0xFF, 0xEE, 0xDD, 0xCC, 0xBB, 0xAA, 0x99, 0x88]))
        expect(mismatched.session == 0 &&
               mismatched.compatibility == .mismatch(firmwareBuildId: "2026-09-20 fff0000"),
               "a mismatch ack names the firmware build and starts no session")
        var afterMismatch = aiRequest
        afterMismatch.session = 1
        let mismatchSentCount = mismatchedSent.count
        mismatched.handle(CompanionCodec.encode(afterMismatch)!)
        expect(mismatchedSent.count == mismatchSentCount, "nothing is answered after a mismatch")
        expect(!mismatched.livenessExpired(at: Date().addingTimeInterval(60)),
               "no liveness reconnect while incompatible")
        let silent = CompanionSession(applications: FakeApplications())
        silent.outgoing = { _ in }
        silent.startHandshake()
        silent.handshakeTimedOut()
        expect(silent.compatibility == .noAnswer, "an unanswered HELLO is reported")
        silent.reset()
        silent.startHandshake()
        expect(silent.compatibility == .noAnswer, "the no-answer hint survives retries")
        silent.handle(helloAck(session: 4))
        expect(silent.compatibility == .matched(firmwareBuildId: "2026-09-29 abc1234"),
               "a later answer clears the hint")

        if failed > 0 {
            fputs("\(failed) checks failed\n", stderr)
            exit(1)
        }
        print("companion core checks passed")
    }

    static func fixture(_ name: String) -> [UInt8] {
        var url = URL(fileURLWithPath: #filePath)
        for _ in 0..<5 { url.deleteLastPathComponent() }
        url.appendPathComponent("protocol/companion/fixtures/\(name)")
        return [UInt8](try! Data(contentsOf: url))
    }
}

final class FakeApplications: ApplicationControlling {
    var active: String? = "dev.zed.Zed"
    var activateResult: ActivateResult = .ok
    var running: Set<String> = ["org.telegram.desktop", "dev.zed.Zed"]
    var activated: [String] = []
    var launched: [String] = []
    var observer: ((String?) -> Void)?

    func activeApplication() -> String? { active }
    func activate(bundleIdentifier: String) -> ActivateResult {
        if activateResult != .ok { return activateResult }
        if running.contains(bundleIdentifier) {
            activated.append(bundleIdentifier)
            active = bundleIdentifier
            return .ok
        }
        launched.append(bundleIdentifier)
        running.insert(bundleIdentifier)
        active = bundleIdentifier
        return .ok
    }
    func observeActiveApplication(_ handler: @escaping (String?) -> Void) {
        observer = handler
    }
}

final class FakeMetrics: SystemMetricsCollecting {
    var calls = 0
    func collect() -> SystemMetricsSample? {
        calls += 1
        var sample = SystemMetricsSample()
        sample.cpuPercent = 34
        sample.memory = (8192, 16384)
        sample.memoryPressure = .normal
        sample.diskUsedPercent = 63
        sample.thermalState = .fair
        return sample
    }
}

final class FakeDetails: SystemDetailsCollecting {
    private let samples: [SystemDetailsGroup: SystemDetailsSample]
    var requested: [SystemDetailsGroup] = []
    init(_ samples: [SystemDetailsGroup: SystemDetailsSample]) { self.samples = samples }
    func collect(_ group: SystemDetailsGroup) -> SystemDetailsSample? {
        requested.append(group)
        return samples[group]
    }
}

final class FakeAiUsage: AiUsageCollecting {
    private let value: AiUsageSnapshot
    init(_ value: AiUsageSnapshot) { self.value = value }
    func snapshot() -> AiUsageSnapshot? { value }
}

final class FakeLoginRegistration: LoginRegistration {
    var isEnabled = false
    var shouldFail = false
    func setEnabled(_ enabled: Bool) throws {
        if shouldFail { throw NSError(domain: "FakeLoginRegistration", code: 1) }
        isEnabled = enabled
    }
}
