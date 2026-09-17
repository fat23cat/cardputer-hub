import Foundation

public final class CompanionSession {
    public private(set) var session: UInt16 = 0
    public private(set) var lastActiveBundle: String?
    private var lastEventBundle: String?
    private let applications: ApplicationControlling
    public var outgoing: ([UInt8]) -> Void = { _ in }
    private var awaitingHelloAck = false

    public init(applications: ApplicationControlling) {
        self.applications = applications
        applications.observeActiveApplication { [weak self] bundle in
            self?.handleForegroundChange(bundle)
        }
    }

    public func startHandshake() {
        guard let bytes = CompanionCodec.encode(CompanionCodec.hello()) else { return }
        awaitingHelloAck = true
        outgoing(bytes)
    }

    public func reset() {
        session = 0
        lastActiveBundle = nil
        lastEventBundle = nil
        awaitingHelloAck = false
    }

    public func handle(_ data: [UInt8]) {
        guard let message = CompanionCodec.decode(data) else { return }
        switch message.kind {
        case .helloAck:
            guard awaitingHelloAck,
                  message.session != 0,
                  message.payload == [CompanionConstants.protocolVersion]
            else { return }
            awaitingHelloAck = false
            session = message.session
            sendInitialActive()
        case .request:
            guard session != 0, message.session == session else { return }
            handleRequest(message)
        default:
            break
        }
    }

    private func handleRequest(_ message: CompanionEnvelope) {
        switch message.operation {
        case .ping:
            let response = CompanionCodec.pingResponse(
                session: message.session, requestId: message.requestId, token: message.payload)
            send(response)
        case .capabilities:
            send(CompanionCodec.capabilitiesResponse(session: message.session, requestId: message.requestId))
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
        guard let bytes = CompanionCodec.encode(message) else { return }
        outgoing(bytes)
    }
}
