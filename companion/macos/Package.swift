// swift-tools-version: 5.9
import PackageDescription

let package = Package(
    name: "CardputerCompanion",
    platforms: [.macOS(.v13)],
    products: [
        .executable(name: "CardputerCompanion", targets: ["CardputerCompanion"]),
        .executable(name: "CompanionCoreCheck", targets: ["CompanionCoreCheck"]),
        .library(name: "CompanionCore", targets: ["CompanionCore"]),
    ],
    targets: [
        .target(
            name: "CompanionCore",
            path: "Sources/CompanionCore",
            linkerSettings: [
                .linkedFramework("AppKit"),
            ]
        ),
        .executableTarget(
            name: "CardputerCompanion",
            dependencies: ["CompanionCore"],
            path: "Sources/App",
            exclude: ["Info.plist"],
            linkerSettings: [
                .linkedFramework("AppKit"),
                .linkedFramework("CoreBluetooth"),
                .linkedFramework("IOKit"),
                .linkedFramework("ServiceManagement"),
                .linkedFramework("SystemConfiguration"),
            ]
        ),
        .executableTarget(
            name: "CompanionCoreCheck",
            dependencies: ["CompanionCore"],
            path: "Sources/CompanionCoreCheck"
        ),
    ]
)
