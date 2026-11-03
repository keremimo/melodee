# Melodee Pro / Elite: the SMK-37's own UX

A design for the SMK-37 build (`BOARD_SMK37`, [SMK37.md](../SMK37.md)): what the 16 RGB pads, the knob and fader
banks, the four faders and the two wheels do, so the extra controls carry real functionality before the build ships
as "Melodee Pro / Elite". The FM-1 build is untouched by any of it.

Status (2026-11-03): phases 1-3 of section 9 are built on this branch, behind `BOARD_SMK37`, untested on hardware:
the banks (`firmware/src/ui_smk.c`, the faders in `main.c`), the pad modes and their lights, taps and maps
(`ui_input.c` smk_pads_*, `hal/smk_input.h` smk_pad_key), the GLO and SCALE maps. Later the same day: the keys never
choose a pattern or a track on the SMK (Kerem: only the pads trigger, arm and record sequences); the PATTERNS pads
carry the FM-1's whole pattern-key gesture set (`ui_input.c` pads_patterns_on, smk_pat_bit: the one state machine,
`pattern_keys`, fed by the pads instead of the keys), SEQ held puts them on the pads everywhere, and the keys go on
playing (and recording, armed) with SEQ held, on PATTERNS and under the pattern dialog. Where the build differs from
the first proposal below, the text says so in place. The open questions (section 8) stand, answered for now by the
build's defaults.

Mocks: `docs/smk37-ux/<scene>.png` is the screen as the firmware draws it (`tests/smk_ux_mocks.c` on
`tests/ui_render.c`'s scenes; the parts the design adds are drawn over the real page with the same primitives)
beside the pads as the design lights them (`tools/smk_ux_mocks.py`; the pad maps are design data, the firmware does
not light them so yet), the button LEDs and a caption. `sheet.png` has them all, `legend.png` the colour grammar.
Regenerate with `tools/smk_ux_mocks.py` after `./build.sh` (it needs `build/gen`).

## 1. The model

Four things are in play at any moment, each with a default that follows the page, and an override:

| Axis | Values | Default (AUTO) | Picked by |
|---|---|---|---|
| Page | as the FM-1 (Stage, the pages, the layers) | - | the page buttons, SELECT |
| Pad mode | KEYS, DRUM, STEPS, PATTERNS | from the page and the track (section 2.4) | a BANK button held + a bottom-row pad; the Stage sheet's **Pads** row |
| Knob bank (K1..K4) | PAGE, SOUND, FILTER, ENV, LFO, FX, ARP, LEVELS | PAGE | KNOB BANK tap (next), held (picker) |
| Fader bank (faders 1..4) | the same eight | LEVELS | FADER BANK tap (next), held (picker) |

The pads, the knobs and the faders are independent: a knob bank does not change the pads, a pad mode does not change
the faders. The page is the FM-1's page; nothing in the navigation standard changes (K1 across, K2 down, OCT+ Enter,
OCT- Esc, SELECT pages). Momentary states (SEQ held, a layer held, NAME, a BANK button held) take the pads for as long
as they last and give them back.

What stays as SMK37.md has it: K1..K4 = KNOB 1..4, K5 MASTER, K6 SELECT, K7 ALGORITHM, K8 PRESETS; the page buttons
(FX, SCALE = SCL, CHORD = ENV, NOTE REPEAT = LFO, PARA = EDIT, GLOBE = GLO, PATCH = HOME, BT = SAVE, ARP, SEQ, SEQ PLAY,
SEQ REC, OCTAVE -/+); the keybed (the middle 27 as the FM-1's, the outer 10 more notes); the wheels; `<` `>` one SELECT
detent; the power-on combos. What changes: KNOB BANK is no longer "HOME held" (PATCH held opens the menu, as HOME held
does on the FM-1); FADER BANK gets a picker and the shared bank list.

## 2. Pads

### 2.1 Colour grammar

One hue per context, the selected track's screen colour (coral, amber, mint, lilac; `T_TRK`, converted to the pads'
RGB), so the pads say the same thing the screen's rows say. Five states, told apart by colour and level, never by level
alone (`legend.png`):

