# Melodee 1.6.0-rc.2

Second release candidate for 1.6, adding timed master punch FX to song sections
alongside the song-arrangement and Bluetooth work since 1.5.1. This is a prerelease; stable remains 1.5.1.

## New since rc.1: timed punch FX

- **SONG > OCT+ > Punch FX** opens the selected section's master FX lane. Add up to
  **four punches per section**, choosing any of the 27 SLOOP effects, a start step
  and a length. KNOB 2 selects the field; KNOB 1 changes its value. Effect Off
  removes a punch; Clear section FX removes all four and can be undone.
- Place punches at **whole-step positions**. Timing follows track 1's pattern
  division and swing; resting and continued sections use 16 straight sixteenth
  steps. Every section repeat replays the punches, including a full-section punch.
  Placement between steps and live recording of FX keys are not included.
- Punches affect the **complete master mix** and are saved per song section. Using
  the same note pattern in another section leaves that section's FX independent.
  Section copy, duplicate, move, delete and undo include the punches; a single
  track's cell copy/move changes only its note-pattern reference.
- Overlapping punches use the latest start; the later punch slot breaks ties.
  An earlier punch resumes if its duration remains. Holding a live FX key
  temporarily overrides the song lane until that key is released.
- The footer shows the section's saved punch count. The shared header names the
  active effect through its fade, or shows FX MEMORY if a buffered effect cannot
  allocate. Buffered song punches capture history from PLAY until STOP or song
  end; filter-only songs do not allocate history.
- Timed punch songs save in **FBKN**. Earlier firmware cannot load them; see the
  compatibility section below. Physical playback of this feature remains untested.

## Arrange longer songs

- **64 sections**, with an elapsed **minutes:seconds** ruler above the patterns.
- **Per-track rests** leave room for parts to drop out. Releases, delay and reverb
  tails decay naturally; sequenced notes, recorded notes and motion stay silent.
- **KNOB 4 resizes the selected block** into following sections on that track.
  Continued sections retain note and automation phase. Trimming leaves rests;
  extending stops at another occupied block and can add empty sections at the end.
- **PLAY starts at the selected section**, including the position within a spanning
  block. It resumes held/recorded notes and motion there, continues through later
  sections, and stops at the end. Moving the cursor while playing does not seek.
- **Loop song** in the tapped or held OCT+ menu is saved per project, defaults Off,
  and is set while stopped. Enabled songs return from the final section to section
  one with fractional boundary time preserved. This loops the complete song even
  when PLAY began at a later cursor section. PLAY still stops and restores the
  patterns selected before song playback. The shared header shows LOOP / SONG LOOP.

KNOB 1 selects the section, KNOB 2 the track, KNOB 3 REST or P1–P8, and KNOB 4 the
selected block's span. Track 1 pattern starts retain the existing section timing;
track 1 rests and continuations use a straight four-beat bar per retained section
repeat. Resizing a block preserves the following sections' times.

## Edit on the arrangement grid

- **OCT+ edits**: Set rest, Copy to…, Move to…, **Add empty section**, and Loop song.
  Added empty sections contain rests on every track at the cursor. Punch FX is
  available in both the tapped and held OCT+ menus.
- Pick a copy/move destination on the visible grid, OCT+ to apply, OCT− to cancel.
  Copy/move acts on a whole block and keeps the underlying note pattern shared.
  Editing that pattern's notes changes every occurrence. Occupied destinations
  must be cleared before placing another block there.
- Hold OCT+ for section insertion, duplication, copy, move, deletion, Take jam and
  Clear song. Hold OCT+ and turn KNOB 1 to select a phrase; tap OCT+ for its actions.
- EDIT held undoes arrangement edits; EDIT + OCT− / OCT+ undo/redo. Up to eight
  snapshots are acquired on demand and may be reclaimed for sounding audio.
- The web editor understands rests, block continuations, 64 sections and the saved
  loop option while retaining compatibility with earlier protocol versions.

## Bluetooth MIDI and radio recovery

- Bluetooth MIDI input/output supports one connection in either role. Settings
  expose discovery, connection, disconnect/reconnect and pairing controls. Radio
  starts Off at boot; USB MIDI/audio and TRS input remain available while it runs.
- Pairing, passkey and connection/setup state use the shared header. Successful
  bonds and the device's own radio identity/calibration are retained in settings.
  Pairing keys and calibration are excluded from musical backup archives.
- The web editor's Bluetooth recovery page bundles the matching calibration helper
  and Melodee firmware. It supports this FM-1's saved calibration or a bounded SDK
  helper, with progress, interruption recovery and return-to-Melodee handling.

See [Bluetooth MIDI](BLE_MIDI.md) and [radio recovery](BLE_CALIBRATION.md) for setup
and limits. The calibration-generation/return cycle has been checked on one FM-1;
radio interoperability with regenerated trims remains unverified. Bluetooth audio
and simultaneous central/peripheral links are not included.

## Compatibility and limits

- Existing FUN and FBKK/FBKL records retain their fixed legacy layouts and load
  through the existing compatibility paths. Older songs default to Loop Off.
- Longer songs, rests, spanning blocks or Loop On save in the new **FBKM** container.
  Earlier firmware cannot read this format. Ordinary short songs without these
  features continue to save in their earlier format.
- Songs with timed punches use **FBKN**, which adds 768 bytes for the song FX
  lane to the FBKM arrangement data. Earlier firmware rejects FBKN. Projects
  without punches retain their existing format. The web editor can back up and
  restore FBKN, but timed punches are edited on the device; replacing the whole
  arrangement through the web editor clears its punch lane.
- FBKM and FBKN use sparse pattern storage in the existing slot extent. Very dense projects
  may reach **PROJECT FULL** sooner. An oversized save is refused before replacing
  the saved copy; patterns are never truncated to make an arrangement fit.
- Eight patterns per track, 64 steps per pattern and 1,024 shared recorded notes
  remain the limits. The web editor retains its four-track arrangement interface.
- The 1.5.1 web favorites connection fix is included; MIDI Track's unsupported
  factory-favorite control remains disabled.

## Validation and downloads

The release workflow builds and attests firmware through the reviewed
[`zvenson/fm1-build` workflow at `v1`](https://github.com/zvenson/fm1-build/tree/v1).
Host/web tests block publication. Firmware/app provenance, matching application
bytes, all four native recovery archives and checksums must pass before the draft
is published. Only the candidate installer channel is updated; stable remains
1.5.1 and its manual is retained.

Automated checks cover reference audio/state, CPU/resource/stack budgets, song
loop timing and STOP restoration, timed punch playback/repeats/live overrides,
section FX edits and persistence, cursor playback, rests/continuations, recorded
notes, motion, menu gestures, undo, project/stream/backup codecs, old-format imports,
web protocol, screen layouts, Bluetooth models, installers and recovery. These
checks do not establish sustained physical audio/MIDI deadlines or Bluetooth
interoperability across devices. The loop option and timed song punches have host/model coverage;
they have not been physically validated on the FM-1.

Downloads include the dual-core firmware, application binary, Bluetooth calibration
helper, native recovery archives for Windows x64, macOS Apple Silicon, macOS Intel
and Linux x64, licence notices, BUILD_INFO.json and SHA256SUMS. Verify the firmware:

```sh
gh attestation verify melodee-1.6.0-rc.2.fwsc --repo keremimo/melodee \
  --signer-workflow zvenson/fm1-build/.github/workflows/build.yml \
  --source-ref refs/tags/v1.6.0-rc.2
```

Retained third-party notices and the distributed licence archive document attribution;
see [licensing](../LICENSING.md).
