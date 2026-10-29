# Storage savings plan

Status, 2026-10-11: the full storage savings update, legacy reader and engine removal, shared bank cache,
and protection for unreadable records are part of **Melodee 1.2.0**. Merge the complete feature history into
`next`; there is no separate bridge release or 2.0 release for this work. Started from `next` 2598876.
The flash-map v2 / XIP hardware probe remains parked on `feat/xip-probe`.

## Rubik restoration (2026-10-27)

Rubik and the FONT selector are restored alongside Melodee Pixel. The native
MEL1 storage experiment is isolated on `experiment/native-mel-storage`; nightly
retains the four-slot shared pool described below. No new font build or tests
were run for this restoration, at the user's request.

## Project storage update (2026-10-26)

At that checkpoint the firmware kept Melodee Pixel only. Rubik remained for historical mockups,
with its licence; that build omitted Rubik glyphs, kerning tables and icon numerals.
That checkpoint removed FONT and ignored its preference bit; the restoration above reverses it.
The measured nightly app shrinks from 575,084 to 543,016 bytes (32,068 bytes recovered), leaving
34,232 bytes below the firmware body limit. The update package remains padded to its fixed app slot.

The existing 56 project sectors (`0xA0000..0xC7FFF`, `0xEA000..0xF9FFF`) now form one pool.
Each of the four project slots owns one committed record. A save allocates free sectors, writes and
verifies the complete payload, then publishes the header last. Only after publication may the old
record's sectors be reused. Working space rotates through the pool rather than reserving a full
permanent duplicate for each slot.

- Maximum storage payload per project: **41,888 bytes**, formerly **27,904 bytes** (+50.1%).
- Four maximum-sized records use 44 sectors. Eleven sectors remain available for a maximum-sized
  save, with one additional sector spare. Small projects allocate fewer sectors.
- A record's header authenticates a 56-bit sector bitmap, length, object, sequence and payload CRC.
  Every sector reserves 32 leading bytes and leaves its last 256 bytes erased for the SPL scan.
- Legacy A/B projects remain readable. The first save migrates only that slot into free space;
  interruption cannot erase the last committed version of any slot. Unknown musical formats with
  valid storage checksums are protected and remain available to the existing archive reader.
- The existing project format, notes, patterns, steps, RAM allocation, four slot identities and web
  protocol remain unchanged. The larger storage budget is available for future project-format growth;
  this update does not raise musical limits.
- Settings and preset banks retain their existing A/B protection. App, update-loader staging,
  boot, settings, preset and key/MAC boundaries stay unchanged. All firmware image savings remain
  firmware headroom; no app sectors are transferred to the data pool.

This storage layout is read by the new firmware. Export projects before downgrading to an older
Melodee version, which only understands fixed A/B locations. Stock installation and recovery retain
their existing boundaries; a stock round trip is not a project-preservation guarantee.

Validation includes four fully occupied slots, repeated saves, legacy migration, program/byte
cutoffs, every allocated erase boundary, stale-stage rejection, readback verification, region guards,
archive round trips and layout rendering. Hardware power-cut and flash-latency qualification is pending.

The older whole-device flash-map v2 proposal below remains unimplemented.

## Decisions (Kerem, 2026-10-11)

- **2 sub-pixel phases for the fonts: yes.** Item 4.
- **ANALOG engine can go: yes.** Item 5. Tracks and presets saved with ANALOG keep engine 0 and play silent
  (no converter). Track 1 powers on with PROPHET FUNK BASS II instead of ANALOG ACID.
- **Going back to stock firmware must always work.** Users must be able to revert at any time, so the
  official loader's staging area stays at 5 sectors, and every flash layout change has to survive a
  round trip to stock and back. See Part 2.

## Progress (branch `feat/storage-savings`)

Done, full host suite passing:

