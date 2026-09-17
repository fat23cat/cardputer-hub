import Foundation

public enum CompanionAttachAction: Equatable {
    case idle
    case retryLater
    case connectCompanion(Int)
    case connectHid(Int)
    case discoverServices
    case discoverCharacteristics
    case subscribe
    case startHandshake
    case cancelCurrentAndRetry
    case cancelCurrentAndProbeHid(Int)
    case resumeConnect
    case resetCentral
    case ambiguous
}

public enum CompanionConnectStart: Equatable {
    case proceed
    case waitForCancellation
}

public enum CompanionAttachPhase: Equatable {
    case idle
    case connecting
    case discoveringServices
    case discoveringCharacteristics
    case subscribing
    case handshaking
    case cancelling
}

public struct CompanionAttachEngine: Equatable {
    public private(set) var holdingPeripheral = false
    public private(set) var hidCount = 0
    public private(set) var hidIndex = 0
    public private(set) var probingHid = false

    public init() {}

    public mutating func lookup(companionCount: Int, hidCount: Int) -> CompanionAttachAction {
        if holdingPeripheral { return .idle }
        if companionCount == 1 {
            probingHid = false
            holdingPeripheral = true
            return .connectCompanion(0)
        }
        if companionCount > 1 {
            return .ambiguous
        }
        self.hidCount = hidCount
        hidIndex = 0
        probingHid = hidCount > 0
        if hidCount == 0 {
            return .retryLater
        }
        holdingPeripheral = true
        return .connectHid(0)
    }

    public mutating func connected() -> CompanionAttachAction { .discoverServices }

    public mutating func connectFailed() -> CompanionAttachAction {
        probingHid ? advanceHid() : failAndRetry()
    }

    public mutating func servicesFound() -> CompanionAttachAction {
        probingHid = false
        return .discoverCharacteristics
    }

    public mutating func servicesMissing() -> CompanionAttachAction {
        probingHid ? advanceHid() : failAndRetry()
    }

    public mutating func characteristicsReady() -> CompanionAttachAction { .subscribe }

    public mutating func characteristicsMissing() -> CompanionAttachAction { failAndRetry() }

    public mutating func notifyReady() -> CompanionAttachAction { .startHandshake }

    public mutating func notifyFailed() -> CompanionAttachAction { failAndRetry() }

    public mutating func handshakeTimeout() -> CompanionAttachAction { failAndRetry() }

    public mutating func disconnected() -> CompanionAttachAction { failAndRetry() }

    public mutating func failAndRetry() -> CompanionAttachAction {
        holdingPeripheral = false
        probingHid = false
        hidIndex = 0
        hidCount = 0
        return .cancelCurrentAndRetry
    }

    private mutating func advanceHid() -> CompanionAttachAction {
        holdingPeripheral = false
        hidIndex += 1
        if hidIndex >= hidCount {
            probingHid = false
            hidCount = 0
            return .cancelCurrentAndRetry
        }
        holdingPeripheral = true
        return .cancelCurrentAndProbeHid(hidIndex)
    }
}

public struct CompanionAttachCoordinator: Equatable {
    public static let cancellationTimeout: TimeInterval = 2

    public private(set) var engine = CompanionAttachEngine()
    public private(set) var currentId: UUID?
    public private(set) var generation: UInt64 = 0
    public private(set) var phase = CompanionAttachPhase.idle
    public private(set) var pendingConnectId: UUID?
    private var cancellingId: UUID?
    private var cancelledIds: Set<UUID> = []
    private var cancellationElapsed: TimeInterval = 0

    public init() {}

    public mutating func lookup(companionCount: Int, hidCount: Int) -> CompanionAttachAction {
        engine.lookup(companionCount: companionCount, hidCount: hidCount)
    }

    public mutating func beginConnect(_ id: UUID) -> CompanionConnectStart {
        if phase == .cancelling, cancellingId == id {
            pendingConnectId = id
            return .waitForCancellation
        }
        if phase == .cancelling, let previous = cancellingId, previous != id {
            cancelledIds.insert(previous)
            cancellingId = nil
            cancellationElapsed = 0
        }
        startConnecting(id)
        return .proceed
    }

