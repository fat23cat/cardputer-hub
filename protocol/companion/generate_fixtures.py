#!/usr/bin/env python3
"""Write the Companion protocol fixtures and the protocol fingerprint.

Run from the repository root. Firmware and Companion are always built from the
same commit, so there is one wire format and no version negotiation. The
fingerprint is the first 8 bytes of SHA-256 over this file: every payload
layout below is defined here, so any wire change changes it. It is written into
a C++ header and a Swift file; HELLO carries it and both sides refuse a peer
built from a different protocol definition.

`--root DIR` writes every output under DIR instead of the repository, for the
consistency test.
"""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path

MARKER = 0xC7
HELLO, HELLO_ACK, REQUEST, RESPONSE, EVENT = 1, 2, 3, 4, 5
PING, APP_ACTIVE, APP_ACTIVATE, APP_ACTIVE_CHANGED = 1, 3, 4, 5
SYSTEM_METRICS, AI_USAGE, SYSTEM_DETAILS = 6, 7, 8
OK, NOT_AVAILABLE, NOT_FOUND, UNSUPPORTED, MALFORMED = 0, 1, 2, 3, 4
SESSION = 42
COMPANION_BUILD = b"2026-09-29 abc1234"
FIRMWARE_BUILD = b"2026-09-29 abc1234"
OTHER_FINGERPRINT = bytes([0xFF, 0xEE, 0xDD, 0xCC, 0xBB, 0xAA, 0x99, 0x88])


def fingerprint() -> bytes:
    return hashlib.sha256(Path(__file__).read_bytes()).digest()[:8]


def envelope(kind: int, session: int, request_id: int, operation: int, status: int,
             payload: bytes, marker: int = MARKER) -> bytes:
    if len(payload) > 248:
        raise ValueError("payload too large")
    return bytes(
        [marker, kind, session & 0xFF, (session >> 8) & 0xFF, request_id, operation, status,
         len(payload)]
    ) + payload


def u16(value: int) -> bytes:
    return value.to_bytes(2, "little")


def u32(value: int) -> bytes:
    return value.to_bytes(4, "little")


def bundle(identifier: str) -> bytes:
    encoded = identifier.encode("utf-8")
    return bytes([len(encoded)]) + encoded


def hello_payload(protocol: bytes, build: bytes) -> bytes:
    return protocol + bytes([len(build)]) + build


def as_c_array(data: bytes) -> str:
    return ", ".join(f"0x{byte:02X}" for byte in data)


def fixtures(protocol: bytes) -> dict[str, bytes]:
    token = bytes([0x01, 0x02, 0x03, 0x04])
    return {
        # Session start: fingerprint (8), build id length (1), build id (ASCII).
        "hello.bin": envelope(HELLO, 0, 0, 0, OK, hello_payload(protocol, COMPANION_BUILD)),
        "hello-ack.bin": envelope(HELLO_ACK, SESSION, 0, 0, OK,
                                  hello_payload(protocol, FIRMWARE_BUILD)),
        # A different fingerprint: no session, status UNSUPPORTED.
        "hello-ack-mismatch.bin": envelope(HELLO_ACK, 0, 0, 0, UNSUPPORTED,
                                           hello_payload(OTHER_FINGERPRINT, FIRMWARE_BUILD)),
        "ping-request.bin": envelope(REQUEST, SESSION, 1, PING, OK, token),
        "ping-response.bin": envelope(RESPONSE, SESSION, 1, PING, OK, token),
        "app-active-request.bin": envelope(REQUEST, SESSION, 3, APP_ACTIVE, OK, b""),
        "app-active-response.bin": envelope(RESPONSE, SESSION, 3, APP_ACTIVE, OK,
                                            bundle("dev.zed.Zed")),
        "app-activate-request.bin": envelope(REQUEST, SESSION, 4, APP_ACTIVATE, OK,
                                             bundle("org.telegram.desktop")),
        "app-activate-response.bin": envelope(RESPONSE, SESSION, 4, APP_ACTIVATE, OK, b""),
        "app-active-changed-event.bin": envelope(EVENT, SESSION, 0, APP_ACTIVE_CHANGED, OK,
                                                 bundle("dev.zed.Zed")),
        "system-metrics-request.bin": envelope(REQUEST, SESSION, 5, SYSTEM_METRICS, OK, b""),
        # validity, CPU %, RAM used/total MiB, pressure, disk %, battery %, thermal,
        # download/upload KiB/s, power source, battery minutes.
        "system-metrics-response.bin": envelope(
            RESPONSE, SESSION, 5, SYSTEM_METRICS, OK,
            u16(0x1FF) + bytes([34]) + u32(11500) + u32(16384) + bytes([1, 63, 82, 2]) +
            u32(12698) + u32(1843) + bytes([2]) + u16(102)
        ),
        "ai-usage-request.bin": envelope(REQUEST, SESSION, 7, AI_USAGE, OK, b""),
        # state, provider count, generation; provider, plan, freshness, metric count;
        # metric kind, unit, used, limit, remaining, remaining %, reset epoch, reset
        # seconds; reset-credit flag.
        "ai-usage-response.bin": envelope(
            RESPONSE, SESSION, 7, AI_USAGE, OK,
            bytes([2, 1]) + u32(9) + bytes([3, 4, 1, 1, 1, 1]) + u32(8) + u32(100) +
            u32(92) + bytes([92]) + u32(1780000000) + u32(3600) + bytes([0])
        ),
        "system-details-request.bin": envelope(REQUEST, SESSION, 8, SYSTEM_DETAILS, OK,
                                               bytes([1])),
        # group, validity, then the group body.
        "system-details-response-cpu.bin": envelope(
            RESPONSE, SESSION, 8, SYSTEM_DETAILS, OK,
            bytes([1]) + u16(0x0F) + bytes([61, 18, 27, 2]) +
            bytes([38, 5]) + b"Xcode" + bytes([21, 13]) + b"Google Chrome"
        ),
        "system-details-response-power.bin": envelope(
            RESPONSE, SESSION, 8, SYSTEM_DETAILS, OK,
            bytes([2]) + u16(0x1F) + u16(142) + bytes([96, 91]) + u16(214) +
            bytes([12, 11]) + b"Magic Mouse"
        ),
        "system-details-response-network.bin": envelope(
            RESPONSE, SESSION, 8, SYSTEM_DETAILS, OK,
            bytes([3]) + u16(0x0F) + u16(18) + u16(3) + bytes([(-54) & 0xFF]) + u16(866)
        ),
        "system-details-response-memory.bin": envelope(
            RESPONSE, SESSION, 8, SYSTEM_DETAILS, OK,
            bytes([4]) + u16(0x0F) + u32(14438) + u32(3994) + u32(3482) + u32(1229) +
            u16(212) + u16(994) + u32(348160) + u32(59392)
        ),
        # A HELLO from a Companion built before plan 043 (v1-framed version list).
        "legacy-hello.bin": envelope(HELLO, 0, 0, 0, OK, bytes([4, 6, 5, 4, 3]), marker=1),
        "malformed-length.bin": bytes([MARKER, REQUEST, SESSION, 0, 1, PING, 0, 10, 1, 2]),
        "unknown-marker.bin": envelope(REQUEST, SESSION, 1, PING, OK, token, marker=0x42),
        "wrong-session.bin": envelope(RESPONSE, 99, 1, PING, OK, token),
        "unknown-operation.bin": envelope(REQUEST, SESSION, 1, 0x7F, OK, b""),
    }


