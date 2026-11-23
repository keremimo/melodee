# Melodee 2.0.0-beta.1

Melodee 2.0 turns every synth into a module that the installer writes beside the
firmware and names by its ID instead of a number. DRUM is split into kit synths of their
own, user kits become a 16-pad SAMPLER, and the web installer converts a Melodee 1.x
FM-1's projects and sounds during the update. This beta also carries everything prepared
for 1.7.0, which was never released: packed projects, tunings apart from scales, free
take, MIDI LEARN for panel controls, MIDI THRU, two Bluetooth links and the SMK-37 family.

**This is a beta, and it changes how data is stored.** Firmware 2.0 does not read Melodee
1.x's records at all. Update an FM-1 with the web installer's **Pre-release** channel: it
saves a backup to your computer and converts your data before it writes the firmware.
Read [Upgrading](#upgrading) first.

**Hardware status:** a 1.6.1 to 2.0 update with the web installer ran on one FM-1
development unit, with projects, a preset bank, settings and a starred sound on it. The
records it wrote back are byte for byte what the converter makes on a computer. That unit
runs 2.0, with the synth modules, GLO > STORAGE, the 24-bit bus, the master knee and
rendering ahead, but none of those has been listened to critically yet. **Not tried on
hardware:** any SMK-37 on 2.0, SAMPLER's Record kit since the fix for distorted takes,
going back from 2.0 to 1.x, and everything listed as untried under
[Prepared for 1.7.0](#prepared-for-170). Please report what works and what does not, with
the device and connection you used.

## Every synth is a module

- **Synths beside the firmware:** the firmware image carries no synth engines. The
  Prophet, FM6, CZ-1, SID, 808 and SAMPLER are six module files, about 113 KB, that the
  installer writes into the flash pool after the firmware, where projects, preset banks
  and kits also live. A synth loads into RAM when a track, a song row or a pattern sound
  uses it, and leaves RAM about 3 seconds after nothing does.
- **Named by ID:** projects, presets, preset banks, backups and SysEx name a synth by
  its ID, never by a number. A synth that is not installed keeps its sounds; they play
  again once it is.
- **GLO > SYNTHS:** each synth with the RAM it needs and the tracks that use it. A synth
  that does not fit in RAM says **NO RAM FOR** and its name.
- **GLO > STORAGE:** everything the flash keeps, with **Delete** for synths, kits, banks
  of sounds and projects.
- **Synths from other authors:** a module built with the
  [Melodee Module SDK](https://github.com/keremimo/melodee-module-sdk) installs beside
  the shipped ones with `tools/fm1_modules.py`. None ships with Melodee.

## Drum kits are synths

- **DRUM is gone.** 808, 909, SID KIT, CR78 and FM KIT are synths of their own, picked
  like any other synth.
- **909, SID KIT, CR78 and FM KIT are not installed by default,** to leave the flash for your
  projects. They come as separate synth files on the Melodee hub, and
  `tools/fm1_modules.py` installs one. Sounds and projects that use them keep them, and
  play once the synth is installed.
- **SAMPLER:** 16 pads on C4 to D#5, chromatic, pad 1 on C4, for MIDI in and out, the
  pads, the knob and the keys. Other notes play the 808's GM map. Each pad's take is a
  record of its own and can run as long as the free storage allows, where a 1.x kit held
  11 drums in about 28 KB.
- **Record kit** streams a take into the flash while you play it over USB audio. The
  first device takes were distorted; the fix encodes the take in RAM and writes it once
  it ends. That fix has not been tried on hardware yet.
- **Your 1.x kits** become SAMPLER kits during the update: drum n's take on pad n, with
  its level and settings.

## Updating from Melodee 1.x

- **The web installer converts.** On an FM-1 with 1.x records, it reads them, saves
  `Melodee-1.x-backup-<date>.json` to your computer, converts them, writes the firmware
  and the synths, then writes the converted records back.
- **If something goes wrong:** a converter that fails stops the update before the
  firmware is written. If writing the converted records back fails, 2.0 still runs, and
  the installer saves `Melodee-2.0.0-beta.1-converted-<date>.json` for the editor's
  Restore.
- **The 2.0 editor** opens 1.x backups and 1.x `.mel` project files, converting them
  first. It is in this repository's `web/` folder and not yet on the site.
- **`tools/fm1_install.py`** writes the firmware, the resources and the synths, but
  does not convert.

## Sound and timing

- **24-bit bus:** levels stay 24-bit from each track to the DAC and to USB audio, where
  they used to be cut back to 16 bits. USB PCM24 is exact both ways; 16-bit capture is
  dithered.
- **Master knee:** the soft clip after the limiter put every sample through a full tanh,
  which added intermodulation (-26 dB on a four-note chord). It is now linear up to
  -2.5 dBFS and bends to a ceiling of -1.2 dBFS (-52 dB on that chord). Peaks held by the
  limiter come out about 1.6 dB louder.
- **Rendering ahead:** audio is rendered one buffer half ahead, so each half has twice
  the time to render. That adds 2.9 ms of latency (about 8.7 ms from MIDI in to audio
  out), and MIDI and clock out now lead the audio by that half.
- **FM-1 scan at 5 kHz:** the keys, knobs and buttons are read 5,000 times a second
  instead of 10,000, which frees CPU time. A key press reaches the engines 3.5 ms after
  contact on average, up from 1.75 ms. The SMK-37 stays at 10 kHz for velocity.

## Fixes since 1.7.0 was prepared

- **Song:** a held track-1 block lasts one loop of its pattern per section (4 bars over
  2 sections is 8 bars, not 5).
- **Synth crashes** are blamed on the synth whose code faulted, and a synth's own fault
  no longer keeps the second core off until power-off.
- **Backups:** a settings backup made with a voice budget set restores again, and an
  empty preset bank's restore deletes its record instead of taking a storage sector.
- **Storage:** the SLOTS card counts free room again after a kit, synth or preset bank
  comes or goes.

## Prepared for 1.7.0

These were built for 1.7.0 and ship for the first time here.

- **Packed projects:** a saved project is stored packed, so a typical one takes one flash
  sector where it took two or three. Each sector holds 4,056 bytes instead of 3,808, and
  the FM-1's pool grows from 79 to 86 sectors. There are still 32 project slots.
- **Project file:** the web editor's Projects tab saves a project, with the kits it plays,
  as one `.mel` file and opens it on the same unit or another one.
- **Tunings apart from scales:** SCL's list has a Tuning row under Scale, with equal, just,
  historical and maqam tunings, for any 12-tone scale and every QNT mode. Picking Rast,
  Bayati, Saba, Husayni or Sikah on SCALES at QNT OFF brings its tuning; Hijaz is equal.
  A project on one of the 16 entries that moved keeps its stored scale.
- **Microtonal:** fixed chord shapes (MAJ to POW) aim at just ratios on every microtonal
  entry, so a fixed chord on an old microtonal project can land on different notes. Notes
  read by the nearest semitone and the cents off it, such as E4-14. MIDI OUT sends the
  nearest semitone instead of a degree address.
- **Free take:** stopped, on an empty pattern, play a phrase, then press and hold **REC**
  on the next 1. The pattern plays it from the top and the screen says what it made, such
  as **CAPTURED 2 BARS 120 BPM**. If nothing else has notes, the tempo follows the loop.
  Hold **EDIT** to undo.
- **MIDI LEARN:** another controller's knobs and buttons stand for the panel's own
  controls on every page, and stay learned after power-off. Binding a CC to one page's
  parameter is gone. **FORGET LEARNED** clears them.
- **MIDI THRU:** a HOME menu row, OFF by default. What comes in also goes out the other
  ports. Channel messages pass; clock and transport only from the input CLK follows; SysEx
  does not.
- **Two Bluetooth links at once:** a computer, iPad or phone connected to Melodee and a
  Bluetooth keyboard Melodee connected out to. The header says **COMPUTER + DEVICE** with
  both up.
- **SMK-37 family (beta):** packages for the SMK-37 Pro, SMK-37 Elite and MKE-P37, with
  pad modes, knob and fader banks, velocity, sustain pedal and TRS MIDI out. Thanks to
  genkigenki for the port (PR #63). See [SMK37.md](../SMK37.md).
- **PLAY light** green while playing, red only while recording. The update loader shows
  **UPDATING** with a progress bar.
- **Capture:** undoing a Capture takes all its notes away (#113), and a Capture keeps what
  a step already played.
- **Bluetooth:** a device the FM-1 once paired with on CONNECT OUT can connect back on
  VISIBLE.
- **USB HOST behind hubs (PR #115, thanks to recursivefieldinstruments):** more hubs are
  recognised, and the screen says why no MIDI device was found.
- **M-VAVE's updater:** the firmware answers the plain handshake with at most three digits
  after the underscore, which M-UPGRADE 3.11 needs, and gives its whole identity only to
  Melodee's installers.
- **Recovery:** `tools/fm1_recover.py` accepts the UC25HQ80 flash (JEDEC 0xB36014), tried
  read-only on #110. `--write` has not been tried on this chip.

Covered by automated tests only: two Bluetooth links (and the reorganised single link),
free take, MIDI LEARN, MIDI THRU, tunings, microtonal MIDI OUT, the USB hub search and the
Capture and Bluetooth fixes.

## Upgrading

- **FM-1:** use the [web installer](https://keremimo.github.io/melodee/) in Chrome or
  Edge, open **Advanced installs**, choose **Pre-release** and install. Keep the
  `Melodee-1.x-backup-<date>.json` it saves.
- **SMK-37 family:** nothing published converts an SMK-37's 1.x data yet. The web
  installer does not offer these packages, `tools/fm1_install.py` does not convert, and
  the 2.0 editor is not on the site. Installed with `tools/fm1_install.py`, 2.0 leaves an
  SMK-37's 1.x projects and sounds unread, so back them up with the 1.x editor first. Not
  tried on any SMK-37.
- **The web editor on the site is still 1.6.1's.** It does not know 2.0's synth IDs, so
  do not use it with a 2.0 FM-1.
- **Going back is not tried.** Melodee 1.x cannot read 2.0's records and may write over
  them. To go back, install 1.6.1, then restore the 1.x backup the update saved with
  1.6.1's editor.
- **From 1.5.1 or earlier:** read Upgrading in the
  [1.6.0 release notes](RELEASE_1.6.0.md) first.
- **MIDI LEARN users:** learn your controls again as panel controls.

## Validation and downloads

The release uses the reviewed
[`zvenson/fm1-build` workflow at `v1`](https://github.com/zvenson/fm1-build/tree/v1).
Host and web tests block publication. Firmware and application attestations, source tag
and commit, matching application bytes, all four native recovery archives and checksums
must pass before the draft is published.

Downloads include the dual-core firmware, its application binary, its synths
(`melodee-2.0.0-beta.1-mods.bin`), the resource file (`melodee-2.0.0-beta.1-res.bin`),
the 1.x converter the installer uses (`melodee-2.0.0-beta.1-convert.bin`), native
recovery archives for Windows x64, macOS Apple Silicon, macOS Intel and Linux x64,
licence notices, `BUILD_INFO.json` and `SHA256SUMS`. The SMK-37 packages, each with its
application binary and synths, are `melodee-smk37-2.0.0-beta.1.fwsc` (Pro),
`melodee-smk37-elite-2.0.0-beta.1.fwsc` (Elite) and
`melodee-smk37-mke-2.0.0-beta.1.fwsc` (MKE-P37).

```sh
gh attestation verify melodee-2.0.0-beta.1.fwsc --repo keremimo/melodee \
  --signer-workflow zvenson/fm1-build/.github/workflows/build.yml \
  --source-ref refs/tags/v2.0.0-beta.1
```

See [licensing](../LICENSING.md) for retained third-party notices and attribution.
