import AppKit
import CompanionCore
import SwiftUI

/// The Inventory window: lists the records on the connected Cardputer's microSD
/// card and edits one name and item list at a time. It keeps no copy of the
/// records after the session ends.
final class InventoryWindowController: NSObject, NSWindowDelegate {
    private let model: InventoryEditorModel
    private var window: NSWindow?

    init(model: InventoryEditorModel) {
        self.model = model
        super.init()
    }

    func show() {
        if window == nil {
            let window = NSWindow(
                contentRect: NSRect(x: 0, y: 0, width: 720, height: 480),
                styleMask: [.titled, .closable, .miniaturizable, .resizable],
                backing: .buffered, defer: false)
            window.title = "Cardputer Inventory"
            window.contentMinSize = NSSize(width: 560, height: 360)
            window.isReleasedWhenClosed = false
            window.delegate = self
            window.contentViewController = NSHostingController(
                rootView: InventoryView(model: model, windowState: InventoryViewState()))
            window.center()
            self.window = window
        }
        NSApp.activate(ignoringOtherApps: true)
        window?.makeKeyAndOrderFront(nil)
        model.refresh()
    }

    func close() {
        window?.delegate = nil
        window?.close()
        window = nil
    }

    func windowShouldClose(_ sender: NSWindow) -> Bool {
        guard model.hasUnsavedChanges else { return true }
        let alert = NSAlert()
        alert.messageText = "Discard unsaved changes?"
        alert.informativeText = "The edits have not been saved to the Cardputer."
        alert.addButton(withTitle: "Discard")
        alert.addButton(withTitle: "Cancel")
        guard alert.runModal() == .alertFirstButtonReturn else { return false }
        model.discardChanges()
        return true
    }
}

/// Window-only state, kept out of the edit model.
final class InventoryViewState: ObservableObject {
    @Published var pendingSelection: InventoryId?
    @Published var confirmingDelete = false
}

struct InventoryView: View {
    @ObservedObject var model: InventoryEditorModel
    @ObservedObject var windowState: InventoryViewState

    var body: some View {
        HSplitView {
            sidebar
                .frame(minWidth: 180, idealWidth: 220, maxWidth: 320)
            editor
                .frame(minWidth: 360)
        }
        .alert("Discard unsaved changes?", isPresented: Binding(
            get: { windowState.pendingSelection != nil }, set: { if !$0 { windowState.pendingSelection = nil } })) {
            Button("Discard", role: .destructive) {
                if let id = windowState.pendingSelection { model.select(id) }
                windowState.pendingSelection = nil
            }
            Button("Cancel", role: .cancel) { windowState.pendingSelection = nil }
        } message: {
            Text("The edits have not been saved to the Cardputer.")
        }
        .alert("Delete this container?", isPresented: $windowState.confirmingDelete) {
            Button("Delete", role: .destructive) { model.delete() }
            Button("Cancel", role: .cancel) {}
        } message: {
            Text("Its record is removed from the Cardputer’s microSD card for good. " +
                 "Erase the tag on the Cardputer to reuse it.")
        }
    }

    private var sidebar: some View {
        VStack(spacing: 0) {
            List(selection: Binding(get: { model.selection }, set: { request($0) })) {
                ForEach(model.entries, id: \.id) { entry in
                    Text(entry.valid ? entry.name : "Damaged record")
                        .foregroundColor(entry.valid ? .primary : .secondary)
                        .tag(Optional(entry.id))
                }
            }
            Divider()
            HStack {
                Text(listCaption)
                    .font(.caption)
                    .foregroundColor(.secondary)
                    .lineLimit(2)
                Spacer()
                Button {
                    model.refresh()
                } label: {
                    Image(systemName: "arrow.clockwise")
                }
                .buttonStyle(.borderless)
                .disabled(!model.connected)
                .help("Reload the list from the Cardputer")
            }
            .padding(8)
        }
    }

