import Combine
import Foundation

/// Edit state of the macOS Inventory window. The Cardputer's microSD card is
/// the only store: this model holds the record list and one draft for the live
/// session, and every save is a revision-checked put that the Cardputer
/// validates and commits.
public final class InventoryEditorModel: ObservableObject {
    public enum State: Equatable {
        case disconnected
        case idle
        case loading
        case clean
        case dirty
        case saving
        case saved
        /// The record advanced on the Cardputer since it was loaded.
        case conflict(current: UInt32)
        case error(String)
        case deleting
    }

    public enum ListState: Equatable {
        case idle, loading, loaded, failed(String)
    }

    @Published public private(set) var connected = false
    @Published public private(set) var state: State = .disconnected
    @Published public private(set) var listState: ListState = .idle
    @Published public private(set) var entries: [InventoryListEntry] = []
    @Published public private(set) var selection: InventoryId?
    @Published public private(set) var loaded: InventoryRecord?
    @Published public var name = "" { didSet { draftChanged() } }
    @Published public var description = "" { didSet { draftChanged() } }
    /// A selected record the Cardputer reports as damaged: it can only be deleted.
    @Published public private(set) var damagedSelection: InventoryId?

    private var client: InventoryClient?
    private var busy = false
    /// Set by a new session: the next listing re-reads the selected record.
    private var sessionStarted = false

    public init() {}

    // ---- Connection ---------------------------------------------------------------

    /// A new session always lists afresh and re-reads the selected record; an
    /// unsaved draft stays a draft and is saved again only when the user asks.
    public func setTransport(_ transport: CompanionRequesting?) {
        let wasConnected = connected
        client = transport.map(InventoryClient.init(transport:))
        connected = transport != nil
        if !connected {
            busy = false
            entries = []
            listState = .idle
            state = .disconnected
            return
        }
        if !wasConnected {
            state = loaded == nil ? .idle : (isDraftUnchanged ? .clean : .dirty)
            sessionStarted = true
            refresh()
        }
    }

    public func refresh() {
        guard let client, !busy else { return }
        busy = true
        listState = .loading
        client.list { [weak self] result in
            guard let self else { return }
            self.busy = false
            switch result {
            case .success(let entries):
                self.entries = entries
                self.listState = .loaded
                self.reconcileSelection(with: entries, reload: self.sessionStarted)
                self.sessionStarted = false
            case .failure(let error):
                self.entries = []
                self.listState = .failed(error.message)
                self.sessionStarted = true
            }
        }
    }

    // ---- Selection and editing ------------------------------------------------------

    public var hasUnsavedChanges: Bool { loaded != nil && !isDraftUnchanged }
    public var canEdit: Bool {
        connected && listState == .loaded && loaded != nil && !busy && state != .loading
    }

    /// Loads a record. The window confirms before discarding unsaved changes.
    public func select(_ id: InventoryId) {
        guard connected, listState == .loaded, !busy else { return }
        selection = id
        damagedSelection = nil
        if let entry = entries.first(where: { $0.id == id }), !entry.valid {
            loaded = nil
            damagedSelection = id
            state = .error("The record on the Cardputer is damaged and left unchanged")
            return
        }
        load(id)
    }

    /// Discards the draft and fetches the authoritative record.
    public func reload() {
        guard listState == .loaded, let selection else { return }
        load(selection)
    }

    /// Returns the draft to the loaded record without contacting the Cardputer.
    public func discardChanges() {
        guard let loaded else { return }
        name = loaded.name
        description = loaded.description
    }

    // ---- Validation ---------------------------------------------------------------

    /// The draft as it would be saved: normalized name and description.
    public var draft: InventoryRecord? {
        guard let loaded else { return nil }
        return InventoryRecord(id: loaded.id, name: InventoryText.normalize(name),
                               description: InventoryText.normalizeDescription(description),
                               revision: loaded.revision)
    }

    public var nameLength: Int { InventoryText.length(InventoryText.normalize(name)) }
    public var descriptionLength: Int {
        InventoryText.length(InventoryText.normalizeDescription(description))
    }
    public var encodedSize: Int { draft?.encodedSize ?? 0 }

    public var problems: [String] {
        guard let draft else { return [] }
        var found: [String] = []
        for problem in draft.problems {
            switch problem {
            case .name:
                found.append("Name needs 1–\(InventoryLimits.maxNameLength) characters")
            case .description:
                found.append("Description is longer than \(InventoryLimits.maxDescriptionLength) characters")
            case .tooLarge:
                found.append("The record is larger than \(InventoryLimits.maxRecordBytes) bytes")
            case .revision:
                found.append("Reload the record")
            }
        }
        return found
    }

