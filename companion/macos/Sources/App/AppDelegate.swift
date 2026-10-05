import AppKit
import CompanionAgentHooks
import CompanionCore
import CompanionProviders
import CoreBluetooth
import Foundation
import os.log
import ServiceManagement

private let log = Logger(subsystem: "org.cardputer.companion", category: "runtime")

final class CompanionCentral: NSObject, CBCentralManagerDelegate, CBPeripheralDelegate {
    private let session: CompanionSession
    private let aiUsage: AiUsageCollector
    private let status: CompanionStatusStore
    private var connectionError = false
    private var stopped = false
    private var manager: CBCentralManager?
    private var peripheral: CBPeripheral?
    private var hostToDevice: CBCharacteristic?
    private var deviceToHost: CBCharacteristic?
    private var retry: Timer?
    private var handshakeWatchdog: Timer?
    private var reassemblyTick: Timer?
    private var outgoingId: UInt8 = 1
    private let reassembler = CompanionReassembler()
    private var coordinator = CompanionAttachCoordinator()
    private var companionMatches: [CBPeripheral] = []
    private var hidMatches: [CBPeripheral] = []
    private var pendingTarget: CBPeripheral?
    private var cancellationTick: Timer?
    private var workspaceObservers: [NSObjectProtocol] = []
    private var livenessTick: Timer?
    private let inventory: InventoryEditorModel
    private var inventorySession: UInt16 = 0

    init(applications: ApplicationControlling, metrics: SystemMetricsCollecting,
         details: SystemDetailsCollecting, aiUsage: AiUsageCollector, status: CompanionStatusStore,
         inventory: InventoryEditorModel, agentStatus: AgentStatusProviding) {
        self.aiUsage = aiUsage
        self.inventory = inventory
        session = CompanionSession(applications: applications, metrics: metrics, details: details,
                                   aiUsage: aiUsage, agentStatus: agentStatus)
        self.status = status
        super.init()
        session.outgoing = { [weak self] bytes in self?.send(bytes) }
        status.readAiUsage = { [weak aiUsage] in aiUsage?.snapshot() }
    }

