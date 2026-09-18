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

        let hello = CompanionCodec.encode(CompanionCodec.hello())
        expect(hello == fixture("hello-v1.bin"), "hello fixture")

        var ack = CompanionEnvelope()
        ack.kind = .helloAck
        ack.session = 42
        ack.payload = [1]
        expect(CompanionCodec.encode(ack) == fixture("hello-ack-v1.bin"), "hello-ack fixture")

        var ping = CompanionEnvelope()
        ping.kind = .request
        ping.session = 42
        ping.requestId = 1
        ping.operation = .ping
        ping.payload = [1, 2, 3, 4]
        expect(CompanionCodec.encode(ping) == fixture("ping-request-v1.bin"), "ping fixture")
        expect(CompanionCodec.decode(fixture("unsupported-version.bin")) == nil, "unsupported version")
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

        var helloActivate = CompanionCodec.hello()
        helloActivate.operation = .appActivate
        expect(CompanionCodec.encode(helloActivate) == nil, "hello+activate encode rejected")
        var helloWire = fixture("hello-v1.bin")
        helloWire[5] = CompanionOperation.appActivate.rawValue
        expect(CompanionCodec.decode(helloWire) == nil, "hello+activate decode rejected")
        var requestStatus = fixture("capabilities-request-v1.bin")
        requestStatus[6] = CompanionStatus.notFound.rawValue
        expect(CompanionCodec.decode(requestStatus) == nil, "request status decode rejected")
        var zeroRequestId = fixture("ping-request-v1.bin")
        zeroRequestId[4] = 0
        expect(CompanionCodec.decode(zeroRequestId) == nil, "zero request id rejected")
        var shortPing = Array(fixture("ping-request-v1.bin").prefix(CompanionConstants.envelopeSize + 3))
        shortPing[7] = 3
        expect(CompanionCodec.decode(shortPing) == nil, "short ping rejected")
        var capsPayload = fixture("capabilities-request-v1.bin")
        capsPayload[7] = 1
        capsPayload.append(1)
        expect(CompanionCodec.decode(capsPayload) == nil, "non-empty capabilities request rejected")
        var activePayload = fixture("app-active-request-v1.bin")
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

        var helloAck = CompanionEnvelope()
        helloAck.kind = .helloAck
        helloAck.session = 7
        helloAck.payload = [1]
        session.handle(CompanionCodec.encode(helloAck)!)
        expect(session.session == 0, "unsolicited hello-ack ignored")

        session.startHandshake()
        expect(CompanionCodec.decode(sent[0])?.kind == .hello, "hello sent")
        var validAck = helloAck
        validAck.session = 7
        validAck.payload = [1]
        var zeroSession = CompanionCodec.encode(validAck)!
        zeroSession[2] = 0
        zeroSession[3] = 0
        session.handle(zeroSession)
        expect(session.session == 0, "zero session hello-ack ignored")
        var wrongVersion = CompanionCodec.encode(validAck)!
        wrongVersion[wrongVersion.count - 1] = 2
        session.handle(wrongVersion)
        expect(session.session == 0, "wrong version hello-ack ignored")
        session.handle(CompanionCodec.encode(validAck)!)
        expect(session.session == 7, "session stored")
        helloAck.session = 9
        session.handle(CompanionCodec.encode(helloAck)!)
        expect(session.session == 7, "duplicate hello-ack ignored")

        var caps = CompanionEnvelope()
        caps.kind = .request
        caps.session = 7
        caps.requestId = 2
        caps.operation = .capabilities
        session.handle(CompanionCodec.encode(caps)!)
        expect(CompanionCodec.decode(sent.last!)?.payload == [3, 1, 2, 3], "capabilities")

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