    @ViewBuilder private var editor: some View {
        if !model.connected {
            placeholder("Connect the Cardputer to edit its inventory",
                        detail: "Records are stored on the Cardputer’s microSD card.")
        } else if case .failed(let message) = model.listState {
            placeholder(message, detail: "Reload the list after the Cardputer is ready.")
        } else if model.listState == .loading {
            placeholder("Loading…", detail: nil)
        } else if model.loaded == nil {
            if model.damagedSelection != nil {
                VStack(spacing: 10) {
                    placeholder("The record on the Cardputer is damaged",
                                detail: "It is left unchanged. Delete it to remove the file.")
                    Button("Delete…") { windowState.confirmingDelete = true }
                        .disabled(!model.canDelete)
                }
                .padding(.bottom, 20)
            } else if case .error(let message) = model.state {
                placeholder(message, detail: nil)
            } else if model.state == .loading {
                placeholder("Loading…", detail: nil)
            } else {
                placeholder("Select a container", detail: "Register tags on the Cardputer first.")
            }
        } else {
            form
        }
    }

    private var form: some View {
        VStack(alignment: .leading, spacing: 10) {
            HStack {
                TextField("Name", text: $model.name)
                    .textFieldStyle(.roundedBorder)
                    .font(.title3)
                Text("\(model.nameLength)/\(InventoryLimits.maxNameLength)")
                    .font(.caption.monospacedDigit())
                    .foregroundColor(model.nameLength > InventoryLimits.maxNameLength ? .red : .secondary)
            }
            Text("Description")
                .font(.caption)
                .foregroundColor(.secondary)
            TextEditor(text: $model.description)
                .font(.body)
                .frame(minHeight: 160)
                .overlay(RoundedRectangle(cornerRadius: 4).stroke(Color.secondary.opacity(0.3)))
            HStack {
                Text("What is inside — one thing per line or comma-separated")
                    .font(.caption)
                    .foregroundColor(.secondary)
                Spacer()
                Text("\(model.descriptionLength)/\(InventoryLimits.maxDescriptionLength)")
                    .font(.caption.monospacedDigit())
                    .foregroundColor(model.descriptionLength > InventoryLimits.maxDescriptionLength
                                     ? .red : .secondary)
            }
            ForEach(model.problems, id: \.self) { problem in
                Text(problem).font(.caption).foregroundColor(.red)
            }
            Divider()
            HStack {
                Text(stateText)
                    .font(.callout)
                    .foregroundColor(stateColor)
                Spacer()
                Button("Delete…") { windowState.confirmingDelete = true }
                    .disabled(!model.canDelete)
                Button("Reload") { model.reload() }
                    .disabled(!model.connected || model.state == .saving || model.state == .loading)
                Button("Save") { model.save() }
                    .keyboardShortcut("s", modifiers: .command)
                    .disabled(!model.canSave)
            }
        }
        .padding(14)
        .disabled(!model.canEdit)
    }

    private func placeholder(_ title: String, detail: String?) -> some View {
        VStack(spacing: 6) {
            Text(title).font(.headline)
            if let detail { Text(detail).font(.callout).foregroundColor(.secondary) }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .padding()
    }

    private func request(_ id: InventoryId?) {
        guard let id, id != model.selection else { return }
        if model.hasUnsavedChanges {
            windowState.pendingSelection = id
        } else {
            model.select(id)
        }
    }

    private var listCaption: String {
        switch model.listState {
        case .idle: return model.connected ? "" : "Disconnected"
        case .loading: return "Loading…"
        case .loaded: return model.entries.count == 1 ? "1 container" : "\(model.entries.count) containers"
        case .failed(let message): return message
        }
    }

    private var stateText: String {
        switch model.state {
        case .disconnected: return "Disconnected"
        case .idle: return ""
        case .loading: return "Loading…"
        case .clean: return "Up to date"
        case .dirty: return "Unsaved changes"
        case .saving: return "Saving…"
        case .saved: return "Saved"
        case .conflict(let current):
            return "Changed on the Cardputer (revision \(current)) — reload to continue"
        case .error(let message): return message
        case .deleting: return "Deleting…"
        }
    }

    private var stateColor: Color {
        switch model.state {
        case .conflict, .error: return .red
        case .saved: return .green
        default: return .secondary
        }
    }
}
