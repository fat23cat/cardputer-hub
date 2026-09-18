import AppKit
import Foundation

public enum ActivateResult: Equatable {
    case ok
    case notFound
}

public protocol ApplicationControlling: AnyObject {
    func activeApplication() -> String?
    func activate(bundleIdentifier: String) -> ActivateResult
    func observeActiveApplication(_ handler: @escaping (String?) -> Void)
}

public final class WorkspaceApplicationController: ApplicationControlling {
    private var observer: NSObjectProtocol?

    public init() {}

    deinit {
        if let observer {
            NotificationCenter.default.removeObserver(observer)
        }
    }

    public func activeApplication() -> String? {
        NSWorkspace.shared.frontmostApplication?.bundleIdentifier
    }

    public func activate(bundleIdentifier: String) -> ActivateResult {
        let running = NSRunningApplication.runningApplications(withBundleIdentifier: bundleIdentifier)
        if let app = running.first {
            if #available(macOS 14.0, *) {
                app.activate()
            } else {
                app.activate(options: [.activateIgnoringOtherApps])
            }
            return .ok
        }
        guard let url = NSWorkspace.shared.urlForApplication(withBundleIdentifier: bundleIdentifier) else {
            return .notFound
        }
        NSWorkspace.shared.openApplication(at: url, configuration: NSWorkspace.OpenConfiguration())
        return .ok
    }

    public func observeActiveApplication(_ handler: @escaping (String?) -> Void) {
        observer = NSWorkspace.shared.notificationCenter.addObserver(
            forName: NSWorkspace.didActivateApplicationNotification,
            object: nil,
            queue: .main
        ) { _ in
            handler(NSWorkspace.shared.frontmostApplication?.bundleIdentifier)
        }
    }
}
