#!/usr/bin/env python3
"""Script pour intégrer le module FETCHER"""
import re
import sys
from pathlib import Path
from datetime import datetime

def backup_file(p: Path) -> Path:
    ts = datetime.now().strftime("%Y%m%d_%H%M%S")
    b = p.with_suffix(p.suffix + f".bak_{ts}")
    b.write_bytes(p.read_bytes())
    return b

def find_function_bounds(text: str, func_name: str) -> tuple[int, int] | None:
    pat = rf"(?:^|\n)([ \t]*(?:static[ \t]+)?(?:inline[ \t]+)?[^\n;{{}}]*\b{re.escape(func_name)}\s*\([^;]*\)\s*\{{)"
    m = re.search(pat, text, re.MULTILINE)
    if not m:
        return None

    start = m.start() if m.group(0)[0] == '\n' else m.start()
    pos = m.end()
    brace_count = 1

    while pos < len(text) and brace_count > 0:
        if text[pos] == '{':
            brace_count += 1
        elif text[pos] == '}':
            brace_count -= 1
        pos += 1

    if brace_count != 0:
        return None

    while pos < len(text) and text[pos] in ' \t\n':
        if text[pos] == '\n':
            pos += 1
            break
        pos += 1

    return (start, pos)

def remove_function_with_comment(text: str, func_name: str) -> str:
    dox_pat = rf"(^/\*\*[^*]*\*+(?:[^/*][^*]*\*+)*/\s*\n)(?=[ \t]*(?:static[ \t]+)?(?:inline[ \t]+)?[^\n;{{}}]*\b{re.escape(func_name)}\s*\()"
    dox_match = re.search(dox_pat, text, re.MULTILINE)

    if dox_match:
        comment_start = dox_match.start()
        bounds = find_function_bounds(text, func_name)
        if bounds:
            _, func_end = bounds
            return text[:comment_start] + text[func_end:]
    else:
        bounds = find_function_bounds(text, func_name)
        if bounds:
            start, end = bounds
            return text[:start] + text[end:]

    return text

def ensure_include(text: str, include_line: str) -> str:
    if include_line in text:
        return text
    m = re.search(r'^#include[^\n]*\n', text, re.MULTILINE)
    if not m:
        lines = text.split('\n')
        for i, line in enumerate(lines):
            if line.strip() and not line.strip().startswith('/*') and not line.strip().startswith('*') and not line.strip().startswith('//'):
                return '\n'.join(lines[:i] + [include_line] + lines[i:])
        return include_line + '\n' + text
    idx = m.end()
    return text[:idx] + include_line + '\n' + text[idx:]

def replace_calls(text: str, repls: list[tuple[str, str]]) -> str:
    for old, new in repls:
        text = re.sub(rf"\b{re.escape(old)}\b", new, text)
    return text

def main():
    if len(sys.argv) != 2:
        print("Usage: refactor_patch_fetcher.py <path/to/app_hls_player.c>", file=sys.stderr)
        sys.exit(2)

    p = Path(sys.argv[1])
    if not p.exists():
        print(f"ERROR: File not found: {p}", file=sys.stderr)
        sys.exit(1)

    orig = p.read_text(encoding="utf-8", errors="strict")
    backup = backup_file(p)
    print(f"Backup created: {backup}")

    text = orig

    # Add include
    print("\n=== Adding include ===")
    text = ensure_include(text, '#include "app_hls_player_fetcher.h"')
    print("Include added")

    # Remove functions (helpers now in internal.h, fetch_task in module)
    print("\n=== Removing legacy functions ===")
    functions = ["interruptible_delay_ms", "hls_should_stop_now", "hls_fetch_task"]
    for fn in functions:
        old_len = len(text)
        text = remove_function_with_comment(text, fn)
        new_len = len(text)
        if new_len < old_len:
            print(f"✓ Removed: {fn} ({old_len - new_len} chars)")
        else:
            print(f"⚠ Not found: {fn}")

    # Replace calls
    print("\n=== Replacing function calls ===")
    replacements = [
        ("interruptible_delay_ms", "hls_interruptible_delay_ms"),
    ]

    for old, new in replacements:
        count = len(re.findall(rf"\b{re.escape(old)}\b", text))
        if count > 0:
            text = replace_calls(text, [(old, new)])
            print(f"✓ Replaced {count} calls: {old} → {new}")

    # Write back
    p.write_text(text, encoding="utf-8")
    print(f"\n=== Patched: {p} ===")

    # Stats
    orig_lines = len(orig.splitlines())
    new_lines = len(text.splitlines())
    print(f"\nStats: {orig_lines} lines → {new_lines} lines (Δ{new_lines - orig_lines:+d})")

    # Sanity checks
    print("\n=== Sanity checks ===")
    if "app_hls_player_fetcher.h" in text:
        print("✓ FETCHER header included")
    else:
        print("✗ MISSING: FETCHER header")

    if "static void hls_fetch_task" in text or "static bool interruptible_delay_ms" in text:
        print("✗ LEGACY STILL PRESENT: fetch helpers")
    else:
        print("✓ No legacy fetch functions")

    if "hls_interruptible_delay_ms" in text:
        print("✓ Helper calls updated")
    else:
        print("⚠ Note: hls_interruptible_delay_ms not found (might be unused)")

    print("\n✓✓✓ ÉTAPE 4 (FETCHER) complète! ✓✓✓")

if __name__ == "__main__":
    main()
