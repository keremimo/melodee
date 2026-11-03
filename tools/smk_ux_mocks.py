#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kerem Kilic (Ellic Studio)
"""The SMK-37 UX mocks (docs/SMK37_UX.md): each screen as the firmware draws it (tests/smk_ux_mocks.c on
tests/ui_render.c) beside the 16 pads as the design lights them, into docs/smk37-ux/.

  tools/smk_ux_mocks.py [--out docs/smk37-ux] [--no-build]

Builds and runs tests/smk_ux_mocks.c (cc, as tests/run_tests.sh builds ui_render; needs build/gen from ./build.sh),
then for every scene writes <out>/<scene>.png (screen x2 | the pads, the button LEDs, a caption), the pad map alone as
<out>/pads_<scene>.png, the colour grammar as <out>/legend.png and a contact sheet <out>/sheet.png. Pillow only.
The pad maps are design data (SCENES below): the firmware does not light the pads this way yet."""
import argparse
import os
import subprocess
import sys

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONT = os.path.join(ROOT, "assets", "fonts", "Rubik[wght].ttf")
NIGHT = {"bg": (0x17, 0x14, 0x1F), "panel": (0x1D, 0x19, 0x26), "surf": (0x22, 0x1E, 0x2C), "line": (0x2E, 0x29, 0x39),
         "text": (0xF3, 0xEE, 0xF8), "mid": (0x9A, 0x93, 0xA8), "dim": (0x5A, 0x54, 0x68)}
TRK = [(0xFF, 0x7A, 0x5C), (0xFF, 0xC1, 0x45), (0x46, 0xD9, 0xB0), (0x9D, 0x8C, 0xFF)]   # coral amber mint lilac
# the pads' grammar (SMK37_UX.md "Colour grammar"): one hue, the track's, five states
OFF = (0x4A, 0x1F, 0x06)          # dark ember: nothing there, or silent
ACTIVE = (0xE8, 0xF4, 0xFF)       # white-blue: happening now (held, playing, the one selected)
WAIT = (0xFF, 0x6E, 0x00)         # orange, breathing: waiting for the bar, armed


def mix(a, b, pct):
    return tuple(int(x + (y - x) * pct / 100) for x, y in zip(a, b))


def pad_colour(state, hue):
    base = TRK[hue % 4] if isinstance(hue, int) else hue
    return {"off": OFF, "none": mix(NIGHT["bg"], OFF, 35), "on": mix(OFF, base, 42), "filled": base, "active": ACTIVE,
            "wait": WAIT}[state]


def font(size, weight="Medium"):
    f = ImageFont.truetype(FONT, size)
    try:
        f.set_variation_by_name(weight)
    except Exception:
        pass
    return f


# ----------------------------------------------------------------------------------------------------- the scenes --
# a pad: (state, label, hue); a row: eight pads left to right; a map: (top row, bottom row). Steps and patterns 1..8 top,
# 9..16 bottom; notes low at the bottom (hal/smk_input.h SMK_WHITE_NOTE: the bottom row is F3 .. F4).
def keys(hue, scale, held=(), roots=()):
    names = ["F3", "G3", "A3", "B3", "C4", "D4", "E4", "F4", "G4", "A4", "B4", "C5", "D5", "E5", "F5", "G5"]
    row = []
    for n in names:
        st = "active" if n in held else "filled" if n in roots else "on" if n[0] in scale else "off"
        row.append((st, n, hue))
    return (row[8:], row[:8])


def steps(hue, filled, play, dim=()):
    rows = []
    for i in range(16):
        st = "active" if i + 1 == play else "filled" if i + 1 in filled else "on" if i + 1 in dim else "off"
        rows.append((st, str(i + 1), hue))
    return (rows[:8], rows[8:])


def tracks_patterns(sel, used_tracks, playing, waiting, used, armed=()):
    top = []
    for t in range(8):
        st = "wait" if t in armed else "active" if t == sel else "on" if t in used_tracks else "none"
        top.append((st, "T%d" % (t + 1), t % 4))
    bot = []
    for p in range(8):
        st = "active" if p == playing else "wait" if p == waiting else "on" if p in used else "off"
        bot.append((st, "P%d" % (p + 1), sel % 4))
    return (top, bot)


