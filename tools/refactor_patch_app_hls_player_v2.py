#!/usr/bin/env python3
"""
Refactoring script pour app_hls_player.c
Approche par marqueurs précis pour éviter les sur-suppressions
"""
import re
import sys
from pathlib import Path
from datetime import datetime

def die(msg: str):
    print(f"ERROR: {msg}", file=sys.stderr)
    sys.exit(1)

def backup_file(p: Path) -> Path:
    ts = datetime.now().strftime("%Y%m%d_%H%M%S")
    b = p.with_suffix(p.suffix + f".bak_{ts}")
    b.write_bytes(p.read_bytes())
    return b

def find_function_bounds(text: str, func_name: str) -> tuple[int, int] | None:
    """
    Find start and end positions of a function by name.
    Returns (start_pos, end_pos) or None if not found.

    Strategy:
    1. Find function declaration (static ... func_name(...) {)
    2. Count braces to find matching closing brace
    """
    # Find function declaration
    pat = rf"(?:^|\n)([ \t]*(?:static[ \t]+)?(?:inline[ \t]+)?[^\n;{{}}]*\b{re.escape(func_name)}\s*\([^;]*\)\s*\{{)"
    m = re.search(pat, text, re.MULTILINE)
    if not m:
        return None

    start = m.start() if m.group(0)[0] == '\n' else m.start()
    pos = m.end()
    brace_count = 1  # We're after the opening brace

    # Count braces to find the matching closing brace
    while pos < len(text) and brace_count > 0:
        if text[pos] == '{':
            brace_count += 1
        elif text[pos] == '}':
            brace_count -= 1
        pos += 1

    if brace_count != 0:
        print(f"Warning: Unmatched braces for {func_name}")
        return None

    # Include trailing newline if present
    while pos < len(text) and text[pos] in ' \t\n':
        if text[pos] == '\n':
            pos += 1
            break
        pos += 1

    return (start, pos)

def remove_function_with_comment(text: str, func_name: str) -> str:
    """
    Remove a function including its preceding Doxygen comment if present.
    """
    # First, try to find Doxygen comment before the function
    # Look for /** ... */ followed by the function
    dox_pat = rf"(^/\*\*[^*]*\*+(?:[^/*][^*]*\*+)*/\s*\n)(?=[ \t]*(?:static[ \t]+)?(?:inline[ \t]+)?[^\n;{{}}]*\b{re.escape(func_name)}\s*\()"
    dox_match = re.search(dox_pat, text, re.MULTILINE)

    if dox_match:
        # Remove comment + function
        comment_start = dox_match.start()
        bounds = find_function_bounds(text, func_name)
        if bounds:
            _, func_end = bounds
            return text[:comment_start] + text[func_end:]
    else:
        # No comment, just remove function
        bounds = find_function_bounds(text, func_name)
        if bounds:
            start, end = bounds
            return text[:start] + text[end:]

    return text  # Function not found, return unchanged

def ensure_include(text: str, include_line: str) -> str:
    """Add include if not already present, after the first #include line."""
    if include_line in text:
        return text

    # Find first #include
    m = re.search(r'^#include[^\n]*\n', text, re.MULTILINE)
    if not m:
        # No includes found, add at top after initial comments
        lines = text.split('\n')
        for i, line in enumerate(lines):
            if line.strip() and not line.strip().startswith('/*') and not line.strip().startswith('*') and not line.strip().startswith('//'):
                return '\n'.join(lines[:i] + [include_line] + lines[i:])
        return include_line + '\n' + text

    # Add after first include
    idx = m.end()
    return text[:idx] + include_line + '\n' + text[idx:]

def replace_calls(text: str, repls: list[tuple[str, str]]) -> str:
    for old, new in repls:
        text = re.sub(rf"\b{re.escape(old)}\b", new, text)
    return text

def remove_block_between(text: str, start_marker: str, end_marker: str, label: str) -> str:
    """Remove text between two markers (inclusive)."""
    start_idx = text.find(start_marker)
    if start_idx == -1:
        print(f"Note: Start marker not found for {label}: {start_marker[:50]}...")
        return text

    end_idx = text.find(end_marker, start_idx + len(start_marker))
    if end_idx == -1:
        print(f"Note: End marker not found for {label}: {end_marker[:50]}...")
        return text

    # Include the end marker
    end_idx += len(end_marker)
    return text[:start_idx] + text[end_idx:]

