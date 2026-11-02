# Melodee 1.6.0

Melodee 1.6 is about writing whole songs on the FM-1: 64 sections with rests and
long blocks, timed punch FX, a preset for each block or pattern, 16 patterns on
each synth track and 32 projects. It also adds Bluetooth MIDI, MIDI LEARN, two
drum kits and note editing while the sequence plays. This changelog covers the
changes since stable **1.5.1**, including the eight 1.6 release candidates and
the changes made after rc.8.

**Before you update:** read Upgrading below. Existing projects load, but older
firmware cannot read what 1.6 saves, and CZ-1 tracks now sit lower in the mix.

## Arrange longer songs

- **64 sections**, with an elapsed **minutes:seconds** ruler above the patterns.
- **Per-track rests** let parts drop out. Releases, delay and reverb tails decay
  naturally; sequenced notes, recorded notes and motion stay silent.
- **KNOB 4 resizes the selected block** into following sections on that track.
  Continued sections keep note and automation phase. Trimming leaves rests;
  extending stops at another occupied block and can add empty sections at the end.
- **PLAY starts at the selected section**, including the position inside a long
  block. Held and recorded notes and motion resume there. A note still held at
  that point plays from there; notes that already ended stay silent.
- **Loop song** in the OCT+ menu is saved per project and defaults Off. The song
  returns from its last section to section one, and PLAY still stops it.
- **Edit on the grid:** Set rest, Copy to…, Move to… and Add empty section on
  OCT+. Hold OCT+ for section insert, duplicate, copy, move, delete, Take jam and
  Clear song. Hold OCT+ and turn KNOB 1 to select a phrase. EDIT held undoes; EDIT
  with OCT− / OCT+ undoes and redoes, up to eight edits.

KNOB 1 selects the section, KNOB 2 the track, KNOB 3 REST or the pattern, and
KNOB 4 the selected block's span.

## Timed punch FX

- Tap **FX** on SONG to place master punches in the selected section: up to
  **four per section**, any of the 27 punch effects, with START and END step
  numbers. START 5 / END 8 covers steps 5 through 8, on every section repeat.
- Punches affect the whole master mix and are saved per section. Effect-coloured
  spans show inside the song blocks, and PLAY auditions them with a playhead.
- Holding a live FX key overrides the lane until you release it. Edits are
  stopped-only and can be undone.

## A preset for each block or pattern

- **SONG > OCT+ > Set preset** gives one track's block its own sound, across all
  its continued sections. **PATTERNS > OCT+ > Set preset** gives a pattern its own
  sound, and switching patterns changes the sound with them.
- Playback resolves **song block preset, then pattern preset, then the track's
  sound**. STOP returns to the track's sound.
- Each assignment stores an independent copy, native FM6, CZ, Prophet and drum
  patches included, so later library edits cannot change the song. Copy, move,
  delete, undo, saves and backups keep them. Up to 64 distinct copies, subject to
  RAM and the project's saved size.

## 16 patterns and 32 projects

- **Tracks 1–4 have 16 patterns;** the MIDI-only tracks 5–8 keep 8. With PATTERNS
  open or SEQ held, the white keys F3–G5 select patterns directly. PATTERNS shows
  them as two pages of eight, and SONG can use all of them.
- **32 project slots** instead of four, in one storage pool shared with the preset
  banks. A project takes what it holds, typically about two sectors instead of
  eight. The list numbers slots 01–32 and shows the room left in KB. Erase frees a
  slot's space, and a save commits last, so an interrupted save keeps the previous
  version. When the pool is full a save says **STORAGE FULL**.
- An edit that would not fit in a project's save is refused with **PATTERN STORAGE
  FULL**, so the music can always be saved. About 1,450 set steps fit across a
  project's patterns. The limit of 1,024 shared recorded notes is unchanged.
- **Factory sounds, the Rubik font and the Bluetooth radio program** moved out of
  the application into a resource region in data flash, leaving room for future
  engines. The update package carries it and Melodee's loader writes it after the
  application. If the factory sounds are still missing, the FM-1 says **Setup
  unfinished** at startup; your own sounds stay.

## Drum kits

