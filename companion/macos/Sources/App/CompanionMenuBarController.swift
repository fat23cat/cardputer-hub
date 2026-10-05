import AppKit
import Combine
import CompanionAgentHooks
import CompanionCore

/// Owns the status item and its menu. Everything except the connection header
/// and the Start at Login switch is a standard `NSMenuItem`, so highlight,
/// submenu chevrons, keyboard navigation, and disabled styling come from AppKit.
final class CompanionMenuBarController: NSObject, NSMenuDelegate {
    private let status: CompanionStatusStore
    private let item: NSStatusItem
    private let menu = NSMenu()
    private let diagnosticsMenu = NSMenu(title: "Diagnostics")
    private let agentHooksMenu = NSMenu(title: "AI Agent Hooks")
    private let header = ConnectionHeaderView()
    private let loginRow = SwitchMenuRowView(title: "Start at Login")
    private let reconnectItem = NSMenuItem(title: "Reconnect", action: nil, keyEquivalent: "")
    private let loginErrorItem = NSMenuItem(title: "Could not update login setting", action: nil,
                                            keyEquivalent: "")
    private var connectionObservation: AnyCancellable?
    private var clock: Timer?

    init(status: CompanionStatusStore) {
        self.status = status
        item = NSStatusBar.system.statusItem(withLength: NSStatusItem.squareLength)
        super.init()

        menu.autoenablesItems = false
        menu.showsStateColumn = false
        menu.delegate = self
        diagnosticsMenu.autoenablesItems = false
        diagnosticsMenu.showsStateColumn = false
        diagnosticsMenu.delegate = self
        agentHooksMenu.autoenablesItems = false
        agentHooksMenu.showsStateColumn = false
        agentHooksMenu.delegate = self

        let headerItem = NSMenuItem()
        headerItem.view = header
        menu.addItem(headerItem)
        menu.addItem(.separator())

        reconnectItem.action = #selector(reconnect)
        reconnectItem.target = self
        menu.addItem(reconnectItem)

        loginRow.toggle.target = self
        loginRow.toggle.action = #selector(setStartAtLogin(_:))
        let loginItem = NSMenuItem()
        loginItem.view = loginRow
        menu.addItem(loginItem)

        loginErrorItem.isEnabled = false
        menu.addItem(loginErrorItem)
        menu.addItem(.separator())

        let inventoryItem = NSMenuItem(title: "Inventory", action: #selector(openInventory),
                                       keyEquivalent: "i")
        inventoryItem.target = self
        menu.addItem(inventoryItem)

        let agentHooksItem = NSMenuItem(title: "AI Agent Hooks", action: nil, keyEquivalent: "")
        agentHooksItem.submenu = agentHooksMenu
        menu.addItem(agentHooksItem)

        let diagnosticsItem = NSMenuItem(title: "Diagnostics", action: nil, keyEquivalent: "")
        diagnosticsItem.submenu = diagnosticsMenu
        menu.addItem(diagnosticsItem)
        let aboutItem = NSMenuItem(title: "About", action: #selector(about),
                                   keyEquivalent: "")
        aboutItem.target = self
        menu.addItem(aboutItem)
        menu.addItem(.separator())

        let quitItem = NSMenuItem(title: "Quit", action: #selector(quit),
                                  keyEquivalent: "q")
        quitItem.target = self
        menu.addItem(quitItem)
        item.menu = menu

        connectionObservation = status.$connection.removeDuplicates().sink { [weak self] state in
            self?.updateItem(state)
            self?.updateMenu()
        }
    }

    func menuNeedsUpdate(_ menu: NSMenu) {
        if menu === self.menu {
            status.refreshLogin()
            status.tick()
            updateMenu()
        } else if menu === diagnosticsMenu {
            updateDiagnostics()
        } else if menu === agentHooksMenu {
            updateAgentHooks()
        }
    }

    func menuWillOpen(_ menu: NSMenu) {
        guard menu === self.menu else { return }
        let timer = Timer(timeInterval: 1, repeats: true) { [weak self] _ in
            self?.status.tick()
            self?.updateMenu()
        }
        RunLoop.main.add(timer, forMode: .common)
        clock = timer
    }

    func menuDidClose(_ menu: NSMenu) {
        guard menu === self.menu else { return }
        clock?.invalidate()
        clock = nil
    }

    func stop() {
        clock?.invalidate()
        clock = nil
        connectionObservation = nil
        item.menu = nil
        menu.delegate = nil
        diagnosticsMenu.delegate = nil
        agentHooksMenu.delegate = nil
        NSStatusBar.system.removeStatusItem(item)
    }

    @objc private func reconnect() {
        status.onReconnect?()
    }

    @objc private func openInventory() {
        status.onOpenInventory?()
    }

