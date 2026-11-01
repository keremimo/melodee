# Melodee 1.6.0-rc.8

Eighth release candidate for 1.6. Stable remains **1.5.1**. This candidate
puts the factory sounds into the firmware package itself, so an FM-1 updated
through Firmware Hub, or any other tool that only sends the package, gets its
presets. Everything else is as in rc.7; see the [rc.7 notes](RELEASE_1.6.0-rc.7.md)
for its fixes, the [rc.6 notes](RELEASE_1.6.0-rc.6.md) for its features and the
[rc.5 notes](RELEASE_1.6.0-rc.5.md) for the baseline.

## Fixes

- **No presets after a package-only update.** Since rc.5 the factory sounds,
  the Rubik font and the Bluetooth radio program live in a resource region that
  only the web installer's Finish setup step wrote. Firmware Hub installs the
  `.fwsc` alone, so those FM-1s started without factory presets. The package now
  carries the resource file (`res.bin`) and Melodee's update loader writes it
  after the application, from any host. It skips the write when the FM-1
  already holds this build's copy, and a failed write never fails the install.
- **Setup unfinished.** If the factory sounds are still missing at startup, the
  FM-1 now says so: "Setup unfinished", "Factory sounds missing", "Install the
  update again". CONTINUE closes it; your own sounds stay. Finish setup in the
  web installer still works and closes the screen as well.

## Compatibility and limits

- **From 1.5.1 or earlier through Firmware Hub, install twice.** Older firmware
  kept preset banks where the resource region now lives. The first install
  leaves them alone; the new firmware moves them into the shared pool at
  startup and shows Setup unfinished. The second install writes the factory
  sounds. The web installer still needs one install.
- **Export projects before downgrading.** As in rc.6, a project saved with a
  pattern preset uses the **FBKQ** format, which rc.5 and earlier cannot load.
- The storage, resource-region and backup notes of [rc.5](RELEASE_1.6.0-rc.5.md)
  still apply.

## Validation and downloads

The release uses the reviewed
[`zvenson/fm1-build` workflow at `v1`](https://github.com/zvenson/fm1-build/tree/v1).
Host/web tests block publication; the loader tests now cover the resource
write, including power cuts during it, and a new startup test covers the
moved-banks case. A Firmware Hub install on hardware has not been tested yet,
nor has the two-install path from 1.5.1. rc.7's pending listens still apply:
song playback from a later row, the CR78 and FM kits' levels by ear,
pattern-preset switching during playback and song sections, and note-length
edits while playing.

Downloads include dual-core firmware, application binary, the resource file
(`melodee-1.6.0-rc.8-res.bin`), native recovery archives for Windows x64, macOS
Apple Silicon, macOS Intel and Linux x64, licence notices, `BUILD_INFO.json` and
`SHA256SUMS`.

```sh
gh attestation verify melodee-1.6.0-rc.8.fwsc --repo keremimo/melodee \
  --signer-workflow zvenson/fm1-build/.github/workflows/build.yml \
  --source-ref refs/tags/v1.6.0-rc.8
```

See [licensing](../LICENSING.md) for retained third-party notices and attribution.