| Change | Flash | RAM |
| --- | ---: | ---: |
| Factory banks: Prophet bit-packed, CZ tones packed with the shared `lz_unpack` (ex-`sid_unpack`) against one tone used as a dictionary; Prophet and CZ preset names read from the native data (`eng_preset`) | −28,768 B | +96 B |
| FM6 RAM tables rebuilt at boot (`dsp_tables_init`, bit for bit); x0x tanh shares `P5_TANH`; 909 pot curves computed | −21,664 B | 0 |
| Fonts: 2 phases, kerning as class matrices | −26,256 B | 0 |
| `minsize` on more main-loop files | −1,152 B | 0 |
| RAM: one scope snapshot, MIDI note owners as nibbles, backup settings and template staged in `set_rec`, update scratch in the project staging | +336 B | −10,432 B |
| **Total** | **581,340 → 503,836 B (−77,504 B)** | **.data + .bss 97,824 → 87,392 B** |

The original savings branch also implemented a boot-time bridge normalizer. The subsequent legacy reader
removal supersedes that normalizer in the final 1.2 build; it is retained only in the historical branch.

### Reader removal and bank cache (included in 1.2)

1.2 reads the project, template and preset formats written since Melodee 1.0 and PER5 settings. The deleted
readers and engine removals are included in the same release as the savings. Unreadable committed records
stay on flash: projects and presets show OLD FORMAT, preset banks are not formatted at boot, and the device
asks before replacing them. Replacing a wrong-shape bank warns that all 16 slots are
replaced. Settings and templates are protected until their replacement dialog is accepted. Direct preset
protocol writes are refused over protected records; full backups retain the unreadable bytes. Current
formats retain their existing save, load and backup paths. ANALOG and DIGITAL engine IDs remain reserved;
those sounds render silence as approved.

| Step | Image | Notes |
| --- | ---: | --- |
| DIGITAL engine and its FM6 conversion (`fm4_convert.c`, `MELODEE_FM4`) | −4,000 B | engine 1 plays silent |
| Projects, banks, templates of before 1.0 (FUN1..FUN15, FBK9..FBKJ, TPL5..TPLE) | −8,556 B | |
| User preset versions below 4, the 192-byte bank, UPF owned voices, `fm6_bank.c`, `native_boot`'s migrations, `RES_LEGACY0/1` | −2,848 B | audio arena minimum 152 → 139 KiB |
| Settings PER1..PER4, untagged palettes, untagged FX keys, SET3 | −800 B | |
| ANALOG (engine 0 retired), its UI picture and designer recipe | −3,252 B | power-on track 1: PROPHET FUNK BASS II |
| `cz_legacy.h` → `cz_panel.h` (the live CZ panel model only) | 0 | the removed parts were already dead code |
| One bank cache for user, FM6, CZ-1 and Prophet banks (`bank_buf`, `bank_claim`) | +64 B | POOL −7,992 B |
| List picker over whole columns; Prophet part DC blocker (5 Hz) and regress on the real factory programs | +176 B | |

Final 1.2 image with safeguards: **487,212 B** (581,340 at the start: −94,128 B; 94,352 B free in the app slot).
.data + .bss 87,472 B (−10,352 B from 97,824 B). The shared bank cache separately reclaims 7,992 B from the pool.
Audio arena: 163,776 B available, 142,336 B required (21.4 KB of headroom, was 128 B).

Along the way:
- `fix(backup)`: a settings record with recording preferences (the tagged zoom word) was refused on restore.
  Included in 1.2 with the complete feature history.
- Backup restore (web): the editor now begins and aborts every object first, so an archive 1.2 cannot read
  (owned FM6 voices, PER4, FUN6 ..) is refused whole before anything is written.
- Tests that relied on engine 0 as "a synth" use `HOST_SYNTH` (CZ-1, the generic voice path) or SID.

Kept on purpose:
- The editor protocol's retired stubs (sample commands, FM6 bank rc 3, font weight): a few dozen bytes, and they
  keep older editors from timing out.
- `MELODEE_LEGACY_EXTRAS` engines (PHASE, VOICE, TRIO, WHEEL, PHYS, NOISE): not in the image; reference tests still
  build them. Deleting them is tree cleanup only.

Sound: only the 909 kit golden changed (2 tanh entries an ULP apart, pot curves within 3e-7). FM6 tables were
compared value by value with the old generator's.

## Where we stand (before the work)

