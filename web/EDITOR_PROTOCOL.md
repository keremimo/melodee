# Melodee editor protocol (SysEx over USB-MIDI)

The firmware side is `firmware/src/editor.c`. Commands 1-15, track selection, dumps, step reads
and parameter writes (27, 29, 30, 31) were checked on hardware. `tests/editor_test.c` also exercises the real C
handler, malformed transfers and stop timeouts against simulated flash. User-preset
flash writes and live pushes (16-26, 32) have host coverage; that does not establish
their hardware behavior. The step chance byte, motion (64) and the full backup (65-67) have host
coverage only (`tests/editor_test.c`, `tests/backup_test.c`, `web/test_backup.mjs`).

**v3 (four tracks):** the device has four tracks, each a synth part (since 1.0; before it, track 4
was a GM drum track: see "Track 4 since 1.0" below). One
of them is *selected* (ALGORITHM on the device, or `TRACK`). Every v1 / v2 command acts on the
selected track (its parameters, engine, preset, steps, the user presets it stores or loads); `TRACK`,
`TRACK_MIX`, `TRACK_DUMP` and `TRACK_STEP` reach any track. Command numbers 1-26 are unchanged.

**v4 (any track's parameters):** `TRACK_PARAM` gets or sets a parameter of any track without changing
the selection, and the `TRACK_CHANGED` push follows level, pan and mute of the tracks that are not
selected. The v1-v3 commands are byte for byte as before; v4 is asked for with bit 1 of `WATCH`.

**v5 (the drum grid):** a step also holds lane hits and their accents (see "v5: the drum grid" below).
`STEP_GET` / `STEP_SET` / `TRACK_STEP` replies get 3 more bytes, `STEP_SET` / `TRACK_STEP` take them
optionally, and `UP_GET` / `UP_PUT` carry a user preset's 16-step grid after its pattern. Every
earlier byte is where it was; an editor that reads the first bytes of a reply keeps working.

**v7 (motion, chance, backup, 89 parameters):** a step gets a chance byte after its hits. `MOTION` (64) reads
and edits a track's recorded knob moves. `BACKUP_LIST` / `BACKUP_GET` / `BACKUP_PUT` (65-67) read and
restore the whole device. P_COUNT is 89 and P_E0 is 81: twenty FM operator parameters (ids 61..80) went in
before the engine parameters, which moved from 61..68 to 81..88. Always take P_E0 from `INFO`. INFO ends with
tagged capability blocks for these (see `INFO`).

**Chord keys (91 parameters, 1.0):** two track parameters, `CHRD` (81) and `VOIC` (82), went in before the
engine parameters, which moved from 81..88 to 83..90: P_COUNT 91, P_E0 83. No command changed; an editor that
takes P_COUNT and P_E0 from `INFO` keeps working (see "The chord keys" below).

## Framing

The device USER sheet also exports general Melodee presets as `F0 7D 4D 4C 01 <packed record>
<checksum> F7`. This is a preset file rather than an editor request. The record is the complete
238-byte `up_rec_t`, including its twelve-byte name and stored pattern. Pack groups of seven
raw bytes as one high-bit mask (bit j is byte j's bit 7), followed by seven low-seven-bit bytes.
The final shorter group uses only its corresponding mask bits. The checksum makes the sum of
all bytes between F0 and F7 zero modulo 128. Version, exact length, 7-bit encoding, checksum and
record validity are checked before staging. Imports open the general USER list for choosing a
slot and confirming replacement. FM6, CZ-1 and Prophet slot exports use their native single-voice
formats instead. Bulk export concatenates a frame for each used selected slot.

A request is `F0 7D 46 4C <cmd> <args...> F7`:

- `7D` is the non-commercial SysEx ID.
- `46 4C` is "FL".

Every valid request gets exactly one reply, with the same header and the same `<cmd>`.
Unknown commands and invalid fixed argument lengths get no reply; argument errors
follow the command-specific rules below.
Fixed-size commands require exactly the lengths in the tables; step writes accept the
8-byte legacy step, the 11-byte grid step or the 12-byte step with its chance (a chance above 100 gets
no reply). Every data byte is 7 bit.
MIDI realtime bytes may occur inside SysEx; another status aborts the partial frame. While the editor
watches (v2, `WATCH`), the device also sends push frames (cmds 23, 24, 26) at any time.

| Item | Encoding |
| --- | --- |
| value (v14) | 2 bytes, LSB first, holding value + 8192, so the range is -8192..8191. `[lo, hi]`: value = (lo \| hi << 7) − 8192 |
| u32 (v7) | 5 bytes, 7 bits each, LSB first; the last byte is 0..15. Used by the backup commands |
| string | ASCII bytes, ended by a 0 byte |
| scope | 0 = parameter of the selected track (`P_*`, 0..P_COUNT−1); 1 = global parameter (`G_*`, 0..G_COUNT−1) |
| track | 0..3: tracks 1..4 (synth parts) |
| engine byte | 0..NENGINES−1 (firmware before 1.0: NENGINES = its drum track, no engine). The numbers are fixed, new engines are appended: 0 reserved (ANALOG before 2.0), 1 reserved (DIGITAL before 1.0: see below), 2 PHASE, 3 SID (LOFI before this change), 4 reserved (SAMPLE retired), 5 VOICE, 6 reserved (TRIO retired), 7 reserved (WHEEL retired), 8 reserved (GRAIN retired), 9 reserved (PHYS retired), 10 DRUM, 11 NOISE, 12 FM6, 13 reserved (SLICE retired), 14 reserved (OBXF retired), 15 CZ-1, 16–18 reserved, 19 PROPHET (NENGINES 20). The new-sound browser order is PROPHET FM6 PHASE CZ-1 SID VOICE NOISE DRUM (`ENGINE_ORDER`). 0 ANALOG is retired since 2.0 (its stored sounds keep the number and play silent); the numbers stay |

The engine parameters are `P_E0..P_E7`: P_COUNT−8 .. P_COUNT−1 (83..90), and `INFO` gives `P_E0`.
Their meaning, range and names depend on the current engine, so re-read `DESC` for them
after an engine change.

The modulation matrix (the MOD page, `firmware/src/mod.c`) is twelve track parameters at ids 49..60 (since
P_COUNT 69): slot k (1..4) is `SRCk`, `DSTk`, `AMTk` at ids 49 + 3 (k − 1) .. 51 + 3 (k − 1).

| id | label | values |
| --- | --- | --- |
| 49, 52, 55, 58 | SRC1..SRC4 | enum: 0 OFF, 1 LFO, 2 ENV, 3 VEL, 4 KEY, 5 RAND, 6 MODW (CC1), 7 AT (channel aftertouch), 8 EXPR (CC11) |
| 50, 53, 56, 59 | DST1..DST4 | enum (20 names): 0 OFF, 1 PITCH, 2 CUT, 3 SHP, 4 AMP (per voice); 5 PAN, 6..9 retired (empty names), 10 RATE (LFO rate), 11 VIB (LFO pitch depth), 12..19 E1..E8 = `P_E0..P_E7` (per block) |
| 51, 54, 57, 60 | AMT1..AMT4 | −64..63 (fmt BIPCT) |

`DESC` names E1..E8 as such; the device shows the engine's label of that parameter instead (`DESC` of
`P_E0 + n`), and so does the editor. A slot with SRC, DST or AMT at 0 does nothing; every default is 0
(a factory preset sets all twelve to 0). The modulation never changes the stored values: `GET`,
`DUMP` and the pushes report what was set.

**DIGITAL (engine 1) was replaced by FM6 (Dexed-based)** in 1.0. The number stays reserved: `INFO` names it
"-" (a build with `MELODEE_FM4=1` has DIGITAL back, named "DIGITAL"), `NAMES 1` lists no presets, its EDIT
descriptors are "-" with DIGITAL's ranges, and no track ever has it. Whatever brings a DIGITAL sound plays it as
FM6 with a patch converted from its values (`firmware/src/fm4_convert.c`; the patch is the track's own, `FM6_GET`
reads it, a project keeps it): `SET` of `G_ENGSEL` = 1 (DIGITAL's first preset), `PRESET` 1 k (its preset k),
`UP_LOAD` of a slot stored with engine 1 (`UP_PUT` still takes engine 1: the record keeps the DIGITAL values, every
load converts them), projects with DIGITAL tracks (their motion on E1..E8 and ids 61..80 is dropped). The track
reports engine 12 (FM6) and the FM6 preset that covers the sound. The editor converts DIGITAL patches of library
files the same way (it equals the firmware's, `test_web.mjs`).

The four FM operator envelopes (the DIGITAL engine's OP1..OP4 ENV and OP LEVEL pages, `eng_digital.c`) are
twenty track parameters at ids 61..80, five per operator: operator k (1..4) has `ATK`, `DEC`, `SUS`, `REL`,
`LVL` at ids 61 + 5 (k − 1) .. 65 + 5 (k − 1). Since DIGITAL was retired they are inert: no page and no editor
layout shows them, only the conversion of a DIGITAL sound reads them (its FM6 tracks hold the defaults). The ids
and labels stay (library files key values by label).

| id | label | fmt, range, default |
| --- | --- | --- |
| 61, 66, 71, 76 | ATK | TIME, 0..127, 0 |
| 62, 67, 72, 77 | DEC | TIME, 0..127, 0 |
| 63, 68, 73, 78 | SUS | PCT, 0..127, 127 |
| 64, 69, 74, 79 | REL | TIME, 0..127, 0 |
| 65, 70, 75, 80 | LVL | PCT, 0..127, 127 |

The defaults change nothing: the sound is the same as before the parameters existed. The master `ENV`
(ids 1..4) shapes the whole voice; these shape each operator on top of it. Other engines ignore them, and the
device shows the pages for DIGITAL only. Descriptors are the same for every engine.

The chord keys (SCL > CHORD on the device, `firmware/src/chord.c`) are two track parameters at ids 81, 82:

| id | label | values |
| --- | --- | --- |
| 81 | CHRD | enum: 0 OFF, 1 DIA3, 2 DIA7 (the triad / seventh of the track's ROOT and SCALE on the key: every tone in key), 3 MAJ, 4 MIN, 5 DOM7, 6 MAJ7, 7 MIN7, 8 SUS4, 9 POW (fixed shapes) |
| 82 | VOIC | enum: 0 CLOSE, 1 OPEN (1-5-3), 2 INV1, 3 INV2, 4 +OCT (the root an octave down; a seventh drops its fifth) |

With CHRD on, a key, a MIDI note of that track and so the arp's held notes play the chord (at most 4 notes),
and live recording writes it into one step; MONO / LEGATO / UNISON play its root; a kit (DRUM, SAMPLE PERC)
ignores it. Both defaults are 0: nothing changes until CHRD is set. They are the track's, like ARP and SCL: a
sound load (a factory preset, `UP_LOAD`, an audition) keeps them, and motion never records them.

Other enums that grew: `P_SDIV` (DIV, id 30) has 10 names (1/4, 1/8, 1/16, 1/32, 8T, 16T, 1/2, 1/1, 2BAR,
4BAR; the first six keep their numbers), `P_AMODE` (id 17) 7 (OFF, UP, DN, UPDN, RND, ORD, REPEAT) and the
global `G_CLOCK` (id 2, label "CLK") 3 (INT, USB, TRS). `G_MIDI` (id 12) is an enum of USB / TRS that nothing reads.

## Commands

| cmd | Request args | Reply args |
| --- | --- | --- |
| 1 INFO | — | version string, NENGINES, P_COUNT, G_COUNT, NSTEP, P_E0, then NENGINES engine-name strings, then (v3) NTRK (4), then (v6) CHAIN_ROWS (16), then the tagged blocks `55 01 uiCaps`, `4D 01 64 01` and `42 01 3` (below); older firmware ends earlier |
| 2 GET | scope, id | scope, id, v14 |
| 3 SET | scope, id, v14 | scope, id, v14 (the value after clamping). Setting global `G_ENGSEL` (id from DESC label "ENG") changes the engine: its defaults, then its first preset (as on the device) |
| 4 DUMP | — | engine, preset, then P_COUNT × v14 (the selected track), then G_COUNT × v14 (globals), then the preset's high bits (preset >> 7; see "Presets past 127") |
| 5 DESC | scope, id | scope, id, fmt, min v14, max v14, def v14, label string, unit string, then for an enum (fmt 8) one name string per value (at most 24; firmware before the matrix: at most 16) |
| 6 STEP_GET | index 0..NSTEP−1 | index, n (0..4 notes), note0..note3, time (0 NOTE, 1 TIE, 2 REST), flags (1 accent, 2 slide), vel, then (v5) hits (3 bytes, below), then (v7) chance 0..100 |
| 7 STEP_SET | index, n, note0..3, time, flags, vel [, hits (3 bytes, v5) [, chance 0..100 (v7)]] | same as STEP_GET (after the write). Without the hits the step keeps its own; without the chance it keeps its own. The chance can only follow the hits |
| 8 PRESET | engine, preset [, preset >> 7] | engine, preset, preset >> 7 (applies the preset's sound; the steps and the track's own parameters stay, see "Sound loads and undo") |
| 9 PROJECT | op (0 load, 1 save, 2 query), slot 0..3 | op, slot, used (1/0). Save writes flash: allow ~2 s; it stops the transport first (see "Saves while playing") |
| 10 NAMES | engine [, first preset, 2 × 7 bit] | engine, count (≤ 127), count preset-name strings (≤ 20 characters) from the first, then the two edit-page titles, then all presets and the first (2 × 7 bit each). Ask again from first + count until all are read |
| 11 SMP_BEGIN | slot 0..2 | slot, rc (0 ok). Erases the slot's header sector: the slot is empty from now on |
| 12 SMP_WRITE | slot, offset (3 × 7 bit, LSB first), pack7 data (≤ 256 bytes) | slot, offset, rc: 0 ok, 1 arguments, 2 erase, 3 write, 4 slot in use (send SMP_BEGIN first). Offset ≥ 512 and a multiple of 256; writes go in increasing order (a write at a 4 KiB boundary erases that sector) |
| 13 SMP_END | slot, pack7 header (480 bytes) | slot, rc: 0 ok, 1 size, 2 header, 3 data CRC, 4 flash, 5 zones |
| 14 SMP_ERASE | slot | slot, rc (erases the whole slot, ~1 s) |
| 15 SMP_INFO | — | slots, slot KiB, then per slot: zone count (0 = empty), name string, data KiB |
| 16 UP_LIST | start, count (1..16) | start, count, total slots, then per slot: used (0/1), engine, name string ("" if unused) |
| 17 UP_GET | slot | slot, used, engine, name, P_COUNT × v14, 16 × (note, flags), then (v5) kind (0 a note pattern, 1 a drum grid) and for kind 1 16 × hi |
| 18 UP_PUT | slot, engine, name, P_COUNT × v14, 16 × (note, flags) [, kind 0 or 1, 16 × hi (v5)] | slot, rc (0 ok, 1 args, 2 flash). Writes flash: allow 1 s |
| 19 UP_STORE | slot, name | slot, rc. Stores the current sound: engine, parameters, the first 16 sequencer steps as the pattern (TIE steps → flag 4) |
| 20 UP_LOAD | slot | slot, rc (0 ok, 1 empty/invalid). Applies its sound (not its pattern; the steps stay) |
| 21 UP_ERASE | slot | slot, rc |
| 22 WATCH | on (0/1; v4: 3 = also `TRACK_CHANGED`; Studio: bits 2..4, see "Studio session") | on (0/1; v4 firmware: 3 when 3 was asked for; Studio firmware: the bits it knows of those asked for). While on, the device pushes cmds 23, 24, 26 (and 32 with bit 1; 55..57 with bits 3, 4) |
| 23 CHANGED (push) | — | scope, id, v14 |
| 24 RELOAD (push) | — | engine, preset, then (v3) the selected track, then preset >> 7 |
| 25 PING | — | 0 |
| 26 STEP_CHANGED (push) | — | index, then (v3) the selected track |

**Presets past 127.** PROPHET has 201 factory presets (INIT, then Sequential's 200 v1.03 programs).
A preset number is 7 bits where the tables above say "preset"; its high bits follow at the end of the
reply (older editors ignore them, older firmware sends none: read them only if present). `PRESET` takes
them as a third byte. `NAMES` pages: firmware before this sends one reply with every name and no totals;
an older editor sees the first page (the names that fit 1024 bytes).

| cmd (v3) | Request args | Reply args |
| --- | --- | --- |
| 27 TRACK | — (query), or track (select it) | selected track, NTRK, then per track: engine byte, preset, level v14, mute (0/1), armed (0/1, live recording), then per track preset >> 7 |
| 28 TRACK_MIX | track (get), or track, level v14 (0..127), mute (set) | track, level v14, mute: the track's `P_LEVEL` and `P_MUTE` |
| 29 TRACK_DUMP | track | track, engine byte, preset, P_COUNT × v14 (that track's parameters; no globals), then preset >> 7 |
| 30 TRACK_STEP | track, index (get), or track, index, n, note0..3, time, flags, vel [, hits (v5) [, chance (v7)]] (set) | track, index, n, note0..3, time, flags, vel, then (v5) hits, then (v7) chance |

| cmd (v4) | Request args | Reply args |
| --- | --- | --- |
| 31 TRACK_PARAM | track, id (get), or track, id, v14 (set); id = `P_*` (0..P_COUNT−1) | track, id, v14 (the value after clamping, as `SET`). The selection does not change; no push about the editor's own write |
| 32 TRACK_CHANGED (push) | — | track, id, v14: `P_LEVEL`, `P_PAN` or `P_MUTE` of a track that is not selected changed on the device (only while `WATCH` was sent with bit 1) |

| cmd (v7) | Request args | Reply args |
| --- | --- | --- |
| 64 MOTION | track (query); track, 1, on 0/1 (play on / off); track, 2 (clear); track, 3, step, id, v14 (set an event); track, 4, step, id[, v14] (delete an event) | track, rc, on (0/1), count (this track's events), max (64), then count × (step, id, v14) |
| 65 BACKUP_LIST | — | 1, rc, count (11), then per object: id, size u32, crc u32 |
| 66 BACKUP_GET | id, offset u32, count lo, count hi (≤ 256) | id, rc, offset u32, count lo, count hi, pack7 data |
| 67 BACKUP_PUT | op 0 begin: 0, id, size u32, crc u32; op 1 data: 1, id, offset u32, pack7 data; op 2 commit: 2, id; op 3 abort: 3, id | op, id, rc |

Without flash (no flash part found at boot) `SMP_BEGIN`, `SMP_WRITE`, `SMP_END` and `SMP_ERASE` get no
reply.

**pack7:** groups of up to 7 bytes, each preceded by one byte holding their top bits
(bit j = bit 7 of byte j). A mask must have at least one following data byte;
unused mask bits in the last group must be zero. Oversized or incomplete transfers
are rejected; SMP_WRITE decodes at most 256 bytes, SMP_END exactly 480 bytes.

**User sample slot** (80 KiB each, SAMPLE engine sets USR1..USR3; reference uploader
`tools/fm1_sample_upload.py`, slot builder `sampleio.user_slot`; the editor's port of it is
checked byte for byte by `web/test_web.mjs`): header at 0, ADPCM data at 512.

| Offset | Field |
| --- | --- |
| 0 | magic `"FSMP"` (u32 0x504D5346), u16 version 1, u8 zone count 1..16, u8 0 |
| 8 | name, 8 ASCII bytes (0-padded) |
| 16 | u32 data length (bytes), u32 CRC-32 (zlib) of the data, 8 bytes 0 |
| 32 | 16 zones × 28 bytes: u32 off (in the data), n (samples), loop start, loop end, rate (Hz / 44100 × 65536); i16 root × 16 (MIDI note), ADPCM predictor at the loop start; u8 step index at the loop start, lo note, hi note, looped (0/1) |

Data is IMA ADPCM, 4 bit, low nibble first, starting from predictor 0 and step index 0.
All little endian.

The slot's last 4 KiB sector (offset 0x13000) holds SLICE's slices set by hand on the device (`src/slice_store.c`:
magic `"SLM1"`, the sample's length and data CRC, the slice starts) when the data leaves it free (at most 77,312
bytes). An upload writes over it like any other data; a record that is not the slot's sample's is ignored.

`fmt` values (`firmware/src/core.h`):

| Value | Name | Value | Name | Value | Name |
| --- | --- | --- | --- | --- | --- |
| 0 | INT | 5 | CUTOFF | 10 | NOTE |
| 1 | PCT | 6 | DB | 11 | ONOFF |
| 2 | BIPCT | 7 | SEMI | 12 | OCT |
| 3 | TIME | 8 | ENUM | 13 | STEPS |
| 4 | LFOHZ | 9 | BPM | | |

The editor should show the value with the unit; formatting it exactly like the device does
is not required.

## v2: user presets

A user preset = engine (0..NENGINES−1), name (1..12 chars, ASCII 32..126; the device shows it upper
case), all P_COUNT instrument parameters (v14 each, the same order as `DUMP`), and a 16-step pattern:
16 × (note 0..127 (0 = rest), flags: 1 accent, 2 slide, 4 tie). Loading one applies the engine and
the parameters of the sound, as a factory preset (the track's own parameters, the steps and LEN stay; see
"Sound loads and undo"). The stored pattern is kept and returned by `UP_GET`; the device no longer loads
it (its phrase list, SEQ > PHRASES, was removed in 2026-10). The slots are
numbered 0..63 (the device shows U01..U64).

- `UP_LIST`: count is cut at 16 and at the last slot (start ≥ 32: count 0, no entries).
- `UP_GET` of an empty slot has the same shape with used 0, engine 0, name "" and all values 0.
  Values come back in the current parameter order, inside their ranges.
- `UP_PUT`: rc 1 for a slot ≥ 32, an engine ≥ NENGINES, a name that is empty, longer than 12 or has
  bytes outside 32..126, or an incomplete or oversized frame. Values are clamped to their ranges for that
  engine. A note with flag 4 is stored as a tie (note 0); flags on a rest are dropped.
- **DRUM** (engine 10) was PHYS's MODEL 4 (DRUM) before 1.0. A record of PHYS with E1 (MODEL) = 4,
  stored then or sent by `UP_PUT` from an older editor, is the DRUM engine: the device rewrites it (engine
  10; E1..E8 {MODEL, TUNE, TONE, DECY, SNAP, ACC, KICK 0..127, PERC 0..127} become {KIT = PERC / 32, TUNE,
  TONE, DECY, SNAP, ACC, KICK 0 PUNCH / 1 ROUND (old ≥ 64), DRV 0}), and `UP_GET` / `UP_LIST` give it so.
  Projects do the same. PHYS's MODEL is 0..3 (MODAL STRNG MEMB SYMP) now.
- `UP_STORE`: name "" stores with the automatic name the device uses (engine name + slot number,
  "SID 07"). rc 1 for a bad slot or name.
- rc 2 = the flash write failed or there is no flash. A failed flash write keeps the previous
  record in RAM and flash, including its name. Without flash, a valid change is RAM-only until power-off.
  (`UP_PUT` / `UP_STORE` / `UP_ERASE` stop the transport first, see "Saves while playing"; a device
  that cannot stop it in 100 ms answers rc 2 and writes nothing.)
- Frames stay below 640 bytes (`UP_PUT` is 5 + 1 + 1 + 13 + 2 × P_COUNT + 32 + 1).

**On the device:** SAVE > USER page: KNOB 1 picks the slot, KNOB 2 LOAD, KNOB 3 ERASE, KNOB 4 SAVE;
OCT+ executes the selected action, OCT- goes back. SAVE over a used slot asks
"OVERWRITE U07?" with OCT+ / OCT-. SAVE uses the automatic name. The PRESETS knob and the SAVE > PRESETS browser continue past the factory presets into the used user
presets.

**Flash** (`firmware/src/upreset.c`): two storage objects (`OBJ_UPRESET0/1`, A/B sector pairs at
0xDC000..0xDFFFF), 16 records of 238 bytes each (192-byte legacy records migrate on load), behind a bank header (magic "UPB1", record size,
slot count; a mismatch reads as an empty bank). A record keeps its layout version (versions 1..5 are read; another: empty)
and the P_COUNT it was stored with; another count is mapped by count (last 8 values = P_E0..P_E7, the
first ones = P_LEVEL.. in order, missing ones = defaults). Versions 4 (a note pattern) and 5 (a drum grid)
store each value as one byte, value + 64, so P_COUNT can reach 127; versions 1..3 stored 16-bit values.
Version-1 records with 58 or 62 parameters belong to pre-1.0 Melodee's incompatible MPC/chord/engine
layout. They are treated as empty without changing their bytes; they are not mapped as upstream presets.
The wire format is unchanged: `UP_GET` / `UP_PUT` still carry v14 values, and `UP_PUT` now answers rc 1
for a value outside −64..127. P_COUNT was 53 (P_E0 45) until the SLICER
parameters (SLCR, PAT, RATE, DEPTH: ids 45..48) went in just before P_E0: P_COUNT 57, P_E0 49; then
the twelve matrix parameters (ids 49..60): P_COUNT 69, P_E0 61. An editor takes both from `INFO`;
records stored with 53 load with the SLICER off and every matrix slot off, with 57 with every slot off. The
twenty FM operator parameters (ids 61..80) went in before P_E0 after that: P_COUNT 89, P_E0 81. Records stored with 69 load with
those at their defaults, which change nothing. Then the chord keys (ids 81, 82): P_COUNT 91, P_E0 83; records
stored with 89 (or 69) load with CHRD OFF and VOIC CLOSE, their engine values at 83..90.

## v5: the drum grid

A DRUM track (engine 10) is edited on the device as a grid of 8 lanes × the steps (SEQ > STEP). The
grid lives in the steps themselves, so every engine has it:

- **Hits.** Each step has a lane mask `hit` and an accent mask `acc` (bit l = lane l, `acc` only on
  lanes that hit). The lanes and the General MIDI note each plays: 0 KICK 36, 1 SNARE 38, 2 CLAP 39,
  3 HAT CL 42, 4 HAT OP 46, 5 TOM 45, 6 RIM 37, 7 BELL 56 (KIT HAND plays CONGA / CLAVE on lanes 5 / 6, KIT
  CYM a cymbal on lane 8). A NOTE step plays its notes and then its hits, on any engine (DRUM strikes its
  lanes, SAMPLE PERC its GM kit, a synth plays the pitches); an accented hit at velocity 127, the others at
  the step's velocity (0 = 96). A TIE or REST step plays no hits.
- **On the wire** (after `vel`): `hit & 127`, `acc & 127`, then `(hit >> 7) | (acc >> 7) << 1`. A
  `STEP_SET` / `TRACK_STEP` of 8 step bytes (an editor of before v5) leaves the step's hits as they are;
  send `0, 0, 0` to clear them.
- **Notes on lanes.** A step's notes keep playing as before; the device shows each note on the lane
  it strikes (GM 35..81, the others folded into their octave of 36..47: a low tom 41 on TOM, a crash 49 on
  BELL). When the device edits a lane of a step, notes on that lane become the lane's hit. DRUM tracks of
  projects saved before the grid get their lanes' own notes as hits (the same notes and velocities play).
- **Sound loads never convert steps** (as before): switching a track to or from DRUM keeps its notes
  and hits.
- **User presets.** A user preset stored from a DRUM track whose first 16 steps strike a lane holds a
  16-step grid instead of a note pattern (record version 3; version 5 with byte values). `UP_GET` gives its low 7 bits in the 16
  (note, flags) pairs (hits, accents), then kind 1 and 16 bytes of bit 7s (bit 0 hits, bit 1 accents);
  a note pattern ends with kind 0. `UP_PUT` with kind 1 and the 16 bytes stores a grid (accents only on
  hits); without an extension, a note pattern as before. If an extension is sent, it must be
  exactly 17 bytes: kind 0 or 1 followed by 16 bytes (kind 0 ignores those bytes).
  Unknown kinds, truncated extensions and extra bytes are rejected without changing the slot.
  Firmware before v5 shows a grid record as empty.
- **Projects** in v5 used format 5 ("FUN5"): 10-byte steps and 40 reserved bytes per track.
  FUN6 retained those fields and added the song chain (v6 below); FUN7 is current (v7 below). Older formats
  are converted on load.

## v2: live sync

- `WATCH 1` starts the pushes. Watching ends by itself 3 s after the last request of any kind (send
  `PING` about every 1 s), on a USB reset, and when the host goes away; `WATCH 0` ends it at once.
- **CHANGED** (scope, id, v14): a parameter changed on the device (knob, menu, sequencer edit of a
  `P_*`), not by the editor's own `SET`. Coalesced: each (scope, id) at most every 20 ms, with the
  latest value.
- **RELOAD** (engine, preset): the engine, a preset, a user preset or a project was loaded; re-read
  `DESC` of the engine parameters, `DUMP` and the steps. It is also sent after loads the editor asked
  for (`SET` of G_ENGSEL, `PRESET`, `PROJECT` load, `UP_LOAD`).
- **STEP_CHANGED** (index): a sequencer step changed on the device (record, clear, step edit,
  pattern load); not after the editor's own `STEP_SET`.
- Push frames have the normal header. Accept them at any time, also while waiting for a reply:
  match replies by cmd (23, 24 and 26 are never replies). The device sends at most a few per
  ~5 ms pass, and only when its USB send queue has room, so a push never delays a reply.

## v3: tracks

- **Track 4 since 1.0** is a synth part like tracks 1..3: `DUMP` / `RELOAD` / `TRACK` / `TRACK_DUMP`
  give its real engine byte (power-on: SAMPLE, preset PERC, the General MIDI kit), and `PRESET`, `SET` of
  `G_ENGSEL`, `UP_LOAD` and `UP_STORE` work on it; its level is its `P_LEVEL`. The commands are byte for
  byte as before; only the meaning changed. Firmware before 1.0 had a GM drum track there: engine byte
  NENGINES, no presets (`UP_LOAD` / `UP_STORE` rc 1), its level the global `G_DRLVL`. Drums are now the
  SAMPLE engine's PERC set and the DRUM engine on any part: the first C key is the kick (C2 = 36), notes are GM numbers.
- Global ids 24..26 are retired format padding, as are 4..11. `G_COUNT` stays 27.
  Their descriptors have empty labels and no page; loading a song clears their runtime values.
  MIDI input follows the ROUT range filter described below, with the DRUM override only inside that range.
- Selecting a track with `TRACK` does not push `RELOAD` (the editor re-reads `DUMP`, the steps and the
  engine `DESC` itself); selecting one on the device does (`RELOAD` with the new track).
- Pushes are about the selected track only: `CHANGED` (scope 0) and `STEP_CHANGED` refer to it, and
  changes to other tracks (live recording from MIDI into another track, `TRACK_*` writes) push nothing.
- Level and mute are also `P_LEVEL` / `P_MUTE` of the selected track (`SET`); `TRACK_MIX` reaches the
  others. Presets and user presets change a part's sound but keep its `P_LEVEL`, `P_PAN`, `P_MUTE`.
- Projects (`PROJECT`) save and load all four tracks, the selection, the song chain and the motion (FUN7;
  FUN5 added the drum grid, FUN6 the chain). Formats 6..1 are converted; a format 1 project loads into track 1. A project saved before
  1.0 loads its drum track as track 4 with the power-on sound (SAMPLE PERC), its steps kept.
- Older firmware (no NTRK in `INFO`): one instrument; skip the track UI.

## v4: any track's parameters

- **Finding out:** send `WATCH 3`. v4 firmware answers 3; v3 (0.8) firmware answers 1, does not know
  cmds 31 / 32 (no reply) and never pushes `TRACK_CHANGED`. `WATCH 1` behaves exactly as in v2 / v3
  (reply 1, no `TRACK_CHANGED`). Match the `WATCH` reply by bit 0.
- `TRACK_PARAM` clamps like `SET` scope 0: to the range of that parameter; the engine parameters
  `P_E0..P_E7` to the ranges of that track's engine. A parameter
  with a fixed range (min = max) keeps its value. A track ≥ NTRK or an id ≥ P_COUNT gets no reply.
  For the selected track it is the same as `SET` scope 0.
- `TRACK_CHANGED` is never about the selected track (its changes stay `CHANGED` scope 0). Coalesced like
  `CHANGED` (each track and id at most every 20 ms, latest value), and not sent for the editor's own
  `TRACK_PARAM` / `TRACK_MIX` writes. After a selection change (`RELOAD`, or the editor's `TRACK`) the
  device takes the current values as known.

## Notes for the editor

- **One request at a time.** Wait for the reply, about 10–50 ms, before sending the next.
  The device holds only one incoming SysEx frame.
- **Following the device.** With v2 firmware, `WATCH` and `PING` (above). Older firmware pushes
  nothing (no reply to `PING`): poll `DUMP` about every 300–500 ms while the page is visible.
- **Port.** The device's MIDI port is named "Melodee" (USB 1209:0001). Updates use the same
  port with other SysEx (the `F0 22 24 35 …` keys, `00 59 …` frames); never send those
  from the editor. Since 1.0 the same USB device also has an audio input ("Melodee",
  44.1 kHz stereo; bcdDevice 3.11); the MIDI port and this protocol are unchanged, and both
  work while the computer records.
- **Global ids.** `G_ROUTE` (id 14, label "ROUT", GLO > SYSTEM) was a placeholder ("--", range 0..0);
  it is the MIDI IN routing: 0 "CH1-4", 1 "SEL", 2 "CH5-8", 3 "CH9-12", 4 "CH13-16".
  Each range maps only its four channels to tracks 1..4 in order; other channels and tracks not in use
  are ignored. SEL receives every channel on the selected track. DRUM redirects its channel to the
  first DRUM track only within the selected range (or in SEL); without a DRUM track, normal routing applies.
  Note-offs and sustain release still reach their original owners after changing ROUT. Earlier firmware
  sent out-of-range channels to the selected track; this fallback is removed. Stored values 0 and 1,
  global id 14, the global count and the project layout are unchanged. ROUT persists between starts and
  is saved with projects/templates. The editor shows it under GLOBAL > SYSTEM.
- **Sound loads and undo.** A load that changes a track's sound (`PRESET`, `SET` of `G_ENGSEL`, `UP_LOAD`,
  and the same on the device) changes the sound only: the engine and the parameters of the sound. It
  never changes the steps, nor the track's own parameters: `P_LEVEL`, `P_PAN`, `P_MUTE` (0, 39, 40), the
  ARP pages, SCL and LEN / DIV / SWING / GATE (17..32). It does drop the track's recorded
  motion (the device's SAVE held brings it back). (Before 1.0 a preset also
  set the arp and replaced the steps with its pattern; a factory preset turned the SLICER off.) The byte
  layout of every command is unchanged. Steps are recorded on the device or written by the editor with
  `STEP_SET` / `TRACK_STEP` (the device's factory phrases were removed in 2026-10).
  The device keeps one copy of the track from before the last load (any track); SAVE held 0.7 s on the
  device swaps back what the load changed (the sound). Loads in a row on one track with nothing changed between them keep the copy from before
  the first; `SET` (scope 0) within 1.5 s of a load on the selected track counts as part of that load, so
  an audition (`G_ENGSEL`, then the patch's values by `SET`, the track's own parameters skipped as
  `UP_LOAD` skips them) and the next one still undo to the state before the first. `PROJECT` load takes no
  copy.
- **Saves while playing.** A flash erase silences the audio and stalls the sequencer for a moment. The
  device refuses its own saves while the transport plays ("STOP TO SAVE"); `PROJECT` save, `UP_PUT`,
  `UP_STORE`, `UP_ERASE`, `BACKUP_LIST`, `BACKUP_PUT` and sample BEGIN / WRITE / END / ERASE from the editor stop the transport
  first (`BACKUP_GET` does not: it answers rc 3 while the transport runs). If it does not stop within 100 ms, no flash operation starts: user-preset commands
  return rc 2, sample commands a nonzero rc, backup commands rc 3. PROJECT retains its existing reply shape
  (op, slot, used); a failed PROJECT save gets no reply, allowing the editor to report
  a timeout instead of confirming the previous used slot. The previous slot is kept.
  A pending PLAY or SONG start also prevents a device save.
- **Published samples.** SMP_END repeated with an identical committed header succeeds without
  another write or zone scan. A different header is rejected (rc 2) until SMP_BEGIN,
  so sounding sample voices cannot see their zone table change.
- **Safety.** `PROJECT` save, sample-slot commands, `UP_PUT` / `UP_STORE` / `UP_ERASE`, and the tagged preference writes below write flash, and only in
  Melodee's own storage; never the app or the update area.


## Pattern banks (commands 73–74)

INFO sends 0 in the former untagged CHAIN_ROWS byte, then retains the tagged UI, motion and backup
capabilities. `50 01 08 10` announces eight patterns per track and 16 bank-song rows; FM6's tag follows.
Editors must use the tag, not infer support from a version string. Older firmware retains command 33's
project-based SONG behavior; its historical format is documented below.

| cmd | Request | Reply |
| --- | --- | --- |
| 73 PATTERN | track 0..3, op 0 query; op 1 select, bank 0..7; op 2 copy, source 0..7, destination 0..7 | track, op, rc, active bank, queued bank (127 none), bank count 8 |
| 74 BANK_SONG | op 0 query; op 1 set, count 0..16, count × (T1 bank, T2 bank, T3 bank, T4 bank, repeat 1..16); op 2 start; op 3 stop | op, rc, count, running, row, remaining, count × (four banks, repeat) |

rc 0 succeeds, 1 rejects invalid values or a blocked action, 2 means busy or motion capacity exceeded.
Bank-song rows validate completely before replacing the arrangement. Pattern selection queues at that
track's loop end while playing; selecting the active bank cancels its pending change. STOP applies queued
choices. Copy retains four-note chords, ties, drum data, chance, timing and motion. Copying into the active
bank requires STOP; SONG blocks bank changes/copies. All pattern IDs on the wire are zero-based.

BANK_SONG switches all four tracks at track 1's loop boundary, preserves the current sounds and mix,

Bank capability tag `50 02 08 40` advertises 64 sections and REST bank id 8. Version 1 remains 16 sections,
bank ids 0..7. BANK_SONG op 1 accepts up to 64 rows on version 2; its reply has the same five bytes per row.
Op 2 optionally accepts a zero-based start row (`02 row`); omitted means row 0. Invalid start rows are rejected.
Playback starts at step zero with that section's complete repeat count and continues to the final section.
A resting track suppresses grid notes, recorded notes and motion; track 1 rests and continuations use a four-beat bar per repeat.
Existing FUN and FBKK/FBKL codecs retain their fixed 16-row layout. Songs requiring longer arrangements or rests
use FBKM, a new sparse bank container of the same extent: FBKL fixed fields, then count + three zero bytes,
64 × (repeat, eight bank ids), then sparse patterns. Active FUN metadata retains the first 16 sections, with rests
mapped to bank zero in that compatibility prefix. FBKM validators check both representations before publication.
Capability tag `50 03 08 40` additionally supports continuation id 9. It extends the preceding pattern block
without restarting sequence phase, held notes, recorded playback or motion at the section boundary. A continuation
cannot start a track or follow REST. FBKM stores it in the same assignment byte; its legacy prefix maps it to zero.
Older firmware does not read FBKM. Sparse saves retain the existing PROJECT FULL capacity check and never truncate
patterns to make a song fit. Normal songs continue to save as FBKK/FBKL.

Songs with timed master punch FX use **FBKN** (`0x4e4b4246`), with the same record extent and FBKM
arrangement prefix. Immediately after that prefix, before the sparse steps, are 64 sections × 4 punches ×
3 bytes: effect (0 = disabled, 1–27 = SLOOP effect ID + 1), zero-based start step, and duration in conductor
steps. A disabled event has three zero bytes. Enabled events require start < 64 and duration 1–(64 − start);
events past the section count must be disabled. The FX are section-local and repeat with the section.
All event fields are validated before restoring a project. Older firmware rejects FBKN; saves without punches
continue to use FBKK/FBKL/FBKM as appropriate. Backup transfers preserve FBKN unchanged. Timed punch editing
is currently available on the device through SONG > OCT+ > Punch FX; the song protocol operations still
describe note-pattern assignments, and replacing the arrangement clears its punch lane.

pauses recording and sequence edits, and restores the original bank selections on STOP or the song's end.
Each bank has LEN/DIV/SWING/GATE. Automation has a project-wide limit of 64 events; events belong to a
track and bank, while the ON switch remains track-wide. WATCH sends RELOAD when a bank changes.

Projects and runtime backups are FBK9 (`46 42 4B 39` in byte order, size 20224). Header words are magic
0x394B4246 and size; a complete 3584-byte FUN8 record follows at byte 8. Byte 3592 holds four active bank
IDs. Seven other banks per track follow, with the same nine-byte packed steps as FUN8. Then come 32 sets
of four int16 timing values, 16 × four SONG bank assignments, and 64 motion bank tags. Then (byte 20108) the
tracks' FM6 function settings: `46 4E 36 31` ("FN61") and 4 × 16 bytes, track 1 first, in the order of the
device's FM BEND / PORTA / WH/FT / BR/AT pages (bend up, bend down, step, portamento mode, time, glissando, wheel
range, target, foot, breath and aftertouch the same, Dexed velocity, ENGINE 0 MODERN 1 MARK I 2 OPL); all zero in a
record saved before them: Dexed's defaults. Remaining bytes are reserved; the final uint32 is FNV-1a over every
preceding byte. The inner motion record's reserved byte 0
is 1 when bank tags accompany it. Duplicate (place,param) events are valid on different banks only.

Flash objects 8..11 have two five-sector copies each at 0xA0000..0xC7FFF; CRC-verified payload precedes
its header-last commit. The old single-sector project objects remain readable for migration. Old FUN8,
FUN7 and older supported single-pattern projects become bank 1, with other banks empty and the old
project-based SONG cleared. USR1–3 are removed; sample commands refuse uploads/erases and SMP_INFO
reports zero slots. Recorded material and SAMPLE/GRAIN/SLICE are removed. DRUM provides only synthesized 808.

Current BACKUP_LIST has nine objects: 0 runtime (FBK9), 1 settings/template, 2..5 saved projects (FBK9 or
an older format before first save), 6..7 original user presets, 8 retired FM6 bank, 17..18 expanded user records and 19..20 owned FM6 voices. PUT accepts 20224-byte FBK9, 3584-byte
FUN8 and 3388-byte FUN7/FUN6 project records; saved legacy restores become FBK9. Full validation precedes
publication. The web restore checks the target inventory before writes; nonempty samples cannot restore
onto this firmware. Empty sample objects in an older archive are skipped.

## Historical v6: project-based song chain

INFO appends CHAIN_ROWS (16) after NTRK. Earlier INFO bytes and command numbers
stay where they were. No trailing byte means no SONG command: hide its controls.

| cmd | Request args | Reply args |
| --- | --- | --- |
| 33 SONG | op 0 query; op 1 set, count 0..16, count × (slot 0..3, repeat 1..16); op 2 start; op 3 stop | op, rc, count, running 0/1, row 0..15, remaining repeats, count × (slot, repeat) |

A set must have exactly `2 + count × 2` argument bytes. rc 0 = accepted,
1 = invalid rows (or an empty chain on start), 2 = busy, 3..6 = source slot 0..3
is empty. Rejected sets keep every previous row. Set edits RAM; PROJECT save
persists it, PROJECT load recalls it. A stop/start is acknowledged before the
next audio block; query reports the actual running state. `row` and `remaining`
are meaningful only while running. Rows always start from 0, with track 1's
loop as the repeat and transition boundary; the last row stops.

A row takes all four tracks' steps (with their chance) and LEN / DIV / SWING / GATE from its saved
project slot, retaining the current sounds, mix, ARP, scales. The
four sources are copied before starting, with no flash operation in playback.
Original editable patterns and timing are restored on stop. Sources are shared
project slots; overwriting a slot changes its uses on the next start.

STEP_GET / TRACK_STEP and WATCH report the playing source. During chain
playback, STEP_SET / TRACK_STEP writes, SET / TRACK_PARAM writes to
LEN / DIV / SWING / GATE and MOTION edits (rc 3) are ignored, returning the current value. Other
sound and mix parameters remain editable. Recording and panel pattern edits
also require STOP. Older editors keep working; older firmware gets no SONG
requests from the new editor.

Projects now write FUN7 (below), the same 3388 bytes in the same A/B sectors. FUN6 has the same size;
FUN5 (3352 bytes) and FUN1–FUN4 convert with an empty chain, without changing the sample-slot layout.
A chain row also plays the motion of its source project (events for an FM operator parameter are skipped
when the track's engine is not the saved one).

## v7: chance, motion, MIDI clock

- **Chance.** Each step has a chance, 0..100 %, as the last byte of the step reply (and the optional last byte
  of a step write). 100 is the default and what every older pattern holds; 0 means the step never plays. The
  device rolls once each time a step comes round, for all of it: its notes, its drum hits and a TIE. A failed
  roll plays nothing and releases what rang before. Chance is saved in projects (FUN7) and in song rows; a
  user preset does not store it. Changing it on the device pushes `STEP_CHANGED`.
- **Motion** is knob moves recorded per step: 64 events shared by the four tracks. An event is (track, step
  0..63, parameter id, value). While a track plays its motion, the step sets the value at the step and it
  holds until another step changes it; the loop restarts from the sound's own value, and stopping puts the
  sound's own values back. Parameters that can be recorded (`motion_param`): ids 0..16 (LEVEL, ENV, LFO),
  38 (GLIDE), 39 (PAN), 44 (DETUNE), 61..80 (the FM operator parameters) and
  83..90 (the engine parameters; not the chord keys 81, 82). The device records them while the track is armed, playing and selected, from its knobs
  and from `SET` / `TRACK_PARAM` alike.
  - `MOTION` with the track alone is the query. `on` 0 keeps the data and stops playing it; 1 plays it. Clear
    drops the track's events and turns it off; on the device it is undone by SAVE held.
  - Set: `value` must be inside the parameter's range for that track's engine (and −64..127); a
    new event when the 64 are used gets rc 2 ("MOTION FULL" on the device), an event that already exists is
    updated. A set turns the track's motion on. A delete of an event that does not exist succeeds.
  - rc: 0 ok, 1 invalid (step ≥ 64, an id that cannot be recorded, a value outside its range), 2 full, 3 a song is playing.
    A track ≥ NTRK, `on` above 1 or any other length gets no reply.
  - Events come back in the order they are stored. The device does not push motion changes: poll `MOTION`
    (the web editor does, while the sequencer tab shows).
  - `DUMP`, `TRACK_DUMP`, a user preset store and a project save give the sound's own (base) values;
    `GET`, `SET` and the pushes give what is sounding now, which differs while a motion event is in effect.
  - A sound or pattern load, a pattern clear and INIT SOUND on the device drop the track's motion; the
    device's UNDO brings it back. `MOTION` clear does the same through the same undo copy.
- **Projects (FUN7).** 3388 bytes, little endian, in the same A/B sectors as before: `46 55 4E 37` ("FUN7"),
  size u32 (3388), the 27 globals as i16 (bytes 8..61), sel, parts, phys (62..64), P_COUNT as stored (byte 66),
  then from byte 68 for each of the 4 tracks: P_COUNT bytes (value + 64), engine, preset, 64 steps of 9 bytes
  (4 notes; n | time << 3 | flags << 5; vel; hit; acc; chance byte where 0 = 100 %, 1..100, 101 = never);
  then the song chain, then the motion (260 bytes: count, on mask, 2 reserved, 64 × (track << 6 | step, id,
  i16 value)); the reserved tail is zero up to byte 3371; bytes 3372..3383 are the project's name (since
  1.0: ASCII 32..126, upper case, 0-padded; all zero = no name, as firmware before wrote them; a byte
  outside 32..126 reads as no name, it never refuses the project); the last 4 bytes are an FNV-1a hash of all
  before (the name included). The device names projects itself (SAVE > PROJECT, NAME); the editor's PROJECT
  save keeps the current name, and backups carry it as part of the 3388 bytes. FUN6..FUN1 load with no name
  and are bounded to today's ranges. A count smaller than P_COUNT is mapped as for user presets, and so are the
  motion events' ids: a FUN7 of 89 parameters has its engine parameters' events at 81..88, which load as 83..90
  (the ids below its P_E0 stay). 68 + 4 × (91 + 2 + 576) + chain + motion = 3040 bytes: 332 to spare.
- **Projects (FUN8, with FM6).** FUN7 laid out the same way, `46 55 4E 38` ("FUN8"), size 3584: the data up to
  byte 3040 as above (16 bytes to spare), the four tracks' FM6 patches as packed 128-byte records (see "FM6
  patches") at bytes 3056..3567, the name at 3568..3579, the hash last. FUN7 (3388) and older load with the
  init patch on every track. Backups, `PROJECT` and the editor's project files carry the 3584 bytes.
- **MIDI clock** has no SysEx. `G_CLOCK` selects the source: 0 INT, 1 USB, 2 TRS. With 1 or 2 the sequencer
  steps on that port's Clock pulses (the other port's are ignored), Start (0xFA) restarts from step 0,
  Continue (0xFB) resumes and Stop (0xFC) stops; BPM follows the incoming tempo (40..240), and 500 ms
  without a pulse stops the transport. Changing `G_CLOCK` stops it.
- Pitch bend, sustain (CC64), RPN 0 (bend range, ±0..24 semitones), CC120 / 121 / 123 are MIDI only and
  have no parameters, protocol or saved state.

## Historical v7: full backup (65-67)

INFO advertises `42 01 caps`: bit 0 = `BACKUP_LIST` / `BACKUP_GET` (read), bit 1 = `BACKUP_PUT` (restore);
this firmware sends 3. Requests name objects, never flash addresses.

| id | object | size |
| --- | --- | --- |
| 0 | runtime: the music being played now, as a FUN8 project (firmware before FM6: FUN7, 3388) | 3584 |
| 1 | settings (palette, retired speaker padding, HOLD time, favorites, panel calibration, USB audio devices, BOOT, CLK TUNE MIDI ROUT, ...), then the template (SAVE > PROJECT, SLOT TMPL) when one is saved; PER5 only (2.0: older records restore on 1.2), a tagged palette id | the settings record's size: 604, or 1924 with the template |
| 2..5 | PROJECT slots 1..4 (FUN8) | 3584, or 0 if empty |
| 6, 7 | user preset banks (slots 1..16, 17..32) | the bank's size, or 0 if empty |
| 8 | retired FM6 bank; a payload is refused at begin | 0 |
| 17, 18 | user records U33..U48, U49..U64 | 3816 or 0 |
| 19..22 | native FM6 voices F033..F064, F001..F032 | 2060 or 0 |
| 32..34 | user sample slots 1..3: header (512 bytes) then ADPCM data | 512 + data length, or 0 if empty |

Reading: `BACKUP_LIST` (no arguments) stops the transport, then takes a snapshot of the runtime object and
answers `1, rc, count` (12 with FM6, 11 before) and, per object in the order above, `id, size u32, crc u32` (CRC-32, zlib). The other objects are read as
they are in RAM or flash. Then `BACKUP_GET` reads an object in pieces: `id, offset u32, count lo, count hi`
(count 1..256, LSB first 7 bit pair) answers `id, rc, offset u32, count lo, count hi` and the data as pack7. Check each object's CRC
against the list; if it differs the device changed, so start again.

Restoring: `BACKUP_PUT` takes ids 0..8 (the samples are written with `SMP_BEGIN` / `SMP_WRITE` / `SMP_END`,
or `SMP_ERASE` for an empty slot). Begin: `0, id, size u32, crc u32`: size is 3584 (FUN8) or 3388 (FUN7, FUN6) for id 0,
the settings record's size for id 1, 3584, 3388 or 0 (empty the slot) for ids 2..5, the bank's size or 0 for 6 and 7,
0 for 8 (the retired FM6 bank). A size the firmware does not read is refused at begin (rc 1), so the web
editor begins and aborts every object first and refuses unsupported sizes before replacing any saved object. Data: `1, id, offset u32, pack7` with the next offset (they must follow each other)
and at most 256 decoded bytes. Commit: `2, id`: the device checks the length and the CRC, validates the
content, and then publishes the object. Abort: `3, id`. Id 0 replaces the music now playing;
ids 1..7 replace saved objects (settings and presets are applied too). Current nightly project
transfers stage bytes in the inactive flash copy during begin/data. Only a validated commit
publishes its header; abort, timeout, CRC failure and interruption retain the previous committed
project. Non-project objects remain in bounded RAM until commit.
The web editor sends ids 2..7 and the samples first, then 1, then 0 last. A failed restore can leave
earlier objects restored; the file is still the source.

| rc | Meaning |
| --- | --- |
| 0 | ok |
| 1 | invalid: arguments, id, size, offset, count, or an id `BACKUP_PUT` does not take |
| 2 | validation failed: CRC, length or content at commit (and a runtime object that cannot be packed) |
| 3 | stop playback first (`LIST` and `PUT` stop it themselves, and answer 3 if it does not stop in 100 ms; `GET` answered 3 while it played until the Studio session, which reads while playing) |
| 4 | flash write failed |
| 5 | stale: no `LIST` yet, the USB bus was reset, 15 s with no `PUT` request, or a project save / load reused the staging RAM (for `GET`, of id 0 only). Start again with `LIST` / `BACKUP_PUT` begin |

`BACKUP_LIST` answers `1, rc, 0` when rc is not 0. `GET` and `PUT` answer id 127 when they got no arguments.
A `LIST` replaces the snapshot, and a `PUT` begin ends it: a `GET` after a begin gets rc 5.

## FM6 patches (68-71)

The FM6 engine (12) plays one 6-operator voice per track. Its EDIT values are macros
(ALG, FB, MLVL, MRAT, MEG, VMOD, DTUN); the eighth value is unused (0..0).
The main knobs control MLVL, MRAT, MEG and DTUN. Factory voices load through the preset
browser; user presets retain the actual voice. INFO advertises `46 01 18 00` (24 factory, zero bank slots).

A patch is the 128-byte packed record of the generic 6-operator voice (the 32-voice bank's record; every byte is
7-bit, so it travels as it is, no pack7). Operators come sixth first: per operator 17 bytes (R1..R4, L1..L4,
break point, left / right depth, curves `LC | RC << 2`, `RS | DET << 3`, `AMS | KVS << 2`, output level,
`MODE | FC << 1`, fine), then pitch EG rates and levels (102..109), algorithm 0..31 (110), `FB | OKS << 3`,
LFO speed, delay, PMD, AMD, `SYNC | WAVE << 1 | PMS << 4`, transpose (24 = none), the name (10 ASCII bytes).
The device stores every value clamped into its range.

| cmd | Request args | Reply args |
| --- | --- | --- |
| 68 FM6_GET | target, index | target, index, rc, then (rc 0) the 128 bytes |
| 69 FM6_PUT | target, index, the 128 bytes | target, index, rc (target 0, a track: that track's patch from now on) |
| 70 FM6_LIST | — | nfactory, nbank, then per slot (factory first): used (0/1), name string ("" if empty) |
| 71 FM6_ERASE | bank index | index, rc |

Targets: 0 track (index 0..3), 1 retired bank (GET/PUT always rc 3),
2 factory (index 0..23, GET only), 3 user preset voice (index 0..63).
Target 3 GET returns rc 2 for a preset without a stored FM6 voice. Target 3 PUT
requires an existing FM6 user record and writes its voice (every value into its range) to native slot k. Library
transfers send UP_PUT first, then FM6_PUT target 3. UP_STORE saves both automatically.
FM6_LIST lists only factory voices; FM6_ERASE always returns rc 3 for the retired bank.
Erase a user sound through UP_ERASE. Track PUT replaces the track voice.
Other rc values: 0 success, 1 bad arguments, 2 empty voice or flash/transport error.

The web editor (6-OP FM tab) reads and writes these, and imports / exports the generic SysEx files of the
format: a single voice `F0 43 0n 00 01 1B`, the 155-byte unpacked voice, checksum, `F7` (163 bytes), and 32
voices `F0 43 0n 09 20 00`, 32 x 128 packed, checksum, `F7` (4104 bytes); the checksum is the two's complement
of the data's sum, 7 bits. Raw 155 / 4096-byte files are read too.

The editor's FM6 and library importers and the device's USB SysEx receiver also recover the DX7 voice from
the exact 231-byte wrapper `F0 43 0n 7D 04 08 <155-byte VCED> <69 opaque bytes> F7`, observed in a
MainStage-sourced `fulltines.syx`. This is a compatibility recovery, not a standard Yamaha bulk format:
the apparent byte count `04 08` is 520 and does not describe the available payload, and no checksum
scheme has been verified for this wrapper. Every VCED field must already be in range, its name printable,
and every interior byte 7-bit; other headers or lengths are rejected. The extra 69 bytes are ignored;
only the voice and name are imported, without applying any additional function or performance settings.
Exports use the standard checksummed DX7 format above. A syntactically valid change within a field's
range cannot be detected as corruption in this wrapper.

The device itself also takes these as MIDI SysEx on its USB-MIDI port (any channel `n`), so Dexed or a DX7
librarian can edit a track live: a single voice replaces the FM6 track's patch (the selected track when it plays
FM6, else track n + 1, else the first FM6 track; the notes stop, as a DX7 program change). A 32-voice dump
replaces native slots F001–F032 when EDIT > STORE > SLOT is F001–F032, or F033–F064 when SLOT is
F033–F064. Stop playback before importing a bank. Its first voice is loaded onto the FM6 track, if one
exists; all 32 voices can then be browsed as regular FM6 user presets. No web editor is needed.
A voice parameter change `F0 43 1n gg pp dd F7` edits one byte of
the patch (pp + 128 gg; 155: the six operator switches, OP1 = bit 5), a function parameter change
`F0 43 1n 08 pp dd F7` sets the FM6 function settings (64 mono, 65 bend range, 66 step, 68 glissando, 69
portamento time, 70..77 wheel / foot / breath / aftertouch range and target: that track's own, saved with the
project and the template), and the dump requests `F0 43 2n 00 F7` / `F0 43 2n 09 F7` answer with the track's
voice / the 32-slot bank selected by STORE > SLOT. Both replies use the request's channel `n`. Empty bank
slots export as INIT voices. EDIT > STORE > SEND sends the current track's single voice on its MIDI
channel (track 1–4 = channel 1–4). SysEx output uses USB; the TRS jack remains input-only.

Incoming bank bytes are preserved exactly in the native preset pool. Checksums, lengths and 7-bit
framing are validated before import. Flash commits each 16-voice object separately: a failed object
retains its previous patches. If only the first object committed, the device reports `FM6 BANK SAVE ERROR`
and `16 PATCHES SAVED`; resend the bank after resolving the storage failure. Builds with flash disabled
report `FM6 BANK IN RAM`, so those imported presets do not survive power-off. A flash-enabled build
refuses the import if flash is unavailable.

## Tagged device preferences v1

INFO appends `0x55, 1, uiCaps` after the existing `NTRK, CHAIN_ROWS` bytes.
Only this complete tagged extension enables commands 34–38. An untagged byte
from an experimental firmware is not a SONG or preference capability.
Command 33 remains SONG. Older editors can ignore the additional INFO bytes.

`uiCaps`: bit 0 palette, bit 1 font weight (retired: firmware since the 1.0 UI has one
weight and no longer sets it), bit 2 reserved for a MIDI monitor, bit 3 favorites, bit 4 recording preferences, bit 5 layer/screen preferences, bit 6 click level in percent. This build
advertises 121. Layer preference 9 is retired; screen preference 10 remains.

| cmd | Request args | Reply args |
| --- | --- | --- |
| 34 UI_STATE | none | caps, palette, font, monitor, filter, favoriteSig u28, bankSig u28, [click, clickLevel, countin, preview, chordAdd, retired=127, screen] |
| 35 UI_SET | id, value | rc, id, value, UI_STATE payload |
| 36 UI_PALETTES | none | count, count × name string |
| 37 FAV_GET | engine, start v14, count 1..32 | rc; on success: engine, start v14, count, count × on/off |
| 38 FAV_SET | engine, preset v14, on/off | rc; when applied: engine, preset v14, on/off |

UI_SET ids: 0 palette (0..count-1, the order of UI_PALETTES: MONO GREEN AMBER ICE VIOLET ROSE PAPER
HI-CON), 1 font (retired: rc 2), 2 reserved, 3 preset filter (0 all, 1 favorites). Unsupported state
fields are 127. With capability bit 4, UI_STATE appends five bytes after its
13-byte prefix. UI_SET ids 4 click (0 OFF, 1 REC, 2 ON), 5 click level (with bit 6:
0..100 %; without it: 0 LOW, 1 MID, 2 HIGH), 6 count-in (0 OFF, 1 BAR, 2 BARS), 7 note preview (0/1), and
8 chord entry (0 HOLD, 1 ADD) all share capability bit 4. Older clients may
ignore the suffix; newer clients require both the bit and the full suffix.
Both u28 signatures are four least-significant-first 7-bit bytes; compare them
to refresh changed favorites and user slots. Factory references use stable
engine/preset ids; engine NENGINES denotes a user slot. An empty user slot
cannot be marked. Factory DRUM sounds use the same favorite path.
MIDI Track (engine 21) has a loadable preset but no factory favorite storage;
1.5 firmware rejects FAV_GET/FAV_SET for it with rc 1. Editors must skip its
favorite requests and leave its star control disabled.
INFO limits engine names to eight bytes, so its wire name is `MIDI Tra`.
Identify this favorite exclusion by stable engine id 21, not its display name.

rc 0 = applied and saved; 1 = invalid arguments; 2 = unsupported feature;
3 = applied in RAM but not saved (no flash or a failed write);
4 = applied and queued to save after STOP. A failed settings write is retried
by the same persistence path as the panel. These changes never stop playback.
Unchanged writes do not erase flash. Replies echo preference ids/values and
favorite ranges so the editor rejects replies to a different request.

## USB audio diagnostics

Firmware built with USB audio (`MELODEE_USB_AUDIO`, the default) answers command 72 with its USB audio counters;
without it there is no answer. `tools/usb_audio_stats.py` reads them.

| cmd | Request args | Reply args |
| --- | --- | --- |
| 72 AUDIO_STATS | — or 1 (start new maxima after this reply) | schema (2), then 20 counters, each 5 × 7 bits, LSB first: play alt, capture alt, play rate, capture rate (Hz), play fill, capture fill (frames), play underruns, play overruns, capture underruns, capture overruns, bad packets, packets received, packets sent, missed USB frames, longest gap between services (µs), longest service (µs), late renders, the feedback (10.14), longest render (µs), CPU (Q8) |


## OBXF (engine 14)

OBXF is the hardware-float OB-Xf port. `NAMES` exposes 75 curated CC0 factory patches.
EDIT/HOME macros are CUT, RES, ENV, ATK, DEC, REL, DTN, PTCH; the first seven are neutral at zero.
On-device OBXF pages edit the full 95-value patch (HQ is retained but not rendered).
User presets retain PTCH and macros, like FM6. Projects, templates and runtime backups retain the edited patch
and its name, independently of PTCH. Full-patch SysEx transfer and dedicated web pages are deferred.

## Native CZ-1 tones (commands 75/76)

INFO advertises native tones with the tagged capability `43 01 10 01` (hex):
CZ support, version 1, 16-byte native names, bank support. Retired sample commands
no longer upload material; SMP_INFO reports zero factory and user slots.

- **75 CZ_GET**: arguments `target index`; target 0 = track (0..3), 1 = user preset
  (0..127). Reply `target index rc`, followed on success by 288 low-first nibbles
  encoding the complete 144-byte native tone.
- **76 CZ_PUT**: arguments `target index` followed by those 288 nibbles. For a bank
  slot only, an optional 32-byte ordinary pattern may follow. Reply `target index rc`.

Invalid lengths, nibble values and native synthesis parameters are rejected before
changing the track or flash. An unused/non-native preset GET returns an error.
Track PUT selects engine 15 (CZ-1), native tone marker 2, resets engine-specific controls to
neutral defaults, and preserves the track's musical/routing settings.
Preset record version 8 preserves all native bytes in a 238-byte record. Earlier 192-byte banks and next’s v6/v7 CZ records remain readable.
FUN13 projects (12352 bytes), FBKG pattern banks (27200 bytes) and TPLB templates preserve each track's native
tone; older formats remain readable. See [native tones](../docs/CZ1_SYSEX.md).

DRUM now has one factory preset, **808 KIT** (index 0); KIT's stored value stays 4.
Older custom KIT values render the 808. No 909 is included.

OBXF is removed from synthesis, presets, editing and patch storage. Engine 14 stays reserved.

### Legacy CZ bank compatibility

INFO advertises zero bank count and zero slots per bank. Command 77 and CZ_GET target 2 remain
readable for older clients, but the current editor uses command 78. Backup objects 9–16 retain their
2332-byte CZBK layout: LE magic 0x42435A43, u16 version 1 or 2, u16 slot count 16, u32 used mask,
16 bank-name bytes, then 16 complete 144-byte native tones. Version 2 marks completion of native-slot
migration, including empty collections. Factory tones are separate; unsaved user slots start empty.

Earlier next FUNA/FUNB, FBKB/FBKC and TPL8/TPL9 CZ sounds are migrated to raw native tones; common sound settings and patterns are retained.

Recorded synth gates: when a NOTE or TIE has no lane hits, its stored accent byte
holds the final gate as 1..255/255 of that swung step (0 uses the legacy track
GATE). FUN11/FBKE retain these gates without changing record sizes. FUN10/FBKD
and earlier formats remain readable with their original accent validation.
STEP_SET/TRACK_STEP keep existing recorded gates on hit-free notes; converting
to lane hits clears the gate. The 12-byte editor reply continues to report lane
accents only. Full backups retain recorded gates.


### Original performance timing (FUN13 / FBKG)

Track parameter **8**, previously the inert `P_ED_FX` field, is now `P_RECQ`: **OFF** (0),
then the ten existing DIV values plus one (1/4 = 1, 1/16 = 3, 16T = 6, 4BAR = 10).
P_COUNT stays 92 and P_E0 stays 84. This parameter is project-wide, survives sound loads, and
is shown on SEQ > TEMPO and the web sequencer. Scale QNT remains parameter 27. Old projects
and templates that predate recording QNT initialize it to OFF. Templates retain it
without changing size. Runtime values are mirrored in every track slot for project
and protocol compatibility: SET and TRACK_PARAM for parameter 8 change all eight
tracks. Loading older projects with differing values adopts the selected
track's QNT (including MIDI tracks loaded from the FBKL tail).
Web library files use `TQNT` for parameter 8 and keep `QNT` for scale parameter 27, so older
library files retain their scale mapping. The timing page appears only when the device advertises it.

Live recording captures original note edges. When the take finishes, timing QNT commits the new
take's onsets to the nearest swung boundary of the selected division, with loop-end mapping to zero.
Each note retains its captured duration; its release shifts with its committed onset. OFF leaves
the take unchanged. Changing QNT afterward configures the next take without changing playback.
Repeated hits within one step and independent chord releases are retained. A destination occupied
by a manual step or its tie tail rejects the pass without modifying the take or the existing steps.
Playback fires each stored event at most once per loop.

FUN13 LE magic is `0x46554E3D`, size 12352. The original FM6/CZ payload offsets stay fixed.
Header bytes 65 and 67 hold a 12-bit active-bank selection (four 3-bit bank ids, low byte at 65).
At offset 4144 are 1024 eight-byte timed-note records: LE u16 onset fraction, LE u16 duration,
note byte, velocity byte, owner byte, step byte. Velocity zero means unused. Owner low five bits
are `track * 8 + bank`; high three bits are the duration exponent. Step low six bits select the
original onset step, bit 6 marks an overview rounded to its following step, and bit 7 marks overview
wrap to zero. Onset is 0..65535/65536 of that swung step; duration is `u16 * 2^exponent / 65536`
nominal pattern steps. The name remains the final 12 bytes before the FNV-1a checksum.

Step flag 4 marks the overview of recorded events. FUN13 encodes it in the high bit of the step's
metadata byte. FBKG LE magic is `0x474B4246`, size 27200. Each A/B copy retains its five base sectors and
uses two disjoint extension sectors in `0xEA000..0xF9FFF`. The last 256 bytes of each extension
sector stay erased for the SPL update-record scan; payload capacity is 27904 bytes.
The single 256-byte storage header commits last and its CRC covers the complete logical payload. Its inactive steps retain their eight-byte size: four 7-bit notes, then
4 metadata bits (`NOTE n` = 0..4, TIE = 5, REST = 6; bit 3 is the recorded flag), two ordinary flags,
7 velocity bits, 8 hits, 8 accent/gate bits, and 7 probability bits. Counts on REST/TIE normalize to
zero. FUN12/FBKF (152 notes), FUN11/FBKE, FUN10/FBKD and previous formats keep their original decoders and migrate on save.

The 12-byte STEP_SET/TRACK_STEP protocol remains a conventional step edit; it converts that overview
group to manual step playback. Replies mask the internal recorded flag. Full runtime/project backups
retain original timing and every bank. User-preset patterns carry the overview only. Capacity is 1024
notes across the project; overflow reports RECORDING FULL without overwriting existing entries.

All external effects have been removed. Track ids 33..36 and 45..48, global ids
4..11 and 24..26, and modulation destination ids 6..9 retain their positions as
inert format padding. Their labels are empty and editors must hide them. Old
project/preset bytes still parse; runtime loads clear the track/global values,
and old motion or modulation routes to these ids are ignored. New FX motion
cannot be captured. CC91 and CC93 have no effect. No FX pages, hold layer, key
assignment or latch setting remain; UI preference 9 reports 127 (unsupported)
and UI_SET for id 9 returns rc 2. The physical FX button has no action.

## Shared user library (command 80)

INFO appends `55 01 00 03`: shared-library tag U, version 1, capacity **384** as an unsigned
low-first 7-bit pair. Modern clients use command **80** for every engine and every user slot.
All indices below are absolute zero-based shared slots; they are not positions in an engine pool.

Request header: `operation slotLo slotHi`. Every reply begins
`operation slotLo slotHi rc` (0 success, 1 invalid/empty, 2 refused or save error).

| Operation | Request arguments after the header | Successful reply after `rc` |
|---|---|---|
| 0 List | count (1–16) | actualCount totalLo totalHi, then used engine oldFormat name-Z per slot |
| 1 Get | none | used engine name-Z, P_COUNT signed-v14 values, 32 empty legacy phrase bytes, nativeKind [sizeLo sizeHi pack7(raw)] |
| 2 Put | engine name-Z, P_COUNT signed-v14 values, 32 legacy phrase bytes, nativeKind [sizeLo sizeHi pack7(raw)] | none |
| 3 Store track | track, optional printable name bytes without a terminator | none |
| 4 Load track | track | none |
| 5 Erase | none | none |
| 6 Rename | printable name bytes without a terminator | none |

Native kinds: 0 parameters only, 1 DX7 VMEM (128 bytes), 2 CZ-1 (144 bytes), 3 Prophet
(138 bytes, including model/size/command/address metadata), 4 drum sound offsets (54 bytes).
PUT kind bit 5 preserves native name bytes while assigning the short browser name.
PUT kind bit 6 requests default Felucca controls for an imported native synth voice; otherwise
its common sound controls come from the supplied parameters. Names accept up to 20 bytes for
native voices (the browser's short label remains 12); ordinary sound names accept 12.
Phrase bytes remain present for framing compatibility and are not loaded or stored on flash.
GET preserves the complete drum patch, including offsets for all sixteen sounds.

All save paths share the same occupancy and overwrite rules. Device SysEx imports ask before
replacing any occupied slot, regardless of its engine. Native bank imports validate the full
transfer before writing and commit once per affected storage object. On failure, only the
unsaved suffix remains staged. Audio and receive callbacks never access the library cache.
Loading restores the sound and preserves track mix, effects, scale, ARP, and sequence data.
Favorites share each preset's atomic commit; changes made while playing are queued until STOP.
DUMP and native status replies continue to encode the loaded slot plus one with two 7-bit bytes.
Projects use an optional `U,3` origin trailer with four little-endian 16-bit shared references;
embedded sound bytes remain authoritative even when an origin slot is erased.

Flash format UPB2 (`0x32425055`): 8-byte header (record size 206, 18 records), followed by
18 records, total **3716 bytes**. The storage commit header uses reserved word 0 tag
`0x32504D43` (CMP2) to put the payload at byte 32 in every reused pair. Old copies retain
their original offset during conversion; the last 256 bytes of each new copy remain erased
for the stock loader's update-record scan. A compact record contains the first 160 bytes and final 46 bytes
of the existing 238-byte sound record; the unused 32 phrase bytes are omitted. Native versions
11 (FM6), 12 (Prophet), and 13 (CZ) put their voice in the 144-byte payload and 45 named sound
controls in the tail. Used-byte bit 1 is the persistent favorite flag. General parameters and
drum patches retain their existing versions. Unknown records stay inert and protected.

BACKUP_LIST contains **29 objects, IDs 0–28**. Existing object IDs and physical A/B pairs remain:

| Shared bank (18 slots per bank) | Backup IDs | Previous owner |
|---|---|---|
| 0–3 | 6, 7, 17, 18 | General presets |
| 4–7 | 21, 22, 19, 20 | FM6 voices |
| 8–15 | 9–16 | CZ-1 tones |
| 16–20 | 23–27 | Current Prophet programs |
| 21 (six visible slots) | 28 | Previously free 0x9D000/0x9E000 pair |

Normal nightly builds keep 4,104 bytes of shared wire scratch. Runtime LIST snapshots and
project PUT payloads use an unpublished inactive project copy, spanning its base and extension
sectors. LIST and begin/abort preflight therefore incur flash erases, while leaving saved project
headers and current payloads intact. GET reads the exact frozen copy across screen draws.
Saves capture decoded active state and the inactive-pattern tail together, stage the tail first,
then reuse its RAM for active serialization. All four saved slots and existing file bytes remain.
Profiling builds retain the complete wire buffer so guarded diagnostic LIST/runtime PUT sessions
continue to operate entirely in RAM, without flash erases or writes.

ID 8 remains empty and refuses payloads. First boot converts current preceding formats in place
using A/B commits. General FM6 sidecars are embedded before their source banks are converted.
The eight excess programs in each old 26-program Prophet bank move to the two spare records
in each preceding 16-record bank; the last six use bank 21. Destinations are deterministic, so
an interrupted conversion retries identical writes. Foreign occupied spill destinations and
unsupported bank shapes are preserved; an incomplete conversion blocks subsequent preset writes.
Boot maintenance initializes the display before any conversion writes and shows
`MIGRATING PRESETS`, completed-bank progress, and `KEEP POWER CONNECTED`.
Update staging cleanup uses the same screen (`FINISHING UPDATE`) with sector progress.
Failures remain visible with restart-to-retry guidance. On preset conversion errors, OCT+
keeps presets and continues with preset writes protected; OCT- opens a destructive wipe
confirmation (OCT- back, OCT+ wipe). Update cleanup errors never offer preset wiping.
Confirmed wiping clears both A/B copies of every shared/preceding preset object, retaining
projects and settings. Bank 21's final unused record (index 17, outside the 384 visible slots)
reserves the 12-byte name `WIPE-PENDING` in an otherwise empty record as durable reset intent.
This is committed before source banks are cleared and removed only after every preceding
copy is empty; interrupted resets resume wiping before attempting any conversion. Successful
reset unlocks preset writes, invalidates loaded slot references and stops future migration
retries. Wipe errors remain visible with writes protected. Clean boots skip maintenance.
The retired Prophet format is never migrated. Unknown committed bytes remain in full backups.
A complete preceding 28-object restore clears the derived extension before conversion; runtime
music restores last, after all source banks are present.

Legacy commands 16–21 address the first 128 shared positions. Command 78 retains the older
64-FM6/128-CZ/128-Prophet addressable prefixes and manufacturer wire framing; it refuses to
replace or erase a slot belonging to another engine. Its INFO capabilities remain compatible.
Clients that need the full bank or mixed-engine writes must use command 80. The historical
storage descriptions below apply to backups from older firmware.

## Previous user storage

UP_LIST reports 64 shared user slots, U01..U64. All slot indices remain 7-bit.
BACKUP_LIST lists ids 0..27. Id 8 is always empty (the retired FM6 bank) and refuses a payload.
Ids 17 and 18 hold user records U33..U48 and U49..U64 (3816-byte UPB1 banks). Ids 19..22 hold the
native FM6 voice banks F033..F048, F049..F064, F001..F016, F017..F032 (2060 bytes each); the owned-voice
objects (UPF6, 3728 or 4240 bytes) that ids 19 and 20 held before 1.0 are refused since 2.0
(Melodee 1.2 restores and rewrites them). An FM6 general preset's voice is native slot k.

On flash, extension user banks omit their eight-byte UPB1 header (3808 record
bytes), which is reconstructed at boot and included in backups. New storage objects
start payloads at byte 32 and leave bytes 3840..4095 erased for OTA record scanning.
Existing user record banks and storage object numbers remain in place.


## Previous native FM6 and CZ-1 pools (command 78)

INFO appends `4E 01 40 00 00 01`: tag N, version 1, FM6 capacity u14 = 64, CZ capacity u14 = 128.
These are unsigned low-first 7-bit pairs (unlike signed v14). Native slots appear after each engine's
factory presets in global and engine-specific scrolling; only used slots are listed for playing.
SAVE > USER and FM6 STORE write to these collections. They store no common Felucca parameters,
patterns or function settings. Loading preserves those track settings and same-engine macros;
changing engine initializes only its engine-specific controls. General UP_* commands retain 64 legacy/general
slots for compatibility. New native transfers use command 78 rather than UP_PUT + a sidecar voice.

Request: `engine op index [data]`, engine 12 FM6 or 15 CZ-1. Reply: `engine op index rc [data]`.
Indices are zero-based 7-bit bytes, including CZ index 127. rc 0 success, 1 invalid/empty load,
2 empty GET or flash error, 3 transport could not stop. Writes stop transport before touching flash.

| op | request data | successful reply data |
|---|---|---|
| 0 list | count 1..16, index is page start | count clamped to capacity, capacity u14, count × (used, name NUL) |
| 1 get | none | FM6: 128 raw 7-bit VMEM bytes; CZ: 144 low-first nibble pairs (288 bytes) |
| 2 put | native bytes as above | none |
| 3 store track tone | track 0..3 | none |
| 4 load slot to track | track 0..3 | none |
| 5 erase | none | none |
| 6 rename | ASCII name, 1..10 FM6 or 1..16 CZ bytes; no terminator | none |
| 7 current slot | none; index is track 0..3 | slot+1 u14, or zero if no native origin |

List names use at most 12 characters for display; GET preserves the full native name (10 FM6, 16 CZ).
The web editor imports DX7/Dexed .syx voices into free FM6 slots and Casio .syx frames into free CZ slots;
collection exports use those same native formats. Native favorites use frozen categories 17 (FM6)
and 18 (CZ), independent of NENGINES, through FAV_GET/SET with their existing signed v14 index encoding.

Before Prophet, complete backup inventories contained 23 objects, ids 0..22. Current inventories add 23..27, as documented below. General ids 6,7,17,18 are unchanged.
CZ ids 9..16 retain the layout above. FM6 ids 21,22,19,20 hold F001..F016, F017..F032, F033..F048,
F049..F064 respectively. Each is 2060 bytes: LE magic 0x314D464E (NFM1), u16 version 1, u16 slot count
16, u32 used mask (low 16 bits), then 16 × 128 native VMEM bytes. Used voices must contain only 7-bit
bytes. Empty restore writes a valid empty object so erased slots cannot resurrect on reboot.
Old 3728-byte UPF6 objects 19/20 remain accepted for owned-voice migration. Retired selector-based
bank object 8 accepts only an empty restore.

Native FM6 storage uses new objects 23/24 (A/B pairs 0xD8000..0xDBFFF) for slots 1..32, and reuses
old objects 7/22 for slots 33..64. Migration commits the new objects before reusing old voice sectors,
then clears successfully migrated shared records. Existing CZ bank tones keep their indices; embedded
CZ user tones move into free native slots, without overwriting existing tones. If the CZ collection is full,
unmatched general records remain accessible. All new objects use the existing CRC-checked A/B save path.

### Microtonal SCL selections

Track parameter 26 (SCL) now has 76 names (IDs 0–75); the original 0–15 retain their meanings.
IDs 16–69 are pitch tables, 70–75 are 12-tone maqam scales. DESC replies carry up to 96 enum names
within the existing 1024-byte response buffer. Consumers should read the names present in the reply,
allowing older firmware's shorter descriptors. A device whose list has `BAYAT-D` has tunings: leave
its retired entries out of a picker unless selected (the catalogue says which).
Track parameter 47 (TUNING) is the tuning, 0 Equal: an enum on firmware that has it, an empty label
on older firmware. Like SCL it is shared across tracks, and setting SCL to a maqam also sets it, reported as a change.
It is not a sound's: preset loads leave it, and motion ignores it.
With a pitch-table scale and QNT enabled, note addresses are scale degrees around address 60 = ROOT at
C4 + TRN. Recordings and STEP/NOTES keep these 7-bit addresses. MIDI OUT sends the nearest semitone,
without tuning messages. See [the catalogue](../assets/scales/README.md) for mapping and register limits.


### Prophet native programs (engine 19)

INFO adds the capability bytes `35 01 13 10 11 12 14 00 01` (hex): tag 0x35,
version 1, engine ID 19, frozen general/FM6/CZ/Prophet-user categories
16/17/18/20, flags 0, native protocol version 1. Native collection capacities
also include engine 19 with 128 slots. Never derive collection categories from
the engine count when this namespace block is present. Factory Prophet
favorites use category 19; general presets retain category 16. Preset command
8 recognizes factory engine IDs only: use native command 78 for user slots.
General UP_STORE/UP_PUT reject engine 19; full Prophet records use native storage.

**Command 95:** `operation track [data]`, track 0–3.

| operation | request data | reply |
| --- | --- | --- |
| 0 exact GET | none | `1 rc track` plus native frame without F0/F7 |
| 1 complete PUT | native frame without F0/F7 | `1 rc track` |
| 2 panel field | raw offset, value | `2 rc track` |
| 3 rename | 1–20 printable ASCII bytes, no terminator | `3 rc track` |
| 4 send native edit dump | none | `4 rc track`, plus a separate native dump |

rc 0 success, 1 invalid arguments/frame. Field/rename operations require engine
19 on the destination track. Field offsets are 0–54, 86 (bend) and 87
(priority/retrigger); only defined descriptors are accepted. Bool fields are
0/1; knobs usually 0–120, Fine B/Poly-Mod envelope/Vintage 0–127; keyboard
tracking 0–2, unison count 1–5, detune 0–7, bend raw 0–11, priority 0–3.
Bend displays 1–12 semitones. Imported native records can retain values above
panel limits: DSP clamps these without rewriting them.

A complete PUT validates before changing RAM, initializes native performance,
sets engine 19, resets its eight common macros and clears user-slot origin.
It retains matrix, scales, chords and sequencer and participates in
sound undo/redo. Whole patches replace sounding voices using the normal panic
fade. Field edits affect held notes; unknown/opaque fields cannot be addressed
by panel operation 2. Native names occupy raw 65–84.

Native frames use `F0 01 model command ... F7`, model 0x31 or 0x32, command
2 (program: group 0–9, program 0–39) or 3 (edit buffer). Payloads are exactly
128 raw / 147 packed bytes or 133 raw / 152 packed bytes. Eight-byte packed
groups contain a high-bit mask then up to seven low-seven-bit bytes. Final
unused mask bits must be zero. Length, embedded status bytes and addresses
are checked before mutation. Exact GET/export preserves original model,
length, command, address and all unused bytes. Native dump send uses command 3.

Direct USB native command 2/3 stages the validated named patch and opens the Prophet USER list for
destination selection. OCT+ > Import here commits it while stopped; occupied slots ask first.
Command 95 PUT still loads the selected RAM track without a flash
save. Native command 6 requests its edit dump. Command 5 plus group/program
requests a used user slot at `group * 40 + program`, limited to 0–127.
Unsupported/empty program requests have no native reply. Native NRPN/CC
parameter editing and TRS native SysEx are outside this implementation.

Command **78** adds engine 19 using the same operations as FM6/CZ. Its native
record is 138 bytes: raw[133], raw size, model, command, group, program. All
138 bytes use low-first nibble pairs (276 transfer bytes). Rename permits
1–20 characters, GET retains all twenty, LIST truncates only its display name
to twelve. LOAD sets native performance and keeps track matrix/patterns.
The dedicated collection stores the complete native record rather than common
Melodee parameters. Native slot indices remain unsigned bytes 0–127.

**Backups:** inventory IDs 0–22 remain fixed; 23–27 are Prophet banks. Each bank
is 3600 bytes: LE u32 magic `0x31553550` (P5U1), used mask, favorite mask,
then 26 complete 138-byte records. Banks 0–3 use mask bits 0–25; bank 4 uses
0–23. Bank 0's favorite bit 31 holds the legacy factory INIT star; current
factory Prophet stars, including all 200 programs, live in the settings record.
Other high bits are invalid. Used records require valid wire metadata. Fresh
user banks and an empty restore are empty; factory programs remain available
in the regular browser. Older archives that omit these objects leave them intact.
The editor validates nonempty Prophet objects and target capability before
starting restore writes.

**Projects:** FUN14 magic `0x46554E3E`, size 12904, preserves all previous
FM6/CZ/timed-note offsets. Four 138-byte Prophet records start at offset
12336; name/checksum move to the final sixteen bytes. FBKH magic `0x484B4246`,
size 27752, embeds the FUN14 record at offset 8; its extra-bank sections follow
the larger record. TPLC magic `0x434C5054` includes the same four full native
records. Older FUN13/FBKG/TPLB and earlier data migrate with INIT Prophet
records for tracks without native data. Projects, runtime backups, templates
and sound undo/redo retain opaque bytes independent of user collection slots.
Current projects also keep each Prophet track's user-slot origin in reserved
bytes so browsing resumes at that slot. Older projects locate an unchanged
embedded factory or user patch by its complete record.

The app boundary is 0x89000. Five native A/B pairs occupy 0x89000–0x92FFF.
Normal OTA/loader paths reject mismatched older app extents before writing.
See [prototype validation and release gates](../docs/PROPHET5_PROTOTYPE.md).

### Second-core diagnostic extension

AUDIO_STATS (72) flag bit 2 (`4`) requests schema 4: the 20 schema-2 counters,
the six schema-3 voice counters, then six further 32-bit values encoded as five
7-bit bytes each: `core1_online`, `core1_jobs`, `core1_max_job_us`,
`core1_max_wait_us`, `core1_timeouts`, `fm6_pairs`. A serial build returns zero
for these six values. Existing requests still receive schema 2 or 3. Flag bit 0
also clears worker job/wait maxima after the snapshot; counts remain cumulative.
The worker is idle whenever the main-loop handler takes this snapshot.
With flag 4, schema-3 fields 22 and 23 (formerly OBXF's voices, always 0) carry
`core1_rejected` (requests CPU1 misread and refused) and `core1_faults` (CPU1
exceptions caught); either retires the worker (`core1_online` 0). Both stay 0 on
a healthy unit. Test builds (`MELODEE_CORE1_TEST=1`) add command 79: argument 0 report, 1 inject
a misread request, 2 a job at address 0, 3 a job that never returns, 4 reboot (a
second argument 1 keeps CPU1 barred, otherwise the bar clears as at power-off),
10 crash CPU0 (no reply); the reply is mode, rc, then eleven 32-bit values as
five 7-bit bytes each. Arguments 5 (rails, read only, and clock), 7 count
misreads only (0/1) and 8 hold/release eight notes on track 0 reply mode, rc and
sixteen such values; 9 replies mode, rc and P33 0x00..0x3F, 0x72, 0x74, 0x90,
0x92, 0x9B as 7-bit pairs (see `tools/core1_fault_test.py`).
`core1_jobs` includes both FM6 and Prophet sample jobs; `fm6_pairs` continues to
count only FM6 pairs. For Prophet measurements, combine worker jobs with the
schema-3 voice counts and audio timing counters.

### Recording/performance data extension

P_COUNT is 100 and P_E0 is 92. Common IDs 84–91 are drum lane levels
(KICK, SNARE, CLAP, HATCL, HATOP, TOM, RIM, BELL), range 0–127, default 127.
FUN15 magic `0x46554E3F`, size 12936, moves the FUN14 FM6/CZ/note/Prophet
tail offsets by 32 bytes. FBKI magic `0x494B4246`, size 27784, embeds FUN15
at offset 8. TPLD magic `0x444C5054` adds 64 bytes of track parameters to TPLC.
FUN14/FBKH/TPLC and earlier data remain readable, with missing lane levels
initialized to 127. Settings retain PER5 and its template boundary; a
`0x5250` high-word tag in the unused zoom word identifies recording preferences.

### Native synth motion (1.0.2)

Native events use parameter `112 + patch_byte` (up to 255) and value
`engine_tag << 8 | byte_value`, with tags 1 = Prophet, 2 = CZ-1, 3 = FM6. Voice assignment,
CZ line selection, FM6 transpose and names are excluded. Fields and values are validated.
The stored four-byte event and project extents are unchanged; older firmware rejects native events.
The project snapshot saves the original patch bytes, independently of sounding motion.

In command 64 queries, writes and deletes, the high bit of the parameter is carried in bit 6
of the step byte: `wire_step = step | ((param & 128) >> 1)`, `wire_param = param & 127`.
Decode `step = wire_step & 63`, `param = wire_param | ((wire_step & 64) << 1)`.
Existing events keep their original wire representation. Values retain the signed 14-bit encoding.
The web editor displays and edits the raw native byte while preserving its engine tag.

With three track FX slots (`P_COUNT = 117`, `P_E0 = 109`), common engine IDs
112–115 overlap the first native IDs. Native values carry a tag and are at least
256; ordinary values remain -64..127. Both kinds may coexist at the same step
and raw ID. A six-byte delete request includes the event's v14 to select its
kind. The legacy four-byte delete removes both kinds at that step and ID.
Editors should send typed deletes when INFO reports `pe0 >= 108`.

FX slots occupy IDs 96–99, 100–103 and 104–107 (TYPE, MIX, RATE, CHAR). They
belong to the track; preset loads and sound undo preserve them. Projects and
templates store all three slots. REVERB is appended as type 8; previous type IDs
are unchanged.

INFO advertises motion capability version 2 (`4D 02 40 01`), so cached v1 editors
do not offer a motion interface that would truncate native parameter IDs. The current editor
accepts both versions 1 and 2 and retains all other tagged capabilities.

The send bus appends track SEND at ID 108; engine parameters are 109–116. Globals 27–38 are three shared FX slots (TYPE/MIX/RATE/CHAR); global 39 is RETURN. `G_COUNT=40`. SEND is track-owned during preset loads; shared controls are song globals. FUN19/TPLI store the routing; FUN18/TPLH and earlier records map engine IDs by count and default shared FX off. Native tagged motion IDs retain their meaning during conversion.

## Complete audio profiling (laboratory builds)

`72 AUDIO_STATS`, flag `32`, returns schema 7: all 55 schema-6 fields followed
by complete interrupt maximum bound (us), last complete C span (us), elapsed C
deadline violations, 80% target violations, cross-track batch pairs, occupied
slot capacity steals, admission rejections, admission-enabled flag, and fixed
benchmark status. Benchmark status bit 0 is its lease; higher bits count
suppressed emergency fade requests. Existing diagnostic flags remain compatible.
The complete bound includes an assembly entry/exit envelope, the largest observed
nested TIMER5 span as a footer-preemption allowance, and 4 us wrapper allowance.

With `MELODEE_AUDIO_PROFILE=1`, editor 98 (`[]` or `[0]` read, `[1]` read then
reset scope totals) replies `rc, schema=1, scope_count=21`, then three unsigned
32-bit values per scope (five little-endian 7-bit bytes each): total TIMER4 ticks,
calls, maximum ticks. Scope order is defined in `audio_profile.h`. Five trailing
values are worker ticks, CPU0 waiting ticks, lifetime worker jobs, waits since
scope reset, and current SYS PLL clock in MHz. Inclusive/nested scopes must not
be summed; worker engine spans are published privately and merged after joins.
The FX span includes track inserts, shared preparation and the CPU1 shared
kernel; output includes master processing, USB copy and output conversion.

Editor 100 is a read-only target query available in production too. It replies
`rc=0, schema=2, count=12`, then twelve u32 values in five 7-bit bytes each:
PLL clock MHz, maximum TIMER5 ticks, maximum assembly audio-envelope ticks,
profiling-enabled, benchmark-support-enabled, admission-enabled, half-buffer
frames, four native engine capacities, and the selected shared workload budget
in points. Schema 1 has the same eleven-field prefix but reports the earlier
fixed track reservations; readers should accept both schemas. TIMER4 runs at
24 MHz. The footer
allowance above is valid only while maximum TIMER5 service is below its 100-us
period (2400 ticks). This command writes no musical or persisted state.

With `MELODEE_AUDIO_BENCH=1`, editor 99 `[0|1]` disables/enables a 30-second fixed
voice lease. It bypasses normal admission and records emergency requests without
cutting voices, preventing overloaded runs from reducing their own workload.
`[2, track, mode]` sets the live FM6 render mode (0 MODERN, 1 MARK I, 2 OPL) for
laboratory coverage. Both reply `rc`. Neither writes saved projects or patches.
These controls are absent from production builds. A passing performance contract
must also be checked with normal admission and emergency protection enabled.

### Laboratory persistence guard and transport/REC controls

With `MELODEE_AUDIO_PROFILE=1`, editor 98 also accepts:

- `[2]`: the bounded audio-envelope histogram. Reply `rc=0, 2, bins=64`,
  then u32 bin width in TIMER4 ticks, u32 maximum envelope ticks and 64 u32 bin
  counts (five 7-bit bytes each). The last bin counts every longer buffer.
- `[3]` reads and `[3, 0|1]` clears/sets the persistence guard. Reply
  `rc=0, 3, guard`. Setting it replies only `3` while the transport is busy
  (playing, counting in, song chain, pending Start), a momentary snapshot or an
  old template is active, or any voice sounds. The guard remembers the current
  project slot; clearing restores it. While set, autosave, kept-value and settings
  writes are skipped, storage commits fail, and editor 67 restores preferences in
  RAM only. A malformed request replies only `1`.
- `[4]`: committed storage headers. Reply `rc=0, 4, count`, then per object
  a present flag and u32 length, CRC and sequence.

Editor 101 exists only when **both** `MELODEE_AUDIO_PROFILE=1` and
`MELODEE_AUDIO_BENCH=1`; other builds give it no reply. It drives the same
queued transport and REC arming as the panel, for guarded CPU fixtures. It adds
no state, persistence or audio-tick work. Request `[op, mask]`, exactly two bytes:

| op | Action | mask |
| --- | --- | --- |
| 0 | Query | 0 |
| 1 | Queue internal-clock Start (count-in applies as on the panel) | 0 |
| 2 | Queue Stop; also cancels a pending Start or count-in | 0 |
| 3 | Arm latched REC | one track bit |
| 4 | Arm one-pass REC | one track bit |
| 5 | Disarm (seal, finish and quantize the take as a REC tap does) | nonzero track mask |

The reply is always ten bytes: `rc, schema=1, playing, counting, rec, rec_wait,
rec_pass, rec_latch, pending, clock`. The fields are one snapshot taken after the
operation with audio interrupts paused. `rec*` are NTRK-bit track masks, `pending` is
the queued transport request (0 none, 1 Start, 2 Stop, 3 restart) and `clock` is
CLK (0 INT, 1 USB, 2 TRS).

`rc` 0 means accepted. `rc` 1 is a malformed request: wrong length, op above 5,
a mask beyond NTRK, a mask present for ops 0–2 or missing for ops 3–5, or
more than one track bit for ops 3–4.
`rc` 3 is refused: no persistence guard (every op, including query); Start or
arming while a song chain is armed or running, a momentary snapshot is active or
the template is old; Start with external clock, while playing or counting in, or
with any transport request pending; arming a track beyond the tracks in use.
Stop and disarm need only the guard, so cleanup stays possible.

Start acknowledges a queued request, not a running transport: the audio interrupt
applies it at its next block. Poll query until `pending=0` and `playing=1` (or
`counting=1`). Never resend Start after a lost reply; query, then Stop if needed.
Query, Stop and disarm are idempotent. Arming follows native REC: during playback
a newly armed track finishes and disarms any previous track, waits for loop zero
(`rec_wait`), and a one-pass take disarms
after its pass. Arming an already armed track leaves it, including its latch mode,
unchanged, so a retried arm cannot alter the take. Recorded notes change only the
live project, exactly as panel recording would.

### Shared workload budget

Global ID 41 is BUDGET (`G_COUNT=42`), range 8–32 integer points. Existing
global IDs remain unchanged; FXMODE is 40. GLO > GLOBAL knob 2 edits this
device setting. Like A4, it is a virtual global value preserved across project
and template loads. A tagged high word (`0x5742`) in PER5 `ext.usb_off` stores
nondefault budgets in bits 8–15 while retaining the low two USB flags. The
604-byte settings record and project/template layouts are unchanged. Unknown
or absent tags use the build default (16 with admission enabled).

GLO > WEIGHTS shows device settings on the reserved global IDs 4–9 (P5, CZ-1,
FM6, SID and DRUM points per voice, 0 = AUTO; SAFETY). They are panel-only:
`global_reserved()` still covers these IDs, so the editor reads 0 and cannot set
them, and projects never store them. PER5 favorites row 8 bytes 8–15 hold them
('W', version 1, five points, bit 0 of the last byte SAFETY OFF).

The shared pool borrows idle capacity and retains native engine slot counts.
Its weights are Prophet 3 points with one active Prophet track, 4 with multiple
active Prophet tracks; FM6, SID and DRUM 2 points; CZ 3 points. Capacity fades
retain the legacy `voices_given_up` count and also increment `capacity_steals`;
normal pool fades can be read as `voices_given_up - voices_shed`. Emergency
CPU cuts remain `voices_shed`. Automatic settings writes wait until voices and
USB audio streams are idle.

### FX TONE

`P_COUNT=122`, `P_E0=114`, `G_COUNT=45`. Track IDs 111–113 and global IDs
42–44 are the three insert/shared slots' TONE, −63..63, default 0. Format 17
(`F_TONE`) displays 0 as FULL, a value below 0 as DARK (a high cut of
`120 + (64 − |v|)² × 2520 / 3969` Hz: 2640 Hz at −1 down to 120 Hz at −63) and
a value above 0 as THIN (a low cut of `150 + v² × 1130 / 3969` Hz: 150 Hz at 1
up to 1280 Hz at 63), using integer division. Echo, Room and Reverb filter their
wet inputs and always keep a low cut of their own (Echo 100 Hz, Room and Reverb
150 Hz) at FULL and under a high cut; THIN raises it. Other effects ignore TONE,
and the device hides its knob for them. The editor places each slot's TONE on a page
beside its main controls and hides it when that effect type does not support it.
These values belong to tracks/projects, so instrument loads and librarian capture
preserve/exclude them like the existing insert settings. FUN22 stores the three
shared TONEs + 64 in the first three of the six reserved bytes before
`PROJ_FM6_OFF` (the other three stay 0); track values map by count. TPLL stores
the shared TONEs + 64 in master bytes 14–16 of its 20-byte master block (17–19
zero).

The firmware before it (201cbbd, never released) had a low cut and a high cut
per slot (`P_COUNT=125`, `P_E0=117`, `G_COUNT=48`; FUN21 and TPLK, formats 17
`F_HIGHPASS` and 18 `F_LOWPASS`). Its records load: each slot's cut becomes the
nearest TONE, the low cut when both were set; a low cut at or under 150 Hz counts
as the effects' own and leaves the high cut (or FULL). FUN20/TPLJ and all previously
supported imports load TONE FULL and retain their routing and engine/motion values.

### Chord strum

Common track IDs 114 `STRUM` and 115 `STROKE` are appended before the engine
parameters: `P_COUNT=124`, `P_E0=116`, `G_COUNT=45`. Earlier common IDs retain
their values; engine IDs are read from INFO. STRUM is an enum: 0 OFF, 1 1/8,
2 1/16, 3 1/32, 4 1/64, 5 1/128, 6 16T, 7 32T. STROKE is 0 DOWN, 1 UP,
2 ALT. Both default to 0 and survive sound loads. The device abbreviates STROKE to DIR. Projects map older counts
with STRUM OFF; existing engine values retain their mapping.

The division is the complete first-to-last sweep, with an immediate first tone
and evenly spaced remaining tones. Input release and STOP cancel pending tones;
MIDI sustain follows existing ownership. ARP bypasses strumming. Live recording
captures the staggered onsets; STEP entry continues to write a complete chord.


Bank capability `50 04 08 40` adds song looping. BANK_SONG op 4 takes `[4, loop]` with loop 0 or 1;
it is refused while armed/running. Version 4 replies append the loop byte after all rows (older replies omit it).
Op 1 preserves the existing loop choice. FBKM byte 1 of its arrangement extension stores the loop flag;
bytes 2 and 3 remain zero. A loop returns to section 1, preserves fractional boundary time, and PLAY still stops.

Songs with local preset copies use **FBKO** (`0x4f4b4246`). The FBKM song and FBKN
punch tails are both present, followed by a sound extension: count (1–64), parameter
count, little-endian sound-record size, then 64 × 8 one-byte sound IDs (0 means the
normal track sound), then `count` immutable `song_sound_t` records. IDs are compacted
when saved. Records hold biased parameters, engine/preset/origin, FM6 operator mask,
a terminated 16-byte name and a 156-byte native-patch union. The full extension and
its references validate before restore; insufficient memory refuses the load before
replacing the current project. Sparse pattern bytes follow the extension. The bank
extent and checksum stay unchanged. Earlier containers load without overrides, and
saves without overrides retain their earlier container. Backup transfers preserve
FBKO unchanged. Preset assignment editing currently uses the device's SONG browser.

SONG preset selection applies one snapshot to the entire selected block, from
its start through all HOLD continuations. Set/remove and block resizing update
all covered sound IDs atomically. Earlier FBKO copies that only named the block's
first section inherit that sound through empty HOLD cells. Other occurrences of
the same pattern retain independent assignments. The FBKO byte layout is unchanged.


Projects with Pattern preset overrides use **FBKQ** (`0x514b4246`). This extends
FBKP's 16-pattern layout: the song and punch extensions are mandatory, song byte
2 is 1, and the existing compacted sound extension is followed by `NTRK × NPAT`
one-byte Pattern sound IDs, in track-major order, before the sparse step banks.
Zero inherits the default track sound. MIDI-only tracks' banks 8–15 must be zero;
nonzero assignments on MIDI-only tracks must reference MIDI sounds. At least one
Pattern assignment is required. Song and Pattern IDs share the same immutable
sound pool (64 snapshots maximum); saves include only referenced snapshots and
validate all IDs, engine constraints, snapshots and bounds before restore.
Projects without Pattern overrides still save as FBKP (or the existing dense
legacy fallback). Earlier formats load with no Pattern overrides. Firmware that
predates FBKQ cannot load projects saved with Pattern overrides.

Playback resolves **Song override > Pattern override > Default track sound**.
HOLD sections inherit the originating block's Pattern assignment when no Song
sound is assigned; rests have no Pattern fallback. Manual pattern playback uses
the same Pattern/default resolution, including queued loop-boundary switches.
STOP restores the default sound. The stopped foreground reserves the baseline
buffer before publishing/editing/importing assignments; playback starts and
boundaries copy bounded RAM without allocating or reading the preset library.
PATTERNS → Set preset/Remove preset edits the selected audio track's active bank.
Browser acceptance stores an independent snapshot, cancellation restores the
track, and assignment edits preserve notes and automation. Copy carries the
assignment; Delete removes it with the pattern and supports the existing undo.
The assignment map participates in project hashes, compact storage, backups and
track removal. No new editor wire command is introduced.

## Paging and argument checks (firmware helpers for new commands)

New commands (numbers 39–63, see "Flash ledger" below) reuse the shared helpers in `firmware/src/editor_wire.h`
(`ed_v`, `ed_left` and `ED_ARGN` are in `editor.c`); the wire rules they implement are the ones the existing
commands already follow:

- **Fixed argument lengths:** `ED_ARGN[cmd]` holds one bit per accepted length (bit n: n argument bytes,
  n ≤ 15). A request whose length has no bit gets no reply, as before. An entry of 0 means the handler checks its
  own lengths (variable payloads, sub-operations): commands 39–63 add an entry, or check in their handler.
- **Absent bytes are echoed as 127:** a reply that starts with the request's operation / target / index bytes
  echoes 127 for each one the request did not carry (`ed_arg`, `ed_echo`), as FM6 (68–71), CZ (75–77),
  native (78), library (80), Prophet (95) and BACKUP_GET / BACKUP_PUT do.
- **Names:** a name sent to the device is printable ASCII, 32–126, nothing else (`ed_text`; any other byte: rc 1).
  A stored name read back shows anything not printable as a space (`ed_shown`).
- **Paged replies:** a reply holds at most 1024 bytes, F0 to F7. A paged reply carries `first, count, total`
  and then `count` items; `count` is the number of the requested items that exist (`ed_page(first, want,
  total)`), cut further where the next item would not fit (`ed_left()`: payload bytes left before the F7). The
  editor asks again from `first + count` until it has `total`. New commands send `first`, `count` and `total` as
  14-bit values (`ed_u14`: low 7 bits, then the next 7).
- **Numbers:** a parameter value is a v14 (offset 8192, `ed_v`); a count or index past 127 is a plain 14-bit
  value (`ed_u14`); a 32-bit value is five 7-bit groups, low first (`ed_u35`); native CZ-1 bytes travel as nibble
  pairs, low first (`ed_nibbles` / `ed_unnibble`).
- **Command families** with their own file answer through `ED_HANDLERS[]` (each returns 1 when the command is
  its own); a new family is one more entry.

## Studio session (commands 39–43, 55–57)

Melodee Studio's session layer (branch F1 `feat/editor-studio-session`): what the firmware supports, all eight
tracks, the transport, the 32 project slots, the storage pool, meters and the pushes that keep Studio in step. The
commands only add: every earlier command answers byte for byte as before, except `BACKUP_GET` while playing (below)
and the bounds of `TRACK_MIX` / `TRACK_PARAM` on tracks 5..8 (their pushes now have a place; before, the shadow
overran). Code: `firmware/src/editor_session.c` (39–41, 55–57, the WATCH bits), `firmware/src/editor_projects.c`
(42, 43), `firmware/src/editor_backup.c` (inventory). Tests: `tests/editor_test.c` `studio_session`,
`tests/backup_test.c` `studio_projects` (32 slots and the pool on simulated flash).

**Finding out.** `INFO` ends with one more tagged block, `53 01 27` ('S', version 1, the `CAPS` command 39). The old
editor parses INFO's trailer by position and by `findIndex` from offset 25 (`web/editor.html` 622–640); the new block
comes after all of them and matches none of its searches. Firmware without it never answers 39–43.

| cmd | Request args | Reply args |
| --- | --- | --- |
| 39 CAPS | — | revision (1), then TLV entries until the F7: tag, length, length value bytes |
| 40 TRACKS | — (query); 1, track (select); 2, solo lo, solo hi (14-bit mask); 3, track, on 0/1 (arm); 4 (add a track); 5, track (remove it) | op (127: query), rc, then the tracks (below) and the sounds' names |
| 41 TRANSPORT | — (query); 1 (play); 2, row (play the song from row); 3 (stop); 4, on 0/1 (song loop) | op (127: query), rc, then the transport (below) |
| 42 PROJECTS | — (status); 0, first, count (list); 1, slot (load); 2, slot (save); 3, slot, name (save as); 4, slot, name (rename); 5, slot, to (duplicate); 6, slot (delete) | op (127: status), rc, the music's slot (0..31, 127 none), then per op (below) |
| 43 STORAGE | — | rc, sectors u14, free u14, bytes per sector u14, then (rc 0) one owner byte per sector |
| 55 METERS (push) | — | state, the song's row, NPART, NPART × peak, NTRK, NTRK × step |
| 56 TRACKS_CHANGED (push) | — | the tracks, as in `TRACKS` (no names) |
| 57 TRANSPORT_CHANGED (push) | — | the transport, as in `TRANSPORT` |

Requests of the wrong length get a reply with rc 1 (and the state), except `CAPS` and `STORAGE`: any argument gets no
reply. Slots and tracks count from 0 on the wire (slot 0 = the device's slot 01, track 0 = track 1). A name is 0..12
printable ASCII bytes, 32..126 (`ed_text`), sent without a terminator; replies end names with 0. Every reply fits one
1024-byte frame; the longest is `PROJECTS` list with all 32 slots (≤ 600 bytes).

### CAPS (39)

| tag | length | value |
| --- | --- | --- |
| `B` (0x42) | 1 | board: 0 FM-1, 1 SMK-37 |
| `L` (0x4C) | 4 | the longest reply, then the longest request, F0 to F7, 14 bits each: 1024, 640 |
| `T` (0x54) | 3 | `TRACKS` version 1, NTRK (8), NPART (4: the tracks that sound; 5..8 are MIDI-only) |
| `X` (0x58) | 1 | `TRANSPORT` version 1 |
| `P` (0x50) | 3 | `PROJECTS` version 1, slots (32), name length (12) |
| `G` (0x47) | 1 | `STORAGE` version 1 |
| `K` (0x4B) | 1 | 1: `BACKUP_LIST 1` (an inventory) and `BACKUP_GET` while playing |
| `W` (0x57) | 1 | the `WATCH` bits known (31: bits 0..4; 63 with F3's bit 5, "Studio song"; 127 with F2's bit 6, "Studio patterns") |
| `M` (0x4D) | 2 | `METERS` version 1, milliseconds between pushes (66, ~15 Hz) |
| `S` (0x53) | 6 | (F3) `SONG2`, `SONG_SOUND`, `PUNCH` versions (1, 1, 1), the song's sections (64), its sounds (64), the punch effects (27): see "Studio song" |
| `D` (0x44) | 4 | (F2) the patterns' version (1: `PATTERN_DATA`, `NOTES`, `MOTION` on a bank, `PATTERN` 3..5, `PATTERN_CHANGED`), steps a pattern holds (64), timed notes (1,024, 14 bits): see "Studio patterns" |

Skip unknown tags by their length. Later branches (F2..F5) add tags; a tag's version grows when its command changes.

### TRACKS (40)

The tracks after `op, rc` (and the whole of `TRACKS_CHANGED`):

| bytes | field |
| --- | --- |
| 1 | NTRK (8) |
| 1 | the tracks in use, 1..8 (`trk_n`: always from track 1) |
| 1 | the selected track, 0..7 (the real one, not `TRACK`'s clamped 0..3) |
| 2 | solo: the tracks that sound alone, 14 bits (bit k = track k + 1; 0 none) |
| 2 | armed: the track armed for live recording, 14 bits (REC: one at a time) |
| NTRK × 5 | per track its sound: engine byte, preset (low 7 bits), preset >> 7, the user slot it came from + 1 (14 bits; 0 none) |
| NTRK × 3 | per track: level v14 (`P_LEVEL`), mute (0/1) |
| NTRK × string | (the reply, not the push) the sound's name as the device shows it, ≤ 16 characters |

Tracks past the ones in use keep their place and values (a track added later starts fresh). MIDI-only tracks (5..8)
report engine 21 (MIDI Track) and no audio.

| op | does | rc |
| --- | --- | --- |
| — | nothing (query) | 0 |
| 1 | selects the track, as ALGORITHM does (no `RELOAD` push for it: the editor re-reads) | 1: not in use |
| 2 | sets the solo mask (bits past the tracks in use are dropped). It is latched in RAM, not saved; a GLO solo key held on the device wins while held; GLO's "all on" key clears it | 0 |
| 3 | arms (on 1) or disarms (on 0) the track for live recording, as REC: another armed track is disarmed first; armed while playing it waits for the pattern start | 1: not in use, on > 1; 3: arming while the song plays |
| 4 | adds the next track (Stage's "Add track"): selected, its power-on sound, every pattern empty; no browser opens on the device | 1: eight already |
| 5 | removes the track (Stage's "Remove track", Studio asks first): the tracks after it move down with everything that is theirs, the solo and armed bits too | 1: not in use or the only one; 2: failed (the project did not fit, nothing changed); 3: stop playback first (removing never stops it) |

### TRANSPORT (41)

The transport after `op, rc` (and the whole of `TRANSPORT_CHANGED`): state (bit 0 playing, bit 1 the song plays or
is about to, bit 2 counting in, bit 3 a track armed), clock (`G_CLOCK`: 0 INT, 1 USB, 2 TRS), song loop (0/1), the
song's row, its rows, repeats left of the row, track 1's step (0..63; track 1 clocks the song).

| op | does | rc |
| --- | --- | --- |
| — | nothing (query) | 0 |
| 1 | plays the patterns, as PLAY on Stage (nothing when playing already) | 2: external clock |
| 2 | plays the song from row (stops first if playing; the row is the song position) | 1: row past the song or the song invalid; 2: external clock; 3: did not stop in 100 ms; 4: the song's sounds could not be prepared |
| 3 | stops | 2: external clock |
| 4 | sets the song loop (as SONG's sheet; works on an external clock too) | 1: on > 1; 3: the song plays (STOP TO EDIT) |

Plays and stops wait until the audio interrupt takes them (at most 100 ms), so the reply shows the new state. While
the FM-1 follows an external clock (CLK USB or TRS) that clock starts and stops it: play, song and stop answer rc 2 and
change nothing.

### PROJECTS (42)

After `op, rc` and the music's slot (the slot it was loaded from or last saved to; 127 none):

- status (no arguments): dirty (1: the music differs from its slot, or it has no slot and holds notes or a song with
  events; as NEW SONG's question), then the music's name. Dirty packs the music to compare: a `BACKUP_GET` of id 0 from
  an earlier `LIST` then answers rc 5, as after a save.
- list (op 0): first, count, total (14 bits each, total 32), then count slots: slot, state (0 empty, 1 a project,
  2 a record this firmware does not read: "OLD FORMAT"), stored bytes (3 × 7 bits, low first), name. count is the
  slots asked for that exist; all 32 fit one reply.
- every other op: the slot written or loaded, as a list item (duplicate: the slot copied to).

| op | does | rc |
| --- | --- | --- |
| 1 | loads the slot | 1: empty or old format; 2: it did not load |
| 2 | saves the music to the slot, its name kept (replaces what the slot held: Studio keeps a copy first) | 2: failed (STORAGE FULL, PROJECT FULL, a flash error) |
| 3 | saves the music to the slot with this name ("" no name) | as 2 |
| 4 | renames the stored project; the music too when the slot is its slot | 1: empty slot; 2: failed |
| 5 | copies the stored project to slot `to`, its name kept (replaces what `to` held); when `to` is the music's slot, the music no longer has a slot | 1: empty slot; 2: failed |
| 6 | deletes the slot (when it is the music's slot, the music no longer has one) | 2: failed |

Every op but list and status may write flash or change the music, so it stops the transport first and leaves it
stopped (a load, rename or duplicate of an empty slot answers rc 1 without stopping it) (Kerem 2026-11-06: Studio's button says "Stops FM-1 playback"); rc 3 if it does not stop within 100 ms. Studio
asks about unsaved changes itself (the dirty flag) and reads a slot (`BACKUP_GET`) before it overwrites or deletes
one. The status of an invalid request (rc 1, wrong length, slot ≥ 32, a bad name) changes nothing. Host builds without
flash have four slots (total 4).

### STORAGE (43)

rc (0 ok, 4: no flash or a read failed), the linear sectors (FM-1 108: 0x93000..0xFEFFF; SMK-37 60), the free ones
(what a new record may take), the payload bytes a sector holds (4,056), then one byte per sector, lowest address
first: 0 free, 1..32 project slot 01..32, 33..40 user kit 1..8, 41 another record (the preset banks), 127 not the
pool's (the update loader's staging, the settings pair, the resource region). A record holding k sectors stores up to
k × 4,056 bytes; the list's stored bytes tell how many it holds.

### Backup inventory and reads while playing

- `BACKUP_LIST 1` is an inventory: the stored objects (projects 1..32, presets, kits) and the settings, with sizes and
  CRCs, as `BACKUP_LIST`. It does not stop the transport, does not capture the music and writes no flash. Object 0
  (the music) is listed with size 0; `GET` of it answers rc 5 until a `BACKUP_LIST` without the flag captures it.
  `BACKUP_LIST` with any other argument answers rc 1 (as before).
- `BACKUP_GET` reads while the transport runs (before: rc 3). Stored objects are read from flash in 256-byte pieces,
  as a project load does; a save cannot happen meanwhile (saves stop the transport).

### WATCH bits 2..4 and the pushes

`WATCH` takes bits 2..4 on top of bits 0 (on) and 1 (`TRACK_CHANGED`), and answers with the bits it knows of those
asked for (firmware before: 0..1, so `WATCH 31` answers 3 there). The old editor sends 1 or 3 and gets the same
answer and pushes as before.

| bit | adds |
| --- | --- |
| 2 | the eight tracks: `RELOAD` and `STEP_CHANGED` carry the real selected track (0..7, not clamped to 3), `TRACK` reports it too, and `TRACK_CHANGED` follows tracks 5..8 as well |
| 3 | `TRACKS_CHANGED` (56) when the tracks in use, the selection, solo, armed or a track's sound change on the device (not level or mute: `CHANGED` / `TRACK_CHANGED` carry those), and `TRANSPORT_CHANGED` (57) when the transport changes but its step (play, stop, a song row, a repeat, the loop, the clock) |
| 4 | `METERS` (55) every 66 ms (~15 Hz) while watching. Studio sets the bit only while Stage or Mixer is open and its tab visible |
| 5 | (F3) `SONG_CHANGED` (58) when the song changes on the device: see "Studio song" |
| 6 | (F2) `PATTERN_CHANGED` (59) when patterns change on the device: see "Studio patterns" |

A `WATCH` with bits 2..4 while watching only changes the bits: the shadows stay, so no push is lost (a `WATCH` of 0..3,
or one that starts watching, takes the device's values as known, as before). The editor's own `TRACKS` /
`TRANSPORT` change pushes nothing. Pushes go out at most four per ~5 ms pass and only into a half-empty SysEx ring
(`usb.c` 64 packets): 56 is 77 bytes, 57 13, 55 22.

`METERS`: state (as `TRANSPORT`), the song's row, NPART (4), per part its peak since the last `METERS`, NTRK (8), per
track its step (0..63). A peak is 8 × log2 of the largest |sample| (Q15, after level, solo and mute), 0..127: 120 is
full scale, each 8 is 6 dB (dBFS ≈ (peak − 120) × 0.75), 0 silence. The device's own meters and `METERS` each get
every peak (`trk_peak_take`: two readers).

## Studio song (commands 47–49, 58)

Melodee Studio's song (branch F3 `feat/editor-song-full`, on F1): the whole song with all eight tracks, the
sections' lengths, the block sounds, the punch-in effects and a push when the song changes on the device. The
commands only add: every earlier command answers byte for byte as before (`SONG` 33 and `BANK_SONG` 74 keep
their four-track rows, and their writes still clear the punch events and block sounds). Code:
`firmware/src/editor_song.c`; the song page's own operations (`ui_song.c`, `ui_song_sound.c`) do the edits. Tests:
`tests/editor_test.c` `studio_song`.

**Finding out.** `CAPS` (39) has a tag `S`: `53 06` SONG2 version 1, SONG_SOUND version 1, PUNCH version 1, the
sections a song holds (64), the song's sounds (64), the punch effects (27); `W` is 63 (bits 0..5). Firmware without
the tag never answers 47–49.

| cmd | Request args | Reply args |
| --- | --- | --- |
| 47 SONG2 | — (head); 0, first, count (sections); 1 (the banks' timing); 2, rows, loop, rows × (repeats, 8 cells) (write); 3, at (insert); 4, first, last (duplicate); 5, first, last (delete); 6, first, last, to (move) | op (127: head), rc, the head (below), then 0: the sections; 1: the timing table |
| 48 SONG_SOUND | 0, first, count (the song's sounds); 1, row, track, engine, preset lo, preset hi; 2, row, track, slot lo, slot hi; 3, row, track, id | op, row, track (0: first, count), rc, the song's sounds (count), then 0: the sounds |
| 49 PUNCH | — (state); 0 (names); 1, effect (hold); 2 (release); 3 (the song's events); 4, first, n, n × 4 × (effect, start, length) | op (127: state), rc, held, the editor's, the song's, effects (27), then 0: names; 3, 4: the events |
| 58 SONG_CHANGED (push) | — | the head |

Sections and tracks count from 0 on the wire. A cell is a pattern (0..15 on tracks 1..4; 0..7 on the MIDI-only
tracks 5..8), 16 a rest, 17 goes on (the block above continues: never on section 1, never after a rest). Every reply
fits one 1024-byte frame (the paged ones are cut to fit: 41 sections, 50 sounds a reply); the longest request is a
64-section `SONG2` write, 579 argument bytes (585 with the frame).

rc: 0 done; 1 malformed or invalid (nothing changed); 2 refused by the device (nothing changed: no edit memory,
the song full, a move onto itself, a sound that could not be kept); 3 busy (the song plays or is about to: STOP TO
EDIT; SONG_SOUND 1 and 2 also while anything plays, PUNCH 1 while a menu, dialog or NAME is open on the device).
Unlike the older commands, a request of the wrong length still gets its reply, with rc 1.

### SONG2 (47)

The head (also the whole of `SONG_CHANGED`): the sections (0..64), loop (0/1), BPM (14 bits: 40..240), the song's
sounds (0..64).

- **Sections** (op 0): `first, count, total` (14 bits each), then `count` sections of 24 bytes:

  | bytes | field |
  | --- | --- |
  | 1 | repeats, 1..16 |
  | 1 | steps: the conductor's (track 1's) pattern length, 1..64; 16 when track 1 rests or goes on |
  | 1 | division of those steps (`P_SDIV`: 0 1/4, 1 1/8, 2 1/16, 3 1/32, 4 8T, 5 16T, 6 1/2, 7 1/1, 8 2BAR, 9 4BAR); 2 when track 1 rests or goes on |
  | 5 | the section's length in sequencer units, repeats included (32 bits, five 7-bit groups, low first) |
  | 8 × 2 | per track: the cell, the block sound id stored on it (0: none) |

  **Time.** A beat (quarter note) is 24,576 units at any tempo; the section lasts `units × 60 / (24,576 × BPM)`
  seconds and the song the sum of its sections (the device's own `chain_row_samples` / `chain_seconds`, swing
  included: an odd-length conductor pattern carries its swung step). The device clocks a section by track 1: its
  pattern's steps × division (with track 1's swing plus global swing), × repeats; a section where track 1 rests or
  goes on lasts four beats (16 sixteenths) a repeat. The other tracks restart their pattern at each section start
  (unless they go on) and loop or are cut at its end.

  **Sounds.** A block's sound is the id on its first cell; a goes-on cell with id 0 plays the sound of the block it
  continues (older songs named only the first section), one with its own id plays that one. 0 on a block's first
  cell: the track's own sound (or the pattern's, see FBKQ).
- **Timing** (op 1): global swing (0..100), then per track (8): its patterns (16 or 8), then per pattern its steps
  (1..64), division (as above), swing (0..100). With them Studio draws each cell's pattern inside its section: a
  pattern lasts `steps × division + (steps odd ? swing' × division / 250 : 0)` units, `swing'` = the pattern's swing
  + global swing, at most 100, and the division in units: 1/4 24,576, 1/8 12,288, 1/16 6,144, 1/32 3,072, 8T 8,192,
  16T 4,096, 1/2 49,152, 1/1 98,304, 2BAR 196,608, 4BAR 393,216.
- **Write** (op 2): replaces the cells, repeats and loop. The punch events and the block sounds of the sections
  kept stay (FBKN / FBKO): a cell written as a rest loses its sound; sections past `rows` lose theirs (as a delete).
  Everything is checked first (repeats 1..16, a pattern the track has, goes-on after a block); any fault: rc 1,
  nothing changed.
- **Insert / duplicate / delete / move** (ops 3–6) are the song page's own: insert puts an empty section (all rests,
  1 repeat) at `at` (past the end: at the end); duplicate copies sections `first..last` right after `last`; delete
  removes them; move puts them before section `to` (0..rows). Punch events and sounds move with their sections; a
  block cut by the edit becomes its own block, its first cell named by the pattern it played (as on the device). The
  device shows the page's message ("SECTION DUPLICATED", ...) and its song cursor moves there. rc 1: `first > last`,
  `last` past the song, `to` past the end; rc 2 (the page refuses, nothing changed): the song would pass 64
  sections, a move inside or right after its own range, no edit memory.

Every write is one of the song page's undoable edits: EDIT on the device undoes and redoes it. All writes answer rc 3
while the song plays or is about to (TRANSPORT bit 1); reads work while playing.

### SONG_SOUND (48)

A block's sound is an immutable snapshot in the song's pool (up to 64; FBKO), as the song sheet's "Set preset" makes
it. The song's sounds (op 0): `first, count, total` (14 bits each), then `count` sounds: id (1..64), engine, preset
(low 7 bits, then bit 7), name (≤ 15 characters, 0-terminated): what the sound was made from. A snapshot keeps
no user slot (as on the device): a sound made from a user slot shows that slot's name and preset 0.

| op | does | rc |
| --- | --- | --- |
| 1 | the factory preset `engine, preset` (14 bits) on the block of `track` at `row`: the track auditions it, its snapshot is stored (an identical one is reused) and the block's cells get its id, then the track's own sound comes back | 1: row past the song, a MIDI-only track, engine or preset unknown; 2: the sound could not be kept (an engine this firmware does not play, the pool full, PROJECT FULL, no memory); 3: the transport runs, or the device's own Set preset is open |
| 2 | as 1 from user slot `slot` (14 bits, 0..383) | 1: as 1, or the slot empty; 2, 3 as 1 |
| 3 | sound `id` of the pool on the block (0: none, the track's own: "Remove preset") | 1: row past the song, id past the pool, an id on a MIDI-only track; 2: no edit memory; 3: the song plays |

The block is the cell's: from the block's first section (its origin) through every goes-on cell after it. The reply's
count tells whether a new sound was stored; Studio reads the sections again for the ids.

### PUNCH (49)

The punch-in effects of the FX layer: 27 effects (0..26, as the keyboard F3..G5 maps them: 16 white keys, then 11
black), the master mix through one at a time.

The state after `op, rc`: held (the effect the keys or the editor hold, 127 none), the editor's (1: the editor's
hold), the song's (the effect a song section plays now, 127 none), effects (27).

| op | does | rc |
| --- | --- | --- |
| 0 | the names follow: 27 × name (0-terminated) | 0 |
| 1 | holds `effect` (as a held key); a key pressed on the device takes over | 1: effect ≥ 27; 3: a menu, dialog or NAME is open on the device (no layer plays) |
| 2 | releases the editor's hold (a key's stays) | 0 |
| 3 | the song's events follow | 0 |
| 4 | writes the events of sections `first..first+n-1` (n ≥ 1), 4 per section, then the events follow | 1: past the song, an event invalid; 2: no edit memory; 3: the song plays |

The editor's hold ends with STOP, when the device opens a menu, dialog or NAME, and 3 s after the editor's last request
(Studio sends `PING` while a pad is held). The events: the sections (count), then per section 4 × (effect + 1, 0
none; start step 0..63; length 1..64 − start; 0, 0, 0 for none), in conductor steps from the section's start,
repeated with each repeat (FBKN). The later start wins where two overlap; a held effect wins over the song's.

### WATCH bit 5: SONG_CHANGED (58)

`WATCH` with bit 5 (32) pushes `SONG_CHANGED` (the head: 5 bytes, 11 with the frame) when the song changes on the device: its cells,
repeats, loop, block sounds or punch events (an edit, an undo, a project load, a new song). Studio then reads it again.
The editor's own writes push nothing. The device notices a change within one screen pass.

## Studio patterns (commands 44, 45, 59; 64 and 73 extended)

Melodee Studio's patterns (branch F2 `feat/editor-patterns-any-bank`, on F3 and the virtual FM-1 core): any track's
pattern bank, the one playing or another: its steps and timing, its timed notes, its motion, and the edits of SEQ's
pattern dialog. The commands only add: every earlier command answers byte for byte as before; `MOTION` and `PATTERN`
take operations that got no reply before. Code: `firmware/src/editor_patterns.c` (44, 45, 59), `editor.c` (64, 73),
`firmware/src/pattern_dialog.c` (the dialog's delete, half and double, moved out of `ui_input.c` so the editor and the
virtual FM-1 have them). Tests: `tests/editor_test.c` `studio_patterns`.

**Finding out.** `CAPS` (39) has a tag `D`: `44 04` version 1 (all of the below), the steps a pattern holds (64), the
timed notes a project holds (1,024, 14 bits); `W` is 127 (bits 0..6). Firmware without the tag never answers 44 and
45, ignores `MOTION` op 5 and `PATTERN` ops 3..5, and knows no bit 6.

| cmd | Request args | Reply args |
| --- | --- | --- |
| 44 PATTERN_DATA | track, bank, half (read); track, bank, half, LEN, DIV, SWING, GATE, 32 × step (write) | track, bank, half, rc, then (a bank the track has) LEN, DIV, SWING, GATE, 32 × step |
| 45 NOTES | track, bank, 0, first (14 bits), count (list); track, bank, 1, m, m × note (write) | track, bank, op, rc, then 0: first, count, total (14 bits each), count × note; 1: the m notes' ids (14 bits each) |
| 64 MOTION | track, 5, bank, then a `MOTION` request's bytes after its track: — (query), 1 on, 2 clear, 3 set, 4 delete | as `MOTION`: track, rc, on, count, max, the events, all of that bank |
| 73 PATTERN | track, 3, bank (clear); track, 4, bank (halve); track, 5, bank (double) | as `PATTERN`: track, op, rc, active bank, queued bank (127 none), the track's banks |
| 59 PATTERN_CHANGED (push) | — | NTRK, then per track: the bank playing, the bank queued (127 none) |

Tracks and banks count from 0 on the wire (16 banks on tracks 1..4, 8 on the MIDI-only tracks 5..8); half 0 is steps
1..32, half 1 steps 33..64. Every reply fits one 1024-byte frame (`PATTERN_DATA` 398 bytes; `NOTES` pages are cut to
fit: 59 notes); the longest request is a `NOTES` write of 37 notes (639 bytes with the frame).

rc: 0 done; 1 malformed or invalid (nothing changed); 2 no room (`NOTES`: the 1,024 timed notes in use, or no edit
memory on the device; `PATTERN`: PATTERN DATA FULL, or the loop-end queue full); 3 busy (a song plays or is about to:
STOP TO EDIT; `NOTES` also while the track records live); 4 PATTERN STORAGE FULL (the project could not be saved:
nothing changed); 5 (`PATTERN` 3) waits for the track's loop end. Like F1 / F3, a 44 or 45 request of the wrong length
still gets its reply, with rc 1.

### PATTERN_DATA (44)

The bank's timing as stored with it (`P_SLEN` .. `P_SGATE`; the bank playing: the track's own): LEN 1..64, DIV
(`P_SDIV`, as `SONG2`: 0 1/4, 1 1/8, 2 1/16, 3 1/32, 4 8T, 5 16T, 6 1/2, 7 1/1, 8 2BAR, 9 4BAR), SWING 0..100, GATE
1..127. Then 32 steps of 12 bytes: the step's 11 bytes as the device keeps them (`step_t`), the eighth bits last.

| byte | field |
| --- | --- |
| 0..3 | notes, 0..127 (the first n are in use) |
| 4 | n, the notes in use (0..4) |
| 5 | time: 0 NOTE, 1 TIE, 2 REST |
| 6 | flags: 1 accent, 2 slide, 4 the view of timed notes (below) |
| 7 | velocity (0: the track's) |
| 8 | the drum lanes hit, lanes 1..7 (bits 0..6) |
| 9 | with hits: their accents (bits 0..6); without: the step's own gate, 1..255 of a step (0: the track's GATE) |
| 10 | chance: 0 always (a pattern from before chance), 1..100 %, 101 never |
| 11 | bit 0: lane 8 hit, bit 1: bit 7 of byte 9 |

A write replaces the half's 32 steps and the timing (the other half stays; the timing is clamped into its ranges). Each
step must be one the device can hold (n ≤ 4, time ≤ 2, flags ≤ 7, chance ≤ 101, accents only on lanes hit): else rc 1,
nothing changed. On the bank playing the track hears it at once (as `STEP_SET`). A step written without flag 4 where
timed notes were drops them (as clearing it on the device does); flag 4 on a step without timed notes plays nothing.
Not undoable on the device (as `STEP_SET`). rc 3 while a song plays; rc 4 when the project would not save.

### NOTES (45)

Timed notes are notes played in live (or turned into timed notes on the NOTES page): each keeps its onset inside its
step, its own length and its velocity. The bank's steps are their views: flag 4, the notes whose onset rounds to that
step (up to four pitches, the highest velocity, accent above 110; on a DRUM track the lanes' hits).

A note, 17 bytes:

| bytes | field |
| --- | --- |
| 2 | id: its slot, 0..1023 (14 bits; kept until the next write, take or load) |
| 1 | step, 0..63 |
| 5 | onset in the step, 0..65,535 of the step's span (32 bits, five 7-bit groups, low first) |
| 5 | length: 65,536 a step (unswung), 1..8,388,480 (32 bits) |
| 1 | note, 0..127 |
| 1 | velocity, 1..127 |
| 1 | pitch + 64: a drum hit's own tune, −24..24 (64 for every other note) |
| 1 | flags: bit 0 a gated drum hit; read only: bit 1 shown on the next step (an onset in the step's second half), bit 2 shown on step 1 (the last step's second half) |

Time: a note starts `onset × span / 65,536` after its step's start, the span being the step's own (its division, swung
as `SONG2`'s timing describes); its length counts straight steps of the bank's division.

- **List** (op 0): `first, count, total` (14 bits each), then `count` notes of the bank in slot order (`count` cut to
  fit). Notes past LEN are listed too (they stay hidden, as steps past LEN do).
- **Write** (op 1): m ≥ 1 notes, all checked before anything changes: an id that is one of the bank's notes, or 16383
  for a new one (velocity ≥ 1); velocity 0 removes the note; step, onset, length, pitch and flags in their ranges; a
  note may not land on a step that holds its own notes (rc 1: clear the step, or turn it into timed notes on the
  device, first). rc 2 when the new notes need more than the free slots (RECORDING FULL); rc 4 when the views would
  not save; rc 3 while a song plays or the track records live. The notes are then written in order (a later one in the
  request wins) and the bank's views are made again from its notes: a step that loses its last note becomes a rest; a
  view keeps its chance and slide. The reply: the m notes' ids (a new note's slot). On the bank playing, stopped: one
  edit the device's EDIT held undoes (whichever track is selected); on another bank or while playing it is not
  undoable (as the device's own edits there).

### MOTION (64) on any bank

`track, 5, bank`, then the bytes a `MOTION` request has after its track (none: the query; `1, on`; `2`; `3, step, id,
v14`; `4, step, id[, v14]`), acts on that bank's events instead of the bank playing's. On / off stays the track's (all
of its banks). The reply is `MOTION`'s with that bank's count and events. A clear on another bank is the device's
MOTION CLEARED (undoable as a load). The rules are `MOTION`'s (64 events shared by the parts' banks: rc 2 full; tracks
5..8 have no motion: no reply; bank ≥ 16: no reply).

### PATTERN (73): clear, halve, double

SEQ's pattern dialog on the device (SEQ held, then a pattern key held), with its messages on the device's screen:

- **3 clear** (the dialog's Delete): the bank's steps, timed notes, motion and its own sound (FBKQ); LEN and DIV stay.
  Stopped: at once; playing: at the track's loop end (rc 5: DELETE AT LOOP END on the device; `PATTERN_CHANGED`
  follows). An empty bank: rc 0, nothing done.
- **4 halve**: LEN halved (an odd LEN keeps its middle step); the steps past it stay, hidden. rc 3 at 1 step.
- **5 double**: LEN doubled, steps 1..LEN copied after themselves with their timed notes and motion (what lay hidden
  there goes). rc 3 past 32 steps (64 STEPS MAX), rc 2 when the timed notes or the motion do not fit, rc 4 storage.

On the bank playing they are undoable (EDIT held), on another bank not (as on the device). rc 1: a song plays, or a
bank past the track's.

### WATCH bit 6: PATTERN_CHANGED (59)

`WATCH` with bit 6 (64) pushes `PATTERN_CHANGED` (17 bytes, 23 with the frame) when patterns change on the device: a
track's steps or timing of the bank playing, a bank written (copied, cleared, halved, doubled, loaded), the banks
playing or queued, the motion, the timed notes. Studio then reads again what it shows. `PATTERN_DATA`, `NOTES`,
`MOTION` and `PATTERN` push nothing for the editor's own changes; other commands' do (`STEP_SET`, `TRACK_STEP`, a
`SET` of LEN, ...). The device looks every push pass (~5 ms) while the bit is set (a hash of ~6 KB).

## Flash ledger (editor commands)

Melodee Studio needs new editor commands (branches F0–F5) in an app body that is nearly full. Every change that
touches the editor records its cost here, measured on both boards from a full build (`./build.sh`, the build's
`image` line). The app body is the image minus the 3,808-byte boot stub and must stay at or below `BODY_MAX`
(`tools/fm1pkg_make.py`, 0x8C000 = 573,440 B; the commit marker's sector follows). Editor code = the `ed_*`,
`p5_native_frame` and `p5_accept_patch` functions in `build/melodee.elf`.

Boards: **FM-1** = plain build (`./build.sh`); **SMK** = SMK-37 Elite (`BOARD=smk37 MODEL=elite ./build.sh`).

### Baseline (nightly 5a4005ef, 2026-11-06)

| | FM-1 | SMK |
|---|---:|---:|
| image | 573,220 B | 573,696 B |
| app body (image − 3,808) | 569,412 B | 569,888 B |
| free under `BODY_MAX` | **4,028 B** | **3,552 B** |
| editor code | 20,270 B | 20,182 B |
| RAM .data+.bss (of 98,304) | 95,304 B | 95,880 B |
| audio pool (of 357,888) | 139,556 B | 141,176 B |

Other room (not app-body code): the resource region holds 53,852 B in 14 of its 15 (FM-1) / 16 (SMK) sectors; the
SMK-37's app tail (0x93000..0x9CFFF) has 30,200 B free after `.smkx`.

### Changes

| Branch / commit | Change | FM-1 | SMK |
|---|---|---:|---:|
| F0 `chore/editor-flash-room` | `editor_res.c` and `editor_wire.h` built for size (the other `editor*.c` already were); shared helpers: 7-bit writers (`ed_n7` / `ed_u14` / `ed_u35` / `ed_u35s`), request echo (`ed_arg` / `ed_echo`), names (`ed_text` / `ed_shown`), paging (`ed_page` / `ed_left`), nibbles, table-driven argument lengths (`ED_ARGN`), handler table, INFO from a constant table, STEP + TRACK_STEP and SONG + BANK_SONG share their code, FAV kind limits, backup object source. No command retired, every reply byte for byte as before | **−1,644 B** | **−1,604 B** |

After F0: FM-1 image 571,576 B, **5,672 B free**; SMK image 572,092 B, **5,156 B free**. Editor code 18,484 B
(FM-1) / 18,462 B (SMK). RAM unchanged (the new tables are constants in flash).

| F1 `feat/editor-studio-session` | Studio session (see "Studio session"), per command: the full build minus a build without it | **+2,588 B** | **+2,628 B** |
|---|---|---:|---:|
| — CAPS 39 | the TLV table, the INFO tag | +48 B | +32 B |
| — TRACKS 40 | select, solo latch, arm, add, remove | +320 B | +352 B |
| — TRANSPORT 41 | play, song from row, stop, loop, external-clock refusal | +192 B | +212 B |
| — PROJECTS 42 | status (dirty), list, load, save, save as, rename, duplicate, delete; empty-slot check before the stop | +572 B | +556 B |
| — STORAGE 43 | the pool map | +284 B | +292 B |
| — pushes 55–57 | TRACKS_CHANGED, TRANSPORT_CHANGED, METERS | +404 B | +336 B |
| — shared | the TRACKS / TRANSPORT bodies the pushes reuse, WATCH bits 2..4, eight-track TRACK_CHANGED, `BACKUP_LIST 1` and `BACKUP_GET` while playing, `project_copy` (rename and duplicate), the two-reader meters, the solo latch, two handler entries | +768 B | +848 B |

After F1: FM-1 image 574,164 B, **3,084 B free**; SMK image 574,720 B, **2,528 B free**. RAM .data+.bss +128 B
(FM-1, 95,432 of 98,304) / +80 B (SMK, 95,960): the WATCH bits and signatures, eight tracks' TRACK_CHANGED shadows,
the editor's meter peaks. Within the F1 estimate (2.5–3.5 KB); F2..F4 (est. 4–7 KB) do not fit what is left without
further room.

How F1 was checked: `tests/run_tests.sh` once (all green but `CPU live takes`, a load-timing flake: its input
lateness 5.1 ms over a 3 ms limit under the parallel load; alone it passes), `editor_test` `studio_session` and
`backup_test` `studio_projects`, and F0's differential harness against nightly c273b78e's `editor*.c`: 200,000 random
and shaped requests (4 seeds), every reply and the state after it identical, leaving out only what F1 changes on
purpose (commands 39–43 and 55–57, INFO's trailer, WATCH bits 2..4, `BACKUP_LIST 1`, `BACKUP_GET` while playing,
`TRACK_MIX` / `TRACK_PARAM` writes on tracks 5..8).

How F0 was checked: `tests/run_tests.sh` (editor_test, backup_test, the old editor's `web/test_web.mjs`), and a
differential run of the old and new `editor*.c` on the host: 200,000 random and well-formed requests to every
command (including all sub-operations of 33, 37/38, 64, 68–80 and 95), every reply and the device state after
it compared byte for byte, identical.

| F3 `feat/editor-song-full` | Studio song (see "Studio song"), per command: the full build minus a build without it | **+2,132 B** | **+2,124 B** |
|---|---|---:|---:|
| — SONG2 47 | head, sections paged (cells, sound ids, steps, division, length in units), the banks' timing table, write keeping the punch events and sounds, insert / duplicate / delete / move through the song page | +812 B | +816 B |
| — SONG_SOUND 48 | the song's sounds paged, a block's sound from a factory preset or user slot (audition, keep, restore), a pool id or none | +460 B | +428 B |
| — PUNCH 49 | state, names, the editor's hold (and its 3 s release), the song's events read and write | +436 B | +420 B |
| — push 58 | SONG_CHANGED on WATCH bit 5 | +168 B | +156 B |
| — shared | the head and paging helpers the commands share, the CAPS `S` tag, WATCH bit 5, the edit counter (`se.edits`), the handler entry, `editor_song.c` built for size; the song sheet's audition and keep split out for the editor (`song_preview_begin`, `song_preset_keep`, `song_block_sound`, `song_rows_clear`: flash-neutral, −1,002 / +962 B on FM-1) | +256 B | +304 B |

After F3: FM-1 image 576,296 B, **952 B free**; SMK image 576,844 B, **404 B free**. RAM .data+.bss +16 B (FM-1,
95,448 of 98,304) / +0 B (SMK, 95,960): the SONG_CHANGED signature and the edit counter. Within the F3 estimate
(1.5–2.5 KB). F2 and F4 (est. 2.5–4.5 KB) do not fit what is left on either board without further room.

The commands' costs are not additive to the byte: the board's global layout moves with the code (the compiler merges
static variables and addresses them from a shared base; a variable pushed past the short offsets costs 4 bytes at every
access), so a build can grow or shrink by ~150 B with no change of its own. A table of push bodies (56–58 in one loop)
measured 264 B larger on FM-1 for that reason and was left out.

How F3 was checked: `tests/run_tests.sh` once, `editor_test` `studio_song` (also under ASan/UBSan), the song page's own
tests (`song_mode_test`, `song_preset_test`, `song_punch_test`: the split-out audition and keep), and F0's differential
harness against F1's `editor*.c` (012c7ed5): 200,000 random and shaped requests (4 seeds), F1's commands 40–43 among
them, every reply and the state after it identical, leaving out only what F3 changes on purpose (`CAPS`, commands
47–49, `WATCH` bit 5).

| F2 `feat/editor-patterns-any-bank` | Studio patterns (see "Studio patterns"), per command: the full build minus a build without it (first build; the total: the final one) | **+3,144 B** | **+1,956 B** |
|---|---|---:|---:|
| — PATTERN_DATA 44 | any bank's steps and timing in 32-step halves, read and write (checked, saveable) | +480 B | +504 B |
| — NOTES 45, list | a bank's timed notes, paged | +404 B | +416 B |
| — NOTES 45, write | add / change / remove, all checked first; the bank's views made again from its notes; undoable on the bank playing (the edit memory) | +1,508 B | +1,532 B |
| — MOTION 64 on a bank | `track, 5, bank, ..`: query, set, delete, clear on any bank | +16 B | +48 B |
| — PATTERN 73 ops 3..5 | clear, halve, double through SEQ's dialog code | +72 B | +96 B |
| — push 59 | PATTERN_CHANGED on WATCH bit 6 (the signature, the banks) | +504 B | −640 B |
| — shared | the CAPS `D` tag, `W` 127, the handler entry, `motion_set_event_at` / `motion_delete_event_at`, SEQ's pattern dialog moved out of `ui_input.c` into `pattern_dialog.c` (built for size with `editor_patterns.c`; left at `-Os` they cost ~0.7 KB more) | +176 B | +128 B |
| — savings found | NOTES reuses `recording_remove` (−40 / −68 B); `installer_report.c`, `note_history.c`, `ui_midi_learn.c` built for size: main-loop only (−60 / −56 B; SMK RAM +32 B from the regrouped globals) | −100 B | −124 B |

After F2: FM-1 image 579,440 B, **2,192 B over** `BODY_MAX` (577,248); SMK image 578,800 B, **1,552 B over**. The
build fails its size check ("app body overlaps the commit-marker sector") on both boards: **F2 does not fit**, and the
cuts are Kerem's (see the options in the side-session note `editor-patterns-any-bank.md`). RAM .data+.bss +16 B
(FM-1, 95,464) / +32 B (SMK, 95,992). Over the F2 estimate (1.5–2.5 KB) on FM-1: the timed notes' write is most of it.
Without it (NOTES read-only) F2 measured +1,696 B on FM-1 (744 B over) and +480 B on SMK (76 B over).

The per-command differences are not additive; on SMK-37 they are mostly layout: the merged globals
(`.L_MergedGlobals*`, 512-byte groups addressed from one base) regroup whenever a static variable is added or grows,
and the interrupt and main-loop code that reads them grows or shrinks with it. F2's SMK build has ~1 KB of such gains
outside the editor (`fm1_main` −246 B, `fm1_timer5_irq` −214 B, `mix_block` −86 B, ...); the build without the push
loses them again, so the push's "−640 B" there is layout, not the push (on FM-1 its code is ~270 B).

How F2 was checked: `editor_test` `studio_patterns` (also under ASan/UBSan), the virtual FM-1 core (`core_check`: the
F2 transcripts natively and in WebAssembly byte for byte), `tests/run_tests.sh` once, and F0's differential harness
against the branch tip (`feat/studio-export` de24e07a, F1 and F3 included): 200,000 random and shaped requests (4
seeds), every reply and the state after it identical, leaving out only what F2 changes on purpose (`CAPS`, commands 44
and 45, `MOTION` op 5, `PATTERN` ops 3..5, `WATCH` bit 6).