    @objc private func setStartAtLogin(_ sender: NSSwitch) {
        status.setStartAtLogin(sender.state == .on)
        updateMenu()
    }

    @objc private func about() {
        NSApp.activate(ignoringOtherApps: true)
        NSApp.orderFrontStandardAboutPanel(options: [
            .credits: NSAttributedString(
                string: "Part of Cardputer Hub",
                attributes: [.font: NSFont.systemFont(ofSize: NSFont.smallSystemFontSize),
                             .foregroundColor: NSColor.secondaryLabelColor]),
        ])
    }

    @objc private func quit() {
        status.onQuit?()
    }

    private func updateMenu() {
        header.update(title: stateTitle, detail: status.compatibilityNotice ?? status.statusMetadataText ??
            (status.bluetoothReady ? "Waiting for Cardputer" : "Bluetooth unavailable"))
        reconnectItem.isEnabled = status.connection != .connecting
        loginRow.toggle.state = status.startAtLogin ? .on : .off
        loginErrorItem.isHidden = !status.startAtLoginError
    }

    private func updateDiagnostics() {
        diagnosticsMenu.removeAllItems()
        addInfo("Connection: \(stateTitle)")
        if let notice = status.compatibilityNotice {
            addInfo(notice)
        }
        if let duration = status.sessionDurationText {
            addInfo("Session: \(duration)")
        }
        if let lastSeen = status.lastSeenText {
            addInfo("Last message: \(lastSeen)")
        }
        addInfo("Bluetooth: \(status.bluetoothReady ? "Ready" : "Unavailable")")
        if let sessionId = status.sessionId {
            addInfo("Session ID: \(sessionId)")
        }

        addSection("Builds")
        addInfo("Companion: \(status.companionBuildId)")
        addInfo("Cardputer: \(status.firmwareBuildId ?? "—")")

        addSection("AI Agent Hooks")
        for application in agentApplications {
            addInfo("\(agentName(application)): \(agentHookState(application))")
        }

        addSection("AI Usage Cache on Mac")
        addInfo("Codex: \(aiProviderState(.codex))")
        addInfo("Cursor: \(aiProviderState(.cursor))")
        addInfo("Claude: \(aiProviderState(.claude))")
    }

    // Read-only values use disabled items, as in the system Battery and
    // Option-click Wi-Fi menus.
    private func addInfo(_ title: String) {
        let info = NSMenuItem(title: title, action: nil, keyEquivalent: "")
        info.isEnabled = false
        diagnosticsMenu.addItem(info)
    }

    private func addSection(_ title: String) {
        diagnosticsMenu.addItem(.separator())
        if #available(macOS 14.0, *) {
            diagnosticsMenu.addItem(.sectionHeader(title: title))
        } else {
            addInfo(title)
        }
    }

    private func aiProviderState(_ provider: AiProviderId) -> String {
        guard let sample = status.aiUsage.providers.first(where: { $0.provider == provider })
        else { return status.aiUsage.state == .discovering ? "Checking" : "No sample" }
        let state = sample.freshness == .fresh ? "Fresh" : "Stale"
        // The two-account limit keeps some providers off the Cardputer.
        let sent = status.aiUsage.sentProviders.contains { $0.provider == provider }
        return sent ? state : "\(state), not sent to Cardputer"
    }

    private let agentApplications: [AgentApplication] = [.codex, .claude, .cursor]

    private func agentName(_ application: AgentApplication) -> String {
        switch application {
        case .codex: return "Codex"
        case .claude: return "Claude Code"
        case .cursor: return "Cursor"
        }
    }

    private func agentHookState(_ application: AgentApplication) -> String {
        guard let state = status.readAgentHooks?(application) else { return "—" }
        let installed: String
        switch state.installed {
        case .some(true): installed = "Installed"
        case .some(false): installed = "Not installed"
        case .none: installed = "Configuration is not plain JSON"
        }
        guard let last = state.lastEvent else { return "\(installed) · no events yet" }
        return "\(installed) · last event \(CompanionPresentation.lastSeen(last, now: Date()) ?? "—")"
    }

    /// One action per application: install when absent, remove when present.
    private func updateAgentHooks() {
        agentHooksMenu.removeAllItems()
        for application in agentApplications {
            let installed = status.readAgentHooks?(application).installed ?? nil
            let title: String
            switch installed {
            case .some(true): title = "Remove \(agentName(application)) Hooks"
            case .some(false): title = "Install \(agentName(application)) Hooks"
            case .none: title = "\(agentName(application)): configuration is not plain JSON"
            }
            let item = NSMenuItem(title: title, action: #selector(toggleAgentHooks(_:)),
                                  keyEquivalent: "")
            item.target = self
            item.tag = Int(application.rawValue)
            item.representedObject = installed == true
            item.isEnabled = installed != nil
            agentHooksMenu.addItem(item)
        }
        agentHooksMenu.addItem(.separator())
        let note = NSMenuItem(title: "Restart the AI app after a change", action: nil,
                              keyEquivalent: "")
        note.isEnabled = false
        agentHooksMenu.addItem(note)
    }

    @objc private func toggleAgentHooks(_ sender: NSMenuItem) {
        guard let application = AgentApplication(rawValue: UInt8(sender.tag)) else { return }
        let installed = sender.representedObject as? Bool ?? false
        guard let message = status.onSetAgentHooks?(application, !installed) else { return }
        NSApp.activate(ignoringOtherApps: true)
        let alert = NSAlert()
        alert.messageText = "Could not change \(agentName(application)) hooks"
        alert.informativeText = message
        alert.runModal()
    }

    private var stateTitle: String {
        switch status.connection {
        case .disconnected: return "Disconnected"
        case .connecting: return "Connecting…"
        case .connected: return "Connected"
        case .error: return "Connection error"
        }
    }

    private func updateItem(_ state: CompanionConnectionPresentationState) {
        guard let button = item.button else { return }
        let symbol = state == .error ? "exclamationmark.triangle" : "pc"
        let image = NSImage(systemSymbolName: symbol, accessibilityDescription: "Cardputer Companion")
        image?.isTemplate = true
        button.image = image
        button.appearsDisabled = state == .disconnected
        button.toolTip = "Cardputer Companion — \(stateTitle)"
    }
}

