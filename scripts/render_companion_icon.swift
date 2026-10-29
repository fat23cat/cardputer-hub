// Renders the Cardputer Companion application icon into an .iconset directory:
// the Cardputer front (Ink body, Bone screen with a Blue status bar, keyboard
// with one Vermilion and one Leaf key) on a Bone tile, in the Hub palette from
// docs/UI_REQUIREMENTS.md. Each size is drawn directly, so small sizes stay
// sharp; scripts/package_macos_companion.sh turns the set into AppIcon.icns.
//
//   swift scripts/render_companion_icon.swift <AppIcon.iconset>

import CoreGraphics
import Foundation
import ImageIO

let bone = CGColor(srgbRed: 0xF4 / 255, green: 0xF2 / 255, blue: 0xEC / 255, alpha: 1)
let ink = CGColor(srgbRed: 0x17 / 255, green: 0x15 / 255, blue: 0x0F / 255, alpha: 1)
let blue = CGColor(srgbRed: 0x1B / 255, green: 0x4F / 255, blue: 0xD0 / 255, alpha: 1)
let vermilion = CGColor(srgbRed: 0xE2 / 255, green: 0x45 / 255, blue: 0x1E / 255, alpha: 1)
let leaf = CGColor(srgbRed: 0x4C / 255, green: 0xB9 / 255, blue: 0x49 / 255, alpha: 1)
let ordinal = CGColor(srgbRed: 0xAB / 255, green: 0xA8 / 255, blue: 0x9D / 255, alpha: 1)
let pale = CGColor(srgbRed: 0xDE / 255, green: 0xDB / 255, blue: 0xD1 / 255, alpha: 1)

/// Artwork in a 100-unit square with a top-left origin.
struct Shape {
    let x, y, width, height: CGFloat
    let color: CGColor
    var radius: CGFloat = 0
}

// Two key rows: four Bone keys left of the Vermilion key, then five Bone keys.
let keys: [Shape] = [(y: 54, count: 4), (y: 63, count: 5)].flatMap { row in
    (0..<row.count).map {
        Shape(x: 26 + CGFloat($0) * 10, y: row.y, width: 8, height: 6, color: bone)
    }
}
let artwork: [Shape] = [
    Shape(x: 0, y: 0, width: 100, height: 100, color: bone, radius: 22),
    Shape(x: 20, y: 16, width: 60, height: 68, color: ink, radius: 4),
    Shape(x: 26, y: 22, width: 48, height: 26, color: bone),
    Shape(x: 26, y: 22, width: 48, height: 6, color: blue),
    Shape(x: 30, y: 33, width: 24, height: 3, color: ink),
    Shape(x: 30, y: 39, width: 16, height: 3, color: ordinal),
] + keys + [
    Shape(x: 26, y: 72, width: 38, height: 6, color: bone),
    Shape(x: 66, y: 54, width: 8, height: 6, color: vermilion),
    Shape(x: 66, y: 72, width: 8, height: 6, color: leaf),
]

/// Draws the artwork into the macOS icon grid: an 824-pixel tile centred on a
/// 1024-pixel canvas, scaled to `pixels`.
func render(pixels: Int) -> CGImage {
    guard let context = CGContext(
        data: nil, width: pixels, height: pixels, bitsPerComponent: 8, bytesPerRow: 0,
        space: CGColorSpace(name: CGColorSpace.sRGB)!,
        bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)
    else { fatalError("Cannot create a \(pixels)px bitmap context") }
    let scale = CGFloat(pixels) / 1024
    context.translateBy(x: 0, y: CGFloat(pixels))
    context.scaleBy(x: scale, y: -scale)
    context.translateBy(x: 100, y: 100)
    context.scaleBy(x: 8.24, y: 8.24)
    for shape in artwork {
        let rect = CGRect(x: shape.x, y: shape.y, width: shape.width, height: shape.height)
        context.addPath(CGPath(
            roundedRect: rect, cornerWidth: shape.radius, cornerHeight: shape.radius,
            transform: nil))
        context.setFillColor(shape.color)
        context.fillPath()
    }
    // A thin Pale edge keeps the Bone tile visible on light Finder backgrounds.
    context.addPath(CGPath(
        roundedRect: CGRect(x: 0.3, y: 0.3, width: 99.4, height: 99.4),
        cornerWidth: 21.7, cornerHeight: 21.7, transform: nil))
    context.setStrokeColor(pale)
    context.setLineWidth(0.6)
    context.strokePath()
    return context.makeImage()!
}

guard CommandLine.arguments.count == 2 else {
    FileHandle.standardError.write("usage: render_companion_icon.swift <AppIcon.iconset>\n".data(using: .utf8)!)
    exit(2)
}
let iconset = URL(fileURLWithPath: CommandLine.arguments[1], isDirectory: true)
try FileManager.default.createDirectory(at: iconset, withIntermediateDirectories: true)
for points in [16, 32, 128, 256, 512] {
    for density in [1, 2] {
        let suffix = density == 2 ? "@2x" : ""
        let url = iconset.appendingPathComponent("icon_\(points)x\(points)\(suffix).png")
        guard let destination = CGImageDestinationCreateWithURL(
            url as CFURL, "public.png" as CFString, 1, nil)
        else { fatalError("Cannot write \(url.path)") }
        CGImageDestinationAddImage(destination, render(pixels: points * density), nil)
        guard CGImageDestinationFinalize(destination) else { fatalError("Cannot write \(url.path)") }
    }
}
