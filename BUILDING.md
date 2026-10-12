# Building Melodee

The build makes three files in `build/`:

| File | What |
| --- | --- |
| `melodee.bin` | the firmware app |
| `loader/ota.bin` | the update loader |
| `melodee.fwsc` | the installable package (app + loader) |

## Prerequisites (macOS)

- Python 3 with Pillow and fontTools: `pip3 install Pillow fonttools` (the UI font and icons are
  rasterised at build time). Pillow's Raqm layout needs FriBidi: `brew install fribidi` (build.sh and
  tests/run_tests.sh point `DYLD_FALLBACK_LIBRARY_PATH` at Homebrew's lib)
- Docker Desktop. The JieLi toolchain is Linux x86-64 only; the build runs each tool in a
  `linux/amd64` `debian:bookworm-slim` container (Rosetta on Apple silicon). Keep the source
  tree in a folder Docker can share, e.g. under `/Users`.
- The JieLi Linux toolchain (clang 4.0.1 for pi32v2, from JieLi's package server):

  ```
  tools/get_toolchain.sh            # installs to ~/.jieli/toolchain
  ```

- The JieLi AC79 SDK (Apache-2.0). The package uses three of its files
  (`cpu/wl82/tools/uboot.boot`, `cfg_tool.bin`, `cfg/eq_cfg_hw.bin`); they are not part of this tree.

  ```
  git clone --depth 1 --branch AC79NN_SDK_V1.2.1_2023-12-13 \
      https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK.git ~/fw-AC79_AIoT_SDK
  ```

- Node.js (optional, for the web tests).

On Linux x86-64 the toolchain runs natively and Docker is not needed.

## Build

```
./build.sh
```

`MELODEE_RAM_AUDIO=1 ./build.sh` builds the resident audio performance profile.
All builds retain a bit-identical dry bypass and support the bounded per-track FX inserts described in README.md.
The optimized audio ISR, all enabled engine callbacks and their reachable helpers
execute from ordinary SRAM; `tools/audio_ram.py` preserves existing low-SRAM DSP
sections and uses volatile indirect calls across the XIP/SRAM boundary. Startup
copies the additional `.audio_text` section before audio or CPU1 starts. Build
checks verify every linked audio function's address and reject direct XIP calls
from that graph. Engine callbacks invoked through function pointers are explicit
roots, including mono-note handling and key mapping; a new engine callback must
be classified before the build passes. The hardware PC guard includes both SRAM
code banks, using the linker-derived `_guard_ram_end`.
`MELODEE_SIZE=0` cannot be combined with this profile.

The profile retains at least a 64 KiB contiguous resource arena for four engine
states and off-record note history. Additional SRAM tails remain available.
Factory banks and cold assets remain in flash; loaded patches and active engine
state are in SRAM. Old effect values remain readable as inert format padding; the new FX use separate parameter IDs.
Run the host suite with `MELODEE_RAM_AUDIO=1` when checking this target variant.
Both target profiles reject the retired effect processors. Host sound regressions
validate dry output against the original engines with all effect sends off.
`tools/usb_audio_stats.py --parts` identifies cuts on each of the four tracks.
The resident profile raises the shared voice budget from sixteen to eighteen
units while CPU1 is online. An offline replay of the captured Prophet / two-FM6 /
808 song needs this increase to avoid allocation cuts; the eighteen-unit replay
recorded no allocation cuts for three minutes without growing memory or recorded event data.
Normal builds and a retired or unavailable CPU1 retain sixteen units. Per-engine
caps and overload shedding remain active. Target CPU headroom for the higher
budget still requires a device measurement; host playback cannot establish it.

`JIELI_TOOLCHAIN` and `AC79_SDK` override the default locations
(`~/.jieli/toolchain`, `~/fw-AC79_AIoT_SDK`).

`./build.sh --release 0.11.1` makes a release build: the package identity becomes `FM-1_90111`
(`FM-1_9` and the version's digits; X.Y without a patch number: 0.11 is `FM-1_9011`) and the version
string `v0.11.1`; the package is `build/melodee-0.11.1.fwsc`, and `build/release-0.11.1/` holds what a
release ships: the package, the app (`melodee-0.11.1-app.bin`), `SHA256SUMS`, `LICENSE`, `LICENSING.md` and
`LICENSES/` (the package contains Apache-2.0 SDK files, so the licence texts travel with it).
For example, `--release 1.0.0` uses the distinct identity `FM-1_91000`.

Build options (environment; most use `0` or `1`; defaults in `firmware/src/build_options.h`, `melodee.c`, `core.h` and `icons.c`):

| Flag | Default | |
| --- | --- | --- |
| `MELODEE_FLASH` | 1 | settings, presets and projects in flash |
| `MELODEE_OTA` | 1 | update entry (needs `MELODEE_FLASH`) |
| `MELODEE_USB_AUDIO` | 1 | USB audio: "Melodee Out" plays the computer through the FM-1, "Melodee In" records the four tracks (16 / 24 bit, 44.1 kHz) |
| `MELODEE_CDC` | 0 (1 without USB audio) | USB serial console; it shares EP2 / EP3 with USB audio, so `MELODEE_USB_AUDIO=0` |
| `MELODEE_UART` | 1 | TRS MIDI IN |
| `MELODEE_DUAL_CORE` | 1 | paired FM6/Prophet rendering on CPU1; `0` builds serial-only firmware |
| `MELODEE_SLICE` | 1 | the SLICE engine |
| `MELODEE_ICONS` | 1 | parameter icons on the knob cards |

## Samples

The CC0 instrument samples that the SAMPLE engine uses are in `assets/samples-cc0/`
(Versilian Studios, see `ATTRIBUTION.txt` there). `tools/fetch_cc0.py` downloads them
again from the source repositories. Without that folder the build still works and the
SAMPLE engine has only the generated drum kit.

## Tests

Audio working memory is allocated on demand from the unused part of POOL and
linker-derived gaps after RAM code, ordinary data and retained state. Allocations
stay within a single bank and preserve boot records and stack guards. Each
track reserves only its current engine state. The off-record note history uses
one bounded allocation; no effect processor allocates audio buffers.

Preset browsing keeps names and slot indexes in RAM, with one bank cache shared by
the general presets and the native FM6, CZ-1 and Prophet banks (the last bank asked for). Melodee 2.0
reads only the formats written since 1.0; 1.2 rewrote older records at boot.

The build reports both permanent POOL usage and the available working arena,
and requires at least 139 KiB for simultaneous maximum engine state, recording
and note history in normal builds (64 KiB in the resident profile). The resource
tests cover eviction, engine transitions, and allocation failure.

USB stem buffers are written only for an active capture stream. Derived tuning
and mixer gains are cached by their effective parameter values, including
modulation. Silent tracks skip unused buffer writes and pitch preparation while
their LFO, bend, engine control state and switch fades keep advancing. The UI
has a second 15 KiB canvas for small regions, so drawing can overlap an existing
LCD transfer; large graphs wait before reusing the full canvas. Transition and
DMA tests check stream resumption, cache invalidation and source-buffer reuse.

```
tests/run_tests.sh
```

Runs the host tests and, with Node.js, the web page tests. Run it after `./build.sh`
(it uses `build/` and needs `AC79_SDK` set as for the build). The suites cover flash storage,
user presets, projects of every format, backup, the keys and knobs, MIDI (USB, TRS, clock,
control), USB audio, the update entry and loader, the command-line installer, the UI (the real
drawing code against stubs: every screen in every palette is rendered and checked for clipped or
overlapping text; PNGs land in `build/ui_new/`), every engine (DRUM, NOISE, PHYS, FM6, SLICE, the
DIGITAL conversion), the chord keys, the modulation matrix, retired effect compatibility
and swing. With `DAISYSP` pointing at a DaisySP checkout, the PHYS models are also compared with
their floating-point originals; without it that test is skipped.

The regression suite (`tests/regress.c`) renders every engine and preset and compares a
hash of each render with `tests/golden.txt`; it also checks levels, voices and the CPU
cost (`tests/cpu_baseline.txt`, `tests/target_budget.txt`). After an intended change of
the sound, `GOLDEN_UPDATE=1 sh tests/run_tests.sh` rewrites the hashes; `BUDGET_UPDATE=1`
does the same for the cost files.

The tests run in parallel, one job per test, as many at once as the CPU has cores
(`PARALLEL=n` sets the count, `PARALLEL=1` runs them one by one); each job's output is printed
in order as it ends and the failures are listed again at the end. `SOUND=0` leaves out the
sound tests (the regression suite, TRACKS and the tests of every engine's and effect's rendered
signal), `SOUND=only` runs them alone.

## Install

Use the web installer in Chrome or Edge:
<https://keremimo.github.io/melodee/webapp/installer/>. It installs the released package.

From the command line (needs `pip3 install mido python-rtmidi`):

```
python3 tools/fm1_install.py build/melodee.fwsc
python3 tools/fm1_install.py --info          # identity of the connected FM-1
```

Or, to install your own build from the web installer, make a local copy of the site and open it from `localhost`
(Web MIDI needs a secure context). See [Local site](#local-site) below.

## Local site

`web/make_site.py` assembles GitHub Pages layout under an output directory:

- `webapp/installer/` — self-contained installer
- `webapp/editor/` — patch editor
- `webapp/manual/` — versioned manuals from `web/manual/` (`index.html` loads the latest `v*/`; each version folder holds its HTML and screenshots)
- `firmware/` — both `.fwsc` packages, LICENSE texts

The manual’s screenshots are committed under each `web/manual/vX.Y.Z/` (NIGHT palette only).
`build/` is gitignored, so regenerating them is a separate step when the UI changes.

```
# 0) Firmware (and host-test assets under build/gen). Needed before UI renders or release packages.
MELODEE_RAM_AUDIO=1 ./build.sh --release 1.3.0
MELODEE_RAM_AUDIO=1 MELODEE_DUAL_CORE=0 ./build.sh --release 1.3.0

# 1) Host UI renders → build/ui_new/<PALETTE>/*.png (and side folders). Needs Pillow.
#    Supported path: the host test suite (ui_renders is one of its jobs).
tests/run_tests.sh
#    SOUND=0 tests/run_tests.sh   # skip the long sound tests if you only need UI/screenshots

# 2) Copy the allowlisted NIGHT frames into the latest versioned manual folder.
python3 tools/manual_shots.py            # build/ui_new/NIGHT → web/manual/v*/ (latest)
# python3 tools/manual_shots.py --version v1.1.0
# python3 tools/manual_shots.py --check  # report missing/stale without writing

# 3) Assemble Pages layout and serve it (Web MIDI needs localhost / HTTPS).
python3 web/make_site.py build/release-1.3.0/melodee-1.3.0.fwsc \
  build/release-1.3.0-1core/melodee-1.3.0-1core.fwsc 1.3.0 /tmp/melodee-site
cd /tmp/melodee-site && python3 -m http.server 8000
# open http://localhost:8000/webapp/installer/
#      http://localhost:8000/webapp/manual/
#      http://localhost:8000/webapp/editor/
```

Steps 1–2 are only required when the UI (or the allowlist in `tools/manual_shots.py`) changed and you need to refresh a version’s screenshots. For a routine site assemble from already-committed screenshots, start at step 3. `make_site.py` still requires at least one `web/manual/vX.Y.Z/` with `index.html` and `*.png` — it copies each version folder into `webapp/manual/`.

Installing firmware is at your own risk. If an install fails and the FM-1 no longer
starts, identify its recovery mode using the steps below.

### Recover a black-screen FM-1

If the device appears as **Melodee Update**, it is in the MIDI update loader:
`python3 tools/fm1_install.py /path/to/working-firmware.fwsc --yes` resumes installation.

If it appears as USB **4c4a:8057**, with inquiry identity **WL82 / UBOOT1.00**, it is
already in mask-ROM UBOOT. `tools/fm1_recover.py` can recover it over a normal USB data
cable, without an RP2040. It does not force a running device into UBOOT.

The [1.3.1 release downloads](https://github.com/keremimo/melodee/releases/tag/v1.3.1)
include standalone recovery archives for Windows x64, Linux x64, macOS Intel/x64
and Apple Silicon/arm64. Extract the matching archive and follow its `RECOVERY.md`,
also available [here](docs/RECOVERY.md). Python, libusb and the RAM helper are bundled.
Windows still needs the ROM device's WinUSB driver; macOS/Linux may need USB permissions.

To run the Python source instead, install Python 3.10 or later, PyUSB
(`python3 -m pip install pyusb`) and libusb:

- macOS: `brew install libusb`. If USB access is denied, run the recovery command
  with `sudo` and the same Python interpreter that has PyUSB installed.
- Linux: install your distribution's libusb 1.0 package (for example,
  `sudo apt install libusb-1.0-0`). USB access may need a udev rule or `sudo`.
- Windows: install a libusb-compatible driver such as WinUSB for **4c4a:8057 only**
  using Zadig, and supply `--libusb C:\path\to\libusb-1.0.dll` if discovery fails.
  Do not replace the driver for the normal Melodee MIDI/audio device.

The tool uses the unmodified `cpu/wl82/tools/wl82loader.bin` from the
[JieLi AC79 SDK](https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK/tree/AC79NN_SDK_V1.2.1_2023-12-13/cpu/wl82/tools).
It is included under `assets/recovery/` and loaded into RAM. The supported file is
30,720 bytes with SHA-256
`d9de9f1c81a3bb52998e5bced678f0ca851223014a7d43965b03f0719a44eb55`.
The tool uses the included file by default. `--loader` or `AC79_SDK` can override it.

Choose a firmware package whose boot has been verified. For investigating 1.3 startup
failures, use a previously working release rather than reinstalling the suspected build.
Download the `.fwsc` and its `SHA256SUMS` from the corresponding release.

```sh
# Identify ROM UBOOT; no loader or package needed.
python3 tools/fm1_recover.py --info

# Validate the package and RAM loader offline; no USB access.
python3 tools/fm1_recover.py working.fwsc --check

# Default: load the RAM helper and save a full backup, with NO flash writes.
python3 tools/fm1_recover.py working.fwsc

# Back up again, restore the application, and verify the entire flash.
python3 tools/fm1_recover.py working.fwsc --write
```

Each connected recovery creates a unique session directory under `fm1-recovery-backups/`
(`--backup-dir PATH` changes the parent). Keep `full-flash-before.bin`, `backup.json` and
`retained-ram.bin`. The full backup and its manifest are flushed to disk before any erase.
The tool checks the package's checksums, chip key, ROM identity and expected flash chip;
writes only changed 4 KiB sectors in **0x4000–0x93000**; and verifies each sector and then
the entire flash. The bootloader below 0x4000 and saved data above 0x93000 are retained.
Legacy Prophet banks at 0x89000 are ignored and replaced by application code. They remain
available in the full pre-recovery backup, but are not moved or loaded by current firmware.

Success creates `recovery-result.json` and prints **RESTORE VERIFIED**. Power-cycle the
FM-1 afterward, including after a backup-only session. Verification proves the flash
contents, not that the selected firmware boots or its audio and controls work.
If USB disconnects during writing, keep the original backup, reconnect the device in
UBOOT, and retry; matching sectors are skipped. No success report is produced for a
failed write or verification. Connect one FM-1 at a time, or select it with `--bus` and
`--address`. `--wait` controls detection time, up to 60 seconds.

The protocol and previous direct-USB recoveries were verified on macOS. The portable
tool has simulated USB/NOR tests, including interrupted writes; its Linux and Windows
hardware paths still need validation. If ROM USB never appears, use
[FM-1-transporter](https://github.com/kurogedelic/FM-1-transporter) and its RP2040 setup.

## Experimental cache RAM

Builds reclaim seven 4 KiB data-cache ways at `0x01F28000..0x01F2F000`
(`MELODEE_CACHE_RAM=1`, opt-in; normal builds retain all data-cache ways; `MELODEE_CACHE_RAM=0 ./build.sh`
turns it off), following the AC79 SDK's flash-execution configuration. All instruction-cache ways remain available.
The first 14 KiB are the UI's (eight undo levels); the rest join the audio allocator, whose worst case still fits
the SRAM banks alone. Normal builds keep eight undo levels in ordinary SRAM, within the existing arena reserve. Keep USB audio enabled during
validation.

The boot routine runs wholly in SRAM with interrupts off and CPU1 held. It
refuses external-memory configurations, verifies the cache registers, tests the
entire bank with address patterns and complements, and only then exposes it to
the allocator. Failed tests restore the inherited configuration; a boot retry
skips cache reconfiguration. Code and stacks remain in ordinary SRAM/XIP.

`tools/usb_audio_stats.py --port Felucca --memory --window` reports cache status
(0 disabled, 1 ready, 2 boot retry, 3 external memory, 4 timeout, 5 register
failure, 6 memory failure), before/after register values, test duration,
resource capacity/usage/failures and bytes allocated in cache RAM. Existing
diagnostic requests retain their schemas. Host tests cover startup refusal,
faulty/aliased banks and rollback, but cannot prove physical cache behavior.
Verify boot, sustained audio/MIDI/USB activity, persistence and update entry
before enabling this option by default.

## Second-core FM6/Prophet rendering

The first experimental hardware install bootlooped with `DBG_MSG=0x400`
(`c1_pc_limit_err_r`) during CPU1's ready handshake. This identifies a CPU1
instruction-fetch range violation. CPU0's captured execution trace was in the
startup handshake, before normal audio rendering. The likely trigger was
enabling application PC-range guards before releasing CPU1: CPU1 first executes
mask-ROM bootstrap code, outside the permitted application RAM/XIP ranges,
before jumping to the entry mailbox. The saved PC fields were unreliable, so
the exact faulting CPU1 instruction was not established. The fault record and
successful corrected starts support this startup-order diagnosis.

The fault handler reset the chip, which retried the same startup sequence.
The boot guard counts restarts that occur before 30 seconds of successful
operation and enters ROM recovery after two failed boots. This explains why
the application MIDI port disappeared and ROM USB recovery became necessary.
It was recovered to the serial build over ROM USB, with a full-flash verification
confirming unchanged
bootloader and saved-data regions. The startup fix defers application PC-range
guards until CPU1 has completed its ROM bootstrap (or has been stopped after a
handshake timeout). Stack, write and bus guards retain their early activation.
The corrected build was flashed successfully on 2026-10-08. CPU1 came online
and completed 5,318 audio jobs with no worker timeouts or late audio blocks;
the diagnostic samples observed up to three active voices. Eight-voice hardware
load, worst-case patches, musical operation and speedup against the serial build
remain unverified. The first image is a known failed build and must not be
installed again.

The failed package has SHA-256
`e0cfb18c63ff1fdfd4c0ed9af49bb02ef191ec84413d3dfca3424482eb1b9d24`.
Its failure occurred during startup; it does not establish an eight-voice
rendering overload. A later reflash of the corrected package also booted with
CPU1 online and no worker timeouts in the idle diagnostic checks.

The successfully installed package has SHA-256
`2c22bdbeec420f2fee90d747a928277bd0f7c096ebe43b1ffcec92fb575a1318`.
Host suite, golden renders, concurrent PCM/state comparison, address/thread
sanitizers and both serial/dual-core builds passed. The startup correction also
passed the target budget check; the serial package remained byte-identical.

`./build.sh` enables a bare-metal CPU1 worker by default. Use
`MELODEE_DUAL_CORE=0 ./build.sh` for serial-only firmware. Application and
assembly startup share this default in `firmware/src/build_options.h`.
There is no async runtime or RTOS dependency. Sustained musical load and
battery impact still need device measurements.

CPU0 prepares a voice's controls and envelopes. CPU1 calculates its planned FM6
operators or Prophet oscillator/filter samples into private buffers while CPU0
prepares and calculates the next voice. CPU0 joins and adds both in their original
order before the track's post processing. Part-wide Prophet wheel buffers and
patch bytes remain read-only during a job; envelopes, allocation and shared RNG
stay on CPU0. Prophet's per-voice sample noise state travels with its voice.
An isolated voice stays on CPU0. Other engines, effects, MIDI, USB and the screen
retain their existing execution paths. This is a targeted synth optimization;
it does not double all firmware throughput.

The FM6, Prophet and synthesized 808 sample loops stay resident in internal
DSP RAM, shared by all four tracks; two FM6 tracks share code while retaining
separate voice state and loaded patches. Startup copies the code and saturation
tables once. Loaded patches and active engine state also live in RAM.
FM6 output mixing and its DC filter, ordinary track mixing and the master output
also run from DSP RAM.
FM6 per-voice control and envelopes are resident too. Other block-rate helpers
and factory-library browsing may still use flash. The build
checks kernel/table placement and retains the existing worst-case audio arena
reserve; neither voices nor recording capacity are reduced to make room.

With an online CPU1, Prophet's polyphony cap is eight and each voice
costs two of the existing sixteen shared budget units. Eight Prophet voices fill
that budget; notes on other tracks still steal voices. CPU1 startup failure keeps
the five-voice cap and three-unit charge. Native unison retains the patch's count
(normally five), with explicit counts up to eight allowed in the online build.
This budget discount is provisional: measure mixed tracks, especially odd voice
counts where each track leaves one unpaired voice on CPU0. Existing overload
shedding remains active. Serial builds retain their original five-voice state.

The startup sequence follows `EnableOtherCpu` in the tested AC79 SDK's
`system.a/port.c.o` and `cpu.a/startup.S.o`: entry mailbox `0x01C7FFF8`, temporary
clock-divider bit 3, and `C1_CON` release/hold bits 3/1. CPU1 has separate 4 KiB
user and supervisor stacks and no enabled interrupts. It polls an internal SRAM
mailbox while idle; the effect on battery consumption still needs measurement. Startup has a 10 ms
handshake; a failure retains serial rendering.
The worker's idle loop is in internal RAM and publishes completion only after
returning from XIP. Every audio block joins its jobs, so main-loop flash writes
cannot overlap an XIP worker callback. Update and reset paths stop CPU1.

### Units whose CPU1 misreads SRAM

1.0.0 crashed on some units (keremimo/melodee#17, #18). On those units, with
CPU1 running, shared SRAM now and then reads wrong (Jangada measured
`0x00200000` where RAM holds 0, more often under audio, screen and USB load; the
same image with CPU1 never started is stable). Two crash reports show both
sides of it:

- `pc 00000002` on a garbled red screen, idle: one misread of the mailbox at
  rest made the worker call `fn`, null until the first paired job. CPU1 fetched
  from 0, took the exception itself and drew the crash screen while CPU0 went on
  drawing the UI; its `fm1_reboot` then held CPU1 before the reset.
- `00000001 03801494 00000000 00001000 0201257A` (vec, pc, emu, dbg, rets):
  DBG bit 12 is `c0_pc_limit_err_r`, CPU0's. In 1.0.0, `0x0201257A` follows
  `call master_out` in `fm1_alnk0_irq`; `master_out` returns with
  `{pc, r11-r4} = [sp++]`, so the return address popped from CPU0's stack read
  `0x03801494` (it looks like a packed stereo sample) while `rets` still held the
  right one. No software check can catch that.

What the FM-1 runs, measured (editor command 79 in a test build, below): the AC79
at 360 MHz (M-VAVE's `isd_config` SYS_CLK; our package carries no clock key, and
the SDK SPL runs it as fast: 329 dependent adds a microsecond), above the SDK's
320 MHz table, on the SPL's core
rail: SYSVDD 11 (1.26 V), VDC14 3 (1.40 V) on its LDO. The stock firmware is
single-core: its system library says `modified #define CPU_CORE_NUM 1` (the SDK
default is 2), so no FM-1 was validated with both cores. JieLi's own profile
above 320 MHz (`isd_config_rule.c`, `CONFIG_OVERCLOCKING_ENABLE`, 396 MHz) sets
DVDD 14 (1.35 V) and DCDC14 4 (1.45 V), and its notes say to raise them on chips
that run low. One healthy unit ran both cores under an eight-voice Prophet chord
without a misread down to SYSVDD 6 (1.11 V): the margin varies from chip to chip.
Melodee does not change a supply rail: what an FM-1 board and its regulators take
at other levels is unknown, so the rails stay where the boot loader sets them.

### OVERCLOCK: the boot clock

The HOME-held menu's OVERCLOCK (`overclock.c`, `main.c` `oc_boot`) picks the SYS
clock at boot, before CPU1, the audio ISR and the LCD start. The retune is PR #27's
by [johnskiller](https://github.com/johnskiller) (`hal/fm1_clock.h`): PLL2_CON1's
multiplier on the SPL's post-divider chain, 2 MHz a step and 2/3 of the VCO.

| Setting | Multiplier | SYS / HSB / LSB / SFC (MHz) |
| --- | --- | --- |
| 320 MHz (default) | 240 | 320 / 160 / 53.3 / 80 |
| 360 MHz | 270 (the SPL's) | 360 / 180 / 60 / 90 |

The rails stay at the boot levels. A 396 MHz test (multiplier 297, JieLi's
overclock rails) crashed on a healthy unit and is not offered; the switch still
recognises 297 at boot and moves away from it. TIMER4 (the 24 MHz crystal), the
audio I2S (its own ALNK PLL) and USB/UART (PLL48M) do not move; the LCD SPI
follows LSB (10.7 / 12 MHz). The switch accepts any of those multipliers at boot
(a soft reset can keep the last one) and leaves an unfamiliar one untouched; it and its rollback each wait on RC16M for at least
20 ms plus the cache handshake (not a PLL lock bit), bounded by 40 ms and a spin
limit. A settled rollback or an unfamiliar clock raises the undo flag in editor
command 79 (v[5]); a failed rollback waits in SRAM for the watchdog.

The setting lives in `favorites.factory[15][30]` bits 2..3, beside KNOB ACCEL, so
older firmware keeps it. A new clock applies at the next start; closing the menu
with one asks to restart (the settings are saved first, the transport stopped).
`oc_guard` (`.noinit`, gone at power-off) marks a session above 320 MHz as live
until an intended reset (the restart, UBOOT, an update): a boot that finds it set
(a crash, the watchdog, a switch that never returned) comes back at 320 MHz and
saves that. A CPU1 retirement in such a session sets 320 MHz for the next start.
With CPU1 the shared voice budget follows the clock (16 or 18 units in the
resident profile); one core keeps 16.

So the second core stays on, and a unit that shows the fault falls back to one:

- The worker runs a request only when it is exactly the next one and its check
  word (`fn`, `context` and the request number, written by CPU0 before the
  request) matches; the count of finished requests stays in a CPU1 register.
  Anything else is counted in `rejected` and never run.
- A misread, a CPU1 fault or a job that does not finish in 10 ms retires the
  worker for the session: CPU1 is held, the Prophet cap returns to five voices
  and every voice renders on CPU0. A misread retires it at the next half-buffer
  render. A CPU1 fault: `fm1_core1_fault_c` records it (`fm1_core1_fault`). If
  CPU1 took the exception, it holds itself; if the debug unit raised it on CPU0
  (`DBG_MSG` CPU1 bits only), CPU1 is held and CPU0 resumes through
  `fm1_fatal_common`'s ISR-style return. A waiting join drops that voice's
  block (a partly advanced voice is never rerun). Before, a stall reset the
  device.
- A retirement, or a CPU0 crash with CPU1 working (its data can go bad too: the
  `master_out` report, which no check can catch), keeps CPU1 out of every boot
  until power-off. The record (`fm1_core1_bar`) is in `.noinit`, never in flash:
  it survives soft resets (a crash, the watchdog, an update) and is gone at
  power-off. A boot after a crash in the first 30 s runs on one core too, so
  such a unit does not crash twice into UBOOT.

`core1_rejected` and `core1_faults` (AUDIO_STATS fields 22 and 23) stay 0 on a
healthy unit. `MELODEE_CORE1_TEST=1 ./build.sh` adds editor command 79
(`tools/core1_fault_test.py`): it injects each failure (misread, null job,
stall; each must end with audio running and CPU1 barred across a soft reset),
crashes CPU0 beside a working CPU1 (`crash`), reports the rails (read only), the
clock registers and a measured CPU rate (`power`), counts misreads without
retiring (`count-only`), holds an eight-note chord on track 0 (mode 8) and dumps
P33 (mode 9). Never release a `MELODEE_CORE1_TEST` build.

`tests/dual_core_test.c` exercises the actual paired renderer with a host thread,
comparing samples across all 32 algorithms, three FM6 models, voice counts,
and all 200 Prophet factory programs at eight voices. Prophet stress covers both
filter modes, sync, Poly-Mod, high resonance, wheel modulation, noise, glide,
unison, effects and mixed FM6 tracks. Both exercise release, retrigger, patch
changes, panic and engine changes, and assert that all jobs join before a block
returns. Allocation checks cover eight voices, budget stealing and the five-voice
startup fallback. Run the regular host suite,
plus address/thread sanitizers, and build both option settings. Host checks do
not validate the hardware launch sequence or establish an FM-1 speedup.

Use `tools/usb_audio_stats.py --cores --voices --window` with the experimental
firmware to inspect worker availability, total FM6/Prophet jobs, completed FM6 pairs, maximum job
and join wait times, and stalls. Compare `audio_max_us`, `cpu_pct`, late renders
and voice shedding with the same patch, notes and USB settings in the serial
build. Verify controls, MIDI timing, audio, patch changes, persistence and update
entry on hardware before enabling this in a release. The eight-voice Prophet cap
is enabled only in this experimental build; validate worst-case patches and
release tails before adopting it in a release.

## Nightly builds from `next`

The web installer keeps the released standard and single-core packages and
adds two development builds from `next`: dual core and single core, both with
`MELODEE_RAM_AUDIO=1`. The clock is chosen on the device (OVERCLOCK, above).
Save a complete backup in the editor before trying a nightly. Selecting
Standard in the installer returns to the released firmware.

`./build.sh --nightly` stamps the current Git commit into the firmware version
(`next-<commit>`) and uses development identity `FM-1_900`. Build both variants
from the same commit; `MELODEE_DUAL_CORE=0 ./build.sh --nightly` produces
`build/melodee-nightly-1core.fwsc`, while the dual-core package is
`build/melodee-nightly.fwsc`. The installer shows the source commit and UTC
build time, verifies the selected channel's identity, and locks all choices
while installing.

The Nightly builds workflow runs on pushes to `next`, manually, and daily at
02:00 UTC. Its scheduled definition must also exist on GitHub's default branch.
It checks out `next`, builds both variants, runs the host/web suite,
and publishes only after successful checks. Packages, checksums and source
metadata are available as an Actions artifact and in `gh-pages/firmware`.
`web/publish_nightly.py` updates the existing Pages checkout while keeping its
published release packages and editor.

## Synth-only firmware

The `synth-only` branch is a one-track sound module for playing and recording into a computer: every
synth with its sound pages, ARP, CHORD and SCALES, the effects (drive, chorus, delay, reverb), and
stereo master capture on the USB audio input. It has no sequencer, songs or project saving; PLAY holds
the ARP, and the last sound comes back at power-on. It keeps the main firmware's flash layout, so the
projects and presets saved on the FM-1 stay and return with Standard. The branch is never merged into
`next`.

The installer offers it as an option (Synth only, dual or single core) when the site carries it, with
its own editor at `webapp/synth-editor/`. Build both variants on `synth-only` from one commit
(`--nightly` stamps `synth-<commit>` with identity `FM-1_900`), then add them to a Pages checkout from
this branch's `web/`:

```
# on synth-only
MELODEE_RAM_AUDIO=1 ./build.sh --nightly
MELODEE_RAM_AUDIO=1 MELODEE_DUAL_CORE=0 ./build.sh --nightly
# from next's web/: the packages, the source commit, the synth-only checkout's web/ (its editor)
python3 web/publish_synth.py <pages checkout> --dual <synth>/build/melodee-synth-nightly.fwsc \
  --single <synth>/build/melodee-synth-nightly-1core.fwsc --version synth-<commit> \
  --commit <full commit> --built-at <UTC time> --web <synth>/web
```

`web/make_site.py` and the nightly workflow keep the synth-only packages, `firmware/synth.json` and
its editor when they regenerate the site.

## EQ accelerator experiment

`MELODEE_HW_EQ=1` enables the AC79 hardware EQ backend. It is **off by default**
until physical arithmetic, DMA completion time and sustained FM-1 load are
measured. An impulse through four nontrivial all-pass sections is checked
against the CPU reference before the first active filter effect. A failed
probe, timeout or invalid output permanently selects the CPU path until reboot.
The wait is bounded to 8192 register polls; audio never waits on an SDK mutex
or semaphore. There are four logical biquads per stereo side per slot (96 track sections plus 24 shared send sections), time-shared through a single mono hardware lane. This is a logical
budget, not evidence that the chip provides 32 independent physical filters.
Tape uses a fused stereo software path: three cascaded one-pole pairs per
channel, with shared coefficients and polarity smoothed over about 15 ms.
Coefficient targets are calculated only when JITTER or the random target
changes. It avoids the six separate filter passes and hardware EQ waits, and
needs no delay allocation. The other filter effects retain the optional EQ path.
Filter effects, Echo and Room run at FS/4 (11.025 kHz). Reverb runs at FS/8 (5.5125 kHz), with a 4 KiB allocation per slot. Two modular running sums provide a triangular CIC input filter; cached block-local converter state avoids repeated SRAM accesses. The seven-tap quarter-rate response and fifteen-tap eighth-rate response reject aliases, with a low-pass pole and upper-mid compensation for Reverb. Wet interpolation and wet/dry mixing are fused in the sample kernels; bounded power-of-two divisions use exact shifts, without a hardware divide.

The eight-line fixed-point Reverb tank uses an orthogonal Householder reflection, input/output diffusers, length-normalised decay and a bounded wet return. SIZE/DEPTH coefficients are cached; drift targets update once per control block. DEPTH retains bass/body and increases return level and tail duration. Synths and the dry mixer retain FS = 44.1 kHz. OFF / MIX 0 releases the slot's delay memory; memory exhaustion bypasses only the failing slot.

Track SEND is post-level/pan/mute/solo and feeds one three-slot stereo auxiliary chain. Shared MIX defaults 127, RETURN defaults 100, SEND and TYPE default 0. CPU0 prepares control values and all allocations, then snapshots the final pointers after compaction. CPU1 owns the shared workspace while CPU0 computes dry master gain, and CPU0 joins before return summing, DC rejection, limiting or any events/flash access. Existing FM6/Prophet voice pairing remains in place. There is no additional block of audio latency. Offline CPU1 runs the same callback serially; a failed in-flight callback is discarded, never rerun with partly advanced histories. Individual USB stems exclude the shared return. The parallel return ramps to silence when the final active slot is bypassed or changes type; RETURN edits are smoothed over about 6 ms.

Opt-in `MELODEE_FX_PROFILE=1 MELODEE_BENCH_SILENT=1` packages allow `tools/fx_performance_device.py` to inject repeatable input and override DSP settings without changing song data. `--bus` measures the shared return. These packages mute physical output and must be replaced by a normal package after measurement. Host `tests/fx_performance.c full` covers all 1,024 ordered extreme/default three-effect combinations. Device deadline/shedding/fault counters remain the performance authority; host instruction counts are not device CPU percentages.

The row/stride/control layout follows the locally installed AC79 V1.2.1 SDK's
`eq.c` and `asm/eq.h`, also described by
[JieLi's EQ documentation](https://doc.zh-jieli.com/AC79/zh-cn/master/module_example/audio/EQ.html).
Host register-model tests cannot establish physical audio quality or CPU savings.
