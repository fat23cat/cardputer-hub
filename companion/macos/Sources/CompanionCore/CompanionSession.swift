import CompanionAgentHooks
import Foundation

public enum CompanionRequestFailure: Error, Equatable {
    case disconnected, timeout, busy, encoding
}

public typealias CompanionResponseHandler = (Result<CompanionEnvelope, CompanionRequestFailure>) -> Void

/// Requests the Mac sends to the Cardputer (inventory operations).
public protocol CompanionRequesting: AnyObject {
    func sendRequest(_ operation: CompanionOperation, payload: [UInt8],
                     completion: @escaping CompanionResponseHandler)
}

public final class CompanionSession: CompanionRequesting {
    public private(set) var session: UInt16 = 0
    public private(set) var compatibility: CompanionCompatibility = .unknown
    public private(set) var sessionStartedAt: Date?
    public private(set) var lastValidMessageAt: Date?
    public private(set) var lastActiveBundle: String?
    private var lastEventBundle: String?
    private let applications: ApplicationControlling
    private let metrics: SystemMetricsCollecting?
    private let details: SystemDetailsCollecting?
    private let aiUsage: AiUsageCollecting?
    private let agentStatus: AgentStatusProviding?
    private let statusPages: StatusPageFetching?
    public let buildId: String
    private let now: () -> Date
    public var outgoing: ([UInt8]) -> Void = { _ in }
    private var awaitingHelloAck = false

    private struct PendingRequest {
        let id: UInt8
        let operation: CompanionOperation
        let session: UInt16
        let deadline: Date
        let completion: CompanionResponseHandler
    }
    private var pending: PendingRequest?
    private var nextRequestId: UInt8 = 1
    /// The longest wait for the Cardputer's answer to a Mac request.
    public static let requestTimeout: TimeInterval = 4

    public init(applications: ApplicationControlling, metrics: SystemMetricsCollecting? = nil,
                details: SystemDetailsCollecting? = nil,
                aiUsage: AiUsageCollecting? = nil,
                agentStatus: AgentStatusProviding? = nil,
                statusPages: StatusPageFetching? = nil,
                buildId: String = BuildIdentity.current,
                now: @escaping () -> Date = Date.init) {
        self.buildId = buildId
        self.applications = applications
        self.metrics = metrics
        self.details = details
        self.aiUsage = aiUsage
        self.agentStatus = agentStatus
        self.statusPages = statusPages
        self.now = now
        applications.observeActiveApplication { [weak self] bundle in
            self?.handleForegroundChange(bundle)
        }
    }

    /// Cardputer sends PING every three seconds; a session with no valid
    /// request for this long has been dropped on the Cardputer side.
    public static let livenessTimeout: TimeInterval = 12

    public func livenessExpired(at now: Date) -> Bool {
        guard session != 0, let lastValidMessageAt else { return false }
        return now.timeIntervalSince(lastValidMessageAt) > Self.livenessTimeout
    }

    public func startHandshake() {
        guard let hello = CompanionCodec.hello(CompanionHello(buildId: buildId)),
              let bytes = CompanionCodec.encode(hello) else { return }
        awaitingHelloAck = true
        // An unanswered HELLO stays reported while the Companion keeps retrying.
        if compatibility != .noAnswer { compatibility = .unknown }
        self.outgoing(bytes)
    }

    public func reset() {
        // A request never outlives its session and is never resent.
        failPending(.disconnected)
        session = 0
        if compatibility != .noAnswer { compatibility = .unknown }
        sessionStartedAt = nil
        lastValidMessageAt = nil
        lastActiveBundle = nil
        lastEventBundle = nil
        awaitingHelloAck = false
    }

    public func stop() {
        reset()
        applications.stopObservingActiveApplication()
    }

    /// The HELLO went unanswered: firmware built before plan 043 cannot read it.
    public func handshakeTimedOut() {
        guard awaitingHelloAck else { return }
        awaitingHelloAck = false
        compatibility = .noAnswer
    }

