import Combine
import CompanionCore
import Foundation

enum CompanionPopoverPage: Equatable {
    case main
    case diagnostics
    case about
}

final class CompanionStatusStore: ObservableObject {
    @Published private(set) var connection: CompanionConnectionPresentationState = .disconnected
    @Published private(set) var protocolVersion: UInt8?
    @Published private(set) var sessionId: UInt16?
    @Published private(set) var sessionStartedAt: Date?
    @Published private(set) var lastMessageAt: Date?
    @Published private(set) var capabilities = CompanionCapabilitySummary([])
    @Published private(set) var bluetoothReady = false
    @Published private(set) var startAtLogin = false
    @Published private(set) var startAtLoginError = false
    @Published private(set) var now = Date()
    @Published var page: CompanionPopoverPage = .main

    var onReconnect: (() -> Void)?
    var onQuit: (() -> Void)?

    private let login: StartAtLoginModel

    init(login: StartAtLoginModel) {
        self.login = login
        refreshLogin()
    }

    var lastSeenText: String? {
        CompanionPresentation.lastSeen(lastMessageAt, now: now)
    }

    var statusMetadataText: String? {
        CompanionPresentation.statusMetadata(connection: connection, protocolVersion: protocolVersion,
                                             lastMessageAt: lastMessageAt, now: now)
    }

    var sessionDurationText: String? {
        CompanionPresentation.sessionDuration(sessionStartedAt, now: now)
    }

    func sync(session: CompanionSession, phase: CompanionAttachPhase,
              error: Bool, bluetoothReady: Bool, waitingToConnect: Bool = false) {
        connection = CompanionPresentation.connection(session: session.session, phase: phase,
                                                       error: error, waitingToConnect: waitingToConnect)
        let connected = session.session != 0
        protocolVersion = connected ? session.selectedProtocolVersion : nil
        sessionId = connected ? session.session : nil
        sessionStartedAt = connected ? session.sessionStartedAt : nil
        lastMessageAt = connected ? session.lastValidMessageAt : nil
        capabilities = CompanionCapabilitySummary(connected ? session.liveCapabilities : [])
        self.bluetoothReady = bluetoothReady
        now = Date()
    }

    func tick() { now = Date() }

    func refreshLogin() {
        login.refresh()
        startAtLogin = login.enabled
        startAtLoginError = login.hasError
    }

    func setStartAtLogin(_ enabled: Bool) {
        login.setEnabled(enabled)
        refreshLogin()
    }
}