| State | Light | Meaning |
|---|---|---|
| OFF | dark ember (the dark orange of today's build) | nothing there, or silent: a key outside the scale, an empty step, an empty pattern, a track not in use |
| ON | the track's colour, dim | something there: a key in the layout, another lane's hit, a pattern holding notes, a track in use, a tile that can be pressed |
| FILLED | the track's colour, full | holding something, or the one picked: the selected lane's hit, the scale's root, the selected track, the drum being edited |
| ACTIVE | white-blue | happening now: a pad held, the step playing, the pattern playing, a tile held |
| WAITING | orange, breathing | a pattern waiting for the bar, a track armed, an edit waiting for the loop end |

Where a row shows several tracks (PATTERNS' top row, GLO's tiles) each pad takes its own track's hue. A pad with no
function in the current map is OFF at a lower level ("none" in the mocks). Nothing else: no second hue, no blinking
outside WAITING (the FM-1's blinking keys become ON here; the breathing plane stays for waiting).

### 2.2 The modes

**KEYS** (`stage_keys.png`, `bank_env_stage.png`, `faders_page_env.png`): the pads play notes, as today: the track's
16 white keys F3 .. G5, low notes on the bottom row, through the track's key map (scale, chord, MPC layout, octave),
with velocity. Lights: a key the layout plays ON, out of the scale (QNT OFF) OFF, the root FILLED, a pad held or its
note sounding (a key, MIDI) ACTIVE. This is the only feedback the scale has left, since the keys lost their lights; the
band on Stage shows the notes and the chord as before. Pressure: channel aftertouch to the track (the MOD matrix's AT
source), as a MIDI controller's would.

**DRUM** (`stage_drum.png`): on a DRUM track the pads are the kit's 16 drums with velocity, in the layout MIDI's MPC
pads already use (`eng_drum.c` drum_mpc_note: the beat under the fingers): bottom row BD SD CP RS CH OH bell cymbal,
top row the toms low to high, maracas, the congas, clave (the 909's and CR-78's names by kit). Lights: every drum ON,
the one DRUM SOUND / DRUM MIX edits FILLED, a drum ACTIVE while its pad is down or, on Stage, while its lane flashes.
Taps play and record as the keys do (the drum's GM note, velocity); pressure is the track's aftertouch (the MOD
matrix's AT source), as in KEYS.

**STEPS** (`notes_steps.png`, `drum_steps.png`, `pads_override.png`): 16 steps of the selected track's pattern, the
page the screen shows (NOTES' and the grid's pages: SELECT or `<` `>` turn them, the pads follow), steps 1-8 on the top
row, 9-16 below, the step playing ACTIVE.
- Synth track: a step holding notes FILLED, empty OFF. Tap an empty step: the last note played goes there (what OCT+
  does on NOTES); tap a step with notes: the cursor goes there (K2 pitch, K3 length, K4 velocity edit it, as on NOTES);
  hold a step and tap it again: clear it (EDIT held undoes). A held pad is the cursor while held.
- DRUM track (the grid): the selected lane's hits FILLED, the other lanes' hits ON (the groove shows), empty OFF. Tap
  toggles the lane's hit, as the FM-1's white keys do; the black keys keep selecting the lane and ACC (the FM-1 map,
  without lights: the grid shows the lane), K2 too. ACC held: the accents FILLED.
- While the transport runs and the pads are not in STEPS, nothing changes: STEPS is a mode, not the playing display
  (today's build shows the steps on any page while playing; that goes, the mode replaces it).

**PATTERNS** (`patterns.png`, `seq_held.png`, `pattern_dialog.png`): what the FM-1 puts on the keys with SEQ held or
on the PATTERNS page, and here the only way a pattern is triggered, armed or recorded: the keys never choose a pattern
or a track on the SMK; they play notes (and record them, armed) with SEQ held, on the PATTERNS page and under the
pattern dialog alike. Top row: tracks 1-8, each in its colour: in use ON, selected FILLED, armed WAITING, not in use
OFF (a tap on one adds the missing tracks through it, as the FM-1's track keys do), a pad held ACTIVE. Bottom row: the
selected track's patterns, eight at a time, the page the PATTERNS screen shows (its dots; elsewhere the page of the
pattern playing or waiting: SELECT or `<` `>` with SEQ held walk the patterns and the page follows, 1-8 / 9-16):
playing ACTIVE, waiting for the bar WAITING, holding something ON, empty OFF, the one held to copy ACTIVE. Gestures as
the FM-1's pattern keys, one state machine (`pattern_keys`): tap = switch at the bar (an empty one arms REC with it,
the one playing hit again at once toggles REC, as REC's tap); hold a second = the pattern's dialog: the pad again
deletes it, another bottom-row pad copies it there, the first two top-row pads halve and double it (WAITING, as the
FM-1's C# D# blinked; the other top-row pads are dark, as its track keys go), the dialog stays while the pad or SEQ is
held and closes with them once the length changed, OCT- leaves. The sheet (OCT+) and Copy to.. stay on the screen as
they are. Why this split instead of 16 patterns of one track: it mirrors the PATTERNS screen exactly (a row of eight,
two pages) and keeps the FM-1's "tracks and patterns in one layout".

### 2.3 Momentary maps

These take the pads for as long as they last, whatever the mode, and give them back:

| While | Pads | Screen |
|---|---|---|
| SEQ held | PATTERNS (above), over a mode picked by hand too; the keys play on | unchanged (`seq_held.png`) |
| GLO held (SET) | top row MUTE 1-4: FILLED while the track sounds, ON when muted or soloed out; bottom row SOLO 1-4 while held (ACTIVE), **All** unmutes, **Tap** taps (ON; dim with an outside clock) | the GLO map as it is (`layer_glo.png`) |
| SCALE held (SET) | a piano: top row C4 .. C5, bottom row the black keys between (the two gaps OFF); the root FILLED, the scale's notes ON, the rest OFF; a tap makes that note the root | the SCL map as it is (`layer_scl.png`) |
| FX held (punch) | the 16 white keys' punches as today, each ON in its family's colour, the one held ACTIVE | the punch layer as it is |
| EDIT held | the FM-1's white-key map on the grid layout: the sections on the pads; Init, Fav, Undo, Store stay on the black keys (built so; a map of their own is a later step) | the sections map as it is |
| NAME | the letters on the 16 pads as today, the cycling pad ACTIVE; the black keys their functions | unchanged (`name.png`) |
| KNOB BANK / FADER BANK held | top row the eight banks (the current ACTIVE, one the engine lacks OFF), bottom row AUTO, KEYS, DRUM, STEPS, PATTERNS (the current ACTIVE); a tap picks and the button may be let go | the bank picker (section 3) |
| a question, the menu | the pads keep their mode; KEYS and DRUM still play, STEPS and PATTERNS taps do nothing (as the FM-1's pattern keys under a dialog), the pattern dialog's own pads excepted | - |

### 2.4 AUTO: which mode the page picks

| Where | Synth / MIDI track | DRUM track |
|---|---|---|
| Stage, the sound pages (EDIT, ENV, LFO, FX, ARP, SCL, VOICE ..), MIXER, the browser, SETTINGS, SONG, PATTERN, MOTION, TEMPO | KEYS | DRUM |
| NOTES | STEPS | STEPS (the grid) |
| DRUM HIT | - | STEPS |
| PATTERNS; SEQ held anywhere | PATTERNS | PATTERNS |
| DRUM SOUND, DRUM MIX | - | DRUM |

Built so: AUTO picks PATTERNS on the PATTERNS page only; on SONG, PATTERN and MOTION the pads keep playing (the
first proposal had PATTERNS there). The keys are never pattern keys on the SMK, whatever the pads show; SEQ held puts
PATTERNS on the pads everywhere, over a mode picked by hand as well.

The rule: where you play, the pads play; where you edit steps, they are steps; where you arrange, they are patterns.
A track change re-evaluates it (DRUM selected: drums at once).

### 2.5 Picking a mode by hand

A BANK button held + a bottom-row pad (AUTO, KEYS, DRUM, STEPS, PATTERNS), or Stage's sheet: a **Pads** row with the
same values (a picker row, as Routing on SHARED's sheet). A mode picked by hand stays through page and track changes
until AUTO is picked again. While it differs from what AUTO would show, the header carries its name as a tag (KEYS,
DRUM, STEPS, PATTERNS; `pads_override.png`): state, not a message, by the README's status-indicator rules (left of the
page name, the label face, the theme colour; the octave pill gives way; a bank's tag comes first). AUTO shows no tag.
KEYS on a DRUM track plays the kit's GM map on the white keys (as the FM-1's keys do); DRUM on a synth track falls
back to KEYS (built so: no message). PATTERNS by hand puts the pattern map on the pads everywhere; the keys go on
playing.

### 2.6 What leaves the keys

The keys have no lights. Everything the FM-1 showed on them has a new home:

| FM-1 key lights | SMK-37 |
|---|---|
| the layout (scale notes dim, held bright, a kit's map) | KEYS / DRUM pads (section 2.2) |
| SEQ held: patterns on the white keys, tracks on the black | PATTERNS pads, SEQ held momentary |
| PATTERNS page (unarmed): the same map | PATTERNS pads, armed or not (the keys play and record) |
| the pattern keys' tap, double hit and hold (switch at the bar, REC, the dialog) | the same gestures on the bottom-row pads; never the keys |
| the pattern dialog's half / double keys (C# D#) | the first two top-row pads while the dialog is up (WAITING, as the keys blinked); the bottom row stays delete / copy |
| the drum grid: steps on the whites, lane / ACC / page on the blacks | STEPS pads; the black keys keep lane / ACC / page without lights (the grid shows them) |
| NAME: letters on the whites, functions on the blacks | as today (pads letters, black keys functions) |
| GLO SET: mutes on the blacks, solos / All / Tap on the whites | GLO's pad map |
| SCL SET: root lit, scale blinking | the piano pads |
| FX held: the punch keys | the punch pads (as today) |
| EDIT held: sections on the whites, Init / Fav / Undo / Store on the blacks | the EDIT pad map |
| SEQ held + black keys 1-8: track select | PATTERNS' top row; the keys play on under SEQ |
| the 10 outer keys | notes only, in every map (no function without lights) |

## 3. Knob banks

What K1..K4 edit. One list, shared with the faders (one table in the firmware; a bank is a set of four parameters,
whichever control reaches them):

| Bank | PROPHET | CZ-1 | FM6 | SID, NOISE, the others | DRUM |
|---|---|---|---|---|---|
| PAGE | the page's four knobs (the FM-1; Stage: the engine's HOME knobs) | | | | |
| SOUND | Stage's knobs from anywhere: CUTOFF RESONANCE ENV AMT RELEASE | line 1 wave, DCW peak, detune, vibrato | the engine's HOME macros | the engine's HOME knobs | DRUM SOUND's TUNE DECAY TONE of the drum being edited |
| FILTER | CUTOFF RESONANCE ENV AMT filter DECAY (today's fader bank B) | DCW peak, DCW key follow, DCA key follow, line 2 DCW | - | the engine's filter knobs where it has them (SID CUTOFF RES MODE ..), else - | - |
| ENV | amp ATTACK DECAY SUSTAIN RELEASE (today's fader bank C) | line 1 amp envelope R1 R2 L1 L2 | the carrier's EG RATE 1-4 (the operator selected) | the track's ATK DEC SUS REL | - |
| LFO | P5 LFO page (rate, shape, ..) | CZ VIBRATO (wave, rate, depth, delay) | FM LFO (wave, speed, delay, sync) | the track LFO (RATE WAVE PHASE FADE) | - |
| FX | the rack: MIX of slots 1-3, SEND (today's fader bank D) | | | | the same |
| ARP | ARP's MODE RATE OCT GATE | | | | - |
| LEVELS | tracks 1-4 LEVEL (the GLO layer's knobs; the fader default) | | | | the same |

A bank the engine has nothing for ("-") is skipped when cycling and dark on the picker's pad; its name stays in the
picker's list, dim. Which parameters exactly go in CZ-1's and FM6's ENV / FILTER rows is an open question (section 8);
the table above is the first proposal.

Switching: **KNOB BANK tap** = the next bank (cycle, PAGE first; a header message names it, as FADER BANK does today);
**held** (0.5 s, the layer gesture) = the picker over the cards, the current bank marked (`pick_knobs.png`): K1 or
`<` `>` walk it, a top-row pad picks, letting go keeps what is shown. The LED is lit while the bank is not PAGE.

The screen: the strip (the four cards under the header) shows the bank's four parameters in place of the page's,
labels, values and gauges as any page's, the knob turning hot (`bank_filter_notes.png`); the page's picture and rows
stay. The bank's name is a header tag (FILTER, ENV, SOUND ..; none for PAGE), a status by the README's rules. On Stage
the bank's cards drop in on the first turn as the engine's knobs do, and go after 1.5 s (`bank_env_stage.png`). In the
firmware this is `page_over` (the quick layers' own four knobs): the bank is a `page_t` built like `stage_page()`,
so `edit_param`, the cards, the pickers and motion capture follow with no new path. K5..K8 are not banked.

## 4. Fader banks

The same eight banks; the default is LEVELS (tracks 1-4). **FADER BANK tap** = the next bank, **held** = the picker
(`pick_faders.png`), the LED lit while the bank is not LEVELS. PAGE on the faders is the classic: on ENV the faders are
A D S R, on a P5 filter page the filter, on MIXER the levels; it follows SELECT.

Pickup: a fader is absolute, a value is not. Today a fader takes over on its first move, so the value jumps to where
the fader stands. Proposed: soft takeover: after a bank or track change a fader is "away" until it crosses the value,
then it follows. The screen shows it: a fader moving brings the bank's four cards into the strip for 1.5 s (Stage's
drop-in, on every page, over the page's own cards), the moving one hot, each card with a pickup gauge under its value:
the value's bar, a tick where the fader stands; away: the bar in MID until the tick reaches it (`faders_page_env.png`,
`faders_levels.png`). A header tag FADERS says the cards are the faders' and not the knobs'. Then the page's cards come
back. A fader on a bank the engine lacks does nothing (its card reads "--").

Momentary: NOTE REPEAT (the FM-1's LFO) held while a fader moves is a momentary edit, as a knob's (today's rule): let
go and the value before comes back, motion does not record it. Recording: fader edits go through motion capture as a
knob's (today); the LEVELS bank captures P_LEVEL as the GLO knobs do. Nothing changes in the DSP or the parameter
set; a bank only decides which existing parameter a fader reaches.

## 5. The SMK-only buttons

| Button | Tap | Held |
|---|---|---|
| KNOB BANK | next knob bank | the bank picker; + pad: pick (top row a bank, bottom row a pad mode); + `<` `>`: previous / next bank |
| FADER BANK | next fader bank | the same for the faders |
| `<` `>` | one SELECT detent (pages, patterns, the roll's note: whatever SELECT turns), as today; with a BANK button held: its bank | - |

PATCH held opens the menu (HOME held). LEDs: KNOB BANK lit off PAGE, FADER BANK lit off LEVELS (today's rule), both
blink while held (the layer's blink). SEQ PLAY's two-colour LED: green as PLAY, its red unused (as today).

## 6. Wheels, pressure, velocity, the outer keys

- Pitch wheel: the selected track's bend (2 semitones; PROPHET its BEND, FM6 its range): as today.
- Mod wheel: CC1 / the MOD matrix's MODW source, the engines' own wheel where they have one: as today.
- Pad pressure: channel aftertouch to the selected track in KEYS (the matrix's AT source; nothing unless a MOD slot
  uses it, so no sound changes by itself). DRUM and STEPS: none in phase 1 (open: pressure on a held step = its
  velocity).
- Velocity: the keys' and pads' velocity as today (the stock curves from the calibration record). Open: a SETTINGS
  row for the pads' curve (soft / normal / hard) if the stock curve feels wrong.
- The 10 outer keys: notes, always (no lights, so no shortcuts).

## 7. Screen changes

All composed from what exists; no new component, no hint rows, no text panels:

1. **Header tags** (ui_draw.c `draw_head`): the knob bank's name, FADERS while the faders' cards are up, the pads'
   mode while set by hand. Same place and face as HOST / BLE; priority under ERASING, WAIT LOOP and messages; the
   octave pill gives way. Bank changes are brief header messages (the existing `ui_say`).
2. **The strip** shows the bank's cards (`page_over`) or, for 1.5 s after a fader moves, the fader bank's cards with
   the pickup gauge (a 2 px bar and a tick under the value, inside the card: `draw_column` gets an optional pickup
   argument). Stage: the drop-in it already has.
3. **The bank picker**: the existing picker card (`pick_draw`'s) with the bank names, over the two left columns,
   while a BANK button is held.
4. **Stage's sheet**: a **Pads** row (AUTO / KEYS / DRUM / STEPS / PATTERNS), after Add / Remove track.
5. **SETTINGS**: no new rows in phase 1 (the pad velocity curve later, if wanted). KEYS LIGHTS has no keys to light on
   the SMK: hidden there (it still dims idle buttons; keep that under a BUTTONS LIGHTS name, or drop).
6. Everything else (PATTERNS, NOTES, the grid, the layers, NAME, the dialogs) is unchanged: the pads mirror it.

## 8. Open questions (for Kerem)

1. PATTERNS pads: tracks + 8 patterns (proposed, mirrors the screen) or the 16 patterns of one track (today's SEQ-held
   map, no track pads)? A third option for later: a 4 x 4 launcher (four tracks x four patterns) as a pad mode of its
   own.
2. The pad-mode pick: BANK held + bottom-row pad (proposed; the top row is banks, the bottom row modes, both lit so
   they invite a press) or a dedicated gesture (`<` + `>` together cycles the mode; both arrows then lose the SELECT
   detent only while together)?
3. The hue: the selected track's colour everywhere (proposed; the pads and the screen agree) or today's fixed blue with
   track colours only where several tracks show?
4. STEPS on a synth track: tap = place / cursor and hold-then-tap = clear (proposed) or a plain toggle (tap on a step
   with notes clears it)? The toggle is faster and riskier; EDIT held undoes either.
5. The drum grid: other lanes' hits ON (proposed: the groove shows) or only the selected lane (the FM-1's map)?
6. Bank contents for CZ-1 and FM6 (FILTER, ENV): the rows in section 3 are a first guess; FM6's ENV could instead be
   the four macro knobs it already exposes.
7. Fader PAGE bank: should SELECT changing the page change what the faders do silently (proposed: yes, with the cards
   showing on the next move), or should the faders hold their last bank until FADER BANK is touched?
8. Pressure on drums / steps, and a velocity-curve setting: later phases or never?
9. KEYS LIGHTS on the SMK: hide, or rename to the button lights it still controls?

## 9. Implementation plan

Budget on the SMK build (2026-11-03): about 7 KB flash under `BODY_MAX`, 824 B of `.data` + `.bss` (big zero-init
buffers go to `.pool`). Estimates below are from the size of the code they resemble (`smk_faders` ~600 B,
`smk_leds_out` ~300 B, the layers' maps, `stage_page`); each phase is measured against `build/melodee-smk37.bin`
before it is merged.

**Phase 0 (this): design and mocks.** `docs/SMK37_UX.md`, `docs/smk37-ux/`, `tests/smk_ux_mocks.c`,
`tools/smk_ux_mocks.py`. No firmware change.

Built 2026-11-03 (measured on the Elite build, image bytes over the 566,144 B baseline): phase 1 +2,720 B (RAM
+48 B), phases 2 and 3 together +2,808 B (RAM +96 B): 571,672 B in all, 680 B of data RAM left. The pads as the only
pattern keys (later that day): 572,092 B, 664 B of data RAM left. The host suite's UI tests and render lint
pass for the FM-1 build; the SMK UI is host-tested through `tests/ui_test.c` built with `BOARD_SMK37` (a stub of
`hal/smk_input.h` in it: the pad queue, the pads' colours), the keys-play-pads-switch gestures and lights; nothing
has run on the Elite.

**Phase 1: banks (knobs and faders), ~2.0 KB flash, ~16 B RAM.**
- `firmware/src/ui.c`: `bank_page(bank)` building a `page_t` per engine (as `stage_page`), the shared table; the knob
  bank as `page_over` while not PAGE; the bank state (two bytes) and the fader pickup state (four bytes + flags).
- `firmware/src/ui_input.c`: KNOB BANK / FADER BANK tap and hold, the picker (the existing `pop` POP_PICK with a bank
  descriptor), `<` `>` while held; the LEDs; the layer blink.
- `firmware/src/main.c` `smk_faders`: the bank table instead of the four cases, soft takeover, the 1.5 s cards.
- `firmware/src/ui_draw.c`: the header tags, the pickup gauge in `draw_column`, the fader cards through Stage's
  drop-in on every page.
- Tests: `tests/ui_test.c` cases behind `BOARD_SMK37` need a host stub of `hal/smk_input.h`'s pads and faders (none
  today: the UI tests build as the FM-1); `tests/ui_render.c` scenes `smk_bank_*` from the mocks; the lint.
- Done: the mocks `bank_*`, `faders_*`, `pick_*` match the device.

**Phase 2: pad modes, ~2.5 KB flash, ~24 B RAM.**
- `firmware/src/ui_input.c` `smk_leds_out`: the mode's lights (KEYS from `play_key_led`, DRUM from the lane flashes
  and the drum selected, STEPS from `grid_leds` / the roll's steps, PATTERNS from `pattern_leds` remapped to
  tracks + a page of patterns), the track hue (`T_TRK` RGB565 -> 8-bit), the mode state and AUTO.
- `hal/smk_input.h`: a third pad layout beside `smk_pad_grid` / `smk_pad_piano`: the DRUM map (pad -> drum note) and
  the PATTERNS map (pad -> track / pattern), set by the UI; pad events carry the pad number where the map is not a key.
- `firmware/src/ui_input.c`: the STEPS taps on a synth track (place / cursor / clear), the PATTERNS taps (the pattern
  key handlers on the bottom row, the track keys on the top row, the dialog's two pads).
- `firmware/src/ui_popup.c`: the Pads row on Stage's sheet; the header tag for an override.
- Done: the mocks `stage_*`, `notes_steps`, `drum_steps`, `patterns`, `seq_held`, `pads_override` match the device.

**Phase 3: the momentary maps, ~1.0 KB flash.**
- GLO, SCL (the piano layout exists: `smk_pad_piano`), EDIT maps on the pads; NAME and FX as today.
- Done: `layer_glo`, `layer_scl` match; the EDIT map by eye.

**Phase 4, later:** pressure on drums and steps, the pad velocity curve, the 4 x 4 launcher, SONG's sections on the
pads, pads as the 16 drums while on NOTES (two-handed step entry).

Flash risk: phases 1-3 add about 5.5 KB against about 7 KB free, so the SMK build should first shed what it cannot
use: the FM-1 key-light planes and KEYS LIGHTS (`fm1_led_dim`, `LIGHTS_MASK`, the menu row), the FM-1 calibration
texts, the SELECT-detent arrows once the banks own them while held (~0.5-1 KB together); the XIP probe parked on
`feat/xip-probe` (image 581 KB -> 485 KB) is the larger lever if the Pro / Elite build keeps growing. RAM is not the
constraint (under 50 B of state; the pad maps are `const`).

Order of work after Kerem's pick: phase 1 first (it changes nothing the pads do and can ship alone), phase 2 as one
commit per mode, each flashed and tried on the Elite before the next.