The FM-1 has a 1 MiB SPI NOR (Puya, JEDEC `0x856014`, checked at boot by `persist_boot`). There is no
larger part to move to, so every gain is a reallocation.

| Region | Sectors | Bytes | Notes |
| --- | ---: | ---: | --- |
| Boot head (SPL, isd_config) | 4 | 16 KB | fixed |
| App area `0x04000..0x92FFF` | 143 | 572 KB | image 581,340 B of 581,564 B (**224 B free**) |
| User data (A/B objects) | 100 | 400 KB | about 174 KB of payload at most |
| OTA staging `0xE0000..0xE4FFF` | 5 | 20 KB | sized for the official loader (`OTA_LDR_LEN` 0x4DE1) |
| `key_mac` `0xFF000` | 1 | 4 KB | SDK |
| Free (`0x9D000`, `0x9E000`, `0xFB000`) | 3 | 12 KB | unused |

RAM is tight as well (.data + .bss 97,728 of 98,304 B), so no item below may move a table from flash
into RAM unless it is already in RAM today.

### What the image is made of (flash bytes)

| Area | Bytes | Biggest pieces |
| --- | ---: | --- |
| UI code | ~120 KB | `draw_page` 27 KB, `ui_input` 17 KB, `ui_draw_frame` 12 KB |
| Fonts | 69 KB | 4 faces; X, S and M have 4 sub-pixel phases per glyph; kerning 16.6 KB |
| Factory banks | 41 KB | `P5_FACTORY` 27.6 KB, `CZ_FACTORY` 9.2 KB, preset name tables |
| DSP tables | ~50 KB | x0x 13.8 KB, FM6 ~20 KB (11.8 KB of it in `.data`), P5 6 KB |
| String literals | ~17 KB | |
| Icons, keycaps | 20 KB | 14.2 KB icons (71 drawings, 3 sizes), 5.8 KB keycaps |
| Editor protocol | ~19 KB | `ed_handle` 14.5 KB |

## Part 1: app image savings

Labels: **measured** = built in a scratch worktree and compared with the 581,340 B baseline;
**computed** = size of the data before and after the encoding, decoder code not counted;
**estimate** = not built.

### 1. Retire legacy formats and migrations (measured: −10.7 KB, more left)

Every format older than what Melodee 1.0.0 writes (projects `FUN16`, banks `FBKK`) dates from
Felucca or from Melodee's 0.x betas (2026-10-03..09). Gating the readers in a scratch build:

| Gate | Saved |
| --- | ---: |
| Project formats FUN1..FUN15 and bank formats FBK9, D..J, CZ_OLD/NEXT (`proj_import_any`, `bank_valid`, `bank_upgrade`, `proj_unpack` branches); user preset legacy records and the 192-byte bank; settings PER1..PER4 | 7,216 B |
| DIGITAL → FM6 conversion (`fm4_convert.c`, `proj_fm4`) | 2,704 B |
| Boot migration of owned FM6 voices and CZ banks into the native stores (`native_boot`) | 768 B |
| **Total measured** | **10,688 B** |

Not gated yet, estimated at 2–4 KB more: the UI's DIGITAL paths (`fm4_apply`, `fm4_load_preset`, DIGITAL
stars in `settings_import`), `fm6_bank.c`, the `up_fm6.c` migration, `cz_legacy.h` (the CZ editor still
uses `lcz_sx_encode`, so only part of it can go), `phys_legacy` / `drum_from_phys`, the legacy favorites
rows, editor backup ids 8 / 19 / 20, and the retired editor protocol commands (`EDITOR_PROTOCOL.md`
lists about a dozen stubs).

#### Inventory: what goes, and what needs an audit first

Plain deletions (each is a reader or converter of data written before Melodee 1.0.0):