DRUM_TOP = ["LT", "MT", "HT", "LC", "MC", "HC", "CL", "MA"]
DRUM_BOT = ["BD", "SD", "CP", "CH", "OH", "TM", "RS", "CB"]


def drums(hue, hitting=(), selected=None):
    def row(names):
        return [("active" if n in hitting else "filled" if n == selected else "on", n, hue) for n in names]
    return (row(DRUM_TOP), row(DRUM_BOT))


def piano(hue, root, in_scale):
    top = ["C4", "D4", "E4", "F4", "G4", "A4", "B4", "C5"]
    bot = ["C#4", "D#4", None, "F#4", "G#4", "A#4", None, "C#5"]

    def st(n):
        if n is None:
            return ("none", "", hue)
        base = n[:-1]
        return ("filled" if base == root else "on" if base in in_scale else "off", n, hue)
    return ([st(n) for n in top], [st(n) for n in bot])


BANKS = ["PAGE", "SOUND", "FILTER", "ENV", "LFO", "FX", "ARP", "LEVELS"]
MODES = ["AUTO", "KEYS", "DRUM", "STEPS", "PATTERNS"]


def bank_pick(hue, bank, mode):
    top = [("active" if b == bank else "on", b, hue) for b in BANKS]
    bot = [("active" if m == mode else "on", m, hue) for m in MODES] + [("none", "", hue)] * 3
    return (top, bot)


def glo(hue_sel, sounding, soloing):
    top = [("filled" if t in sounding else "on", "M%d" % (t + 1), t) for t in range(4)] + [("none", "", 0)] * 4
    bot = [("active" if t in soloing else "on", "S%d" % (t + 1), t) for t in range(4)] + \
          [("none", "", 0), ("none", "", 0), ("on", "All", hue_sel), ("on", "Tap", hue_sel)]
    return (top, bot)


def name_pads(hue, cycling):
    labels = ["ABC", "DEF", "GHI", "JKL", "MNO", "PQR", "STU", "VWX", "YZ-", "123", "456", "789", "0.'", "&+/", "( )", "spc"]
    rows = [("active" if i == cycling else "on", labels[i], hue) for i in range(16)]
    return (rows[:8], rows[8:])


NOTES_STEPS = steps(0, filled=(1, 2, 5, 9, 12), play=6)
DRUM_STEPS = steps(3, filled=(5, 13), play=10, dim=(1, 3, 7, 9, 11, 15, 16, 2, 4, 6, 8, 14))