    func start() {
        aiUsage.start()
        // The Cardputer drops a session it cannot keep up with; reattach instead
        // of waiting for the user to press Reconnect.
        livenessTick?.invalidate()
        livenessTick = Timer.scheduledTimer(withTimeInterval: 3, repeats: true) { [weak self] _ in
            guard let self, self.session.livenessExpired(at: Date()) else { return }
            log.error("companion session went silent; reconnecting")
            self.reconnect()
        }
        manager = CBCentralManager(delegate: self, queue: .main)
        refreshStatus()
        if workspaceObservers.isEmpty {
            let workspace = NSWorkspace.shared.notificationCenter
            workspaceObservers = [
                workspace.addObserver(
                    forName: NSWorkspace.willSleepNotification, object: nil, queue: .main
                ) { [weak self] _ in
                    self?.handleWillSleep()
                },
                workspace.addObserver(
                    forName: NSWorkspace.didWakeNotification, object: nil, queue: .main
                ) { [weak self] _ in
                    self?.handleDidWake()
                },
            ]
        }
    }

    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        guard central === manager else { return }
        if central.state != .poweredOn {
            log.info("bluetooth unavailable; dropping companion attach")
            connectionError = true
            apply(coordinator.handleRadioUnavailable())
            return
        }
        connectionError = false
        lookup()
    }

    private func handleWillSleep() {
        log.info("mac sleeping; dropping companion attach")
        connectionError = false
        if let peripheral, let deviceToHost, deviceToHost.isNotifying {
            peripheral.setNotifyValue(false, for: deviceToHost)
        }
        apply(coordinator.handleRadioUnavailable())
    }

    private func handleDidWake() {
        guard manager?.state == .poweredOn else { return }
        log.info("mac woke; recovering companion attach")
        apply(coordinator.handleSleepWake())
    }

    private func lookup() {
        guard let manager else { return }
        companionMatches = manager.retrieveConnectedPeripherals(
            withServices: [CBUUID(string: CompanionConstants.serviceUUID)])
        hidMatches = manager.retrieveConnectedPeripherals(
            withServices: [CBUUID(string: CompanionConstants.hidServiceUUID)])
        apply(coordinator.lookup(companionCount: companionMatches.count, hidCount: hidMatches.count))
    }

    private func apply(_ action: CompanionAttachAction) {
        defer { refreshStatus() }
        switch action {
        case .idle:
            break
        case .retryLater, .ambiguous, .cancelCurrentAndRetry:
            if action == .ambiguous {
                log.error("multiple Cardputer peripherals connected; not choosing")
                connectionError = true
            } else if action == .retryLater {
                connectionError = false
            }
            resetLocalConnection()
            scheduleRetry()
        case .connectCompanion(let index):
            connectionError = false
            retry?.invalidate()
            connect(companionMatches, index: index)
        case .connectHid(let index):
            connectionError = false
            retry?.invalidate()
            log.info("probing connected HID peripheral")
            connect(hidMatches, index: index)
        case .cancelCurrentAndProbeHid(let index):
            connectionError = false
            retry?.invalidate()
            resetLocalConnection()
            connect(hidMatches, index: index)
        case .resumeConnect:
            cancellationTick?.invalidate()
            cancellationTick = nil
            if let pendingTarget {
                startConnection(pendingTarget)
                self.pendingTarget = nil
            }
        case .resetCentral:
            restartCentral()
        case .radioUnavailable:
            retry?.invalidate()
            retry = nil
            resetLocalConnection()
        case .lookupNow:
            lookup()
        case .discoverServices:
            peripheral?.discoverServices([CBUUID(string: CompanionConstants.serviceUUID)])
        case .discoverCharacteristics:
            guard let peripheral, let service = peripheral.services?.first(where: {
                $0.uuid == CBUUID(string: CompanionConstants.serviceUUID)
            }) else {
                apply(coordinator.failAndRetry())
                return
            }
            peripheral.discoverCharacteristics(
                [CBUUID(string: CompanionConstants.hostToDeviceUUID),
                 CBUUID(string: CompanionConstants.deviceToHostUUID)],
                for: service)
        case .subscribe:
            guard let peripheral, let deviceToHost else {
                apply(coordinator.failAndRetry())
                return
            }
            peripheral.setNotifyValue(true, for: deviceToHost)
        case .startHandshake:
            let attempt = coordinator.generation
            session.startHandshake()
            startReassemblyTick()
            handshakeWatchdog?.invalidate()
            handshakeWatchdog = Timer.scheduledTimer(withTimeInterval: 5, repeats: false) { [weak self] _ in
                guard let self, self.session.session == 0 else { return }
                // A mismatch is final until the user updates one side and reconnects.
                if case .mismatch = self.session.compatibility { return }
                self.session.handshakeTimedOut()
                log.error("companion handshake timed out")
                self.connectionError = true
                self.apply(self.coordinator.handleHandshakeTimeout(attempt))
            }
        }
    }

    private func refreshStatus() {
        status.sync(session: session, phase: coordinator.phase,
                    error: connectionError, bluetoothReady: manager?.state == .poweredOn,
                    waitingToConnect: coordinator.pendingConnectId != nil)
        // Each new session lists the Cardputer's records afresh; nothing is
        // replayed from an earlier one.
        if session.session != inventorySession {
            if inventorySession != 0 { inventory.setTransport(nil) }
            inventorySession = session.session
            if inventorySession != 0 { inventory.setTransport(session) }
        }
    }

    func reconnect() {
        guard !stopped else { return }
        connectionError = false
        coordinator = CompanionAttachCoordinator()
        restartCentral()
        status.sync(session: session, phase: .connecting,
                    error: false, bluetoothReady: manager?.state == .poweredOn)
    }

    func stop() {
        guard !stopped else { return }
        stopped = true
        livenessTick?.invalidate()
        livenessTick = nil
        retry?.invalidate()
        retry = nil
        resetLocalConnection()
        session.stop()
        aiUsage.stop()
        for observer in workspaceObservers {
            NSWorkspace.shared.notificationCenter.removeObserver(observer)
        }
        workspaceObservers.removeAll()
        manager?.delegate = nil
        manager = nil
        coordinator = CompanionAttachCoordinator()
        connectionError = false
        refreshStatus()
    }

    private func connect(_ matches: [CBPeripheral], index: Int) {
        guard manager != nil, index >= 0, index < matches.count else {
            apply(coordinator.failAndRetry())
            return
        }
        let target = matches[index]
        target.delegate = self
        switch coordinator.beginConnect(target.identifier) {
        case .waitForCancellation:
            pendingTarget = target
            startCancellationTick()
        case .proceed:
            pendingTarget = nil
            startConnection(target)
        }
    }

    private func startConnection(_ target: CBPeripheral) {
        guard let manager else {
            apply(coordinator.failAndRetry())
            return
        }
        peripheral = target
        target.delegate = self
        if target.state == .connected {
            apply(coordinator.handleDidConnect(target.identifier))
            return
        }
        manager.connect(target)
    }

    private func resetLocalConnection() {
        handshakeWatchdog?.invalidate()
        handshakeWatchdog = nil
        reassemblyTick?.invalidate()
        reassemblyTick = nil
        cancellationTick?.invalidate()
        cancellationTick = nil
        coordinator.abandonCurrent()
        pendingTarget = nil
        if let manager, let peripheral {
            manager.cancelPeripheralConnection(peripheral)
        }
        peripheral = nil
        hostToDevice = nil
        deviceToHost = nil
        reassembler.reset()
        session.reset()
    }

    private func restartCentral() {
        handshakeWatchdog?.invalidate()
        handshakeWatchdog = nil
        reassemblyTick?.invalidate()
        reassemblyTick = nil
        cancellationTick?.invalidate()
        cancellationTick = nil
        retry?.invalidate()
        retry = nil
        pendingTarget = nil
        manager?.delegate = nil
        peripheral?.delegate = nil
        if let manager, let peripheral {
            manager.cancelPeripheralConnection(peripheral)
        }
        peripheral = nil
        hostToDevice = nil
        deviceToHost = nil
        reassembler.reset()
        session.reset()
        manager = CBCentralManager(delegate: self, queue: .main)
    }

    private func scheduleRetry() {
        retry?.invalidate()
        retry = Timer.scheduledTimer(withTimeInterval: 3, repeats: false) { [weak self] _ in
            self?.lookup()
        }
    }

    private func startCancellationTick() {
        cancellationTick?.invalidate()
        cancellationTick = Timer.scheduledTimer(withTimeInterval: 0.25, repeats: true) { [weak self] _ in
            guard let self else { return }
            self.apply(self.coordinator.updateCancellation(0.25))
        }
    }

    private func startReassemblyTick() {
        reassemblyTick?.invalidate()
        reassemblyTick = Timer.scheduledTimer(withTimeInterval: 0.25, repeats: true) { [weak self] _ in
            self?.reassembler.update(0.25)
            self?.session.expireRequests(at: Date())
        }
    }

    func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        guard central === manager else { return }
        apply(coordinator.handleDidConnect(peripheral.identifier))
    }

    func centralManager(_ central: CBCentralManager, didFailToConnect peripheral: CBPeripheral, error: Error?) {
        guard central === manager else { return }
        log.error("failed to connect to Cardputer")
        connectionError = true
        apply(coordinator.handleDidFailToConnect(peripheral.identifier))
    }

    func centralManager(_ central: CBCentralManager, didDisconnectPeripheral peripheral: CBPeripheral, error: Error?) {
        guard central === manager else { return }
        connectionError = error != nil
        apply(coordinator.handleDidDisconnect(peripheral.identifier))
    }

    func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        let hasCompanion = error == nil && peripheral.services?.contains(where: {
            $0.uuid == CBUUID(string: CompanionConstants.serviceUUID)
        }) == true
        if !hasCompanion {
            log.error("companion GATT service missing")
            connectionError = true
        }
        apply(coordinator.handleDidDiscoverServices(
            peripheral.identifier, hasCompanion: hasCompanion, error: error != nil))
    }

    func peripheral(_ peripheral: CBPeripheral, didDiscoverCharacteristicsFor service: CBService, error: Error?) {
        var host: CBCharacteristic?
        var device: CBCharacteristic?
        for characteristic in service.characteristics ?? [] {
            if characteristic.uuid == CBUUID(string: CompanionConstants.hostToDeviceUUID) {
                host = characteristic
            }
            if characteristic.uuid == CBUUID(string: CompanionConstants.deviceToHostUUID) {
                device = characteristic
            }
        }
        let action = coordinator.handleDidDiscoverCharacteristics(
            peripheral.identifier,
            hostToDevice: host != nil,
            deviceToHost: device != nil,
            error: error != nil)
        guard action != .idle else { return }
        if action == .subscribe {
            hostToDevice = host
            deviceToHost = device
        }
        if error != nil || host == nil || device == nil {
            log.error("companion GATT characteristics missing")
            connectionError = true
        }
        apply(action)
    }

    func peripheral(_ peripheral: CBPeripheral, didUpdateNotificationStateFor characteristic: CBCharacteristic, error: Error?) {
        guard characteristic.uuid == CBUUID(string: CompanionConstants.deviceToHostUUID) else { return }
        let notifying = error == nil && characteristic.isNotifying
        if !notifying {
            log.error("companion notify subscribe failed")
            connectionError = true
        }
        apply(coordinator.handleDidUpdateNotificationState(
            peripheral.identifier, notifying: notifying, error: error != nil))
    }

    func peripheral(_ peripheral: CBPeripheral, didUpdateValueFor characteristic: CBCharacteristic, error: Error?) {
        guard peripheral.identifier == coordinator.currentId,
              error == nil, let data = characteristic.value,
              let assembled = reassembler.ingest([UInt8](data))
        else { return }
        if session.handle(assembled) {
            if case .mismatch = session.compatibility {
                log.error("cardputer firmware built from a different protocol")
                connectionError = true
            }
            refreshStatus()
        }
    }

    private func send(_ bytes: [UInt8]) {
        guard let peripheral, peripheral.identifier == coordinator.currentId, let hostToDevice,
              let framed = CompanionFramer.encode(
                  bytes, messageId: outgoingId,
                  maxPayload: CompanionFramer.payloadSize(
                      maximumWriteLength: peripheral.maximumWriteValueLength(for: .withoutResponse)))
        else { return }
        outgoingId &+= 1
        if outgoingId == 0 { outgoingId = 1 }
        for chunk in framed {
            peripheral.writeValue(Data(chunk), for: hostToDevice, type: .withResponse)
        }
    }
}