- **KIT CR78:** a late-1970s rhythm box after the CompuRhythm CR-78's voice list,
  built from the 808's circuit pieces, with tambourine, cowbell, conga, bongos,
  claves, maracas and its METAL BEAT. The sounds are designed after the box, not
  measured from a unit.
- **KIT FM:** two-operator FM drums with noise. Both kits play all eight lanes on
  one voice, like the SID kit.

## Editing notes and patterns

- **While the sequence plays,** KNOB 3 on SEQ > NOTES changes a grid note's length.
  The keys and MIDI only play on NOTES unless the track is armed; stop to enter
  notes step by step.
- **PITCH** previews each turn and works on a selected range, in Move or Copy
  mode, keeping the intervals. On an empty step it says SELECT A NOTE instead of
  placing a note. One run of turns is one undo.
- **LENGTH and VELOCITY** edit a whole selected range, keeping the differences
  between notes.
- **Copy a pattern to another track:** PATTERNS > Copy to; a track's knob moves
  the target to its row and ALGORITHM picks any track. Steps, timing and recorded
  notes are copied, motion only between tracks of the same engine, and the target
  keeps its own sound.
- **Microtonal SNAP** now picks the nearest scale degree instead of the one below,
  so EDO keys no longer come out flat. Contributed by gabo_tor (PR #93). 12-tone
  SNAP is unchanged.
- **OCTAVE** is a new HOME SELECT and GLO SELECT choice.
- Swung notes played straight now belong to their unswung step, so erasing,
  selecting, deleting and moving hit the note you meant.

## Bluetooth MIDI

- One BLE-MIDI connection, in and out, alongside USB and TRS. Hold HOME and choose
  **Bluetooth**.
- **Mode** picks how you connect: **CONNECT OUT** scans for BLE controllers and
  synths from that page; **VISIBLE** advertises as Melodee XXXX so a Mac, iPad or
  phone can connect to it.
- The radio's on/off state, the pairing and the last device are kept across power
  cycles. After a reboot Melodee looks for the last device until it finds it and
  connects.
- With no stored calibration, the radio measures its own at start (JieLi's AC79
  SDK calibration rewritten in C by Charles Vestal, PR #90).
- Bluetooth audio, two links at once, Secure Connections pairing, wireless updates
  and editor or SysEx traffic are not included. See [Bluetooth MIDI](BLE_MIDI.md).

## MIDI and USB

- **MIDI LEARN** in the HOME menu binds up to 16 CCs to any knob of a track's page:
  start it, turn a knob, send a CC. Bindings are **kept only until power-off**;
  saving them is still open (#105).
- **USB VOLUME** in the HOME menu sets the computer's audio through the FM-1 apart
  from MASTER. The default, 50 %, is the old level at the default MASTER.
- **USB stereo recording no longer follows MASTER** (#103). It records the mix
  before the volume knob, like the stems, so a low knob no longer means a quiet take.
- **USB HOST** finds devices that are more than MIDI, such as an audio interface
  or a sampler with MIDI (tested with an Akai MPC Sample), and devices on a hub
  behind another hub, as in hubs built from two chips. Hubs that answer late get
  longer resets. A hub still needs its own power.

## Sound and levels

- **Held notes no longer duck when you add more notes** (#108). CZ-1 voices ran
  about 6 dB hotter than the other engines and pushed the master limiter down, so
  CZ-1 is now 6 dB lower and the limiter starts at −2 dBFS instead of −5.2 dBFS.
- **Dither:** every cut to whole samples now uses TPDF dither (#100), so fading
  tails stay clean; on Prophet, CZ-1 and FM6 this also removes a dead zone at low
  levels.
- **MASTER** is applied once, to the finished output after punch FX and the
  limiter, so a sounding tail follows the knob too.
- **CLICK LEVEL** is set in percent and no longer follows MASTER. The default,
  12 %, matches the old MID at the default MASTER.
- GLO mute cuts sounding notes with the same short ramp as solo.

## Other changes

- **FM-1s with the UCUN UC25HQ80 flash** (JEDEC ID 0xB36014) now work fully (#110).
  Their flash was treated as unknown, so every save failed and settings were lost,
  and on 1.6 the factory sounds, Rubik and the Bluetooth radio would have been
  missing too. Contributed by HNguyen0325 (PR #111).
- The version shows on the startup screen (#86).
- Dim key lights no longer shimmer. The song's change check held the LED timer
  back on every UI pass; it now runs with interrupts on.
- The web installer shows the resource file's result and adds **Finish setup**.
- The web editor understands rests, continued blocks, 64 sections and Loop song.
  MIDI port discovery and truncated engine names are fixed, and the 1.5.1
  favorites fix is included.
- A web emulator build (David Konsumer, PR #53) is published from nightly to the
  site's `emu/`. It is a development preview, not part of the downloads.
- The host test runner calls Python through `$PYTHON` (Charles Vestal, PR #81).

## Upgrading

- **Back up on your current firmware before updating,** with the web editor's backup
  tools, and export the projects you care about. **That backup is your way back:**
  older firmware cannot read the shared pool, the compact project records or the new
  project formats (FBKM to FBKQ), and may overwrite their sectors. It also refuses
  backups made on 1.6. A stock firmware round trip does not preserve projects, and
  returning to stock erases the resource region.
- **Web installer:** one install. **From 1.5.1 or earlier through Firmware Hub:
  install twice.** Older firmware kept preset banks where the resource region now
  lives. The first install leaves them alone, the new firmware moves them into the
  shared pool at startup and shows Setup unfinished, and the second install writes
  the factory sounds.
- **Projects of every earlier format load,** songs included; patterns 9–16 start
  empty. A dense older project that does not fit the new format is saved in its
  earlier format while patterns 9–16 stay unused. Projects saved by earlier firmware
  keep their full size until saved again.
- **Existing projects can sound different.** CZ-1 tracks are 6 dB lower against the
  other engines, and with the limiter starting later, loud mixes keep more peak
  level: the loudest output rises from about −5.9 to −3.5 dBFS. Check your balance.
- **USB:** stereo recordings are no longer scaled by MASTER, and computer playback
  now follows USB VOLUME instead of MASTER.
- **Backups** now list 57 objects (32 projects). Older archives restore here.
- Every stored pattern keeps room to be completely full in RAM, so very heavy setups
  may find less audio working memory than on 1.5.1.
- The web editor's pattern view still shows eight patterns and its project command
  still addresses slots 1–4. Timed punches and song presets are edited on the device;
  replacing a whole arrangement through the editor clears them. Backups keep them.

## Validation and downloads

The release uses the reviewed
[`zvenson/fm1-build` workflow at `v1`](https://github.com/zvenson/fm1-build/tree/v1).
Host and web tests block publication. Firmware and application attestations, source
tag and commit, matching application bytes, all four native recovery archives and
checksums must pass before the draft is published.

Automated coverage includes song arrangement and punch playback, section and pattern
presets, project, stream and backup round trips, shared-pool interruptions and
migrations, loader resource writes with power cuts, reference audio, CPU and stack
budgets, screen layouts, Bluetooth models, installers and recovery.

Most of these features were also played on one FM-1, including USB HOST with an MPC
Sample on a two-chip powered hub and Bluetooth connecting out to Cubasis on an iPhone.
The UC25HQ80 flash fix was tested by its contributor on their own unit. Other units,
USB devices and Bluetooth peers remain largely untested; automated checks and build
provenance do not establish hardware behaviour.

Downloads include dual-core firmware, application binary, the resource file
(`melodee-1.6.0-res.bin`), native recovery archives for Windows x64, macOS Apple
Silicon, macOS Intel and Linux x64, licence notices, `BUILD_INFO.json` and
`SHA256SUMS`.

```sh
gh attestation verify melodee-1.6.0.fwsc --repo keremimo/melodee \
  --signer-workflow zvenson/fm1-build/.github/workflows/build.yml \
  --source-ref refs/tags/v1.6.0
```

Thanks to Charles Vestal, gabo_tor, David Konsumer and HNguyen0325 for their contributions, to
Optimist for the open BLE stack, to the Felucca and SLOOP authors, and to everyone
who tested the candidates and reported issues. See [licensing](../LICENSING.md) for
retained third-party notices and attribution.