| Where | What |
| --- | --- |
| `project.c` | `proj_import_any` branches FUN1..FUN15 and the two legacy CZ sizes; `proj_import_old` (FUN1..FUN6) and the frozen `project_v1..v6_t`; the version branches in `proj_unpack`; `proj_drums_to_part` (GM drum track 4); `proj_phys` (PHYS DUST / DRUM); `proj_g_from_v2`, `proj_trk_from_v2`, `proj_delay_off`; `proj_fm4`; the "older projects have no origin" Prophet factory match; `tmpl_take`'s TPL5..TPLE formats |
| `pattern_store.c` | every `bank_v9` version (FBK9, D..J, CZ_OLD / CZ_NEXT), `bank_step9_unpack`, `bank_upgrade`, the DIGITAL motion filter in `bank_valid` |
| `upreset.c` | 192-byte records (`UP_BANK_LEGACY_SIZE`), `up_legacy_cz` / `up_legacy_bits` (next's v6 / v7 CZ records), `up_migrate` (PHYS DRUM → DRUM), record versions below 4 |
| `settings_persist.c` | PER1..PER4, palette ids below 20, DIGITAL stars → FM6 stars |
| `native_presets.c` | `native_boot`'s migrations (owned FM6 voices → native FM banks, CZ bank v1 and user records → native CZ), `native_release_shared` |
| `up_fm6.c`, `fm6_bank.c` | the owned-voice and retired FM6 bank formats (migration sources only) |
| `fm4_convert.c`, `ui.c` `fm4_*`, `eng_digital.c` | DIGITAL → FM6, and the whole `MELODEE_FM4` build option with its three test builds |
| `editor_backup.c` | archive ids 8, 19, 20; FBK9 / FUN8 / FUN7 / FUN6 restores; PER4 restores |
| `eng_analog.c` | ANALOG (decided: stored tracks go silent); `TRK_DEF`'s power-on bass changes |
| `eng_phase.c`, `eng_formant.c`, `eng_trio.c`, `eng_wheel.c`, `eng_phys.c` + `phys_*.c`, `eng_noise.c` | `MELODEE_LEGACY_EXTRAS` engines: not in the image, but kept compiling for reference tests; `phys_legacy`, `drum_from_phys` |
| editor protocol | the retired stubs (`EDITOR_PROTOCOL.md`: sample commands, FM6 bank commands answering rc 3, font weight, 8-byte steps), with the matching `web/editor.html` and `web/fm1backup.js` paths |
| tests | `project_test` (FUN1..FUN5), `settings_test` (PER1..PER4), `motion_test` (FUN7), `upreset_test` (versions), `fm4_test`, `digital_test`, `ui_test_fm4`, `ui_render_fm4`, `engine_retirement_test`, `backup_test`'s older projects, `performance_tools_test`'s migrations, `tests/frozen_project.h` |

Needs an audit, not just deletion:
- `cz_legacy.h`: done as `cz_panel.h` (the live CZ editor's panel model; the legacy tone readers deleted).
- `favorites.c`: the rows of retired engines (8 GRAIN, 14 OBXF) now carry the FX key map and the scale
  favorites. Keep the layout and drop only the Felucca 1.0 star migration.
- `up_rec_t`: keep its on-flash layout (`cz_extra[46]` and the rest). Only the readers of old versions go.
- Engine numbers stay reserved (append-only): ENG_RETIRED rows stay; ANALOG's 0 joins them.
- `storage.c`: the retired project object numbers stay reserved, and `OBJ_FM6BANK`'s sectors still hold
  native FM banks 2 and 3.
- The bank caches (~8 KB of RAM) can share one buffer only after `native_release_shared` is gone, and
  only after checking each of the ~200 call sites that holds a bank pointer across another bank's load.
- Since 1.3.1, legacy Prophet banks at `0x89000` are retired. The updater and ROM recovery tool
  ignore them; they are not relocated. Current banks at `0x93000` remain supported.

A catch: legacy *records* can sit inside current containers. A UPB1 bank can still hold version 1–3
records, and a slot that was never saved again after an upgrade still holds FBKJ or older. Dropping the
readers makes these records unreadable. The final 1.2 safeguards preserve them until an explicit replacement;
the original proposal for a separate bridge release below was superseded.

### 2. Factory banks (computed: about −30 KB)

The device already has a byte-LZ77 decoder: `sid_unpack` (`eng_sid.c:141`, about 60 B of code), packed
by `sid_lz` in `tools/gen_tables.py`. It can be reused for the banks.

| Item | Today | After | Saved | How |
| --- | ---: | ---: | ---: | --- |
| `P5_FACTORY` (200 × 133 B) | 27,600 | ~11,400 | ~16.2 KB | per-field bit packing: 89 of 133 fields vary, 55 B per program, a 400 B field table, random access, no scratch RAM. `sid_lz` in blocks of 8 programs gives 13.1 KB plus 1 KB of scratch, so bit packing is better here |
| `P5_TEST_PRESETS` and its 200 name strings | 7,851 | 0 | ~7.8 KB | the names are already inside the native programs (`p5_patch_name`); give `engine_t` a name callback in place of the 24-byte `preset_t` rows |
| `CZ_FACTORY` (64 × 144 B) | 9,216 | ~4,864 | ~4.3 KB | `sid_lz`, 8 tones per block; 1,152 B of main-loop scratch (the `proj_wire_u` staging buffer or the resource arena) |
| `CZ_PRESETS` and its names | 2,322 | 0 | ~2.3 KB | the names are bytes 128..143 of each tone |

### 3. DSP tables (computed: about −22 KB, 12 KB of it bit-exact)

| Table | Bytes | Saved | How | Sound |
| --- | ---: | ---: | --- | --- |
| `x0x_tanh_tab` | 4,104 | 4,104 | identical to `P5_TANH` (both are `tanh(i/128)` as f32); give `P5_TANH` the guard point and share it (it is in RAM, so the drums get faster too) | bit-exact |
| `FM6_SIN` (`.data`) | 4,100 | 4,100 | rebuild at boot with Dexed's integer rotation (`sin.cc`): int64 multiplies, exact float conversion | bit-exact |
| `FM6_MKI_LOG`, `FM6_OPL_LOG` (`.data`) | 5,120 | 2,560 | both are `half + reversed(half)`: store half, mirror it at boot | bit-exact |
| `FM6_MKI_EXP`, `FM6_OPL_EXP` (`.data`) | 2,560 | ~1,900 | monotonic, steps of 3 to 6: delta-code them, expand at boot | bit-exact |
| `x0x_pot_exp` (19 ranges × 128 floats) | 9,728 | ~9,500 | `lo * 2^(log2(ratio) * t)` from `P5_EXP2`, only when a pot moves | golden hashes change by an LSB |

The `.data` tables stay in RAM, so RAM use is unchanged: only their flash load images shrink. Check
`tests/fm6_parity.sh` for the FM6 tables, and update the drum goldens for `x0x_pot_exp`.

### 4. Fonts (computed: −26 KB, 68,959 → ~42,900 B)

| Change | Saved | Effect |
| --- | ---: | --- |
| 2 sub-pixel phases instead of 4 for X, S, M (**accepted**) | 20,064 B | glyph positions snap to 1/2 px. Recheck `tests/text_spacing_test.py` (0.25 px tolerance) and the mock comparisons (`tools/ui_mockcmp.py`) |
| Kerning as class matrices (about 56 × 48 classes per face) in place of 1,500 sorted pairs | 5,958 B | lossless; the lookup becomes O(1) |
| Huffman-code S and X too (`--huff X,S,M,L`) | 1,008 B at 2 phases | not worth it: S labels would draw slower for 1 KB |
| `--kern-min 4` (drop pairs under 1/4 px) | 7,575 B | not needed: the class matrices already shrink the kerning |

### 5. Smaller items

- **Compiler (measured: −1,104 B):** `minsize` for main-loop-only files that `tools/size_fns.py` does not
  list yet: main.c without its ISR, params.c, browse.c, screen.c, panel.c, cz_edit.h, prophet_edit.h,
  fm4_convert.c, cz_legacy.h, ota.c, favorites.c, settings_persist.c. Adding the sequencer files too gives
  −1,744 B, but those run next to the ISR. Leave them alone.
- **Dead UI and icons (estimate: 2–5 KB):** 33 of the 71 icon drawings are never named in the source.
  Some are reached by arithmetic (`ICON_X_TRK1 + k`), so do not grep. Have the host UI test, which
  renders every screen, log the icon ids and code regions it actually uses (cv_icon_on, plus
  `--coverage` on the host build), then delete what nothing reaches.
- **ANALOG engine (accepted, estimate: 3–4 KB):** the browser no longer offers it, but track 1 still
  starts as ANALOG ACID (`TRK_DEF` in engines.c, also used for project defaults in project.c). Give the
  power-on bass a Prophet or FM6 preset and keep engine 0 reserved, like the other retired IDs.
  Consequence: every project started from the power-on default, with track 1 never changed, has an
  ANALOG track. Retired engines render silence, so those bass lines would go quiet. Two ways out:
  (a) accept the silence; (b) have the bridge release convert ANALOG tracks and user presets into Prophet
  patches. The two engines share most controls (2 oscillators with saw / pulse / triangle, mix, noise,
  cutoff, resonance, key tracking). SIN and DRV have no exact counterpart, so the result would sound
  close but not identical. The converter exists only in the bridge release, so 2.0 pays nothing for it.
  **Recommended: (b).**
- **Strings:** the 21 "N-tone equal division" titles can be built from N (~0.4 KB). The credits still
  mention PHASE (retired).

### Tally

| | Accepted | Still optional |
| --- | ---: | ---: |
| Legacy (1) | 10.7 KB measured + ~3 KB | |
| Factory banks (2) | ~30 KB | |
| DSP tables (3) | ~12.6 KB bit-exact | +9.5 KB (`x0x_pot_exp`: drum goldens change) |
| Fonts (4) | ~26 KB (2 phases + class kerning) | |
| Smaller items (5) | ~1 KB `minsize` + 3–4 KB ANALOG | +2–5 KB dead UI and icons |
| **Total** | **~87 KB** | **~100 KB** |

## Part 2: more flash for the app

Today every user object is an A/B pair in fixed sectors. That costs 100 sectors for at most 174 KB of
payload. CZ banks (2,332 B) and native FM banks (2,060 B) each use a whole sector per copy.

### Proposal: rotating-spare pools (flash map v2)

Group the objects by size class. Each class gets N homes plus one spare. A save writes the spare
(commit record last, as today), the newest `seq` wins at boot, and the old home becomes the next spare.
Power-fail safety stays the same as with A/B, wear spreads over the whole class, and the `st_hdr_t`
commit record stays as it is. Only `st_sector` becomes a table that a header scan fills at boot.

| Class | Objects | Sectors today | Sectors in v2 |
| --- | --- | ---: | ---: |
| Projects (≤ 27,904 B, 8 sectors of 3,808 B each) | 4 | 56 | 40 (5 homes) |
| Single-sector objects: P5 ×5, CZ ×8, native FM ×4, user presets ×4, settings | 22 | 44 | 24–26 |
| **Total** | | **100** | **64–66** |

Indicative map with the fixed regions kept (head, OTA staging at `0xE0000` with its SPL record at
`0xE4F00`, `key_mac`):

| Range | Sectors | Use |
| --- | ---: | --- |
| `0x00000..0x03FFF` | 4 | boot head |
| `0x04000..0xB7FFF` | 180 | **app area: 720 KB (+148 KB)** |
| `0xB8000..0xDFFFF` | 40 | project pool |
| `0xE0000..0xE4FFF` | 5 | OTA staging |
| `0xE5000..0xFEFFF` | 26 | single-sector pool (2 spares) |
| `0xFF000` | 1 | `key_mac` |

The app area would grow the way it did on 2026-10-10 (3f86c33): `app.ld` XIP length,
`fm1pkg_make.APP_SLOT`, the loader's `LDR_APP_HI`, and a loader step that moves user data before it writes
the app.

**Do the migration in the update loader, not in the app.** The loader is not part of the app image, so
migration code there costs the app nothing. If a future map migration is approved, copy the body,
then write the commit record, so the move can restart after an interruption. The retired Prophet-bank
relocation must not be reintroduced. The new regions overlap
the old ones, so the move order has to stage through sectors that are free at that moment. Generate the
schedule at build time and run it in the host loader test with a power cut injected after every
erase and program. Sector moves do not need to understand any format: headers and CRCs move unchanged.

Safety net: have the web installer pull a full backup (`fm1backup.js`) before it flashes a layout
change.

Not recommended: dropping A/B, or compressing projects to fit smaller fixed homes. A dense project
does not compress, so this would need a "storage full" path in the UI. A compressed log store is
possible later if more projects per device become a goal.

### Going back to stock firmware (hard requirement)

Users must be able to go back to the stock firmware at any time. Facts from the package and the
update code:

- The update path that installs the stock package stages the official 19.9 KB loader at
  `0xE0000..0xE4FFF` (`ota.c` `OTA_LDR_*`, record at `0xE4F00`). That area stays 5 sectors, stays fixed,
  and `ota.c` keeps the official-loader path in every build, however much else gets trimmed.
- Our package's region descriptors (`fm1pkg_make.flash_image`) copy stock V15's map: code `0..0x93000`
  (PRCT "CODE"), stock's VM storage `0x93000..0xE8FFF`, BTIF `0xE9000`, USR `0xEA000..0xFBFFF`. Most
  Melodee user data already lives in stock's VM range, so going back to stock probably overwrites it
  (not verified: it depends on whether stock formats VM it cannot read). Treat a revert as destructive
  and back up first. The installer should offer the backup automatically, as for a layout change.
- **Growing the app past `0x93000` goes beyond stock's own code partition.** The 2026-10-10 growth only
  came up to stock's boundary, so nothing so far shows that the SPL decrypts or maps code past it.

- The running firmware is the only part of the device that answers the updater, so this path is the
  only supported way back to the factory firmware. `ota.c` recognises the official loader by its CRCs
  and length, and refuses it unless the stock package's flash head (`0x0000..0x3FFF`) equals ours,
  because the official loader rewrites the whole flash, head included. Flash map v2 must therefore
  never change the head: the app's size belongs in the region descriptors inside the app area.
- Last resort, not verified: UBOOT is the chip's mask-ROM USB mode (OCT− + OCT+ for 5 s, SysEx
  `F0 22 24 35 7D F7`, or two crashes within 30 s of boot). It works whatever the flash holds, but only
  JieLi's own PC tools speak to it. Before shipping any loader-side migration, check on a spare unit
  whether V15 can be restored from UBOOT, and document the steps if it can.

So flash map v2 is gated on a hardware probe before any real work:

1. Build a test image whose app area reaches past `0x93000`: a larger PRCT "CODE" descriptor, and a
   known pattern plus a function placed beyond the old boundary. Check that it boots, that XIP reads
   decrypt correctly there, and that the code runs.
2. From that image, go back to stock V15 the way users do (the web installer and the M-UPGRADE path).
   Check that stock boots, saves its settings and survives a power cycle with Melodee's code bytes left
   in its VM range.
3. Install Melodee again. The loader has to tell three cases apart: stock leftovers (no valid `ST_MAGIC`
   commit records, so pools start empty), the old Melodee layout (migrate), and v2 (leave as is).
4. Add a build check that no sector tail in the grown app area parses as an update record: the SPL
   scans sector tails for them, which is why preset objects keep their last 256 B erased today.

If step 1 or 2 fails, keep the app area at `0x93000` and use the freed sectors another way. One option is
read-only assets (fonts, factory banks) in a plain resource region that the app reads through XIP, the
way it already reads bank objects (`ED_BK_FLASH_PTR`). Even then, the round trip to stock still has to
be checked.

Still open: are `cfg_tool.bin` and `eq_cfg_hw.bin` (4,164 B inside the app area) read at all? Keep them
unless stock, or a revert, is shown not to need them.

## Release plan

1. **1.2.0:** merge all completed Part 1 changes together: packed factory banks, rebuilt DSP tables,
   two-phase fonts and class kerning, size optimisation, RAM savings, legacy readers and DIGITAL/ANALOG
   removal, the shared bank cache, UI picker and Prophet DC fixes, backup validation, and unreadable-data
   safeguards. The normal firmware version is v1.2.0. There is no prerequisite bridge installation.
2. Verify the final firmware build and complete host suite before merging into `next`.
3. **Later:** the flash-map v2 / XIP probe and any partition expansion remain separate until hardware
   evidence supports them. Keep the stock loader staging area and stock-firmware return path.
