#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kerem Kilic (Ellic Studio)
"""No synth code in the firmware: firmware/src (comments left out) names no synth module. A synth is a module
(firmware/modules/<name>): its engine number, its engine, its symbols and its names in the firmware's strings belong
there. Every hit fails the check unless ALLOW names its file and pattern with the reason it has to stay; a listed hit
that is gone fails too (the list never grows stale).
    tests/no_synth_code.py           the check
    tests/no_synth_code.py --report  every hit, allowed or not"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "firmware" / "src"
SYNTHS = r"SID|DRUM|CZ|FM6|PROPHET|TRIO|WHEEL|808|909|SIDKIT|CR78|FMKIT|SAMPLER"   # (2.0: DRUM's kits, synths of their own)
PATTERNS = {
    "engine number": re.compile(r"\bENGI_(?:" + SYNTHS + r")\b"),
    "engine": re.compile(r"\bENG_(?:SID|DRUM|CZ|FM6|P5_TEST|TRIO|WHEEL|KIT808|KIT909|KITSID|KITCR|KITFM|SAMPLER)\b"),
    "built-in switch": re.compile(r"\bMELODEE_(?:SID|DRUM|CZ|FM6|P5)_BUILTIN\b"),
    "symbol": re.compile(r"\b(?:drum|drk|dsk|dlk|dr8|d9|sid|cz|fm6|p5|prophet|trio|wheel|kit808|kit909|kitsid|kitcr|kitfm|sampler)_\w+|"
                          r"\b(?:DK|D9|DV|DVT|DRK|FM6|CZ|P5|KIT808|KIT909|KITSID|KITCR|KITFM|SAMPLER)_[A-Z0-9_]+"),
    "name": re.compile(r'"(?:[^"\\]|\\.)*\b(?:SID|DRUM|CZ-1|CZ|FM6|PROPHET|P5|808|909|CR78|TRIO|WHEEL|SID KIT|FM KIT|SAMPLER)\b(?:[^"\\]|\\.)*"'),
    # Melodee 2.0: an engine is a handle, never a number with a meaning (the microtonal.h `eng_req != 10u` of 1.x)
    "engine literal": re.compile(r"\b(?:eng_req|engine|eng)\s*[!=]=\s*\(?[1-9][0-9]*u?\b|\b[1-9][0-9]*u?\s*[!=]=\s*(?:\w+(?:->|\.))?(?:eng_req|engine)\b"),
}
# What has to stay, each with its reason: (file, kind, the hit's text: regular expressions) -> why. No synth stays: the
# synths' sources, engines, numbers, names, tags, cost classes and licences are firmware/modules' (shipped.h, the host
# builds' reference engines in host/, each synth's own directory). What is left are words a synth's name happens to
# be: General MIDI's drums, a preset's name, the mod wheel. A rule that matches nothing fails (no stale rule).
ALLOW = [
    (r"params\.c|ui_smk\.c|browse\.c|icons\.c", r"name", r'"DRUM"|"808"|"909"|"STEEL DRUM"',
     "drum vocabulary, not the synth: GLO > SYSTEM DRUM (General MIDI's drum channel) and its icon, the SMK-37's DRUM "
     "pad mode, the DRUM category and its name words"),
    (r"ui\.c|ui_input\.c", r"name", r'"HAND DRUM"|"MOD WHEEL ?"',
     "a word, not a synth: a preset's name, the mod wheel"),
]
def strip_comments(text):
    out, i, n = [], 0, len(text)
    while i < n:
        c = text[i]
        if c == '"' or c == "'":
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == "\\" else 1
            out.append(text[i:j + 1])
            i = j + 1
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append("\n" * text.count("\n", i, j))
            i = j
        elif text.startswith("//", i):
            j = text.find("\n", i)
            i = n if j < 0 else j
        else:
            out.append(c)
            i += 1
    return "".join(out)


def hits():
    for path in sorted(SRC.rglob("*")):
        if path.suffix not in (".c", ".h") or not path.is_file():
            continue
        rel = str(path.relative_to(SRC))
        for ln, line in enumerate(strip_comments(path.read_text(errors="replace")).splitlines(), 1):
            for kind, rx in PATTERNS.items():
                for m in rx.finditer(line):
                    yield rel, ln, kind, m.group(0)


def allowed(rel, kind, text):
    for i, (f, k, t, why) in enumerate(ALLOW):
        if re.fullmatch(f, rel) and re.fullmatch(k, kind) and re.fullmatch(t, text):
            return i
    return -1


def main():
    report = "--report" in sys.argv
    used, bad, n = set(), 0, 0
    for rel, ln, kind, text in hits():
        n += 1
        i = allowed(rel, kind, text)
        used.add(i)
        if report or i < 0:
            print(f"{rel}:{ln}: {kind}: {text}" + (f"  (allowed: rule {i})" if i >= 0 else ""))
        bad += i < 0
    stale = [i for i in range(len(ALLOW)) if i not in used]
    for i in stale:
        print(f"rule {i} matches nothing (drop it): {ALLOW[i][3]}")
    if report:
        for i, r in enumerate(ALLOW):
            print(f"rule {i}: {r[0]} / {r[1]} / {r[2]}: {r[3]}")
    print(f"no synth code: {bad} hits not allowed; {n - bad} allowed by {len(ALLOW)} rules ({len(stale)} stale)")
    return 1 if bad or stale else 0


if __name__ == "__main__":
    sys.exit(main())
