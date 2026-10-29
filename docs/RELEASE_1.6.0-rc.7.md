# Melodee 1.6.0-rc.7

Seventh release candidate for 1.6. Stable remains **1.5.1**. This candidate
fixes two bugs from earlier candidates: a song started from a later row played
a burst of earlier recorded notes, and the dim key lights shimmered. Everything
else is as in rc.6; see the [rc.6 notes](RELEASE_1.6.0-rc.6.md) for its
features and the [rc.5 notes](RELEASE_1.6.0-rc.5.md) for the baseline.

## Fixes

- **Song playback from a later row.** Starting a song from any row but the
  first fired every recorded note of the rows before it at once, heard as a
  burst of random notes. Recorded notes that ended before the starting point
  now stay silent; a note still held at the starting point plays from there.
  Starting from the first row, loop wraps and the step grid were not affected.
- **Dim key lights.** The dim LEDs shimmered from boot on, at every LIGHTS
  level. Since timed punch FX grew the song's data, the song's change check
  held the LED timer back on every UI pass; it now runs with interrupts on.

## Compatibility and limits

- **Export projects before downgrading.** As in rc.6, a project saved with a
  pattern preset uses the **FBKQ** format, which rc.5 and earlier cannot load.
  Projects from rc.6 load unchanged.
- The storage, resource-region and backup notes of [rc.5](RELEASE_1.6.0-rc.5.md)
  still apply.

## Validation and downloads

The release uses the reviewed
[`zvenson/fm1-build` workflow at `v1`](https://github.com/zvenson/fm1-build/tree/v1).
Host/web tests block publication; the song test now covers starting from a later
row. The LED fix was measured on one FM-1: late LED timer ticks fell from about
86 a second to one in 16 seconds. Song playback from a later row still needs a
listen on hardware, as do rc.6's pending checks: the CR78 and FM kits' levels by
ear, pattern-preset switching during playback and song sections, and note-length
edits while playing.

Downloads include dual-core firmware, application binary, the resource file
(`melodee-1.6.0-rc.7-res.bin`), native recovery archives for Windows x64, macOS
Apple Silicon, macOS Intel and Linux x64, licence notices, `BUILD_INFO.json` and
`SHA256SUMS`.

```sh
gh attestation verify melodee-1.6.0-rc.7.fwsc --repo keremimo/melodee \
  --signer-workflow zvenson/fm1-build/.github/workflows/build.yml \
  --source-ref refs/tags/v1.6.0-rc.7
```

See [licensing](../LICENSING.md) for retained third-party notices and attribution.