def fixtures_header(files: dict[str, bytes]) -> str:
    lines = [
        "#pragma once",
        "#include <cstddef>",
        "#include <cstdint>",
        "namespace cardputer_hub::companion_fixtures {",
        "struct Fixture { const char* name; const std::uint8_t* bytes; std::size_t size; };",
    ]
    table = []
    for name, payload in files.items():
        ident = name.replace("-", "_").replace(".bin", "")
        lines.append(f"inline constexpr std::uint8_t {ident}[] = {{ {as_c_array(payload)} }};")
        table.append(f'    {{"{name}", {ident}, sizeof({ident})}}')
    lines.append("inline constexpr Fixture all[] = {")
    lines.extend(f"{row}," for row in table)
    lines += [
        "};",
        "inline const Fixture* find(const char* name) {",
        "    for (const auto& fixture : all) {",
        "        const char* left = fixture.name;",
        "        const char* right = name;",
        "        while (*left != '\\0' && *left == *right) { ++left; ++right; }",
        "        if (*left == '\\0' && *right == '\\0') return &fixture;",
        "    }",
        "    return nullptr;",
        "}",
        "} // namespace cardputer_hub::companion_fixtures",
        "",
    ]
    return "\n".join(lines)


def fingerprint_header(protocol: bytes) -> str:
    return "\n".join([
        "// Generated by protocol/companion/generate_fixtures.py; do not edit.",
        "// clang-format off",
        "#pragma once",
        "",
        "#include <array>",
        "#include <cstdint>",
        "",
        "namespace cardputer_hub::connectivity {",
        "",
        "// SHA-256 of the protocol generator, first 8 bytes. HELLO carries it; a peer",
        "// with a different value was built from a different protocol definition.",
        "inline constexpr std::array<std::uint8_t, 8> companionProtocolFingerprint{",
        f"    {as_c_array(protocol)}}};",
        "",
        "} // namespace cardputer_hub::connectivity",
        "",
    ])


def fingerprint_swift(protocol: bytes) -> str:
    return "\n".join([
        "// Generated by protocol/companion/generate_fixtures.py; do not edit.",
        "",
        "/// SHA-256 of the protocol generator, first 8 bytes. HELLO carries it; a peer",
        "/// with a different value was built from a different protocol definition.",
        "public enum ProtocolFingerprint {",
        f"    public static let bytes: [UInt8] = [{as_c_array(protocol)}]",
        "}",
        "",
    ])


def outputs() -> dict[str, bytes]:
    protocol = fingerprint()
    files = fixtures(protocol)
    result = {f"protocol/companion/fixtures/{name}": data for name, data in files.items()}
    result["protocol/companion/companion_fixtures.h"] = fixtures_header(files).encode()
    result["src/connectivity/companion/companion_fingerprint.h"] = (
        fingerprint_header(protocol).encode()
    )
    result["companion/macos/Sources/CompanionCore/ProtocolFingerprint.swift"] = (
        fingerprint_swift(protocol).encode()
    )
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    root = parser.parse_args().root
    generated = outputs()
    fixture_dir = root / "protocol/companion/fixtures"
    fixture_dir.mkdir(parents=True, exist_ok=True)
    for stale in fixture_dir.glob("*.bin"):
        if f"protocol/companion/fixtures/{stale.name}" not in generated:
            stale.unlink()
    for relative, data in generated.items():
        target = root / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)


if __name__ == "__main__":
    main()
