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

Knob shades (`gfx.c` T_KNOB, `tools/gen_ui_palettes.py` SHADES): a track's pages are not one colour. Each knob has a
shade of the track's colour, the track's own for knob 1, then turned round the OKLCH hue circle (+32, -32, +64
degrees). A knob's label, its value while it turns and the curve segment it moves take it (ENV: attack,
decay, sustain, release; FM6 EG: R1..R4; LFO: the wave WAVE's, the reach FADE's). Rows in turn take the four (lists,
Settings, mod slots), as do drum lanes, a pattern's beats, ARP's octaves and the roll's notes (by octave); the browser
gives each engine one and the scale picker each family. Palettes: NIGHT, DAY, CONTRAST, SUNSET, OCEAN, NEON, CANDY,
LO-FI (muted: dusty rose, mustard, sage, faded denim on warm charcoal), each with its own four track colours; every track colour and shade passes the generator's contrast checks.

Type (the mockups'; on the device since 2026-10-14 the large type below sets every size and position): Rubik labels
8.5-9 px / 500, capitals, in the track's colour; names 10 px (selected: 500); header 11 px / 500;
values 15 px / 500 with 9 px units in mid; the chord root 30 px / 500, its quality 17 px / 500, the notes 11 px / 500.
On the device the small faces are a pixel larger for legibility: X 10 px (the 9 px labels, units, chips) and S 12 px
(the 11 px names, values, rows), each set on the smaller size's baseline and line height (tools/gen_aa_font.py lifts
them a row), so every position below holds; the mocks keep 9 / 11 px.

Stage (`stage_note_overlay`), all positions in device pixels:

- Header, 18 px: play glyph at x 8; BPM 11 px / 500 at x 20; then, only when they apply, the REC mark (12 px, REC
  red: filled, the selected track armed; a ring, another track armed), the octave shift as an outlined pill ("+2",
  "-1") and a song's row ("ROW 3"), then (a page) its dots; at the right a context chip 82 x 14 r 4 at x 150, filled
  in the track's colour, its text 9 px / 500 in the background colour (the engine: PROPHET-5, 909 KIT ..). No track
  dots: Stage's columns say which track, a page's sound bar starts with the track's numeral (filled in its colour),
  then the engine and the sound.
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

Tracks in use (2026-10-13, `firmware/src/tracks.c`): one to four, always from track 1. Stage's columns share the
four's place (x 4 .. 236): two 114 px (the sound 15 px), three 74 px (5 px apart); every column's content centred, the
four's too (Kerem 2026-10-13; an armed column's role keeps clear of its REC dot); one
track is the synth build's card across it, centred (role 12 px, the sound 30 px, the pattern and ARP / HOLD / CHORD /
scale as pills each in a track colour, the four colours' bars, the 16 steps in a row, the level as the big ring; a
track with no pattern at all, none waiting, not armed: no pattern pill, no steps, the ring in the middle). MIXER's
strips follow Stage's columns (centred); PATTERNS and SONG draw the rows in use; GLO blanks
the keys and cards of the others. Stage's sheet ends with **Add track** (the next, its value T2..; the browser opens
on it) and **Remove track** (the selected; red, its question "Remove track 2?"). NEW SONG's roles start at `--` (no
track). Renders: `stage_1track*` (`_synth`: no pattern), `stage_2tracks`, `stage_3tracks`, `sheet_stage`, `confirm_track`, `mixer_*track*`,
`patterns_2tracks`, `song_2tracks`, `layer_glo_2tracks`, `new_roles_1track`.

Tracks 5–8 (2026-10-24): Stage’s Add track continues to eight; these extra tracks are always MIDI Track and open
on CH/TRS instead of the sound browser. ALGORITHM cycles all tracks. Stage, MIXER, PATTERNS, SONG and GLO show the
selected track’s group (1–4 or 5–8), reusing the four colours and displaying the actual track numerals. With SEQ held,
the first eight black keys select tracks 1–8 directly. GLO keeps mute/solo and shows CH in place of an audio
level for MIDI tracks. Partial groups leave unused places blank. Renders: `stage_midi`, `stage_midi_partial`,
`mixer_midi`, `patterns_midi`, `song_midi`, `layer_glo_midi`.

