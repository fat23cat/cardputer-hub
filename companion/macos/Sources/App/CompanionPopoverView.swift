import CompanionCore
import SwiftUI

enum CompanionPopoverLayout {
    static let width: CGFloat = 320
    static let inset: CGFloat = 16

    static func height(page: CompanionPopoverPage, connection: CompanionConnectionPresentationState,
                       loginError: Bool) -> CGFloat {
        switch page {
        case .main:
            return 228 + (connection == .connected ? 0 : -16) + (loginError ? 18 : 0)
        case .diagnostics:
            return connection == .connected ? 320 : 224
        case .about:
            return 168
        }
    }
}

struct CompanionPopoverView: View {
    @ObservedObject var status: CompanionStatusStore

    var body: some View {
        Group {
            switch status.page {
            case .main: mainPage
            case .diagnostics: diagnosticsPage
            case .about: aboutPage
            }
        }
        .frame(width: CompanionPopoverLayout.width - 2 * CompanionPopoverLayout.inset,
               height: contentHeight, alignment: .topLeading)
        .padding(CompanionPopoverLayout.inset)
    }

    private var contentHeight: CGFloat {
        CompanionPopoverLayout.height(page: status.page, connection: status.connection,
                                      loginError: status.startAtLoginError)
            - 2 * CompanionPopoverLayout.inset
    }

    private var mainPage: some View {
        VStack(alignment: .leading, spacing: 0) {
            Text("Cardputer Companion")
                .font(.headline.weight(.semibold))
                .padding(.bottom, 12)

            connectionStatus
                .padding(.bottom, 8)

            Button {
                status.onReconnect?()
            } label: {
                Label("Reconnect", systemImage: "arrow.clockwise")
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .contentShape(Rectangle())
            }
            .buttonStyle(.plain)
            .font(.subheadline)
            .disabled(status.connection == .connecting)
            .padding(.vertical, 4)
            .padding(.bottom, 4)

            VStack(alignment: .leading, spacing: 3) {
                HStack {
                    Text("Start at Login")
                        .font(.subheadline)
                    Spacer()
                    Toggle("Start at Login", isOn: Binding(
                        get: { status.startAtLogin },
                        set: { status.setStartAtLogin($0) }
                    ))
                    .labelsHidden()
                    .toggleStyle(.switch)
                }
                if status.startAtLoginError {
                    Label("Could not update login setting", systemImage: "exclamationmark.circle")
                        .font(.caption)
                        .foregroundStyle(.red)
                }
            }
            .padding(.bottom, 8)

            Divider()
                .padding(.bottom, 4)

            navigationRow("Diagnostics", symbol: "chevron.right") {
                status.page = .diagnostics
            }
            navigationRow("About", symbol: nil) {
                status.page = .about
            }

            Button("Quit") { status.onQuit?() }
                .buttonStyle(.plain)
                .font(.subheadline)
                .frame(maxWidth: .infinity, alignment: .leading)
                .contentShape(Rectangle())
                .padding(.top, 4)
                .padding(.vertical, 4)
        }
    }

    private var connectionStatus: some View {
        VStack(alignment: .leading, spacing: 3) {
            HStack(spacing: 9) {
                connectionIndicator
                    .frame(width: 10, height: 10)
                    .accessibilityHidden(true)
                Text("Cardputer")
                    .font(.subheadline.weight(.semibold))
                Spacer(minLength: 8)
                Text(stateTitle)
                    .font(.subheadline)
                    .foregroundStyle(.secondary)
                    .lineLimit(1)
            }
            if let metadata = status.statusMetadataText {
                Text(metadata)
                    .font(.caption)
                    .foregroundStyle(.secondary)
                    .padding(.leading, 19)
            }
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .accessibilityElement(children: .combine)
    }

    @ViewBuilder
    private var connectionIndicator: some View {
        switch status.connection {
        case .connected:
            Circle().fill(Color.green)
        case .connecting:
            Circle().fill(Color.accentColor)
        case .disconnected:
            Circle().strokeBorder(Color.secondary, lineWidth: 1.5)
        case .error:
            Image(systemName: "exclamationmark.circle.fill")
                .foregroundStyle(Color.red)
        }
    }

    private var diagnosticsPage: some View {
        VStack(alignment: .leading, spacing: 0) {
            backButton("Diagnostics")
                .padding(.bottom, 15)

            VStack(alignment: .leading, spacing: 7) {
                detailRow("Connection", stateTitle)
                if let version = status.protocolVersion {
                    detailRow("Protocol", "v\(version)")
                }
                if let duration = status.sessionDurationText {
                    detailRow("Session", duration)
                }
                if let lastSeen = status.lastSeenText {
                    detailRow("Last message", lastSeen)
                }
                detailRow("Bluetooth", status.bluetoothReady ? "Ready" : "Unavailable")
                if let sessionId = status.sessionId {
                    detailRow("Session ID", String(sessionId))
                }
            }
            .padding(.bottom, 14)

            Divider()
                .padding(.bottom, 11)
            Text("Capabilities")
                .font(.caption.weight(.semibold))
                .foregroundStyle(.secondary)
                .padding(.bottom, 9)
            VStack(alignment: .leading, spacing: 7) {
                capabilityRow("App Control", status.capabilities.appControl)
                capabilityRow("App Events", status.capabilities.appEvents)
                capabilityRow("System Metrics", status.capabilities.systemMetrics)
            }
        }
    }

    private var aboutPage: some View {
        VStack(alignment: .leading, spacing: 0) {
            backButton("About")
                .padding(.bottom, 18)
            Text("Cardputer Companion")
                .font(.subheadline.weight(.semibold))
                .padding(.bottom, 5)
            Text("Part of Cardputer Hub")
                .font(.subheadline)
                .foregroundStyle(.secondary)
                .padding(.bottom, 15)
            Divider()
                .padding(.bottom, 11)
            VStack(alignment: .leading, spacing: 7) {
                detailRow("Version", Bundle.main.infoDictionary?["CFBundleShortVersionString"] as? String ?? "—")
                detailRow("Build", Bundle.main.infoDictionary?["CFBundleVersion"] as? String ?? "—")
            }
        }
    }

    private func backButton(_ title: String) -> some View {
        Button {
            status.page = .main
        } label: {
            HStack(spacing: 7) {
                Image(systemName: "chevron.left")
                    .font(.caption.weight(.semibold))
                Text(title)
                    .font(.headline.weight(.semibold))
            }
            .frame(maxWidth: .infinity, alignment: .leading)
            .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
    }

    private func navigationRow(_ title: String, symbol: String?, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            HStack {
                Text(title)
                Spacer()
                if let symbol {
                    Image(systemName: symbol)
                        .font(.caption.weight(.semibold))
                        .foregroundStyle(.secondary)
                }
            }
            .frame(maxWidth: .infinity, alignment: .leading)
            .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .font(.subheadline)
        .padding(.vertical, 4)
    }

    private func detailRow(_ title: String, _ value: String) -> some View {
        HStack(spacing: 10) {
            Text(title)
                .foregroundStyle(.secondary)
            Spacer(minLength: 4)
            Text(value)
                .lineLimit(1)
        }
        .font(.subheadline)
    }

    private func capabilityRow(_ title: String, _ available: Bool) -> some View {
        detailRow(title, available ? "Available" : "Unavailable")
    }

    private var stateTitle: String {
        switch status.connection {
        case .disconnected: return "Disconnected"
        case .connecting: return "Connecting…"
        case .connected: return "Connected"
        case .error: return "Connection error"
        }
    }
}
