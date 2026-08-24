#!/usr/bin/env python3
"""
M6.5 D5: verify the event catalog is mirrored between the C++ source of
truth (stack/common/include/common/events.h) and the Web LMT mirror
(lmt/src/events.ts).

* Every ev::<NAME> referenced in C++ sources must exist in events.h.
* Every constant declared in events.h must appear in events.ts.
Exits non-zero on any drift.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CPP_HEADER = ROOT / "stack/common/include/common/events.h"
TS_MIRROR = ROOT / "lmt/src/events.ts"
CPP_SOURCES = [
    p
    for pat in ("stack/**/*.cpp", "stack/**/*.h")
    for p in ROOT.glob(pat)
    if "common/events.h" not in str(p) and "/tests/" not in str(p)
]


def declared_cpp() -> set[str]:
    text = CPP_HEADER.read_text()
    return set(re.findall(r"inline constexpr char ([A-Z0-9_]+)\[\]", text))


def declared_ts() -> set[str]:
    text = TS_MIRROR.read_text()
    return set(re.findall(r"^\s{2}([A-Z0-9_]+):", text, re.M))


def used_in_sources(catalog: set[str]) -> set[str]:
    used: set[str] = set()
    pattern = re.compile(r"\bev::([A-Z][A-Z0-9_]*)")
    for path in CPP_SOURCES:
        used |= set(pattern.findall(path.read_text(errors="ignore")))
    return used & catalog | (used - catalog)


def main() -> int:
    cpp = declared_cpp()
    ts = declared_ts()

    problems = []

    missing_in_ts = sorted(cpp - ts)
    if missing_in_ts:
        problems.append(f"events.ts missing constants: {missing_in_ts}")

    extra_in_ts = sorted(ts - cpp)
    if extra_in_ts:
        problems.append(f"events.ts has unknown constants: {extra_in_ts}")

    used = used_in_sources(cpp)
    undeclared = sorted(used - cpp)
    if undeclared:
        problems.append(f"ev::<NAME> used but not declared in events.h: {undeclared}")

    never_used = sorted(cpp - used)
    if never_used:
        print(f"[warn] declared but never emitted in stack/: {never_used}")

    if problems:
        for p in problems:
            print(f"[FAIL] {p}", file=sys.stderr)
        return 1

    print(f"[ok] event catalog consistent: {len(cpp)} events, "
          f"{len(used)} emitted in stack/")
    return 0


if __name__ == "__main__":
    sys.exit(main())
