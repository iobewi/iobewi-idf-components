#!/usr/bin/env python3
"""Script pour intégrer le module AUDIO"""
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
        print("Usage: refactor_patch_audio.py <path/to/app_hls_player.c>", file=sys.stderr)
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
    text = ensure_include(text, '#include "app_hls_player_audio.h"')
    print("Include added")

    # Remove function
    print("\n=== Removing legacy function ===")
    old_len = len(text)
    text = remove_function_with_comment(text, "audio_play_task")
    new_len = len(text)
    if new_len < old_len:
        print(f"✓ Removed: audio_play_task ({old_len - new_len} chars)")
    else:
        print(f"⚠ Not found: audio_play_task")

    # Replace calls
    print("\n=== Replacing function calls ===")
    count = len(re.findall(r"\baudio_play_task\b", text))
    if count > 0:
        text = replace_calls(text, [("audio_play_task", "hls_audio_play_task")])
        print(f"✓ Replaced {count} calls: audio_play_task → hls_audio_play_task")

    # Write back
    p.write_text(text, encoding="utf-8")
    print(f"\n=== Patched: {p} ===")

    # Stats
    orig_lines = len(orig.splitlines())
    new_lines = len(text.splitlines())
    print(f"\nStats: {orig_lines} lines → {new_lines} lines (Δ{new_lines - orig_lines:+d})")

    # Sanity checks
    print("\n=== Sanity checks ===")
    if "app_hls_player_audio.h" in text:
        print("✓ AUDIO header included")
    else:
        print("✗ MISSING: AUDIO header")

    if "hls_audio_play_task" in text:
        print("✓ AUDIO task function called")
    else:
        print("✗ MISSING: AUDIO task function call")

    if "static void audio_play_task" in text:
        print("✗ LEGACY STILL PRESENT: audio_play_task")
    else:
        print("✓ No legacy audio_play_task")

    print("\n✓✓✓ ÉTAPE 3 (AUDIO) complète! ✓✓✓")

if __name__ == "__main__":
    main()