    public var canSave: Bool {
        guard connected, listState == .loaded, !busy, let draft, draft.problems.isEmpty else {
            return false
        }
        switch state {
        case .dirty, .error: return hasUnsavedChanges
        default: return false
        }
    }

    // ---- Saving -----------------------------------------------------------------

    public func save() {
        guard canSave, let client, let draft else { return }
        busy = true
        state = .saving
        client.put(draft) { [weak self] result in
            guard let self else { return }
            self.busy = false
            switch result {
            case .success(let revision):
                var saved = draft
                saved.revision = revision
                self.loaded = saved
                self.name = saved.name
                self.description = saved.description
                self.state = .saved
                if let index = self.entries.firstIndex(where: { $0.id == saved.id }) {
                    self.entries[index] = InventoryListEntry(id: saved.id, valid: true,
                                                             revision: revision, name: saved.name)
                }
            case .failure(.conflict(let current)):
                self.state = .conflict(current: current)
            case .failure(.timeout):
                // The commit may have happened: only a reload tells.
                self.state = .error("No answer — reload to see what the Cardputer saved")
            case .failure(let error):
                self.state = self.connected ? .error(error.message) : .disconnected
            }
        }
    }

    // ---- Deleting ---------------------------------------------------------------

    /// The selected record, or a damaged one, can be deleted from the Cardputer.
    public var canDelete: Bool {
        guard connected, listState == .loaded, !busy else { return false }
        if damagedSelection != nil { return true }
        guard loaded != nil else { return false }
        switch state {
        case .clean, .dirty, .saved, .error: return true
        default: return false
        }
    }

    /// Deletes the selected record for good (the window confirms first). The
    /// Cardputer refuses when the record changed since it was loaded.
    public func delete() {
        guard canDelete, let client, let id = damagedSelection ?? loaded?.id else { return }
        let revision = damagedSelection != nil ? 0 : loaded?.revision ?? 0
        busy = true
        state = .deleting
        client.delete(id, revision: revision) { [weak self] result in
            guard let self else { return }
            self.busy = false
            switch result {
            case .success, .failure(.notFound):
                self.entries.removeAll { $0.id == id }
                self.selection = nil
                self.damagedSelection = nil
                self.loaded = nil
                self.name = ""
                self.description = ""
                self.state = .idle
            case .failure(.conflict(let current)):
                self.state = .conflict(current: current)
            case .failure(let error):
                self.state = self.connected ? .error(error.message) : .disconnected
            }
        }
    }

    // ---- Private --------------------------------------------------------------------

    private var isDraftUnchanged: Bool {
        guard let loaded, let draft else { return true }
        return draft.name == loaded.name && draft.description == loaded.description
    }

    private func load(_ id: InventoryId) {
        guard let client, !busy else { return }
        busy = true
        state = .loading
        client.get(id) { [weak self] result in
            guard let self else { return }
            self.busy = false
            guard self.selection == id else { return }
            switch result {
            case .success(let record):
                self.loaded = record
                self.name = record.name
                self.description = record.description
                self.state = .clean
            case .failure(.rejected):
                self.loaded = nil
                self.state = .error("The record on the Cardputer is damaged and left unchanged")
            case .failure(let error):
                self.loaded = nil
                self.state = self.connected ? .error(error.message) : .disconnected
            }
        }
    }

    /// The selected record must exist in the current listing; a clean one is
    /// re-read when the session is new or its revision moved, and a draft based
    /// on an older revision becomes a conflict.
    private func reconcileSelection(with entries: [InventoryListEntry], reload: Bool) {
        guard let selection else { return }
        guard let entry = entries.first(where: { $0.id == selection }), entry.valid else {
            let damaged = entries.contains { $0.id == selection }
            self.selection = damaged ? selection : nil
            damagedSelection = damaged ? selection : nil
            loaded = nil
            name = ""
            description = ""
            state = .error(damaged ? "The record on the Cardputer is damaged and left unchanged"
                                   : "This record is no longer on the Cardputer")
            return
        }
        damagedSelection = nil
        if hasUnsavedChanges {
            if entry.revision != loaded?.revision { state = .conflict(current: entry.revision) }
        } else if reload || entry.revision != loaded?.revision {
            load(selection)
        }
    }

    private func draftChanged() {
        guard loaded != nil else { return }
        switch state {
        case .clean, .dirty, .saved, .error:
            state = isDraftUnchanged ? (state == .saved ? .saved : .clean) : .dirty
        default:
            break
        }
    }
}
