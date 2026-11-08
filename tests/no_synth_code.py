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
SYNTHS = r"SID|DRUM|CZ|FM6|PROPHET|TRIO|WHEEL"
PATTERNS = {
    "engine number": re.compile(r"\bENGI_(?:" + SYNTHS + r")\b"),
    "engine": re.compile(r"\bENG_(?:SID|DRUM|CZ|FM6|P5_TEST|TRIO|WHEEL)\b"),
    "built-in switch": re.compile(r"\bMELODEE_(?:SID|DRUM|CZ|FM6|P5)_BUILTIN\b"),
    "symbol": re.compile(r"\b(?:drum|drk|dsk|dlk|dr8|d9|sid|cz|fm6|p5|prophet|trio|wheel)_\w+|\b(?:DK|D9|DV|DVT|DRK|FM6|CZ|P5)_[A-Z0-9_]+"),
    "name": re.compile(r'"(?:[^"\\]|\\.)*\b(?:SID|DRUM|CZ-1|CZ|FM6|PROPHET|P5|808|909|CR78|TRIO|WHEEL)\b(?:[^"\\]|\\.)*"'),
}
# What has to stay, each with its reason: (file, kind, the hit's text: regular expressions) -> why. DRUM, the synth phase 5
# took out, appears only for the shipped synths' identity (its engine number, as every other shipped synth's) and the
# host builds' reference engines; nothing of its pads, kits or pages. A rule that matches nothing fails (no stale rule).
ENGINE_SRC = (r"eng_fm6\.c|fm6_core\.c|eng_fm6_rom\.h|eng_prophet_test\.c|prophet_patch\.c|eng_sid\.c|eng_cz\.c|"
              r"eng_phase\.c|cz_native\.c|cz_patch\.h|eng_trio\.c|eng_wheel\.c")
ALLOW = [
    (ENGINE_SRC, r".*", r".*",
     "another synth's engine source, compiled only into its module (and host builds): phase 6 moves it into "
     "firmware/modules/<name> with the self-contained SDK (DRUM's moved in phase 5)"),
    (r"engines\.c|core\.h|browse\.c|module_schema\.h|presets\.c|eng_module_main\.c", r"built-in switch|engine|symbol",
     r"MELODEE_\w+_BUILTIN|ENG_\w+|\w+_part(_t)?|\w+_schema|[A-Z0-9]+_SCHEMA|\w+_preset_patch|P5_OVERSAMPLE",
     "host test builds' built-in reference engines (the module tests' 'built-in' configuration; the device image "
     "has none): phase 6's self-contained SDK takes the table out of firmware/src"),
    (r"engines\.c|core\.h|eng_module_main\.c|project\.c|browse\.c|editor_preferences\.c|shared_presets\.c",
     r"engine number|symbol", r"ENGI_\w+|drum_from_phys",
     "the shipped synths' engine numbers as stored data names them (projects, user presets, 1.x banks, the default "
     "song, the setup check): phase 6's number <-> UID map replaces them"),
    (r"eng_module\.c", r"symbol", r"P5_TANH|FM6_\w+",
     "the host table's shared tables by id (mod_abi3.h MOD3_T_*: frozen ids the firmware fills)"),
    (r"upreset\.c|editor_backup\.c|editor_library\.c|shared_presets\.c|project\.c|pattern_store\.c", r"symbol",
     r"cz_extra|cz_patch_valid|fm6_fn(_ok)?",
     "Melodee 1.x's stored layouts' field names (user preset records, FUN banks: their bytes as written)"),
    (r"params\.c|ui_sections\.c", r"symbol|name", r"CZ_ED|CZ_ROW|\"CZ LEVEL\"|\"CZ \"",
     "PHASE's retired pages (MELODEE_LEGACY_EXTRAS reference builds): phase 6 retires PHASE with the old engines"),
    (r"params\.c|ui_graph\.c|icons\.c|ui_menu\.c", r"name", r".*",
     "the engines' names by number for lists and the editor's tables (N_ENGNAME, the 4-letter tags, GLO > WEIGHTS' "
     "cost classes, icons by name), the licence credits: phase 6 takes names from the modules' files"),
    (r"params\.c|ui_smk\.c|browse\.c", r"name", r'"DRUM"|"808"|"909"|"STEEL DRUM"',
     "drum vocabulary, not the synth: GLO > SYSTEM DRUM (General MIDI's drum channel), the SMK-37's DRUM pad mode, the "
     "DRUM category and its name words"),
    (r"eng_phys\.c|ui\.c|ui_input\.c", r"name", r'"HAND DRUM"|"MOD WHEEL ?"',
     "a word, not a synth: a PHYS preset's name, the mod wheel"),
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