def main():
    if len(sys.argv) != 2:
        print("Usage: refactor_patch_app_hls_player_v2.py <path/to/app_hls_player.c>", file=sys.stderr)
        sys.exit(2)

    p = Path(sys.argv[1])
    if not p.exists():
        die(f"File not found: {p}")

    orig = p.read_text(encoding="utf-8", errors="strict")
    backup = backup_file(p)
    print(f"Backup created: {backup}")

    text = orig

    # ---- Step 1: Add includes ----
    print("\n=== Adding includes ===")
    text = ensure_include(text, '#include "app_hls_player_internal.h"')
    text = ensure_include(text, '#include "app_hls_player_ts_sync.h"')
    text = ensure_include(text, '#include "app_hls_player_http.h"')
    print("Includes added")

    # ---- Step 2: Remove defines and struct (now in internal.h) ----
    print("\n=== Removing legacy defines and struct ===")

    # Remove NOTIF defines
    text = remove_block_between(
        text,
        "// Task Notification bits",
        "#define NOTIF_RESYNC (1u << 2)",
        "NOTIF defines"
    )

    # Remove CONFIG fallback defines
    text = remove_block_between(
        text,
        "// Tailles de buffers depuis Kconfig",
        "#endif",
        "CONFIG defines (first block)"
    )
    # There are multiple #ifndef blocks, remove them one by one
    for config_name in ["CONFIG_APP_HLS_PLAYER_REM_BUFFER_SIZE", "CONFIG_APP_HLS_PLAYER_DEC_BUFFER_SIZE",
                        "CONFIG_APP_HLS_PLAYER_DROP_OLD_ON_FULL", "CONFIG_APP_HLS_PLAYER_GATHER_BUFFER_SIZE"]:
        text = remove_block_between(text, f"#ifndef {config_name}", "#endif", f"{config_name} block")

    # Remove struct definition
    text = remove_block_between(
        text,
        "/**\n * @brief Structure interne du player HLS\n */",
        "};",
        "struct app_hls_player_s"
    )

    # Remove HTTP_BUFFER_SIZE define
    text = remove_block_between(
        text,
        "// [RAM OPT P0.2] Buffer pour HTTP download",
        "#define HTTP_BUFFER_SIZE (4 * 1024)",
        "HTTP_BUFFER_SIZE"
    )

    # ---- Step 3: Remove functions ----
    print("\n=== Removing legacy functions ===")

    functions_to_remove = [
        "hls_rate_limited_resync",
        "get_location_header",
        "ts_resync_smart",
        "ts_find_next_sync",
        "http_event_handler",
        "download_segment",
        "download_m3u8"
    ]

    for func_name in functions_to_remove:
        old_len = len(text)
        text = remove_function_with_comment(text, func_name)
        new_len = len(text)
        if new_len < old_len:
            print(f"✓ Removed: {func_name} ({old_len - new_len} chars)")
        else:
            print(f"⚠ Not found: {func_name}")

    # ---- Step 4: Replace function calls ----
    print("\n=== Replacing function calls ===")
    replacements = [
        ("download_segment", "hls_http_download_segment"),
        ("download_m3u8", "hls_http_download_m3u8"),
        ("ts_resync_smart", "hls_ts_resync_smart"),
        ("ts_find_next_sync", "hls_ts_find_next_sync"),
    ]

    for old, new in replacements:
        count = len(re.findall(rf"\b{re.escape(old)}\b", text))
        if count > 0:
            text = replace_calls(text, [(old, new)])
            print(f"✓ Replaced {count} calls: {old} → {new}")

    # ---- Step 5: Write back ----
    p.write_text(text, encoding="utf-8")
    print(f"\n=== Patched: {p} ===")

    # Stats
    orig_lines = len(orig.splitlines())
    new_lines = len(text.splitlines())
    print(f"\nStats: {orig_lines} lines → {new_lines} lines (Δ{new_lines - orig_lines:+d})")

    # Sanity checks
    print("\n=== Sanity checks ===")
    checks = [
        ("app_hls_player_http.h", "HTTP header included"),
        ("app_hls_player_ts_sync.h", "TS_SYNC header included"),
        ("hls_http_download_m3u8", "M3U8 download function called"),
        ("hls_http_download_segment", "Segment download function called"),
        ("hls_ts_resync_smart", "TS resync function called"),
    ]

    all_ok = True
    for token, desc in checks:
        if token in text:
            print(f"✓ {desc}")
        else:
            print(f"✗ MISSING: {desc}")
            all_ok = False

    # Check that old functions are gone
    old_funcs = ["static esp_err_t http_event_handler", "static char* download_m3u8",
                 "static esp_err_t download_segment", "static bool ts_resync_smart"]
    for func_sig in old_funcs:
        if func_sig in text:
            print(f"✗ LEGACY STILL PRESENT: {func_sig}")
            all_ok = False

    if all_ok:
        print("\n✓✓✓ All checks passed! ✓✓✓")
    else:
        print("\n⚠⚠⚠ Some checks failed, review manually ⚠⚠⚠")
        sys.exit(1)

if __name__ == "__main__":
    main()