struct SystemLoginRegistration: LoginRegistration {
    var isEnabled: Bool { SMAppService.mainApp.status == .enabled }

    func setEnabled(_ enabled: Bool) throws {
        if enabled {
            if !isEnabled { try SMAppService.mainApp.register() }
        } else if SMAppService.mainApp.status != .notRegistered {
            try SMAppService.mainApp.unregister()
        }
    }
}

final class AppDelegate: NSObject, NSApplicationDelegate {
    private var central: CompanionCentral?
    private var status: CompanionStatusStore?
    private var menuBar: CompanionMenuBarController?
    private var inventoryWindow: InventoryWindowController?
    private var agentHooks: AgentHookReceiver?

    func applicationDidFinishLaunching(_ notification: Notification) {
        installEditMenu()
        let status = CompanionStatusStore(login: StartAtLoginModel(service: SystemLoginRegistration()))
        let menuBar = CompanionMenuBarController(status: status)
        let inventory = InventoryEditorModel()
        let inventoryWindow = InventoryWindowController(model: inventory)
        let agentEvents = AgentStatusStore()
        let agentHooks = AgentHookReceiver(store: agentEvents)
        let installer = AgentHookInstaller()
        // The Cardputer lists only applications whose hooks are installed.
        let agentStatus = AgentStatusReport(store: agentEvents) {
            Set(AgentApplication.allCases.filter { installer.isInstalled($0) == true })
        }
        // Keep the helper that installed hooks run in step with this build.
        if FileManager.default.fileExists(atPath: AgentHookPaths.helperPath) {
            try? installer.refreshHelper()
        }
        status.readAgentHooks = { application in
            (installer.isInstalled(application), agentEvents.lastEvent(application))
        }
        status.onSetAgentHooks = { application, install in
            do {
                try install ? installer.install(application) : installer.remove(application)
                agentStatus.invalidate()
                return nil
            } catch {
                return error.localizedDescription
            }
        }
        let central = CompanionCentral(applications: WorkspaceApplicationController(),
                                       metrics: MacSystemMetricsCollector(),
                                       details: MacSystemDetailsCollector(),
                                       aiUsage: AiUsageCollector(), status: status,
                                       inventory: inventory, agentStatus: agentStatus)
        status.onReconnect = { [weak central] in central?.reconnect() }
        status.onQuit = { [weak self] in self?.quit() }
        status.onOpenInventory = { [weak inventoryWindow] in inventoryWindow?.show() }
        self.status = status
        self.menuBar = menuBar
        self.inventoryWindow = inventoryWindow
        self.central = central
        self.agentHooks = agentHooks
        agentHooks.start()
        central.start()
    }