PATTERNS keyboard (2026-10-25): track/pattern selection is the default while the selected track is unarmed,
with the same key map and lights as holding SEQ: the first eight black keys select tracks 1–8 directly,
regardless of the displayed group. Unused track keys stay dark; pressing one creates the missing tracks through
that slot and selects it immediately, staying on PATTERNS. Tracks 5–8 are always MIDI-only. Keys are silent in selection mode.
Arming restores normal playing immediately, including while stopped, during count-in and while waiting for loop zero; disarming or
finishing a single pass restores selection. First note starts an armed stopped take immediately; PLAY uses the
configured count-in. SELECT alone still navigates to SONG; SEQ + SELECT chooses a pattern. Other SEQ pages keep
their existing keyboard behavior.

The pattern-key action dialog keeps half/double available after each resize while its original pattern key or SEQ
is held, so repeated presses work without reopening it. After a resize it closes when both are released. Without a
resize, releasing the pattern key leaves the dialog available for the existing delete/copy gesture or OCT- cancel.

PATTERNS PRESET (2026-10-25): the PRESET knob browses the active audio track's sounds using its existing browser list
and acceleration; tracks 5–8 report `MIDI TRACK` and keep their settings. An undimmed value-picker card shows
`T1 PRESET`, the selected sound's name (including a pending fast turn), its engine and `PLAY KEYS TO TRY` immediately.
This is a temporary edited value below the shared header; transport and REC keep their normal priority above it.
While the card is open, unarmed keys audition instead of selecting tracks/patterns; holding SEQ explicitly restores
selection. Each preset turn or held live input restarts its two-second idle timer; sequencer playback and release
tails do not prolong it. On expiry, OCT-/OCT+, track or page change, the card closes and ordinary key ownership
returns; sounding keys retain their original note-off ownership. Recording remains available as usual when armed.

HOME's second page (2026-10-13, `firmware/src/ui_view.c`; HOME tapped on HOME, two page dots in the header; HOME
from elsewhere returns to the page it was on; SELECT is not a page turn on HOME: it turns the menu's HOME SELECT, a
header message saying the value): a wheel of the twelve notes in fifths centred (160 x 158
at 40, 19; the key's notes marked, its root larger), the notes sounding on the tracks in use (not DRUM) lit in their
track's colour and joined into the chord's shape, the chord's root ringed; in the middle the chord (root L, quality M
on its baseline, the notes S in the selected track's colour), one note L, nothing yet the key (M, mid); let go: the
last notes faded. Track pulses (32 x 32) in the wheel's corners, the waveform (rows 179 .. 204), keys C2 .. B5 (rows
208 .. 237). Stage's knobs drop in over the wheel's top. Renders: `view`, `view_letgo`, `view_note`, `view_empty`,
`view_knobs`, `view_slash`.

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
  then empties it (its notes and motion; EDIT held undoes it).

| Screen | KNOB 1 | KNOB 2 | KNOB 3 / 4 | OCT+ | OCT- |
|---|---|---|---|---|---|
| Browser | list (category) | sound | FAV / engine | keep | back (revert) |
| Settings | value | row | - | open / step | close |
| PROJECT, USER, STORE | slot | slot | - | the slot's sheet | Stage |
| SONG | section | track | REST or pattern / selected block length | cell actions / apply destination | cancel, then Stage |
| PATTERNS | track 1's pattern | track 2's | track 3's / 4's | the pattern's sheet | the queue, Stage |
| PATTERNS: Copy to | the place | the place | the place | copy there | leave |
| MOD | source (its picker) | route | destination / amount | - | Stage |
| MOTION | Play on / off | lane | - | the lane's sheet | Stage |
| NOTES | step | pitch | length / velocity | place a note | Stage |
| Drum STEP | step | lane | hit / accent | set the hit | Stage |
| SCALES | family | scale | - | pick | Stage |
| NAME | place | letter | - | save | cancel |
| NEW SONG | (as its page) | | | next / create | back |

