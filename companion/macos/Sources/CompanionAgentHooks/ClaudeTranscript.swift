import Foundation

/// Claude Code sends no hook when the user stops a run or denies a permission
/// request; it only appends an interruption marker to the session transcript.
/// This reads the transcript tail for that marker alone. Nothing else in the
/// transcript is kept, logged or forwarded.
public enum ClaudeTranscript {
    /// The most read from the end of a transcript.
    public static let tailBytes = 128 * 1024

    private static let markers = ["[Request interrupted by user]",
                                  "[Request interrupted by user for tool use]"]

    /// When the session's last main-conversation message is an interruption
    /// marker, that marker's time; otherwise nil.
    public static func interruptedAt(path: String) -> Date? {
        guard let attributes = try? FileManager.default.attributesOfItem(atPath: path),
              attributes[.type] as? FileAttributeType == .typeRegular,
              let size = (attributes[.size] as? NSNumber)?.uint64Value,
              let handle = FileHandle(forReadingAtPath: path) else { return nil }
        defer { try? handle.close() }
        let offset = size > UInt64(tailBytes) ? size - UInt64(tailBytes) : 0
        guard (try? handle.seek(toOffset: offset)) != nil,
              let tail = try? handle.read(upToCount: tailBytes) else { return nil }
        var lines = tail.split(separator: UInt8(ascii: "\n"), omittingEmptySubsequences: true)
        // The first line is cut unless the whole file was read.
        if offset > 0, !lines.isEmpty { lines.removeFirst() }
        for line in lines.reversed() {
            // A torn line that Claude is still writing is skipped.
            guard let entry = (try? JSONSerialization.jsonObject(with: Data(line))) as? [String: Any],
                  let type = entry["type"] as? String, type == "user" || type == "assistant",
                  entry["isSidechain"] as? Bool != true, entry["isMeta"] as? Bool != true
            else { continue }
            guard type == "user", isMarker(entry["message"]),
                  let stamp = entry["timestamp"] as? String else { return nil }
            return date(stamp)
        }
        return nil
    }

    private static func isMarker(_ message: Any?) -> Bool {
        switch (message as? [String: Any])?["content"] {
        case let text as String:
            return markers.contains(text)
        case let blocks as [[String: Any]]:
            return blocks.contains { $0["type"] as? String == "text" &&
                markers.contains($0["text"] as? String ?? "") }
        default:
            return false
        }
    }

    private static func date(_ stamp: String) -> Date? {
        let formatter = ISO8601DateFormatter()
        formatter.formatOptions = [.withInternetDateTime, .withFractionalSeconds]
        if let date = formatter.date(from: stamp) { return date }
        formatter.formatOptions = [.withInternetDateTime]
        return formatter.date(from: stamp)
    }
}
