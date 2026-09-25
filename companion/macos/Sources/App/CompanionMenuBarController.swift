import AppKit
import Combine
import CompanionCore
import SwiftUI

final class CompanionMenuBarController: NSObject, NSPopoverDelegate {
    private let status: CompanionStatusStore
    private let item: NSStatusItem
    private let popover = NSPopover()
    private var connectionObservation: AnyCancellable?
    private var layoutObservation: AnyCancellable?
    private var clock: Timer?
    private var escapeMonitor: Any?

    init(status: CompanionStatusStore) {
        self.status = status
        item = NSStatusBar.system.statusItem(withLength: NSStatusItem.squareLength)
        super.init()
        popover.behavior = .transient
        popover.contentSize = NSSize(width: CompanionPopoverLayout.width, height: 228)
        popover.contentViewController = NSHostingController(rootView: CompanionPopoverView(status: status))
        popover.delegate = self
        if let button = item.button {
            button.target = self
            button.action = #selector(togglePopover)
            button.imagePosition = .imageOnly
        }
        connectionObservation = status.$connection.removeDuplicates().sink { [weak self] state in
            self?.updateItem(state)
        }
        layoutObservation = status.$page.combineLatest(status.$connection, status.$startAtLoginError)
            .sink { [weak self] layout in
                self?.updatePopoverSize(page: layout.0, connection: layout.1, loginError: layout.2)
            }
    }

    @objc private func togglePopover() {
        if popover.isShown {
            popover.performClose(nil)
            return
        }
        guard let button = item.button else { return }
        status.page = .main
        status.refreshLogin()
        status.tick()
        popover.show(relativeTo: button.bounds, of: button, preferredEdge: .minY)
        NSApp.activate(ignoringOtherApps: true)
        popover.contentViewController?.view.window?.makeKey()
        clock = Timer.scheduledTimer(withTimeInterval: 1, repeats: true) { [weak self] _ in
            self?.status.tick()
        }
        escapeMonitor = NSEvent.addLocalMonitorForEvents(matching: .keyDown) { [weak self] event in
            guard let self, self.popover.isShown, event.keyCode == 53 else { return event }
            if self.status.page == .main {
                self.popover.performClose(nil)
            } else {
                self.status.page = .main
            }
            return nil
        }
    }

    func popoverDidClose(_ notification: Notification) {
        stopVisibleUpdates()
        status.page = .main
    }

    private func stopVisibleUpdates() {
        clock?.invalidate()
        clock = nil
        if let escapeMonitor {
            NSEvent.removeMonitor(escapeMonitor)
            self.escapeMonitor = nil
        }
    }

    func stop() {
        popover.performClose(nil)
        stopVisibleUpdates()
        connectionObservation = nil
        layoutObservation = nil
        NSStatusBar.system.removeStatusItem(item)
    }

    private func updatePopoverSize(page: CompanionPopoverPage,
                                   connection: CompanionConnectionPresentationState, loginError: Bool) {
        popover.contentSize = NSSize(
            width: CompanionPopoverLayout.width,
            height: CompanionPopoverLayout.height(page: page, connection: connection, loginError: loginError))
    }

    private func updateItem(_ state: CompanionConnectionPresentationState) {
        guard let button = item.button else { return }
        let symbol = state == .error ? "exclamationmark.triangle" : "keyboard"
        let image = NSImage(systemSymbolName: symbol, accessibilityDescription: "Cardputer Companion")
        image?.isTemplate = true
        button.image = image
        button.alphaValue = state == .disconnected ? 0.62 : 1
        let stateName: String
        switch state {
        case .disconnected: stateName = "Disconnected"
        case .connecting: stateName = "Connecting"
        case .connected: stateName = "Connected"
        case .error: stateName = "Connection error"
        }
        button.toolTip = "Cardputer Companion — \(stateName)"
    }
}