SONG arrangements hold 64 sections. PLAY starts at step zero of the selected section with its full repeat count,
continues to the end, and stops; PLAY while running or armed stops/cancels. The add-section column starts at section 1.
Starting inside a continued block seeks that block to the selected section, including held and recorded notes and motion.
Moving the cursor during playback does not seek. OCT+ opens the selected cell's sheet (Set rest, Copy to…, Move to…, Add empty section),
or inserts an empty section at the add column. Copy/move choose a destination on the grid; OCT+ applies, OCT- cancels.
Copy/move acts on the entire selected block, including its continued sections, and keeps the note pattern shared.
Moves leave REST at the source. Occupied destinations must be cleared first.
Hold OCT+ for section insertion, duplication, movement, deletion and the existing song actions. Hold OCT+ and turn
KNOB 1 (or SELECT) to select consecutive sections; tap OCT+ for phrase copy, move, duplicate and delete. Section copy
and move insert before the destination; duplication inserts after the source. EDIT held undoes up to eight song
edits; EDIT + OCT-/OCT+ undo/redo. History is acquired on demand and may be reclaimed for audio.
Rests display `--` and suppress sequence notes, recorded notes and motion while allowing releases and FX tails.
Pattern starts on track 1 retain the existing section timing; a rest or continuation on track 1 uses one straight
four-beat bar per retained section repeat. Growing/trimming a block therefore preserves following section times.
The ruler above the blocks shows elapsed minutes:seconds; the footer shows the section position and selected block length.
KNOB 4 grows/shrinks that block into following sections on its own track. Continued sections do not restart notes,
recorded playback or motion. Shrinking leaves rests; extension appends empty sections if needed, and stops at an
occupied block. The underlying note pattern stays unchanged. Section timing/repeats from existing projects are retained. Persistent destination/range status stays
in the shared header until applied/cancelled or leaving SONG. Transport remains on PLAY.

Loop song is available in the tapped and held OCT+ sheets and is saved per project (default Off). Set it while
stopped. A looping song returns from its final section to section 1 with the residual sample phase preserved,
even if PLAY began at a later cursor section. PLAY still stops and restores the original patterns. The shared
header shows LOOP on SONG while enabled, and SONG LOOP during playback on every page. The web editor exposes
the same stopped-only setting when bank capability version 4 is available.

SONG block presets: while stopped, select a pattern block and track, then OCT+ > Set preset.
The normal browser auditions that track; OCT+ saves an independent sound copy across the
whole selected block and returns to SONG, OCT- cancels. Selecting any continued section
edits the same block. Navigation away also restores the normal track sound.
Remove preset clears the whole block. The assignment includes native FM6, CZ, Prophet
and drum data; overwriting a library preset does not change it. Other occurrences of the
same note pattern retain their own sounds. A 64-step block spanning four 16-step sections
keeps its sound through all four; unassigned blocks and STOP restore the normal track sound.
Growing a block extends its sound assignment; trimming clears it from the removed sections.
Track mix, FX routing, scale, arp, notes, timing and motion lanes stay track-owned.
Changing between block sounds releases the old sequence notes; continued sections of a
block retain held notes and automation without reloading the sound at each section edge.
Section and cell copy/move, section duplication/deletion, project backups and song undo include
the sound assignments. Copies share an immutable song snapshot; replacing one cell leaves
other cells unchanged. Up to 64 distinct snapshots can live in a project/history, subject to
available edit memory and the existing PROJECT FULL save limit. Snapshots allocate memory on
demand; playback boundaries do no allocation or library reads. Tracks 5–8 remain MIDI-only.
The shared header shows SET PRESET through browser selection, then the selected cell's sound
name on SONG (LOOP and transport statuses retain priority); the cell sheet title also names the assignment.

TAKE JAM: REC held on SONG (elsewhere REC held captures).

