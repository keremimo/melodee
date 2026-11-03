# Melodee 1.6.0-rc.3

Third release candidate for 1.6. Stable remains **1.5.1**. This candidate adds
section-local sound copies, improves song punch editing, and changes project
storage and the firmware font. Earlier 1.6 arrangement and Bluetooth features
remain included; see the [rc.2 notes](RELEASE_1.6.0-rc.2.md) for that baseline.

## A different preset for one track in one section

- While stopped in **SONG**, select the section with KNOB 1 and track with KNOB 2,
  then **OCT+ > Set preset**. Use the normal preset browser to audition sounds.
  **OCT+ saves a sound copy** into that exact cell; **OCT− cancels**. Leaving the
  browser also restores the normal track sound.
- Each assignment saves an independent copy of the sound, including its native
  FM6, CZ, Prophet or drum patch. Later edits or overwrites in the preset library
  do not change the song. **Remove preset** clears the assignment.
- The sound applies only in that section and its repeats. Unassigned sections
  and STOP restore the track's normal sound. A section inside a spanning block
  can have its own preset without changing the shared note pattern.
- Sound changes release the old sequence notes. Continued blocks keep their
  playback position and automation; subsequent note onsets use the new sound.
  Track mix, FX routing, scale, arp, notes and pattern timing remain track-owned.
- Cell and section copy/move, duplication, deletion, undo, project saves and
  backups preserve sound assignments. The shared header names the selected
  assignment, with transport and LOOP statuses retaining priority.
- Up to 64 distinct sound copies can be retained in project/edit memory, subject
  to available RAM and the existing project-save size limit. Tracks 5–8 remain
  MIDI-only. Section changes copy prepared RAM; they do not read preset storage.

## Visual song punch FX editing

- Tap **FX** on SONG to open the selected section's punches directly, or use
  **OCT+ > Punch FX**. Holding FX keeps the live punch layer.
- Keys choose effects silently. KNOB 1 follows the same physical key order,
  KNOB 2 selects one of four punch slots, KNOB 3 moves **START**, and KNOB 4 sets
  **END**. Both positions are one-based step numbers: START 5 / END 8 includes
  steps 5 through 8. OFF removes the selected punch; EDIT also removes it.
- Four lanes show effect names, timing and selection, with a playhead during
  audition. SELECT browses sections; OCT+ selects the next free punch slot.
- SONG draws effect-coloured spans within the pattern blocks. Pattern labels and
  selection outlines remain visible. The footer keeps song position/progress,
  and the shared header keeps its normal transport context during saved punches.
- Punch timing, section repeats, overlap priority, live-key overrides and song
  undo retain the earlier candidate's behavior. Edits remain stopped-only.

## Project-save storage and firmware headroom

- The existing 56 project sectors form a shared pool. Each of the four project
  slots owns one committed record; common free sectors provide save workspace.
  A save writes and verifies the complete replacement before publishing its
  commit header and making the old record's sectors reusable.
- Maximum storage payload per project grows from **27,904 to 41,888 bytes**.
  This provides storage headroom; musical/project-format limits are unchanged:
  eight patterns per track, 64 steps per pattern and 1,024 shared recorded notes.
- Legacy fixed A/B project records remain readable. The first save migrates only
  that slot. Settings and preset banks keep their existing A/B protection.
  App, boot, loader, settings, preset and key/MAC boundaries remain unchanged.
- Firmware now uses **Melodee Pixel** throughout, including icon numerals. The
  FONT menu is removed and old font preference bits are ignored. Historical
  Rubik design sources and their licence notices remain in the repository.

## Compatibility and limits

- **Export projects before downgrading.** Older firmware understands only fixed
  A/B project locations and cannot read project saves migrated into the pool.
  A stock firmware round trip does not guarantee project preservation.
- Songs with sound assignments use the versioned **FBKO** container. Older
  firmware cannot read it. Projects without assignments retain the applicable
  earlier container; existing supported projects load without sound overrides.
- FBKO includes immutable sound copies alongside the arrangement and punch data.
  Copying a sound assignment shares its immutable song copy; replacing one cell
  leaves other cells unchanged. Oversized saves report **PROJECT FULL** before
  replacing the previous committed save; notes are never truncated to fit.
- The web editor can back up and restore FBKO. Sound assignments and timed
  punches are edited on the device; replacing the whole arrangement through
  the existing web arrangement command clears this section-local metadata.
- MIDI port discovery also includes the FM-1 naming and truncated engine-name
  handling fixes delivered after the rc.2 tag.

## Validation and downloads

The release uses the reviewed
[`zvenson/fm1-build` workflow at `v1`](https://github.com/zvenson/fm1-build/tree/v1).
Host/web tests block publication. Firmware/app attestations, source tag/commit,
matching application bytes, all four native recovery archives and checksums must
pass before the draft is published. Only the candidate installer channel changes;
stable 1.5.1 and its manual remain available.

Automated coverage includes section sound selection/cancellation, native patches,
library overwrite independence, section playback and STOP restoration, motion
bases, undo, project/flash-stream/backup round trips, malformed sound references,
punch editing and playback, shared-pool save interruption/migration and region
guards, reference audio, resource/stack budgets and screen layouts. Focused
section-preset tests also run with address/undefined-behavior sanitizers.

Physical validation remains pending for section sound transitions, sustained
audio/MIDI timing and the shared storage pool's power-cut/flash-latency behavior.
Bluetooth interoperability limits from the earlier candidate still apply.
Build provenance and automated checks do not establish those hardware behaviors.

Downloads include dual-core firmware, application binary, Bluetooth calibration
helper, native recovery archives for Windows x64, macOS Apple Silicon, macOS Intel
and Linux x64, licence notices, `BUILD_INFO.json` and `SHA256SUMS`.

```sh
gh attestation verify melodee-1.6.0-rc.3.fwsc --repo keremimo/melodee \
  --signer-workflow zvenson/fm1-build/.github/workflows/build.yml \
  --source-ref refs/tags/v1.6.0-rc.3
```

See [licensing](../LICENSING.md) for retained third-party notices and attribution.
