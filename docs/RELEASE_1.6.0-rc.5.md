# Melodee 1.6.0-rc.5

Fifth release candidate for 1.6. Stable remains **1.5.1**. This candidate adds 16
patterns on the synth tracks, replaces the four fixed project slots with a shared
storage pool of 32 projects, restores the Rubik font, and lets the Bluetooth radio
calibrate itself. Earlier 1.6 features remain included; see the
[rc.4 notes](RELEASE_1.6.0-rc.4.md) for that baseline.

## 16 patterns on the synth tracks

- Tracks 1–4 have **16 patterns**; the MIDI-only tracks 5–8 keep **8**.
- With PATTERNS open or SEQ held, **all 16 white keys (F3–G5) select patterns
  1–16 directly**: no pages. On a MIDI-only track the first eight white keys
  are active. The black keys keep track selection and the half/double dialog.
- PATTERNS shows all 16 on each synth track's row (narrower cells, numbered every
  fourth), 8 on each MIDI-only track's row. The Stage, dialogs, song blocks and
  knobs count to 16 (or 8) as well.
- SONG sections can use patterns 9–16 on the synth tracks.
- A project can hold what its save can: an edit that would not fit is refused
  with **PATTERN STORAGE FULL**, so the music can always be saved. About 1,450
  set steps fit across all of a project's patterns (a 16-hit drum pattern uses
  136 bytes of the room).

## Projects: one shared storage pool

- **32 projects** instead of four fixed slots. An empty slot occupies nothing;
  a project occupies what it holds: empty 256-byte pages are not stored, so a
  typical project takes about two sectors instead of eight.
- Project and preset storage share **one pool of 79 sectors**. Each preset bank
  is stored once, with one shared spare sector, instead of twice. An unused bank
  frees its space for projects. A save writes the new copy into free sectors and
  commits its header last, so an interrupted save leaves the previous version.
- The project list numbers its slots 01–32; the card shows the room left in KB.
  BOOT offers slots 1–32; hold SAVE, autosave and the template work as before.
- **Erase frees the slot's space**: every saved copy is removed, the oldest first.
- When the pool is full a save says **STORAGE FULL**; erase a project to make
  room. Projects saved by earlier firmware keep their full size until saved again.

## Rubik, factory sounds and the radio program as resources

- The **Rubik** font and the FONT choice are back.
- Rubik, the Prophet, CZ and FM6 factory sounds and the Bluetooth radio program
  move out of the application into a resource region in data flash. The web
  installer (and `tools/fm1_install.py`) writes it right after the firmware.
  This leaves room in the application for future synths and kits.
- Until the resources are written the FM-1 uses Melodee Pixel, INIT sounds for
  factory presets, and refuses to start the radio with **FINISH SETUP IN WEB APP**.
  A code-only update keeps resources whose content has not changed.

## Bluetooth radio calibration

- When no stored radio calibration exists, the radio now **measures its own at
  start** (JieLi's AC79 SDK calibration rewritten in C by Charles Vestal, PR #90),
  instead of showing **NO CAL**. A calibration restored from the unit's own backup
  (`tools/ble_calibration.py`) still replaces the measurement.
- The separate SDK calibration helper and its web recovery page are removed.

## Compatibility and limits

- **Export projects before downgrading.** Older firmware cannot read the shared
  pool, compact project records or the new FBKP project format, and may overwrite
  their sectors. Projects of every earlier format still load here, songs included;
  patterns 9–16 start empty. A dense older project that does not fit the new
  format is saved in its earlier format while patterns 9–16 stay unused.
- Backups now list 57 objects (32 projects). Older archives restore; archives from
  this candidate are refused by older firmware before anything is written.
- The resource region must be written after updating; returning to stock firmware
  erases it.
- Audio working memory is **92.4 KB** (was 114.3 KB): every stored pattern keeps
  room to be completely full in RAM. Very heavy setups may now find less room.
- The web editor's pattern view still shows eight patterns; its project command
  still addresses slots 1–4.

## Validation and downloads

The release uses the reviewed
[`zvenson/fm1-build` workflow at `v1`](https://github.com/zvenson/fm1-build/tree/v1).
Host/web tests block publication. Physical validation is pending for the shared
storage pool (power cuts, flash latency), the resource region install, 16-pattern
editing and playback, and the radio's self-calibration on more units.

Downloads include dual-core firmware, application binary, the resource file
(`melodee-1.6.0-rc.5-res.bin`), native recovery archives for Windows x64, macOS
Apple Silicon, macOS Intel and Linux x64, licence notices, `BUILD_INFO.json` and
`SHA256SUMS`.

```sh
gh attestation verify melodee-1.6.0-rc.5.fwsc --repo keremimo/melodee \
  --signer-workflow zvenson/fm1-build/.github/workflows/build.yml \
  --source-ref refs/tags/v1.6.0-rc.5
```

See [licensing](../LICENSING.md) for retained third-party notices and attribution.