Timed master punches: tap FX on SONG to open the selected section's punches directly; OCT+ > Punch FX also opens it.
Keys choose an effect silently; KNOB 1 follows the same physical key order. The selected effect is named
above four lanes, each labelled with its slot and effect name. Each lane shows the section's numbered step grid;
coloured cells show the applied range and a white outline marks the selected punch. KNOB 2 selects a slot,
KNOB 3 moves START, and KNOB 4 sets END (the last included step). Both values are one-based step numbers:
START 5 / END 8 colours steps 5 through 8 directly. No beat fractions or duration subtraction are needed.
SELECT browses sections, OCT+ selects the next free slot. KNOB 1 has OFF before the first effect; selecting it
clears only the selected punch and its timing. EDIT also removes that punch, and EDIT held undoes either action.
OCT- returns to SONG. PLAY auditions from the selected section and stops again; edits remain stopped-only.
A playhead crosses the lanes while the displayed section plays. Holding FX on SONG retains the existing live
punch layer. Timing edits redraw only the changed lane and placement cards. Moving a punch retains its duration
at the section edge. SONG shows vertical effect-coloured spans through the filled part of each pattern block,
clipped to its exact rounded outline. Repeat ranges repeat within the section; overlapping spans use the same
latest-start/slot priority as playback. Pattern labels and the selection rim stay in front. Empty, dashed and
rest cells receive no effect colour. The footer keeps its normal section/length information and progress bar;
effect names and numbered slots remain in the punch editor.
The first track's pattern defines the step division, swing and section length; rests and continuations use
16 straight sixteenth-note steps. Timing restarts on every section repeat, including a punch spanning the
whole section. Overlaps use the latest start, with the later punch slot breaking ties; an earlier punch
resumes if its duration remains. Events outside a subsequently shortened pattern do not play; their stored
timing is retained. These are master effects, shared by all tracks, and are local to the song section;
referencing the same note pattern elsewhere does not copy them. Section copy, move, duplicate, delete and
song undo include the FX lane. Cell operations change only the selected track's pattern reference.
Song punches do not replace the footer or shared header with effect names or FX status indicators. A live FX key overrides
the lane until released. Buffered song effects warm their history from PLAY and keep the optional 32 KiB
history until STOP or song end. Filter-only songs require no punch history. Editing remains stopped-only.
Projects with punches use FBKN; projects without punches keep their existing compatible formats.

## Popups (R6)

