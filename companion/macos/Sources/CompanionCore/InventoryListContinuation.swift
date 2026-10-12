import Foundation

/// Enter in a description continues a list the way Apple Notes does: on a
/// "- item", "• item" or "1. item" line it starts the next item, and on an empty
/// item it removes the marker and ends the list. The WEB ACCESS page applies the
/// same rule (`continueList` in web/index.html). Ranges are UTF-16, as in
/// NSTextView selections.
public enum InventoryListContinuation {
    public struct Edit: Equatable {
        public let range: NSRange
        public let replacement: String
        /// The caret after the replacement.
        public let caret: Int
    }

    private static let marker = try! NSRegularExpression(
        pattern: "^([ \\t]*)(?:([-–—•*])|(\\d{1,3})([.)])(?=[ \\t]|$))([ \\t]*)")

    /// The edit for Enter at `selection`, or nil for an ordinary newline.
    public static func newline(in text: String, selection: NSRange) -> Edit? {
        let string = text as NSString
        guard selection.length == 0, selection.location <= string.length else { return nil }
        let caret = selection.location
        let line = string.lineRange(for: NSRange(location: caret, length: 0))
        var lineEnd = NSMaxRange(line)
        if lineEnd > line.location, string.character(at: lineEnd - 1) == 0x0A { lineEnd -= 1 }
        let lineText = string.substring(with: NSRange(location: line.location, length: lineEnd - line.location))
        let lineString = lineText as NSString
        guard let match = marker.firstMatch(in: lineText, range: NSRange(location: 0, length: lineString.length)),
              caret >= line.location + match.range.length
        else { return nil }
        let content = lineString.substring(from: match.range.length)
        if content.trimmingCharacters(in: .whitespaces).isEmpty {
            return Edit(range: NSRange(location: line.location, length: lineEnd - line.location),
                        replacement: "", caret: line.location)
        }
        func group(_ index: Int) -> String? {
            let range = match.range(at: index)
            return range.location == NSNotFound ? nil : lineString.substring(with: range)
        }
        let next: String
        if let bullet = group(2) {
            next = bullet
        } else if let number = group(3).flatMap(Int.init), let delimiter = group(4) {
            next = String(number + 1) + delimiter
        } else {
            return nil
        }
        let replacement = "\n" + (group(1) ?? "") + next + (group(5) ?? "")
        return Edit(range: selection, replacement: replacement,
                    caret: caret + (replacement as NSString).length)
    }
}
