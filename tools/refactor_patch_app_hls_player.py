#!/usr/bin/env python3
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

def remove_block_by_markers(text: str, start_pat: str, end_pat: str, label: str) -> str:
    """
    Remove from first match of start_pat to end_pat inclusive.
    """
    s = re.search(start_pat, text, flags=re.DOTALL | re.MULTILINE)
    if not s:
        die(f"Start marker not found for {label}: {start_pat}")
    e = re.search(end_pat, text[s.end():], flags=re.DOTALL | re.MULTILINE)
    if not e:
        die(f"End marker not found for {label}: {end_pat}")
    start_idx = s.start()
    end_idx = s.end() + e.end()
    return text[:start_idx] + "\n\n" + text[end_idx:]

def remove_function(text: str, func_name: str) -> str:
    """
    Remove a C function by name, assumes it starts at beginning of a line with optional static/inline,
    and ends at a standalone closing brace '}' followed by optional whitespace and newline.
    Works well for typical formatting.
    """
    # Match function signature line through end brace.
    # This is heuristic but good for consistent IDF style.
    pat = rf"^[ \t]*(?:static[ \t]+)?(?:inline[ \t]+)?[^\n;{{}}]*\b{re.escape(func_name)}\s*\([^;]*\)\s*\{{.*?^\}}\s*\n"
    new, n = re.subn(pat, "", text, flags=re.DOTALL | re.MULTILINE)
    if n == 0:
        die(f"Function not found or pattern mismatch: {func_name}")
    return new

def ensure_include(text: str, include_line: str, after_pat: str = r'#include\s+"app_hls_player/app_hls_player\.h"') -> str:
    if include_line in text:
        return text
    m = re.search(after_pat, text)
    if not m:
        # fallback: add after first include
        m2 = re.search(r'^#include[^\n]*\n', text, flags=re.MULTILINE)
        if not m2:
            die("No include found to anchor insertion")
        idx = m2.end()
        return text[:idx] + include_line + "\n" + text[idx:]
    idx = m.end()
    return text[:idx] + "\n" + include_line + text[idx:]

def replace_calls(text: str, repls: list[tuple[str, str]]) -> str:
    for a, b in repls:
        text = re.sub(rf"\b{re.escape(a)}\b", b, text)
    return text