- **Action sheet**: OCT+ on an item opens what can be done with it (KNOB 2 the row, KNOB 1 a row's value, OCT+ does
  it, OCT- closes); OCT+ held opens the page's own (a ⋯ in the header says there are some). A destructive row asks the
  question first. TOOLS is gone, its actions in these (Clear pattern: NOTES / PATTERN; Init sound: the sound pages;
  Delete section, Clear song: SONG); PROJECT / USER / FM6 STORE / P5 STORE are slot lists (no chips), a slot's sheet:
  Load, Save here, Rename, Boot, Erase (PROJECT; Erase asks); the STOREs: Save here, Send, Init sound. SAVE opens
  USER on its slot, no sheet popped up (Kerem, 2026-10-10); OCT+ its sheet, on Save here (SAVE, OCT+, OCT+, OCT+). MOTION: no chips, its lanes (a parameter each,
  scaled to what it moves through, the playhead across); a lane's sheet: Play, Clear <lane>, Clear all motion. MOD:
  the four routes as rows (source -> destination, the amount's bar from the middle), the knobs' chips at the bottom.
- **Picker**: a knob whose value is a list of names (wave, MOD source / destination, chord, delay division) shows the
  list around its value while it turns (a list of numbers, such as GLO's DRUM channel, FM6's algorithm or a CZ
  envelope step, steps on its card like its neighbours: Kerem, 2026-10-11), gone ST_KNOB_MS after the last turn; over the page as it is, not dimmed (2026-10-10:
  it comes with every turn of such a knob, and dimming sent the whole screen twice). SCALES keeps its own list page
  (Kerem: the list is intuitive), no picker.

## Status indicators (2026-10-15)

Ongoing device and transport state belongs in the **header, rows 0–29**. Draw it from the current state, keep it
visible while that state applies, and remove it immediately when the state ends. Do not use timed messages or
place status cards over knobs, lists, waveforms, or notes.

- **Left:** transport, BPM, then the REC mark. A filled REC mark means the selected track is ready/recording;
  an outline means it is waiting or another track is armed.
- **Right:** the active transport status replaces the page/engine title and optional navigation marks. Priority:
  `ERASING`, then `WAIT LOOP` (waiting for the selected track's loop boundary), then brief action feedback, then
  the normal context. Keep transport, BPM, and REC visible. Use REC colour for recording states.
- **Master punch:** keep the normal page/transport header during song punches and their wet fades.
  Show effect names in the punch editor; keep the song footer for position and progress.
  The live FX layer retains its own HOLD / LOCK / MEMORY context and names the selected key.
- **Optional context:** octave, song row, page dots, and the low-battery glyph use the remaining header space in
  the normal layout. Never overlap or truncate a status to preserve optional context. Use a concise, documented
  label that fits both Rubik and Melodee Pixel; `WAIT LOOP` is the header label for “waiting for loop”.
  The signed octave pill (`+2`, `-1`) shows the selected track's MPC bank octave while QNT is MPC;
  otherwise it shows the shared keyboard octave. It updates on track/mode changes and disappears at zero.
  In MPC mode the latest accepted pad note selects its track silently; use the normal track context and
  octave pill, without a “MIDI IN → T…” action message. Note-offs and ignored pads do not change selection.
  Normal channel-routed MIDI also produces no action message and retains the manually selected track.
- **Screens with their own headers:** while waiting, use the shared transport header; restore the screen's header
  on start, cancel, or a change to a track that is not waiting. Dialogs and pickers retain the header above them.

- **`HOST` (USB HOST, 2026-10-21):** the USB-C port is a USB-MIDI host (menu USB HOST). State: `uh.on`; lifetime: from
  switching USB HOST on to switching it off (or a power-cycle: it is never saved); priority: normal context, below
  `ERASING`, `WAIT LOOP` and messages; label `HOST` in the compact label font left of the page name and the
  low-battery glyph, taking its width from the page marks. Dim (`T_MID`) while no controller answers, theme colour
  while one is connected. A controller found, lost or not MIDI, and a low-speed device, are brief toasts.
  The optional octave pill gives way when the tag needs the room. Screens that draw their own header (PRESETS, SONG,
  MENU) do not show the tag yet; the USB HOST row in the menu shows the state there.
  A connected device may provide IN, OUT or both; the same tag covers a USB synth receiving MIDI tracks.
  Crowded headers use the label face, with `P. PHASER` / `P. CHORUS` as compact pedal-effect names when needed.

- **`BLE` (Bluetooth MIDI):** while the radio is on, the shared header keeps a
  BLE tag; setup, scanning, waiting for MIDI and failures replace normal context.
  A generated passkey appears as `PIN 000042` through its pairing lifetime,
  including leading zeros. `BLE WAIT` means the peer has not subscribed to MIDI;
  `BLE SETUP` means outgoing discovery/subscription, and `BLE TIMEOUT` a failed
  attempt held until user action. Transport, BPM and REC remain visible; erasing,
  WAIT LOOP retains its existing priority. Settings > Bluetooth
  uses the same header above its list. No ongoing BLE state uses a timed toast.


Brief action results (saved, copied, invalid action) use the existing header-message area; a long explanation may
wrap in a temporary toast. A question requiring a decision uses a dialog; a value being edited stays at its
control. These are feedback and interaction, rather than ongoing state indicators.

For each new indicator, specify its state, lifetime, priority, and compact label here, and extend the shared
header renderer instead of choosing a new position in a page. Verify both fonts, crowded headers, persistence,
and incremental redraws when the state ends or the selected track changes (`tests/ui_test.c`, `tests/ui_render.c`).

## Drawing and the LCD (2026-10-10)

- The SPI to the LCD (12 MHz) is the UI's bottleneck: the whole screen takes ~77 ms, 1000 px ~1.3 ms. gfx.c sends a
  big canvas drawn where it was drawn before only where it changed (4-row bands x 8-column blocks, hashed), and while a
  frame is drawn its fills wait: the canvases drawn over them later take their place out. A page redrawn whole
  (ui.force) so sends what changed. tests/ui_render.c's LCD load audit turns every knob of every page of every engine
  and lists the worst; each of its frames is checked against sending everything whole.

## Pages: lists or knobs (R6)

- **List pages** (set and leave): only where there are more than four settings (Kerem, 2026-10-11: four or fewer are
  the page's own four knobs). A row a setting (KNOB 2 the row, KNOB 1 its value, OCT+ a list value's full list, OCT-
  back), the page's picture under the rows where it has one: SCL (OCT+ on Scale: the SCALES list; with Quantise MPC
  a row for MPC degree, which had a page of its own), VOICE
  1-3 (one list), FM6's LFO and controller pages (a list each).
  TEMPO, GLOBAL, SYSTEM stay knobs (BPM and TUNE are turned while playing, their digits roll); TEMPO's third knob is
  the selected track's Quantize (TIMING's page retired, Kerem 2026-10-10).