    func applicationWillTerminate(_ notification: Notification) {
        central?.stop()
        agentHooks?.stop()
        inventoryWindow?.close()
        menuBar?.stop()
    }

    private func quit() {
        NSApp.terminate(nil)
    }

    private func installEditMenu() {
        // SwiftUI text controls route these actions through the first responder.
        // An accessory app has no default Edit menu to supply their shortcuts.
        let mainMenu = NSMenu()
        let editItem = NSMenuItem(title: "Edit", action: nil, keyEquivalent: "")
        let editMenu = NSMenu(title: "Edit")
        for (title, action, key) in [
            ("Cut", #selector(NSText.cut(_:)), "x"),
            ("Copy", #selector(NSText.copy(_:)), "c"),
            ("Paste", #selector(NSText.paste(_:)), "v"),
            ("Select All", #selector(NSText.selectAll(_:)), "a"),
        ] {
            let item = NSMenuItem(title: title, action: action, keyEquivalent: key)
            item.keyEquivalentModifierMask = .command
            editMenu.addItem(item)
        }
        editItem.submenu = editMenu
        mainMenu.addItem(editItem)
        NSApp.mainMenu = mainMenu
    }
}

@main
enum CompanionMain {
    static var delegate: AppDelegate?

    static func main() {
        let app = NSApplication.shared
        let delegate = AppDelegate()
        self.delegate = delegate
        app.delegate = delegate
        app.setActivationPolicy(.accessory)
        app.run()
    }
}
