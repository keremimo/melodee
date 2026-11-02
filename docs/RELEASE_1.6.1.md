# Melodee 1.6.1

Melodee 1.6.1 lets external gear follow the FM-1. It is the first release that sends
MIDI clock and transport, and its internal clock no longer drifts. The manual was also
rewritten to match the firmware. Stored data is unchanged from 1.6.0.

**Not yet tried with real gear:** clock output and Song Position are covered by
automated tests, but no external follower has been played against them on hardware.
Please report what works and what does not, with the device and connection you used.

## MIDI clock out

- With **CLK** on **INT**, the FM-1 sends MIDI clock on USB, USB HOST and Bluetooth,
  stopped or playing, with Start on PLAY and Stop on STOP. A DAW or drum machine can
  now follow it.
- **GLO + PLAY** sends Stop then Start. Leaving INT sends one Stop. With CLK on USB,
  TRS or BLE the FM-1 follows and sends no clock.
- **PLAY from a SONG cursor** sends Song Position and Continue, so a follower starts
  at the same place. Song Position has no lead time: gear that needs time to locate
  may miss the first pulses.
- Clock and transport have their own queues, ahead of notes. Pulses wait while a SysEx
  transfer is in flight, so a patch or editor transfer can hold them briefly.
- TRS is an input only, so no clock leaves through it.

## Following Song Position

With CLK on USB, TRS or BLE, Song Position then Continue starts every pattern where
playing from the top would put it, on the input's first clock. The FM-1 stays in its
patterns: it does not run its own SONG sections while following.

## Timing

- **INT no longer drifts.** It now runs on the same exact musical units as external
  clock. Over five minutes, a 1/32 track used to drift +70 ms at 97 BPM, a 1/16 track
  +27 ms, and the arpeggiator −1,375 ms at 120 BPM. All are now zero.
- **Synced LFOs lock to the song.** On INT they are exact. On external clock they keep
  the measured tempo, so pulse jitter cannot modulate them, and are pulled back if they
  stray more than a quarter of a pulse from the song.

## Other changes

- **Loading a project or template keeps CLK.** It is now a device setting, so a song
  saved on INT no longer takes the FM-1 off the clock you chose. Restoring a backup
  still restores it. TUNE, SYSTEM MIDI and ROUT still come from the project.
- **Manual rewritten against the firmware:** new sections for MIDI tracks, clock and
  transport, MIDI LEARN, Bluetooth and USB Host, and corrected Projects, Patterns,
  Notes, Songs, Menu, GLO and USB audio pages. The README and the Bluetooth guide were
  brought up to date too.

## Upgrading

- **From 1.6.0:** install as usual. Projects, presets, settings and backups are
  unchanged, and 1.6.0 can still read them.
- **From 1.5.1 or earlier:** read Upgrading in the
  [1.6.0 release notes](RELEASE_1.6.0.md) first, and back up on your current firmware.
- **Check any setup that relied on a project's CLK.** Loading a project no longer
  changes it, so set GLO > GLOBAL > CLK once on the device.
- **External gear now receives clock.** Anything listening to the FM-1 on USB or
  Bluetooth sees clock and transport with CLK on INT, which can start a DAW or sequencer
  that follows incoming MIDI.

## Validation and downloads

The release uses the reviewed
[`zvenson/fm1-build` workflow at `v1`](https://github.com/zvenson/fm1-build/tree/v1).
Host and web tests block publication. Firmware and application attestations, source
tag and commit, matching application bytes, all four native recovery archives and
checksums must pass before the draft is published.

Automated coverage includes exact no-drift checks over long runs, the clock and
transport messages on each output, Song Position in both directions, synced LFO lock,
and the USB HOST and Bluetooth send paths against models. These are not the real thing:
the USB HOST send path and SysEx tools running alongside the clock carry the most
hardware risk, and automated checks and build provenance do not establish hardware
behaviour.

Downloads include dual-core firmware, application binary, the resource file
(`melodee-1.6.1-res.bin`), native recovery archives for Windows x64, macOS Apple
Silicon, macOS Intel and Linux x64, licence notices, `BUILD_INFO.json` and
`SHA256SUMS`.

```sh
gh attestation verify melodee-1.6.1.fwsc --repo keremimo/melodee \
  --signer-workflow zvenson/fm1-build/.github/workflows/build.yml \
  --source-ref refs/tags/v1.6.1
```

See [licensing](../LICENSING.md) for retained third-party notices and attribution.