    public mutating func abandonCurrent() {
        guard let id = currentId else { return }
        cancellingId = id
        cancelledIds.insert(id)
        currentId = nil
        pendingConnectId = nil
        cancellationElapsed = 0
        phase = .cancelling
    }

    public mutating func updateCancellation(_ delta: TimeInterval) -> CompanionAttachAction {
        guard phase == .cancelling, pendingConnectId != nil else { return .idle }
        cancellationElapsed += max(0, delta)
        if cancellationElapsed >= Self.cancellationTimeout {
            return timeoutCancellation()
        }
        return .idle
    }

    public mutating func handleDidConnect(_ id: UUID) -> CompanionAttachAction {
        guard currentId == id, phase == .connecting else { return .idle }
        phase = .discoveringServices
        return engine.connected()
    }

    public mutating func handleDidFailToConnect(_ id: UUID) -> CompanionAttachAction {
        if phase == .cancelling, cancellingId == id {
            return completeCancellation()
        }
        guard currentId == id, phase == .connecting else { return .idle }
        return finish(engine.connectFailed())
    }

    public mutating func handleDidDisconnect(_ id: UUID) -> CompanionAttachAction {
        if phase == .cancelling, cancellingId == id {
            return completeCancellation()
        }
        if cancelledIds.remove(id) != nil, currentId != id {
            return .idle
        }
        guard currentId == id else { return .idle }
        return finish(engine.disconnected())
    }

    public mutating func handleDidDiscoverServices(_ id: UUID, hasCompanion: Bool, error: Bool)
        -> CompanionAttachAction
    {
        guard currentId == id, phase == .discoveringServices else { return .idle }
        if error || !hasCompanion {
            return finish(engine.servicesMissing())
        }
        phase = .discoveringCharacteristics
        return engine.servicesFound()
    }

    public mutating func handleDidDiscoverCharacteristics(
        _ id: UUID, hostToDevice: Bool, deviceToHost: Bool, error: Bool
    ) -> CompanionAttachAction {
        guard currentId == id, phase == .discoveringCharacteristics else { return .idle }
        if error || !hostToDevice || !deviceToHost {
            return finish(engine.characteristicsMissing())
        }
        phase = .subscribing
        return engine.characteristicsReady()
    }

    public mutating func handleDidUpdateNotificationState(
        _ id: UUID, notifying: Bool, error: Bool
    ) -> CompanionAttachAction {
        guard currentId == id, phase == .subscribing else { return .idle }
        if error || !notifying {
            return finish(engine.notifyFailed())
        }
        phase = .handshaking
        return engine.notifyReady()
    }

    public mutating func handleHandshakeTimeout(_ generation: UInt64) -> CompanionAttachAction {
        guard self.generation == generation, phase == .handshaking else { return .idle }
        return finish(engine.handshakeTimeout())
    }

    public mutating func failAndRetry() -> CompanionAttachAction {
        finish(engine.failAndRetry())
    }

    private mutating func startConnecting(_ id: UUID) {
        generation &+= 1
        if generation == 0 { generation = 1 }
        currentId = id
        pendingConnectId = nil
        phase = .connecting
    }

    private mutating func completeCancellation() -> CompanionAttachAction {
        if let cancellingId {
            cancelledIds.remove(cancellingId)
        }
        let pending = pendingConnectId
        cancellingId = nil
        pendingConnectId = nil
        cancellationElapsed = 0
        phase = .idle
        guard let pending else { return .idle }
        startConnecting(pending)
        return .resumeConnect
    }

    private mutating func timeoutCancellation() -> CompanionAttachAction {
        _ = engine.failAndRetry()
        generation &+= 1
        if generation == 0 { generation = 1 }
        currentId = nil
        pendingConnectId = nil
        cancellingId = nil
        cancelledIds.removeAll()
        cancellationElapsed = 0
        phase = .idle
        return .resetCentral
    }

    private mutating func finish(_ action: CompanionAttachAction) -> CompanionAttachAction {
        switch action {
        case .cancelCurrentAndRetry, .cancelCurrentAndProbeHid:
            abandonCurrent()
        default:
            break
        }
        return action
    }
}