# scene -> (title, pad mode shown, pad map, LEDs lit (KNOB BANK, FADER BANK, SEQ), caption)
SCENES = [
    ("stage_keys", "Stage, synth track", "KEYS",
     keys(1, "ACDEG", held=("A3", "C4", "E4", "G4"), roots=("A4",)), (0, 0, 0),
     "Pads play the track's 16 keys, low notes at the bottom: in the scale (A minor pentatonic here) lit, the root "
     "brighter, the keys held white. The keys themselves have no lights: the band shows what sounds."),
    ("stage_drum", "Stage, DRUM track", "DRUM",
     drums(3, hitting=("BD", "CH", "TM"), selected="SD"), (0, 0, 0),
     "The kit's 16 drums: the eight lanes on the bottom row in the grid's order, the rest above. A drum lights white "
     "as it hits; the one DRUM SOUND edits is brighter. Velocity and pressure go to the drum."),
    ("notes_steps", "NOTES page", "STEPS",
     NOTES_STEPS, (0, 0, 0),
     "The page's 16 steps (1-8 top, 9-16 bottom): a step holding a note lit, the step playing white. Tap: a note on an "
     "empty step (the last played), a held step is the cursor (K3 / K4 edit it); SELECT or < > turn the pages."),
    ("drum_steps", "Drum STEP grid", "STEPS",
     DRUM_STEPS, (0, 0, 0),
     "The selected lane's hits lit, the other lanes' hits dim, the playhead white. Tap toggles the lane's hit; the "
     "black keys still pick the lane and ACC (as on the FM-1), K2 too."),
    ("patterns", "PATTERNS page", "PATTERNS",
     tracks_patterns(1, (0, 1, 2, 3), playing=0, waiting=4, used=()), (0, 0, 0),
     "Top row: tracks 1-8 in their colours (selected white, armed breathing, unused dark). Bottom row: the selected "
     "track's patterns, eight at a time as the screen shows them (< > the page): playing white, waiting orange, "
     "holding something lit, empty dark. Tap: switch at the bar; hold: the pattern's dialog; hold + another pad: copy."),
    ("seq_held", "SEQ held on Stage", "PATTERNS (momentary)",
     tracks_patterns(1, (0, 1, 2, 3), playing=1, waiting=2, used=()), (0, 0, 1),
     "SEQ held anywhere shows the same tracks and patterns on the pads for as long as it is held (the FM-1's SEQ "
     "held), the screen as it was."),
    ("bank_filter_notes", "NOTES, knob bank FILTER", "STEPS",
     NOTES_STEPS, (1, 0, 0),
     "KNOB BANK tapped to FILTER: the strip shows the bank's four cards (the Prophet's filter) in place of the page's, "
     "the bank's name as a header tag, the KNOB BANK LED lit. The pads keep their mode."),
    ("bank_env_stage", "Stage, knob bank ENV", "KEYS",
     keys(1, "ACDEG", held=("A3", "C4", "E4", "G4"), roots=("A4",)), (1, 0, 0),
     "On Stage the bank's cards drop in on the first turn, as the engine's knobs do today."),
    ("faders_page_env", "ENV page, fader bank PAGE", "KEYS",
     keys(0, "CDEFGAB", roots=("C4", "C5")), (0, 1, 0),
     "Fader bank PAGE: the four faders are the page's four knobs. A fader moving shows the cards for 1.5 s with a "
     "pickup gauge: the value's bar, a tick where the fader stands; not yet picked up (SUS, REL): the bar in MID until "
     "the fader reaches the value. No jumps."),
    ("faders_levels", "NOTES, fader bank LEVELS", "STEPS",
     NOTES_STEPS, (0, 1, 0),
     "Fader bank LEVELS (the default): tracks 1-4, each card in its track's colour; fader 3 moving."),
    ("pick_knobs", "KNOB BANK held", "BANK PICK",
     bank_pick(1, "FILTER", "AUTO"), (1, 0, 0),
     "KNOB BANK held: the bank picker (K1 or < > walk it, let go to keep). The pads' top row is the eight banks, the "
     "bottom row the pads' modes (AUTO the default): a tap picks. The cards already show the bank chosen."),
    ("pick_faders", "FADER BANK held", "BANK PICK",
     bank_pick(1, "LEVELS", "AUTO"), (0, 1, 0),
     "FADER BANK held: the same picker for the faders, LEVELS chosen."),
    ("layer_glo", "GLO held", "LAYER",
     glo(2, sounding=(2,), soloing=(2,)), (0, 0, 0),
     "GLO's map on the pads: top row MUTE 1-4 (lit while the track sounds, dim when it does not), bottom row SOLO 1-4 "
     "while held (white), All unmutes, Tap taps the tempo. The screen's map is unchanged."),
    ("layer_scl", "SCALE held", "LAYER",
     piano(0, "D#", ("D#", "F", "F#", "G#", "A#", "B", "C#")), (0, 0, 0),
     "SCALE held: the pads as a piano (whites top, blacks below), the root brighter, the scale's notes lit; a tap "
     "makes that note the root. What the FM-1 showed on the key lights."),
    ("pads_override", "Stage, pads set to STEPS", "STEPS (override)",
     DRUM_STEPS, (0, 0, 0),
     "A mode picked by hand (BANK held + a bottom pad, or the Stage sheet's Pads row) stays until AUTO is picked "
     "again; while it differs from what AUTO would show, the header carries its name."),
    ("name", "NAME", "LETTERS",
     name_pads(0, 5), (0, 0, 0),
     "NAME keeps today's map: the letters on the 16 pads, the cycling pad white; the black keys their functions."),
]