- **Knob pages** (played while tweaked): four rings over the page's picture (R5): the engines' EDIT pages, ENV, LFO,
  CHORUS, FX, ARP, PATTERN; and the pages of four settings or fewer: CHORD (over its chord),
  ENV DEST, LFO DEST, LFO 2 (over the LFO), ARP 2 (over the notes it plays), DLY 2 (over the echoes); MIXER,
  Stage, PATTERNS direct.
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
- **The map**: EDIT pressed on an EDIT page: a row a section, a tile a page (FM6: operators x their pages); KNOB 1 (or
  SELECT) moves to the next page of the category, KNOB 2 to the next category (its last-used page), one per detent,
  every detent counted. OCT+ opens it, OCT- closes the map, HOME returns to Stage.
  For DRUM, KNOB 1 changes Sound's Kit / Hit selection without leaving Sound. One KNOB 2 detent moves
  directly to Drum (Drum sound / Drum mix), with Lanes below. A category starts at its first page until
  another page has been opened there; vertical navigation recalls that last-used page.
- **EDIT held**: undo; EDIT + OCT- / OCT+ undo/redo. Hold SAVE to save the current project. The engine is chosen
  in the browser (KNOB 4).
- **Init sound** (Kerem 2026-10-11: sound design from scratch): the engine's own INIT, nothing changed and dry:
  INIT PROPHET, INIT TONE, INIT SID, FM6's INIT VOICE (the DX7 init voice, a preset after F24 so the stores keep the
  factory's numbers); DRUM its 808 kit. The browser's KNOB 4 (the engine) loads the engine's INIT, and each engine's
  INIT comes first in its sounds there; Init sound (the sound's sheet) the engine's own.

## FX rack (2026-10-15)

FX opens the selected track's **rack**, laid out like MOD's rows: its three insert slots (1–3) and a fourth row for
the shared FX (an arrow, "Shared", the shared effects in order: a chain arrow between them, a comma in SPLIT). KNOB 2
picks the row (outlined and tinted in its knob colour), KNOB 1 its effect (the name picker), KNOB 4 its amount: a
slot's MIX, the shared row the track's SEND (in SPLIT the track's sends are SHARED's rows; the shared row then has no
amount). Empty slots read "--", dim. OCT+ opens the row: a slot's page; an empty slot its effects' list (OCT+ takes
one); the shared row SHARED. OCT- goes back up a level (slot → its rack, SHARED → FX, FX → Stage). FX pressed off the
rack returns to it; on the rack it turns on to the next page, as SELECT does.

**SHARED** is the same rack for the shared bus: its slots A–C (KNOB 4 their MIX; SPLIT: the selected track's send to
each) and RETURN (the four track colours as its numeral); KNOB 3 is ROUTE, Chain or Split (Kerem asked where it had
gone). Its sheet (OCT+ held, or OCT+ on RETURN) holds Routing too and Clear shared FX (asks first).

A **slot's page** shows while the slot holds an effect, so SELECT never stops on an empty one: FX, the slots in use,
SHARED, its slots in use. The header names the effect. Audio controls are MIX, RATE (ROOM: SIZE), the
effect's DEPTH / FOCUS / JITTER / BITE / DECAY / DAMP, and TONE (ROOM and REVERB).
The picture (no words) draws what the effect does: DRIVE's shaped wave, CRUSH's steps, FLANGER's comb, PEDAL PHASER's
notches, PEDAL CHORUS's voices, ROOM/REVERB's tail, NOTE ECHO's echoes (2026-10-24, canvas QDJMbY7BjJiwEL8WKK394N).
KNOB 1 on a rack shows the effect's family (Space, Motion, Dirt, Colour, Notes); the picker and the list (OCT+ on an
empty slot: KNOB 1 family, KNOB 2 effect) give the effects in those families. WIDTH says "Mono changes the tone", a time effect without its
buffer "MEMORY FULL". No hint rows. A shared slot's footer is "All tracks" with the four track colours. These controls
belong to the project and track, never the instrument preset; shared MIX defaults fully wet.

