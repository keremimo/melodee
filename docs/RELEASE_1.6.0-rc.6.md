# Melodee 1.6.0-rc.6

Sixth release candidate for 1.6. Stable remains **1.5.1**. This candidate adds
two drum kits, a preset for each pattern, note editing while the sequence plays,
range and copy PITCH edits, pattern copy to another track, nearest-degree SNAP
for microtonal scales and OCTAVE as a SELECT choice. MASTER now sets the final
output level and CLICK LEVEL its own. USB HOST finds audio+MIDI devices and the
second chip of two-chip hubs. Earlier 1.6 features remain included; see the
[rc.5 notes](RELEASE_1.6.0-rc.5.md) for that baseline.

## CR-78 and FM drum kits

- **KIT CR78**: a late-1970s preset rhythm box after the CompuRhythm CR-78's
  voice list, built from the 808's circuit pieces. A round low kick, a papery
  snare, a noise hi-hat, tambourine, cowbell, conga, bongos, claves, maracas and
  its METAL BEAT (HATS METAL plays it on the two hat lanes). The sounds are
  designed after the box, not measured from a unit; the box had no toms or clap,
  so TOMS are the conga tuned down and CLAP is the tambourine.
- **KIT FM**: two-operator FM drums with noise, the clap as noise bursts.
- Both play all eight lanes on one voice, like the SID kit. Their levels are
  balanced by measurement and may still be tuned by ear.

## A preset for each pattern

- PATTERNS' sheet has **Set preset** and **Remove preset**: a pattern can carry
  its own sound. Playback resolves **Song section preset > Pattern preset >
  the track's sound**; switching patterns, queued switches included, changes the
  sound with them, and STOP returns to the track's sound.
- Pattern copy, delete and undo keep each pattern's preset with it.

## Note editing while playing

- On SEQ > NOTES, KNOB 3 edits a grid note's length (quarter steps, sixteenths
  with ENV held) while the sequence plays instead of asking to stop. The first
  turn on a grid note while playing cannot be undone; later turns can.
- While the sequence plays and the track is not armed, the keys and MIDI on
  NOTES only play: nothing is entered into the cursor step. Arm REC to record
  live; stop to enter notes step by step.

## PITCH edits on NOTES

- KNOB 2 PITCH on an empty step only says SELECT A NOTE; it no longer places a
  note. Each PITCH turn previews the new pitch (a range only when its notes
  share one step).
- PITCH works on a selected range, in the range's Move or Copy mode: the shift
  keeps the intervals and stops at the first note that reaches the edge. With
  Copy armed, the copy is made in place and the copy is pitched. One run of
  turns is one undo.

## Pattern copy to another track

- PATTERNS > Copy to: a track's KNOB moves the target to that track's row, and
  ALGORITHM picks any track, past the four shown. OCT+ pastes. Steps, timing and
  recorded notes are copied; motion only between tracks of the same engine. The
  target keeps its own sound.
- PATTERNS shows its sixteen patterns as two pages of eight, with the old cells.

## Microtonal SNAP

- QNT SNAP now picks the **nearest** scale degree (a tie takes the lower one)
  instead of the one below. In EDOs almost no key lands on a degree, so most keys
  used to come out flat: in 19EDO, F played the 442-cent E# instead of the
  505-cent fourth. Contributed by gabo_tor (PR #93). 12-tone SNAP is unchanged
  and recordings keep their stored degrees.

## OCTAVE on SELECT

- Settings > HOME SELECT and GLO SELECT offer **OCTAVE**: SELECT moves the keys'
  octave one per detent (−3…+3, as OCT−/OCT+ on Stage), said in the header. On an
  MPC track it moves that track's own octave. Saved choices keep their meaning.

## USB HOST

- Devices that are more than MIDI, such as an audio interface or a sampler with
  MIDI (tested with an Akai MPC Sample), are found: the whole USB configuration
  is read as it arrives instead of its first 320 bytes.
- A device on a hub that sits on another hub's port is found. Many hubs are two
  hub chips in one case, with most sockets on the second.
- A hub that answers late after a bus reset gets longer resets, up to 1.6 s of
  recovery, while nothing answers at all.
- The FM-1 cannot put 5 V on the USB-C cable: a direct USB-C link to a
  self-powered device does not connect. Use a powered hub or a powered OTG
  adapter.

## MASTER, CLICK and other fixes

- **MASTER** is applied once, to the finished output after punch FX and the
  limiter, so a sounding tail follows the knob too.
- **CLICK LEVEL** is set in percent and no longer follows MASTER. The default,
  12 %, matches the old MID at the default MASTER.
- GLO mute cuts sounding notes with the same short ramp as solo.
- The codec stays muted until boot is done, so a cold boot no longer clicks.
- The web installer shows the resource file's result and adds **Finish setup**.
- The web site keeps a channel's resource file when it re-reads the channel.
- The host test runner calls Python through `$PYTHON`, as `build.sh` does
  (Charles Vestal, PR #81).
- A web emulator build (David Konsumer, PR #53) is published from nightly to the
  site's `emu/`; it is a development preview, not part of the release downloads.

## Compatibility and limits

- **Export projects before downgrading.** A project saved with a pattern preset
  uses the new **FBKQ** format, which earlier firmware (rc.5 included) cannot
  load. Projects without pattern presets still save as FBKP. Every earlier
  format loads here.
- The storage, resource-region and backup notes of [rc.5](RELEASE_1.6.0-rc.5.md)
  still apply.

## Validation and downloads

The release uses the reviewed
[`zvenson/fm1-build` workflow at `v1`](https://github.com/zvenson/fm1-build/tree/v1).
Host/web tests block publication. Physical validation is pending for the CR78 and
FM kits' levels by ear, pattern-preset switching during playback and song
sections, and note-length edits while playing on hardware. USB HOST was checked
on one FM-1 with an MPC Sample on a two-chip powered hub; other hubs and devices
are untested.

Downloads include dual-core firmware, application binary, the resource file
(`melodee-1.6.0-rc.6-res.bin`), native recovery archives for Windows x64, macOS
Apple Silicon, macOS Intel and Linux x64, licence notices, `BUILD_INFO.json` and
`SHA256SUMS`.

```sh
gh attestation verify melodee-1.6.0-rc.6.fwsc --repo keremimo/melodee \
  --signer-workflow zvenson/fm1-build/.github/workflows/build.yml \
  --source-ref refs/tags/v1.6.0-rc.6
```

See [licensing](../LICENSING.md) for retained third-party notices and attribution.