# ----------------------------------------------------------------------------------------------------- drawing --
PAD, GAP = 54, 8


def draw_pads(d, x0, y0, pmap, f):
    for r, row in enumerate(pmap):
        for c, (state, label, hue) in enumerate(row):
            x, y = x0 + c * (PAD + GAP), y0 + r * (PAD + GAP)
            col = pad_colour(state, hue)
            d.rounded_rectangle((x, y, x + PAD, y + PAD), 9, fill=col, outline=mix(col, NIGHT["text"], 12))
            if state == "wait":
                d.rounded_rectangle((x + 4, y + 4, x + PAD - 4, y + PAD - 4), 7, outline=(0xFF, 0xB0, 0x60), width=2)
            if label:
                ink = NIGHT["bg"] if state in ("filled", "active", "wait") else NIGHT["text"] if state == "on" else \
                    mix(NIGHT["text"], OFF, 45)
                lf = f if d.textlength(label, font=f) <= PAD - 6 else font(10, "SemiBold")   # (a long word, smaller)
                w = d.textlength(label, font=lf)
                d.text((x + PAD / 2 - w / 2, y + PAD - 22), label, fill=ink, font=lf)


def draw_leds(d, x, y, lit, f):
    for i, name in enumerate(("KNOB BANK", "FADER BANK", "SEQ")):
        on = lit[i]
        w = d.textlength(name, font=f) + 22
        d.rounded_rectangle((x, y, x + w, y + 26), 6, fill=NIGHT["surf"], outline=NIGHT["line"])
        d.ellipse((x + 7, y + 9, x + 15, y + 17), fill=(0xFF, 0xA0, 0x30) if on else NIGHT["dim"])
        d.text((x + 20, y + 5), name, fill=NIGHT["text"] if on else NIGHT["mid"], font=f)
        x += w + 8


def wrap(d, text, f, width):
    words, lines, cur = text.split(), [], ""
    for w in words:
        t = (cur + " " + w).strip()
        if d.textlength(t, font=f) > width and cur:
            lines.append(cur)
            cur = w
        else:
            cur = t
    if cur:
        lines.append(cur)
    return lines


def pads_image(pmap, mode, f_pad, f_cap):
    w, h = 8 * (PAD + GAP) - GAP + 24, 2 * (PAD + GAP) - GAP + 24
    im = Image.new("RGB", (w, h), NIGHT["panel"])
    d = ImageDraw.Draw(im)
    draw_pads(d, 12, 12, pmap, f_pad)
    return im


def scene_image(scene, screen, f_pad, f_cap, f_title, f_small):
    name, title, mode, pmap, leds, caption = scene
    sw = screen.width
    pw = 8 * (PAD + GAP) - GAP
    W = sw + 32 + pw + 24
    im = Image.new("RGB", (W, 640), NIGHT["bg"])
    d = ImageDraw.Draw(im)
    im.paste(screen, (16, 16))
    x = sw + 40
    d.text((x, 16), title, fill=NIGHT["text"], font=f_title)
    d.text((x, 48), "PADS  " + mode, fill=TRK[1], font=f_small)
    draw_pads(d, x, 76, pmap, f_pad)
    draw_leds(d, x, 76 + 2 * (PAD + GAP) + 6, leds, f_small)
    y = 76 + 2 * (PAD + GAP) + 46
    for line in wrap(d, caption, f_cap, pw):
        d.text((x, y), line, fill=NIGHT["mid"], font=f_cap)
        y += 22
    d.text((16, sw + 24), name, fill=NIGHT["dim"], font=f_small)
    return im.crop((0, 0, W, max(y + 16, sw + 56)))