/// Leading/trailing inset shared by the custom rows so their text lines up
/// with standard menu item titles when the state column is hidden.
private let menuContentInset: CGFloat = 16

/// Bold title with the connection state, like the header of the system
/// Wi-Fi and Bluetooth menus.
private final class ConnectionHeaderView: NSView {
    private let title = NSTextField(labelWithString: "Cardputer")
    private let state = NSTextField(labelWithString: "")
    private let detail = NSTextField(labelWithString: "")

    init() {
        super.init(frame: NSRect(x: 0, y: 0, width: 280, height: 44))
        autoresizingMask = .width
        title.font = .boldSystemFont(ofSize: NSFont.systemFontSize)
        state.font = .menuFont(ofSize: 0)
        state.textColor = .secondaryLabelColor
        detail.font = .systemFont(ofSize: NSFont.smallSystemFontSize)
        detail.textColor = .secondaryLabelColor
        detail.lineBreakMode = .byTruncatingTail

        for view in [title, state, detail] {
            view.translatesAutoresizingMaskIntoConstraints = false
            addSubview(view)
        }
        NSLayoutConstraint.activate([
            title.leadingAnchor.constraint(equalTo: leadingAnchor, constant: menuContentInset),
            title.topAnchor.constraint(equalTo: topAnchor, constant: 5),
            state.trailingAnchor.constraint(equalTo: trailingAnchor, constant: -menuContentInset),
            state.firstBaselineAnchor.constraint(equalTo: title.firstBaselineAnchor),
            state.leadingAnchor.constraint(greaterThanOrEqualTo: title.trailingAnchor, constant: 12),
            detail.leadingAnchor.constraint(equalTo: title.leadingAnchor),
            detail.topAnchor.constraint(equalTo: title.bottomAnchor, constant: 1),
            detail.trailingAnchor.constraint(lessThanOrEqualTo: trailingAnchor, constant: -menuContentInset),
            detail.bottomAnchor.constraint(equalTo: bottomAnchor, constant: -6),
        ])
    }

    required init?(coder: NSCoder) { nil }

    func update(title stateTitle: String, detail detailText: String) {
        state.stringValue = stateTitle
        detail.stringValue = detailText
    }
}

/// A menu row with a trailing system `NSSwitch`, like the Wi-Fi menu header.
private final class SwitchMenuRowView: NSView {
    let toggle = NSSwitch()

    init(title: String) {
        super.init(frame: NSRect(x: 0, y: 0, width: 280, height: 32))
        autoresizingMask = .width
        let label = NSTextField(labelWithString: title)
        label.font = .menuFont(ofSize: 0)
        toggle.setAccessibilityLabel(title)

        for view in [label, toggle] {
            view.translatesAutoresizingMaskIntoConstraints = false
            addSubview(view)
        }
        NSLayoutConstraint.activate([
            label.leadingAnchor.constraint(equalTo: leadingAnchor, constant: menuContentInset),
            label.centerYAnchor.constraint(equalTo: centerYAnchor),
            label.trailingAnchor.constraint(lessThanOrEqualTo: toggle.leadingAnchor, constant: -12),
            toggle.trailingAnchor.constraint(equalTo: trailingAnchor, constant: -menuContentInset),
            toggle.centerYAnchor.constraint(equalTo: centerYAnchor),
        ])
    }

    required init?(coder: NSCoder) { nil }
}
