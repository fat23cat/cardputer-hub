import Foundation

public final class CompanionSession {
    public private(set) var session: UInt16 = 0
    public private(set) var selectedProtocolVersion: UInt8 = 0
    public private(set) var sessionStartedAt: Date?
    public private(set) var lastValidMessageAt: Date?
    public private(set) var liveCapabilities: [CompanionCapability] = []
    public private(set) var lastActiveBundle: String?
    private var lastEventBundle: String?
    private let applications: ApplicationControlling
    private let metrics: SystemMetricsCollecting?
    private let now: () -> Date
    public var outgoing: ([UInt8]) -> Void = { _ in }
    private var awaitingHelloAck = false

    public init(applications: ApplicationControlling, metrics: SystemMetricsCollecting? = nil,
                now: @escaping () -> Date = Date.init) {
        self.applications = applications
        self.metrics = metrics
        self.now = now
        applications.observeActiveApplication { [weak self] bundle in
            self?.handleForegroundChange(bundle)
        }
    }

    public func startHandshake() {
        guard let bytes = CompanionCodec.encode(CompanionCodec.hello(versions: [2, 1])) else { return }
        awaitingHelloAck = true
        self.outgoing(bytes)
    }

    public func reset() {
        session = 0
        selectedProtocolVersion = 0
        sessionStartedAt = nil
        lastValidMessageAt = nil
        liveCapabilities = []
        lastActiveBundle = nil
        lastEventBundle = nil
        awaitingHelloAck = false
    }

    public func stop() {
        reset()
        applications.stopObservingActiveApplication()
    }

    @discardableResult
    public func handle(_ data: [UInt8]) -> Bool {
        guard let message = CompanionCodec.decode(data) else { return false }
        switch message.kind {
        case .helloAck:
            guard awaitingHelloAck,
                  message.session != 0,
                  message.version == 1,
                  let selected = message.payload.first,
                  message.payload.count == 1,
                  (1...CompanionConstants.latestProtocolVersion).contains(selected)
            else { return false }
            awaitingHelloAck = false
            session = message.session
            selectedProtocolVersion = selected
            sessionStartedAt = now()
            lastValidMessageAt = sessionStartedAt
            sendInitialActive()
            return true
        case .request:
            guard session != 0, message.session == session,
                  message.version == selectedProtocolVersion else { return false }
            lastValidMessageAt = now()
            handleRequest(message)
            return true
        default:
            return false
        }
    }

    private func handleRequest(_ message: CompanionEnvelope) {
        switch message.operation {
        case .ping:
            let response = CompanionCodec.pingResponse(
                session: message.session, requestId: message.requestId, token: message.payload)
            send(response)
        case .capabilities:
            let response = CompanionCodec.capabilitiesResponse(session: message.session,
                                                                requestId: message.requestId,
                                                                version: selectedProtocolVersion)
            liveCapabilities = response.payload.dropFirst().compactMap(CompanionCapability.init(rawValue:))
            send(response)
        case .systemMetrics:
            guard selectedProtocolVersion >= 2 else { return }
            var response = CompanionEnvelope()
            response.kind = .response
            response.session = message.session
            response.requestId = message.requestId
            response.operation = .systemMetrics
            if let sample = metrics?.collect(), let payload = sample.encode() {
                response.payload = payload
            } else {
                response.status = .notAvailable
            }
            send(response)
        case .appActive:
            var response = CompanionEnvelope()
            response.kind = .response
            response.session = message.session
            response.requestId = message.requestId
            response.operation = .appActive
            if let bundle = applications.activeApplication(), let payload = CompanionCodec.bundlePayload(bundle) {
                response.payload = payload
                lastActiveBundle = bundle
            } else {
                response.status = .notAvailable
            }
            send(response)
        case .appActivate:
            var response = CompanionEnvelope()
            response.kind = .response
            response.session = message.session
            response.requestId = message.requestId
            response.operation = .appActivate
            if let bundle = CompanionCodec.readBundle(message.payload) {
                response.status = applications.activate(bundleIdentifier: bundle) == .ok ? .ok : .notFound
            } else {
                response.status = .malformed
            }
            send(response)
        default:
            break
        }
    }

    private func sendInitialActive() {
        lastActiveBundle = applications.activeApplication()
        lastEventBundle = lastActiveBundle
        guard session != 0, let bundle = lastActiveBundle else { return }
        emitActiveChanged(bundle)
    }

    private func handleForegroundChange(_ bundle: String?) {
        guard session != 0, bundle != lastEventBundle else { return }
        lastEventBundle = bundle
        lastActiveBundle = bundle
        emitActiveChanged(bundle)
    }

    private func emitActiveChanged(_ bundle: String?) {
        var event = CompanionEnvelope()
        event.kind = .event
        event.session = session
        event.operation = .appActiveChanged
        if let bundle, let payload = CompanionCodec.bundlePayload(bundle) {
            event.payload = payload
        }
        send(event)
    }

    private func send(_ message: CompanionEnvelope) {
        var outgoing = message
        outgoing.version = selectedProtocolVersion
        guard let bytes = CompanionCodec.encode(outgoing) else { return }
        self.outgoing(bytes)
    }
}
