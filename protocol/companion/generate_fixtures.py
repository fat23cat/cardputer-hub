#!/usr/bin/env python3
"""Write committed Companion protocol v1–v6 fixtures. Run from the repository root."""

from __future__ import annotations

from pathlib import Path

HELLO, HELLO_ACK, REQUEST, RESPONSE, EVENT = 1, 2, 3, 4, 5
PING, CAPABILITIES, APP_ACTIVE, APP_ACTIVATE, APP_ACTIVE_CHANGED = 1, 2, 3, 4, 5
SYSTEM_METRICS = 6
AI_USAGE = 7
SYSTEM_DETAILS = 8
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
        "hello-v3.bin": envelope(HELLO, 0, 0, 0, 0, bytes([3, 3, 2, 1])),
        "hello-ack-v3.bin": envelope(HELLO_ACK, SESSION, 0, 0, 0, bytes([3])),
        "capabilities-response-v3.bin": envelope(
            RESPONSE, SESSION, 2, CAPABILITIES, 0, bytes([5, 1, 2, 3, 4, 5]), 3
        ),
        "ai-usage-request-v3.bin": envelope(REQUEST, SESSION, 7, AI_USAGE, 0, b"", 3),
        "ai-usage-response-v3.bin": envelope(
            RESPONSE, SESSION, 7, AI_USAGE, 0,
            bytes([1, 2, 1]) + (9).to_bytes(4, "little") +
            bytes([1, 1, 1, 1, 1, 1]) +
            (37).to_bytes(4, "little") + (100).to_bytes(4, "little") +
            (63).to_bytes(4, "little") + bytes([63]) +
            (1780000000).to_bytes(4, "little") + (3600).to_bytes(4, "little"), 3
        ),
        "hello-v4.bin": envelope(HELLO, 0, 0, 0, 0, bytes([4, 4, 3, 2, 1])),
        "hello-ack-v4.bin": envelope(HELLO_ACK, SESSION, 0, 0, 0, bytes([4])),
        "capabilities-response-v4.bin": envelope(
            RESPONSE, SESSION, 2, CAPABILITIES, 0, bytes([5, 1, 2, 3, 4, 5]), 4
        ),
        "ai-usage-response-v4.bin": envelope(
            RESPONSE, SESSION, 7, AI_USAGE, 0,
            bytes([2, 2, 1]) + (9).to_bytes(4, "little") +
            bytes([1, 1, 1, 1, 1, 1]) +
            (37).to_bytes(4, "little") + (100).to_bytes(4, "little") +
            (63).to_bytes(4, "little") + bytes([63]) +
            (1780000000).to_bytes(4, "little") + (3600).to_bytes(4, "little") +
            bytes([1, 2, 1, 10]) + b"Full reset" +
            (1790000000).to_bytes(4, "little") + (86400).to_bytes(4, "little"), 4
        ),
        "hello-v5.bin": envelope(HELLO, 0, 0, 0, 0, bytes([4, 5, 4, 3, 2])),
        "hello-ack-v5.bin": envelope(HELLO_ACK, SESSION, 0, 0, 0, bytes([5])),
        "ai-usage-response-v5.bin": envelope(
            RESPONSE, SESSION, 7, AI_USAGE, 0,
            bytes([3, 2, 1]) + (9).to_bytes(4, "little") +
            bytes([3, 4, 1, 1, 1, 1]) +
            (8).to_bytes(4, "little") + (100).to_bytes(4, "little") +
            (92).to_bytes(4, "little") + bytes([92]) +
            (1780000000).to_bytes(4, "little") + (3600).to_bytes(4, "little") +
            bytes([0]), 5
        ),
        "hello-v6.bin": envelope(HELLO, 0, 0, 0, 0, bytes([4, 6, 5, 4, 3])),
        "hello-ack-v6.bin": envelope(HELLO_ACK, SESSION, 0, 0, 0, bytes([6])),
        "capabilities-response-v6.bin": envelope(
            RESPONSE, SESSION, 2, CAPABILITIES, 0, bytes([6, 1, 2, 3, 4, 5, 6]), 6
        ),
        "system-metrics-response-v6.bin": envelope(
            RESPONSE, SESSION, 5, SYSTEM_METRICS, 0,
            bytes([2, 0xFF, 0x01, 34]) + (11500).to_bytes(4, "little") +
            (16384).to_bytes(4, "little") + bytes([1, 63, 82, 2]) +
            (12698).to_bytes(4, "little") + (1843).to_bytes(4, "little") +
            bytes([2]) + (102).to_bytes(2, "little"), 6
        ),
        "ai-usage-response-v6.bin": envelope(
            RESPONSE, SESSION, 7, AI_USAGE, 0,
            bytes([3, 2, 1]) + (9).to_bytes(4, "little") +
            bytes([3, 4, 1, 1, 1, 1]) +
            (8).to_bytes(4, "little") + (100).to_bytes(4, "little") +
            (92).to_bytes(4, "little") + bytes([92]) +
            (1780000000).to_bytes(4, "little") + (3600).to_bytes(4, "little") +
            bytes([0]), 6
        ),
        "system-details-request-v6.bin": envelope(
            REQUEST, SESSION, 8, SYSTEM_DETAILS, 0, bytes([1]), 6
        ),
        "system-details-response-cpu-v6.bin": envelope(
            RESPONSE, SESSION, 8, SYSTEM_DETAILS, 0,
            bytes([1, 1, 0x1F, 0, 61, 18, 27]) + (310).to_bytes(2, "little") + bytes([2]) +
            bytes([38, 5]) + b"Xcode" + bytes([21, 13]) + b"Google Chrome", 6
        ),
        "system-details-response-power-v6.bin": envelope(
            RESPONSE, SESSION, 8, SYSTEM_DETAILS, 0,
            bytes([1, 2, 0x1F, 0]) + (142).to_bytes(2, "little") + bytes([96, 91]) +
            (214).to_bytes(2, "little") + bytes([12, 11]) + b"Magic Mouse", 6
        ),
        "system-details-response-network-v6.bin": envelope(
            RESPONSE, SESSION, 8, SYSTEM_DETAILS, 0,
            bytes([1, 3, 0x1F, 0]) + (18).to_bytes(2, "little") + (3).to_bytes(2, "little") +
            bytes([(-54) & 0xFF]) + (866).to_bytes(2, "little") + bytes([1]), 6
        ),
        "system-details-response-memory-v6.bin": envelope(
            RESPONSE, SESSION, 8, SYSTEM_DETAILS, 0,
            bytes([1, 4, 0x0F, 0]) + (14438).to_bytes(4, "little") +
            (3994).to_bytes(4, "little") + (3482).to_bytes(4, "little") +
            (1229).to_bytes(4, "little") + (212).to_bytes(2, "little") +
            (994).to_bytes(2, "little") + (348160).to_bytes(4, "little") +
            (59392).to_bytes(4, "little"), 6
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