def main():
    if len(sys.argv) != 2:
        print("Usage: refactor_patch_app_hls_player.py <path/to/app_hls_player.c>", file=sys.stderr)
        sys.exit(2)

    p = Path(sys.argv[1])
    if not p.exists():
        die(f"File not found: {p}")

    orig = p.read_text(encoding="utf-8", errors="strict")
    backup = backup_file(p)
    print(f"Backup created: {backup}")

    text = orig

    # ---- Includes ----
    text = ensure_include(text, '#include "app_hls_player_internal.h"')
    text = ensure_include(text, '#include "app_hls_player_ts_sync.h"')
    text = ensure_include(text, '#include "app_hls_player_http.h"')

    # ---- Remove legacy NOTIF/CONFIG fallback defines if present ----
    # Remove from "// Task Notification bits" to the last #endif of the CONFIG block
    # Pattern: match the comment through the CONFIG defines block
    notif_start = r"^// Task Notification bits.*?$"
    notif_end = r"^#endif\s*$"

    # Try to find and remove NOTIF defines block
    try:
        # Find the block starting with "// Task Notification bits"
        notif_match = re.search(r"^// Task Notification bits.*?\n(?:#define NOTIF_\w+.*?\n)+", text, re.MULTILINE)
        if notif_match:
            text = text[:notif_match.start()] + text[notif_match.end():]
            print("Removed legacy NOTIF define block")
    except Exception as e:
        print(f"Note: Could not remove NOTIF block: {e}")

    # Try to remove CONFIG fallback defines block
    try:
        config_match = re.search(
            r"^// Tailles de buffers depuis Kconfig.*?\n(?:^#ifndef CONFIG_.*?\n^.*?\n^#endif\n)+",
            text,
            re.MULTILINE
        )
        if config_match:
            text = text[:config_match.start()] + text[config_match.end():]
            print("Removed legacy CONFIG fallback define block")
    except Exception as e:
        print(f"Note: Could not remove CONFIG block: {e}")

    # Remove struct definition (now in internal.h)
    try:
        struct_match = re.search(
            r"^/\*\*\s*\n\s*\*\s*@brief Structure interne du player HLS.*?\n\s*\*/\s*\nstruct app_hls_player_s\s*\{.*?^\};\s*\n",
            text,
            re.DOTALL | re.MULTILINE
        )
        if struct_match:
            text = text[:struct_match.start()] + text[struct_match.end():]
            print("Removed legacy struct app_hls_player_s definition")
    except Exception as e:
        print(f"Note: Could not remove struct: {e}")

    # Remove hls_rate_limited_resync (now in internal.h)
    try:
        text = remove_function(text, "hls_rate_limited_resync")
        print("Removed legacy function: hls_rate_limited_resync")
    except SystemExit:
        print("Skip: legacy function not found: hls_rate_limited_resync")

    # Remove HTTP_BUFFER_SIZE define and comment (now in http.c)
    try:
        http_buf_match = re.search(
            r"^// \[RAM OPT P0\.2\] Buffer pour HTTP download.*?\n#define HTTP_BUFFER_SIZE.*?\n",
            text,
            re.MULTILINE
        )
        if http_buf_match:
            text = text[:http_buf_match.start()] + text[http_buf_match.end():]
            print("Removed HTTP_BUFFER_SIZE define")
    except Exception as e:
        print(f"Note: Could not remove HTTP_BUFFER_SIZE: {e}")

    # ---- Remove legacy TS_SYNC functions ----
    for fn in ["ts_resync_smart", "ts_find_next_sync"]:
        try:
            # Also remove the Doxygen comment before the function
            dox_pat = rf"^/\*\*.*?\*/\s*\nstatic.*?\b{re.escape(fn)}\s*\([^;]*\)\s*\{{.*?^\}}\s*\n"
            new, n = re.subn(dox_pat, "\n", text, flags=re.DOTALL | re.MULTILINE)
            if n > 0:
                text = new
                print(f"Removed legacy function: {fn}")
            else:
                # Fallback without Doxygen
                text = remove_function(text, fn)
                print(f"Removed legacy function: {fn}")
        except SystemExit:
            print(f"Skip: legacy function not found: {fn}")

    # ---- Remove legacy HTTP functions ----
    for fn in ["get_location_header", "http_event_handler", "download_segment", "download_m3u8"]:
        try:
            # Also remove the Doxygen comment before the function
            dox_pat = rf"^/\*\*.*?\*/\s*\nstatic.*?\b{re.escape(fn)}\s*\([^;]*\)\s*\{{.*?^\}}\s*\n"
            new, n = re.subn(dox_pat, "\n", text, flags=re.DOTALL | re.MULTILINE)
            if n > 0:
                text = new
                print(f"Removed legacy function: {fn}")
            else:
                # Fallback without Doxygen
                text = remove_function(text, fn)
                print(f"Removed legacy function: {fn}")
        except SystemExit:
            print(f"Skip: legacy function not found: {fn}")

    # ---- Replace call sites ----
    text = replace_calls(text, [
        ("download_segment", "hls_http_download_segment"),
        ("download_m3u8", "hls_http_download_m3u8"),
        ("ts_resync_smart", "hls_ts_resync_smart"),
        ("ts_find_next_sync", "hls_ts_find_next_sync"),
    ])
    print("Replaced function calls to use new module functions")

    # ---- Write back ----
    p.write_text(text, encoding="utf-8")
    print(f"Patched: {p}")

    # Quick sanity checks
    must_have = ["app_hls_player_http.h", "hls_http_download_m3u8", "hls_http_download_segment"]
    for s in must_have:
        if s not in text:
            print(f"WARNING: expected token missing after patch: {s}", file=sys.stderr)

    # Print stats
    orig_lines = len(orig.splitlines())
    new_lines = len(text.splitlines())
    print(f"\nStats: {orig_lines} lines → {new_lines} lines (Δ{new_lines - orig_lines:+d})")

if __name__ == "__main__":
    main()
