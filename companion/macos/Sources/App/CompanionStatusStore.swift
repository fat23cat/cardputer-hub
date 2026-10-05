import Combine
import CompanionAgentHooks
import CompanionCore
import Foundation

final class CompanionStatusStore: ObservableObject {
    @Published private(set) var connection: CompanionConnectionPresentationState = .disconnected
    @Published private(set) var firmwareBuildId: String?
    @Published private(set) var compatibility: CompanionCompatibility = .unknown
    @Published private(set) var companionBuildId = BuildIdentity.current
    @Published private(set) var sessionId: UInt16?
    @Published private(set) var sessionStartedAt: Date?
    @Published private(set) var lastMessageAt: Date?
    @Published private(set) var bluetoothReady = false
    @Published private(set) var startAtLogin = false
    @Published private(set) var startAtLoginError = false
    @Published private(set) var now = Date()
    @Published private(set) var aiUsage = AiUsageSnapshot()
    var onReconnect: (() -> Void)?
    var onQuit: (() -> Void)?
    var onOpenInventory: (() -> Void)?
    var readAiUsage: (() -> AiUsageSnapshot?)?
    /// Whether hooks are installed (nil: the configuration is not plain JSON)
    /// and when the last hook event arrived.
    var readAgentHooks: ((AgentApplication) -> (installed: Bool?, lastEvent: Date?))?
    /// Installs or removes an application's hooks; returns an error message.
    var onSetAgentHooks: ((AgentApplication, Bool) -> String?)?

    private let login: StartAtLoginModel

    init(login: StartAtLoginModel) {
        self.login = login
        refreshLogin()
    }

    var lastSeenText: String? {
        CompanionPresentation.lastSeen(lastMessageAt, now: now)
    }

    var statusMetadataText: String? {
        CompanionPresentation.statusMetadata(connection: connection, firmwareBuildId: firmwareBuildId,
                                             lastMessageAt: lastMessageAt, now: now)
    }

    var compatibilityNotice: String? {
        CompanionPresentation.compatibilityNotice(compatibility, companionBuildId: companionBuildId)
    }

    var sessionDurationText: String? {
        CompanionPresentation.sessionDuration(sessionStartedAt, now: now)
    }

    func sync(session: CompanionSession, phase: CompanionAttachPhase,
              error: Bool, bluetoothReady: Bool, waitingToConnect: Bool = false) {
        connection = CompanionPresentation.connection(session: session.session, phase: phase,
                                                       error: error, waitingToConnect: waitingToConnect)
        let connected = session.session != 0
        compatibility = session.compatibility
        companionBuildId = session.buildId
        if case .matched(let firmware) = session.compatibility, connected {
            firmwareBuildId = firmware
        } else if case .mismatch(let firmware) = session.compatibility {
            firmwareBuildId = firmware
        } else {
            firmwareBuildId = nil
        }
        sessionId = connected ? session.session : nil
        sessionStartedAt = connected ? session.sessionStartedAt : nil
        lastMessageAt = connected ? session.lastValidMessageAt : nil
        self.bluetoothReady = bluetoothReady
        now = Date()
    }

    func tick() {
        now = Date()
        aiUsage = readAiUsage?() ?? AiUsageSnapshot()
    }

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