    @discardableResult
    public func handle(_ data: [UInt8]) -> Bool {
        guard let message = CompanionCodec.decode(data) else { return false }
        switch message.kind {
        case .helloAck:
            guard awaitingHelloAck, let firmware = CompanionHello.read(message.payload)
            else { return false }
            awaitingHelloAck = false
            guard message.status == .ok, message.session != 0,
                  firmware.fingerprint == ProtocolFingerprint.bytes else {
                // Built from another protocol: nothing else is exchanged until the
                // user updates one side and reconnects.
                compatibility = .mismatch(firmwareBuildId: firmware.buildId)
                return true
            }
            compatibility = .matched(firmwareBuildId: firmware.buildId)
            failPending(.disconnected)
            session = message.session
            sessionStartedAt = now()
            lastValidMessageAt = sessionStartedAt
            sendInitialActive()
            return true
        case .request:
            guard session != 0, message.session == session else { return false }
            lastValidMessageAt = now()
            handleRequest(message)
            return true
        case .response:
            guard session != 0, message.session == session, let request = pending,
                  request.id == message.requestId, request.operation == message.operation,
                  request.session == message.session else { return false }
            pending = nil
            lastValidMessageAt = now()
            request.completion(.success(message))
            return true
        default:
            return false
        }
    }

    /// Sends one request; only one is outstanding at a time.
    public func sendRequest(_ operation: CompanionOperation, payload: [UInt8],
                            completion: @escaping CompanionResponseHandler) {
        guard session != 0 else { return completion(.failure(.disconnected)) }
        guard pending == nil else { return completion(.failure(.busy)) }
        var message = CompanionEnvelope()
        message.kind = .request
        message.session = session
        message.requestId = nextRequestId
        message.operation = operation
        message.payload = payload
        guard let bytes = CompanionCodec.encode(message) else { return completion(.failure(.encoding)) }
        nextRequestId = nextRequestId == 255 ? 1 : nextRequestId + 1
        pending = PendingRequest(id: message.requestId, operation: operation, session: session,
                                 deadline: now().addingTimeInterval(Self.requestTimeout),
                                 completion: completion)
        outgoing(bytes)
    }

    public var hasPendingRequest: Bool { pending != nil }

    public func expireRequests(at time: Date) {
        guard let request = pending, time >= request.deadline else { return }
        pending = nil
        request.completion(.failure(.timeout))
    }

    private func failPending(_ failure: CompanionRequestFailure) {
        guard let request = pending else { return }
        pending = nil
        request.completion(.failure(failure))
    }

    private func handleRequest(_ message: CompanionEnvelope) {
        switch message.operation {
        case .ping:
            let response = CompanionCodec.pingResponse(
                session: message.session, requestId: message.requestId, token: message.payload)
            send(response)
        case .systemMetrics:
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
        case .systemDetails:
            guard let group = message.payload.first.flatMap(SystemDetailsGroup.init(rawValue:))
            else { return }
            var response = CompanionEnvelope()
            response.kind = .response
            response.session = message.session
            response.requestId = message.requestId
            response.operation = .systemDetails
            if let sample = details?.collect(group), sample.group == group,
               let payload = sample.encode() {
                response.payload = payload
            } else {
                response.status = .notAvailable
            }
            send(response)
        case .aiUsage:
            var response = CompanionEnvelope()
            response.kind = .response
            response.session = message.session
            response.requestId = message.requestId
            response.operation = .aiUsage
            if let payload = aiUsage?.snapshot()?.encode() {
                response.payload = payload
            } else {
                response.status = .notAvailable
            }
            send(response)
        case .aiAgentStatus:
            var response = CompanionEnvelope()
            response.kind = .response
            response.session = message.session
            response.requestId = message.requestId
            response.operation = .aiAgentStatus
            if let snapshot = agentStatus?.agentStatus() {
                response.payload = snapshot.encode()
            } else {
                response.status = .notAvailable
            }
            send(response)
        case .serviceStatus:
            var response = CompanionEnvelope()
            response.kind = .response
            response.session = message.session
            response.requestId = message.requestId
            response.operation = .serviceStatus
            guard let url = ServiceStatusWire.requestURL(message.payload), let statusPages else {
                response.status = .notAvailable
                send(response)
                return
            }
            // The page is fetched over the internet; the answer belongs to the
            // session that asked and is dropped if that session has ended.
            statusPages.fetch(url) { [weak self] report in
                guard let self, self.session == response.session else { return }
                var answer = response
                if let report {
                    answer.payload = report.encode()
                } else {
                    answer.status = .notAvailable
                }
                self.send(answer)
            }
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
        self.outgoing(bytes)
    }
}
