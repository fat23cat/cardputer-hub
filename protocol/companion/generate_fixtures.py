#!/usr/bin/env python3
"""Write committed Companion protocol v1/v2 fixtures. Run from the repository root."""

from __future__ import annotations

from pathlib import Path

HELLO, HELLO_ACK, REQUEST, RESPONSE, EVENT = 1, 2, 3, 4, 5
PING, CAPABILITIES, APP_ACTIVE, APP_ACTIVATE, APP_ACTIVE_CHANGED = 1, 2, 3, 4, 5
SYSTEM_METRICS = 6
SESSION = 42


def envelope(kind: int, session: int, request_id: int, operation: int, status: int,
             payload: bytes, version: int = 1) -> bytes:
    if len(payload) > 248:
        raise ValueError("payload too large")
    return bytes(
        [
            version,
            kind,
            session & 0xFF,
            (session >> 8) & 0xFF,
            request_id,
            operation,
            status,
            len(payload),
        ]
    ) + payload


def bundle(identifier: str) -> bytes:
    encoded = identifier.encode("utf-8")
    return bytes([len(encoded)]) + encoded


def as_c_array(data: bytes) -> str:
    return ", ".join(f"0x{byte:02X}" for byte in data)


def main() -> None:
    root = Path(__file__).resolve().parent / "fixtures"
    root.mkdir(parents=True, exist_ok=True)
    token = bytes([0x01, 0x02, 0x03, 0x04])
    files = {
        "hello-v1.bin": envelope(HELLO, 0, 0, 0, 0, bytes([1, 1])),
        "hello-ack-v1.bin": envelope(HELLO_ACK, SESSION, 0, 0, 0, bytes([1])),
        "ping-request-v1.bin": envelope(REQUEST, SESSION, 1, PING, 0, token),
        "ping-response-v1.bin": envelope(RESPONSE, SESSION, 1, PING, 0, token),
        "capabilities-request-v1.bin": envelope(REQUEST, SESSION, 2, CAPABILITIES, 0, b""),
        "capabilities-response-v1.bin": envelope(
            RESPONSE, SESSION, 2, CAPABILITIES, 0, bytes([3, 1, 2, 3])
        ),
        "app-active-request-v1.bin": envelope(REQUEST, SESSION, 3, APP_ACTIVE, 0, b""),
        "app-active-response-v1.bin": envelope(
            RESPONSE, SESSION, 3, APP_ACTIVE, 0, bundle("dev.zed.Zed")
        ),
        "app-activate-request-v1.bin": envelope(
            REQUEST, SESSION, 4, APP_ACTIVATE, 0, bundle("org.telegram.desktop")
        ),
        "app-activate-response-v1.bin": envelope(RESPONSE, SESSION, 4, APP_ACTIVATE, 0, b""),
        "app-active-changed-event-v1.bin": envelope(
            EVENT, SESSION, 0, APP_ACTIVE_CHANGED, 0, bundle("dev.zed.Zed")
        ),
        "hello-v2.bin": envelope(HELLO, 0, 0, 0, 0, bytes([2, 2, 1])),
        "hello-ack-v2.bin": envelope(HELLO_ACK, SESSION, 0, 0, 0, bytes([2])),
        "capabilities-response-v2.bin": envelope(
            RESPONSE, SESSION, 2, CAPABILITIES, 0, bytes([4, 1, 2, 3, 4]), 2
        ),
        "system-metrics-request-v2.bin": envelope(
            REQUEST, SESSION, 5, SYSTEM_METRICS, 0, b"", 2
        ),
        "system-metrics-response-v2.bin": envelope(
            RESPONSE, SESSION, 5, SYSTEM_METRICS, 0,
            bytes([1, 0x7f, 0, 34]) + (11500).to_bytes(4, "little") +
            (16384).to_bytes(4, "little") + bytes([1, 63, 82, 2]) +
            (12698).to_bytes(4, "little") + (1843).to_bytes(4, "little"), 2
        ),
        "malformed-length.bin": bytes([1, REQUEST, SESSION, 0, 1, PING, 0, 10, 0x01, 0x02]),
        "unsupported-version.bin": bytes([99, REQUEST, SESSION, 0, 1, PING, 0, 4]) + token,
        "wrong-session.bin": envelope(RESPONSE, 99, 1, PING, 0, token),
        "unknown-operation.bin": envelope(REQUEST, SESSION, 1, 0x7F, 0, b""),
    }
    header_lines = [
        "#pragma once",
        "#include <cstddef>",
        "#include <cstdint>",
        "namespace cardputer_hub::companion_fixtures {",
        "struct Fixture { const char* name; const std::uint8_t* bytes; std::size_t size; };",
    ]
    table = []
    for name, payload in files.items():
        (root / name).write_bytes(payload)
        ident = name.replace("-", "_").replace(".bin", "")
        header_lines.append(
            f"inline constexpr std::uint8_t {ident}[] = {{ {as_c_array(payload)} }};"
        )
        table.append(f'    {{"{name}", {ident}, sizeof({ident})}}')
    header_lines.append("inline constexpr Fixture all[] = {")
    header_lines.extend(f"{row}," for row in table)
    header_lines.append("};")
    header_lines.append("inline const Fixture* find(const char* name) {")
    header_lines.append("    for (const auto& fixture : all) {")
    header_lines.append("        const char* left = fixture.name;")
    header_lines.append("        const char* right = name;")
    header_lines.append("        while (*left != '\\0' && *left == *right) { ++left; ++right; }")
    header_lines.append("        if (*left == '\\0' && *right == '\\0') return &fixture;")
    header_lines.append("    }")
    header_lines.append("    return nullptr;")
    header_lines.append("}")
    header_lines.append("} // namespace cardputer_hub::companion_fixtures")
    header_lines.append("")
    (Path(__file__).resolve().parent / "companion_fixtures.h").write_text(
        "\n".join(header_lines), encoding="utf-8"
    )


if __name__ == "__main__":
    main()