**TONE** (ROOM and REVERB; the knob is empty for the others) is one knob from DARK through FULL to THIN: below
FULL a high cut from 2640 Hz down to 120 Hz, above it a low cut from 150 Hz up to 1280 Hz (core.h `fx_tone_hz`, fine
steps where the ear needs them). Smooth 6 dB/octave cuts shape the wet input; the dry signal stays intact.

ROOM keeps a 150 Hz low cut at FULL and under a high cut, so its short comb
reflections do not accumulate bass. TONE shapes only the wet input.

Under the picture a strip on a log axis (20 Hz–2.6 kHz) lights, in TONE's colour, the band that reaches the effect
(from the effect's own low cut), the cut TONE sets under its edge; at FULL dimmed. Each track and shared slot keeps its TONE in projects
(FUN22) and templates (TPLL). FUN21/TPLK's two cuts load as the nearest TONE (the low cut when both were set); older
records load FULL.

Screens for every effect, the racks (picker, list, SPLIT), SHARED's sheet and TONE's two sides are rendered and linted
in every palette by `tests/ui_render.c` (fx_*).

Audio ECHO remains retired: ID 6 imports OFF. SLOOP REVERB replaces Echoverb
at ID 8 (2026-10-20): SIZE sets feedback decay, DAMP reduces highs in the
feedback, TONE shapes wet input. Mono input, stereo return, 44.1 kHz; one
19,396-byte tank per active slot, released after bypass. Echoverb's kernel,
TIME/DIFFUSE controls and 60,340-byte tank are removed. Old ID 8 slots select
the replacement reverb with their stored amount/controls and routing intact.

Append-only IDs 12–19 offer DRIVE (SOFT/HARD/FOLD/FUZZ, its SHAPE on KNOB 4), CRUSH, FLANGER, PEDAL PHASER and
PEDAL CHORUS from SLOOP's insert DSP. Existing PHASER/CHORUS remain separate sounds.
Drives use DRIVE/TONE/SHAPE, CRUSH uses BITS/ALIAS (bits, kHz), and the swept processors use
RATE/DEPTH with signed RESO in the old per-slot TONE field. RESO has no frequency
strip. Drives retain unity output level; CRUSH's protective low-pass is fixed.
Delay storage is allocated only for FLANGER/PEDAL CHORUS. Mono processing folds
only until actual stereo arrives; that transition preserves the delay history.
Shared returns remove the swept pedal's own dry half to avoid dry doubling.
Normal CHORUS retains its independent stereo delay and dormancy optimization.

### MIDI track inserts (2026-10-19)

The three track insert slots also offer **NOTE TAIL** (was MIDIVerb) and **NOTE ECHO** (was MIDIDelay). Neither
appears in the shared rack. They process internal notes before audio rendering;
the normal audio inserts still process the resulting sound. Instrument presets,
stored notes, captured performances and USB MIDI output retain their original data.
These insert settings save with projects and templates, like the audio inserts.

- **NOTE TAIL:** no page of its own (one knob): its rack row's KNOB 4 REL controls the track's envelope release (zero bypasses).
  The rack's amount knob edits the same release. The other knobs are blank.
  Prophet's amp/filter release, FM6's operator release rates, CZ-1's amplitude
  release and SID's release follow it without rewriting their instrument patches.
  The response follows each engine's envelope law; a one-shot sound's own decay
  still limits its tail. Multiple NOTE TAIL slots use the longest release.
- **NOTE ECHO:** LEVEL (the slot's MIX) scales delayed-note velocity relative to the original;
  TIME selects a tempo division (1/32 through 4 bars, including triplets); DECAY
  sets the velocity lost per repeat, including the first. KNOB 4 is blank. New selections start at
  1/8 and 50% decay. Echoes use gates of half the delay time and continue after
  the source note releases. Repeats stop at zero velocity or 32 hits; even zero
  decay loses at least one velocity step per hit. Multiple delay slots run in
  parallel from the source; echoes do not recursively feed another note delay.

Echo timing follows the song tempo (the measured tempo for an external master),
while release deadlines keep running in sample time when the external clock
stops. Held source notes keep ownership when an echo shares their pitch. A full
fixed queue drops extra repeats without starting an unreleaseable note. Stop,
panic, sound changes or editing a delay's controls cancel its track's queued
echoes.
No audio delay buffer is allocated for either MIDI insert.

## FX button (2026-10-20)

Tap FX for the track rack. Hold FX and press a key for SLOOP's 27 master punch
effects, in its original white/black key layout. Double-tap FX locks the layer;
tap FX again to close it. The latest pressed effect key owns the sound until
that key releases; releasing another key does not change it. A key held before
FX remains a normal instrument note. Effect keys generate no notes or MIDI and
are not recorded. The layer draws the 27 keys as a keyboard tinted by family, the key held white, and under it the key's family, name and picture; KNOB 1 walks the keys; other knobs are
consumed, including their release frame, without editing the underlying page.

The complete dry stereo mix and shared returns pass through punch DSP before
the existing master DC blocker, limiter and clipper, including stereo USB
capture. Individual USB track stems remain before the master effects. Loops,
gate and modulation use song tempo, including measured external clock; a
64-sample wet fade joins each change. Loops hold their captured samples across
history wraps. Punch history requests one optional 32 KiB allocation while the
layer is in use, captures from layer entry, and releases after the layer and
all effect keys close. Hold FX briefly before reverse/rewind to capture past
audio. If allocation fails, buffered effects stay dry and the header says
FX MEMORY; filter/kill/gate/mono/fade effects still work. Close and reopen FX
to retry after reducing memory use. FREEZE reuses this same allocation for
SLOOP Reverb, captures 186 ms of the master, then holds undamped feedback until
release. It does not require a rack reverb slot. See ../SLOOP_PUNCH_FX.md.

## Large type (2026-10-14)

The 1.54" panel (27.7 mm across) needs larger text: every screen is set in four type roles, and Settings > FONT picks
the face. Rubik 500 or Melodee Pixel (`assets/fonts/melodee_pixel.txt`, a 5 x 7 1-bit face drawn at whole multiples);
a role has the same line height and baseline in both, so a layout holds in either (`gfx.c` `TY_*`, `ui.c` PREF_PIXEL).

| Role | Use | Rubik | Pixel |
|---|---|---|---|
| `TY_LB` | labels, units, tags, chips, keycaps | 16 px | 2 x (letters and figures 4 wide) |
| `TY_TX` | names, rows, the header, text | 20 px | 2 x |
| `TY_VL` | values | 28 px | 3 x |
| `TY_HR` | the hero: the chord, big names | 48 px | 5 x |

`ty_y(cy, f)` centres a face's capitals on a row; `ty_fit(f, s, w)` drops to the label face before anything is cut.

The grid: header rows 0 .. 29; the knob strip at y 32, four cells 57 x 48 at x 2 + 60 c (label, value; the knob
turning: its value in the track's colour and a bar); the picture at y 84, 124 tall; the sound bar or the keys at y 212,
28 tall. Two layouts (Kerem: "your mix"):

- Knob and curve pages: the strip on top, the picture under it; a knob turning shows in its cell alone, nothing over
  the picture (Kerem 2026-10-15: the large value over it hid what it shapes).
- Stage, lists, Settings, the browser, sheets: rows across the screen, 32 px tall and 34 apart, the picked row tinted
  in the track's colour. Stage's rows are the tracks, each in its colour, with every step of the pattern as dots
  (an LED matrix in the track's colour: a bar a line of 16 in four beats, lines stacked; `ui_stage.c` st_layout picks
  the largest dots that fit: under the name across the row, three or four tracks with the role and pattern at the
  name's right).

Rules, from the reviews: screens stay calm (no boxes or rings around cells, no tabs, no hint rows); nothing is ever
cut or ellipsised (labels have short forms, `ui_draw.c` CELL_SHORT; dialogs and toasts
wrap); Stage is multicolour. `tests/ui_render.c` lints every screen in both faces: overlaps, texts past their canvas
or cell, ellipsised labels; it must report none.

Storage: the four Rubik faces are 4-bit alpha kept in four steps (0, 5, 10, 15) and Huffman-coded, with one kerning
table for all of them (at 28 px, scaled per face); with the pixel face 31.4 KB in all. Glyphs stay in flash: no
decode table lives in RAM (`gfx.c` builds a 64-entry one on the stack per string).