def legend_image(f_pad, f_cap, f_title):
    im = Image.new("RGB", (700, 330), NIGHT["bg"])
    d = ImageDraw.Draw(im)
    d.text((16, 14), "Pad colour grammar", fill=NIGHT["text"], font=f_title)
    rows = [("off", "OFF: nothing there, or silent (dark ember)"),
            ("on", "ON: something there (the track's colour, dim)"),
            ("filled", "FILLED: holding something, or the one picked (the track's colour)"),
            ("active", "ACTIVE: happening now (held, playing, selected; white)"),
            ("wait", "WAITING: for the bar, or armed (orange, breathing)")]
    for i, (st, text) in enumerate(rows):
        y = 52 + i * 48
        for k in range(4):
            col = pad_colour(st, k)
            d.rounded_rectangle((16 + k * 34, y, 16 + k * 34 + 28, y + 28), 6, fill=col)
            if st == "wait":
                d.rounded_rectangle((18 + k * 34, y + 2, 16 + k * 34 + 26, y + 26), 5, outline=(0xFF, 0xB0, 0x60), width=2)
        d.text((164, y + 4), text, fill=NIGHT["text"], font=f_cap)
    d.text((16, 300), "Each track's hue is its screen colour; the four columns show tracks 1-4.", fill=NIGHT["mid"], font=f_cap)
    return im


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(ROOT, "docs", "smk37-ux"))
    ap.add_argument("--build-dir", default=os.path.join(ROOT, "build", "smk_ux"))
    ap.add_argument("--no-build", action="store_true", help="reuse the PPMs in the build dir")
    a = ap.parse_args()
    os.makedirs(os.path.join(a.build_dir, "ppm"), exist_ok=True)
    os.makedirs(a.out, exist_ok=True)
    exe = os.path.join(ROOT, "build", "host", "smk_ux_mocks")
    if not a.no_build:
        os.makedirs(os.path.dirname(exe), exist_ok=True)
        cc = os.environ.get("CC", "cc")
        cmd = [cc, "-O1", "-ffp-contract=off", "-w", "-DMELODEE_USB_AUDIO=1", "-DMELODEE_USB_HOST=1",
               "-I" + os.path.join(ROOT, "build", "gen"), "-I" + os.path.join(ROOT, "firmware", "src"),
               "-I" + os.path.join(ROOT, "tests"), "-o", exe, os.path.join(ROOT, "tests", "smk_ux_mocks.c"), "-lm"]
        subprocess.run(cmd, check=True)
        subprocess.run([exe, a.build_dir], check=True)
    f_pad, f_cap, f_title, f_small = font(13, "SemiBold"), font(15, "Regular"), font(20, "SemiBold"), font(13, "Medium")
    sheet_rows = []
    for scene in SCENES:
        ppm = os.path.join(a.build_dir, "ppm", "NIGHT_%s.ppm" % scene[0])
        if not os.path.exists(ppm):
            sys.exit("smk_ux_mocks: no render for %s (run without --no-build)" % scene[0])
        screen = Image.open(ppm).convert("RGB").resize((480, 480), Image.NEAREST)
        im = scene_image(scene, screen, f_pad, f_cap, f_title, f_small)
        im.save(os.path.join(a.out, scene[0] + ".png"))
        pads_image(scene[3], scene[2], f_pad, f_cap).save(os.path.join(a.out, "pads_%s.png" % scene[0]))
        sheet_rows.append(im)
    legend_image(f_pad, f_cap, f_title).save(os.path.join(a.out, "legend.png"))
    w = max(r.width for r in sheet_rows)
    sheet = Image.new("RGB", (w, sum(r.height + 8 for r in sheet_rows) + 8), NIGHT["line"])
    y = 8
    for r in sheet_rows:
        sheet.paste(r, (0, y))
        y += r.height + 8
    sheet.save(os.path.join(a.out, "sheet.png"))
    print("smk_ux_mocks: %d scenes -> %s" % (len(SCENES), a.out))


if __name__ == "__main__":
    main()
