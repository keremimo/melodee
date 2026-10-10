# Melodee redesign: references and parity

The redesign is built from the proposal's mockups, screen by screen, and checked against them.

- `mock/`: the proposal's mockups as they were drawn (HTML + SVG).
- `ref/<screen>.svg`, `ref/<screen>.png`: one mockup screen each, at the device's 240 x 240 pixels in the bundled
  Rubik. `tools/mock_refs.py` regenerates the PNGs (headless Chrome).
- `tests/ui_render.c` draws the same states on the firmware's own code (`ref_*` scenes, with the device's sounds);
  `tools/ui_mockcmp.py build/ui_new build/ui_mock` puts each beside its mockup (mockup | device | difference) and scores
  it: colour (8 x 8 blocks within 32 of the mockup's), layout (F1 of the edges, 2 px apart allowed), parity (their mean).
  `tests/run_tests.sh` writes the report to `build/ui_mock/report.txt`. A phase is done when its screens reach the
  target below and the side-by-side images are approved.

| Screens | Mockup | Baseline (0.13 + phases 1-5) | Target | Built |
|---|---|---|---|---|
| stage_held, stage_cutoff, stage_drum | `stage_columns` | 57-62 | 85 | R2: 90-93 |
| browser | `concept_screens` (2) | 48 | 80 | R3: 93.5 |
| patterns | `concept_screens` (3) | 48 | 85 | R4: 96.7 |
| song | `concept_screens` (4) | 38 | 80 | R4: 94.7 |
| env, lfo | `r5_pages` | (new) | 80 | R5: 83.4, 92.2 |
| edit_osc, fx, dly | `r5_pages` | (new) | 80 | R5: 93.0, 95.2, 84.0 |
| mixer | `r5_pages` | (new) | 80 | R5: 95.1 |
| notes | `r5_pages` | (new) | 80 | R5: 66.1 (the view centres the notes a row apart) |
| settings, dialog | `r5_pages` | (new) | 80 | R5: 94.5, 88.1 |
| scl_list, arp, pattern, slicer | `r6_pages` | (new) | 80 | R6: 89.2, 88.3, 83.1, 70.3 (the patterns' data) |
| fmeg, grid, scales | `r6_pages` | (new) | 75 | R6: 77.0, 81.7, 70.5 (the families' names) |
| mod, motion | `r6_pages` | (new) | 75 | R6: 71.9, 54.4 (no chips: R6 popups) |
| sheet_sound, sheet_song, sheet_project | `r6_popups` | (new) | 75 | R6: 76.9, 84.4, 92.9 |
| sheet_note, sheet_hit, sheet_motion | `r6_popups` | (new) | 75 | R6: 79.0, 70.6, 74.9 |
| picker_wave, picker_mod | `r6_popups` | (new) | 65 | R6: 68.7, 58.9 |
| sections_p5, map_p5, map_fm6 | `r7_sound_nav` | (new) | 60 | R7: 73.0, 62.0, 64.9 |

NAME, NEW SONG, About and the GLO layer follow `r6_pages` by eye (no ref scene: their texts are the device's).

Texts differ on purpose (the device's own sound names): the targets leave room for them.

## Tokens (from the mockups' code)

Colours: background `#17141F`, panel `#1D1926`, surface `#221E2C`, raised `#262033` (the selected lane), quiet lane
`#1A1722`, line `#2E2939`, text `#F3EEF8`, mid `#9A93A8`, dim `#5A5468`, secondary names `#C9C2D6`. Tracks: coral
`#FF7A5C`, amber `#FFC145`, mint `#46D9B0`, lilac `#9D8CFF`. On a filled track colour: background-coloured text.

Type (Rubik): labels 8.5-9 px / 500, capitals, in the track's colour; names 10 px (selected: 500); header 11 px / 500;
values 15 px / 500 with 9 px units in mid; the chord root 30 px / 500, its quality 17 px / 500, the notes 11 px / 500.

Stage (`stage_note_overlay`), all positions in device pixels:

- Header, 18 px: play glyph at x 8; BPM 11 px / 500 at x 20; four track dots (r 2.5, x 54 + 8 k, y 11: the selected
  white, the others dim); at the right a context chip 82 x 14 r 4 at x 150, filled in the track's colour, its text
  9 px / 500 in the background colour (the engine: PROPHET-5, 909 KIT ..).
- Cards: 54 x 44 at x 4 + 58 k, y 22, r 6, surface; label 8.5 px at baseline 12, value 15 px at baseline 30, unit after
  it, gauge 42 x 3 r 1.5 at y 36 (line, filled in the track's colour). The knob turning: a fill of 22 % track colour
  over the surface, outlined in the track's colour.
- Panel: x 4, y 70, 232 x 82, r 6, panel colour. The waveform 2 px, smooth, in the track's colour (dimmed to 35 % while
  notes are held); the chord in front at x 14 baseline 100, the notes at baseline 120. A knob turning: what it shapes
  (the filter's curve, its cutoff dashed) in place of the waveform, its value at the top right, 13 px.
- Lanes: y 156 + 21 k, 232 x 19, r 5; the selected one raised and outlined in its colour, the others quiet; a 3 px
  stripe in the track's colour at the left; the number 10 px / 500 in the track's colour at x 13; the name 10 px at
  x 25 (selected: text, 500; else secondary); the pattern chip 18 x 12 r 3 at x 104 (selected: filled, else outlined),
  8 px / 500; 16 steps 4 x 8 r 1 from x 128, 5.6 apart (on: the track's colour, the playhead text, off: line); the
  level at x 222: a 3 px bar over a 10 x 2 line.
- Drum track: the lanes BD SD CP CH OH RS as chips 32 x 14 at y 132 (hitting: filled), the waveform dimmed above.

## Navigation (R6)

One way to move on every screen with something to move through (Kerem, 2026-10-10):

- **KNOB 1 across, KNOB 2 up / down**: the cursor (a step, a cell, a column), the row (a slot, a lane, a route, a
  setting); KNOB 3 / 4 edit what the cursor is on where a page has more.
- **OCT+ Enter, OCT- Esc** everywhere but Stage (Kerem, 2026-10-10: the octave is Stage's alone; the sound pages,
  NOTES and drum STEP, recording live included, navigate too). Esc closes the innermost thing first: a question (No),
  a popup, an action picked, a sub-screen (SCALES: back to SCL, About, a name, a pending browse), and only then goes
  to Stage. Enter: a list row's list, a slot's sheet (PROJECT, USER, the STOREs), the action picked, a sound page's
  sheet; NOTES: an empty step gets the last note played, a note opens its sheet (Length, Velocity, Chance, Slide,
  Delete note); the drum grid: an empty place gets the lane's hit, a hit opens its sheet (Accent, Chance, Clear hit:
  hits have no velocity of their own). OCT+ held on NOTES, PATTERN: the pattern's sheet (Clear pattern,
  Clear motion). A clearing row with nothing there says so instead of asking.
- **SELECT** turns the pages, as before, NOTES too (Kerem, 2026-10-10): there KNOB 1 alone moves, the steps one by
  one (empty ones too) and a step's notes one at a time (a chord's, a take's), back onto the step before's last note.
- **Direct pages stay direct**: one knob per track where speed matters (Stage, MIXER, PATTERNS, NEW SONG's roles);
  the sound pages' four knobs edit their four values. PATTERNS: OCT- clears what is queued; OCT+ the selected
  track's pattern's sheet (Copy to.., Delete pattern; Kerem 2026-10-11). Copy to: the place it goes framed in the
  theme's colour on the track's row (the first empty one first, an empty place a "+"), the header "copy 1 to 4"; any
  knob moves it, OCT+ copies (over a pattern in use: "Replace pattern 2?" first), OCT- leaves. Delete pattern asks,
  then empties it (its notes and motion; SAVE held undoes it).

| Screen | KNOB 1 | KNOB 2 | KNOB 3 / 4 | OCT+ | OCT- |
|---|---|---|---|---|---|
| Browser | list (category) | sound | FAV / engine | keep | back (revert) |
| Settings | value | row | - | open / step | close |
| PROJECT, USER, STORE | slot | slot | - | the slot's sheet | Stage |
| SONG | section | track | pattern / repeats | play / stop | Stage |
| PATTERNS | track 1's pattern | track 2's | track 3's / 4's | the pattern's sheet | the queue, Stage |
| PATTERNS: Copy to | the place | the place | the place | copy there | leave |
| MOD | source (its picker) | route | destination / amount | - | Stage |
| MOTION | Play on / off | lane | - | the lane's sheet | Stage |
| NOTES | step | pitch | length / velocity | place a note | Stage |
| Drum STEP | step | lane | hit / accent | set the hit | Stage |
| SCALES | family | scale | - | pick | Stage |
| NAME | place | letter | - | save | cancel |
| NEW SONG | (as its page) | | | next / create | back |

TAKE JAM: REC held on SONG (elsewhere REC held captures).

## Popups (R6)

- **Action sheet**: OCT+ on an item opens what can be done with it (KNOB 2 the row, KNOB 1 a row's value, OCT+ does
  it, OCT- closes); OCT+ held opens the page's own (a ⋯ in the header says there are some). A destructive row asks the
  question first. TOOLS is gone, its actions in these (Clear pattern: NOTES / PATTERN; Init sound: the sound pages;
  Delete section, Clear song: SONG); PROJECT / USER / FM6 STORE / P5 STORE are slot lists (no chips), a slot's sheet:
  Load, Save here, Rename, Boot, Erase (PROJECT; Erase asks); the STOREs: Save here, Send, Init sound. SAVE opens
  USER on its slot, no sheet popped up (Kerem, 2026-10-10); OCT+ its sheet, on Save here (SAVE, OCT+, OCT+, OCT+). MOTION: no chips, its lanes (a parameter each,
  scaled to what it moves through, the playhead across); a lane's sheet: Play, Clear <lane>, Clear all motion. MOD:
  the four routes as rows (source -> destination, the amount's bar from the middle), the knobs' chips at the bottom.
- **Picker**: a knob whose value is a list (wave, MOD source / destination, chord, delay division) shows the list
  around its value while it turns, gone ST_KNOB_MS after the last turn; over the page as it is, not dimmed (2026-10-10:
  it comes with every turn of such a knob, and dimming sent the whole screen twice). SCALES keeps its own list page
  (Kerem: the list is intuitive), no picker.

## Drawing and the LCD (2026-10-10)

- The SPI to the LCD (12 MHz) is the UI's bottleneck: the whole screen takes ~77 ms, 1000 px ~1.3 ms. gfx.c sends a
  big canvas drawn where it was drawn before only where it changed (4-row bands x 8-column blocks, hashed), and while a
  frame is drawn its fills wait: the canvases drawn over them later take their place out. A page redrawn whole
  (ui.force) so sends what changed. tests/ui_render.c's LCD load audit turns every knob of every page of every engine
  and lists the worst; each of its frames is checked against sending everything whole.

## Pages: lists or knobs (R6)

- **List pages** (set and leave): a row a setting (KNOB 2 the row, KNOB 1 its value, OCT+ a list value's full list,
  OCT- back), the page's picture under the rows where it has one. SCL (OCT+ on Scale: the SCALES list), CHORD, VOICE
  1-3 (one list), MPC, ENV DEST, LFO DEST, LFO 2, ARP 2, DLY 2, FM6's LFO and controller pages (a list each).
  TEMPO, GLOBAL, SYSTEM stay knobs (BPM and TUNE are turned while playing, their digits roll); TEMPO's third knob is
  the selected track's Quantize (TIMING's page retired, Kerem 2026-10-10).
- **Knob pages** (played while tweaked): four rings over the page's picture (R5): the engines' EDIT pages, ENV, LFO,
  DLY, REVERB, CHORUS, FX, SLICER, ARP, PATTERN; MIXER, Stage, PATTERNS direct.
- **ENV / LFO follow the engine** (Kerem, 2026-10-10): the Prophet, FM6 and CZ-1 have envelopes and LFOs of their own
  (the track's ADSR and ENV DEST do nothing there), so ENV visits theirs (P5 FLT ENV, AMP ENV, ENV MOD; FM6 EG RATE,
  EG LVL, PITCH EG, PITCH LV; CZ-1 each line's pitch, wave and amp envelope) and the track ADSR hides; LFO visits
  theirs first (P5 LFO, WHEEL; FM LFO; CZ VIBRATO), then the track LFO (it modulates any engine through LFO DEST
  and is the matrix's LFO) and MOD. SID (its ADSR: the chip's rates), DRUM, SLICE keep the track's.
- Defaults (Kerem, 2026-10-10): OCT+ (tapped, or held about 0.7 s) opens a page's own sheet; CHANCE lives in
  the note / hit sheets (its page retired, Kerem 2026-10-10); a picker closes ST_KNOB_MS after the last turn or on OCT-.

## Sound pages, fast (R7: mock/r7_sound_nav)

- **Sections**: an engine's pages in sections (Prophet: Osc, Mixer, Filter, Amp, Mod, LFO, Wheel, Voice; FM6:
  Algorithm, OP1-OP6, Pitch, LFO, Controls; CZ-1 ..). PRESETS jumps a section (to its last-used page), SELECT turns the
  pages (on across the sections). The sections as tabs under the header, the pages of one as the header's dots.
- **The map**: EDIT pressed on an EDIT page: a row a section, a tile a page (FM6: operators x their pages); KNOB 1 / 2
  move, OCT+ opens, OCT- closes.
- **EDIT held**: the sections on the white keys; Init, Favourite, Undo, Store on the black keys. The engine is chosen
  in the browser only (KNOB 4; Kerem, 2026-10-10).
- **Init sound** (Kerem 2026-10-11: sound design from scratch): the engine's own INIT, nothing changed and dry:
  INIT PROPHET, INIT TONE, INIT SID, FM6's INIT VOICE (the DX7 init voice, a preset after F24 so the stores keep the
  factory's numbers); DRUM its 808 kit. The browser's KNOB 4 (the engine) loads the engine's INIT, and each engine's
  INIT comes first in its sounds there; Init sound (the sound's sheet, EDIT held's black key) the engine's own.
