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

`JIELI_TOOLCHAIN` and `AC79_SDK` override the default locations
(`~/.jieli/toolchain`, `~/fw-AC79_AIoT_SDK`).

`./build.sh --release 0.11.1` makes a release build: the package identity becomes `FM-1_90111`
(`FM-1_9` and the version's digits; X.Y without a patch number: 0.11 is `FM-1_9011`) and the version
string `v0.11.1`; the package is `build/melodee-0.11.1.fwsc`, and `build/release-0.11.1/` holds what a
release ships: the package, the app (`melodee-0.11.1-app.bin`), `SHA256SUMS`, `LICENSE`, `LICENSING.md` and
`LICENSES/` (the package contains Apache-2.0 SDK files, so the licence texts travel with it).
For example, `--release 1.0.0` uses the distinct identity `FM-1_91000`.

Build options (environment, `0` or `1`; defaults in `firmware/src/build_options.h`, `melodee.c`, `core.h` and `icons.c`):

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
| `MELODEE_FM4` | 0 | the retired DIGITAL engine (4-operator FM) instead of its FM6 conversion |

## Samples

The CC0 instrument samples that the SAMPLE engine uses are in `assets/samples-cc0/`
(Versilian Studios, see `ATTRIBUTION.txt` there). `tools/fetch_cc0.py` downloads them
again from the source repositories. Without that folder the build still works and the
SAMPLE engine has only the generated drum kit.

## Tests

Audio working memory is allocated on demand from the unused part of POOL and
linker-derived gaps after RAM code, ordinary data and retained state. Allocations
stay within a single bank and preserve boot records and stack guards. Each
track reserves only its current engine state; STUT reserves 8 KiB per enabled
track, while GATE needs no recording buffer. Performance loops reuse the same
32 KiB capacity as the four stutters. Chorus and reverb retain their tails before
releasing their delay lines; silent buses skip the delay processing.

Preset browsing keeps names and slot indexes in RAM, with one bank cache for
general presets, one for FM6 and one shared with the CZ bank reader. Retired FM6
bank and owned-voice caches are replaced by temporary migration workspace,
released after conversion. Existing flash and backup formats remain readable.

The build reports both permanent POOL usage and the available working arena,
and requires at least 152 KiB for simultaneous maximum engine state, recording,
effects and legacy conversion workspace. The resource tests cover eviction,
engine transitions, effect tails and resumption, and allocation failure.

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
DIGITAL conversion), the chord keys, the modulation matrix, the FX layer, the reverbs, the SLICER
and swing. With `DAISYSP` pointing at a DaisySP checkout, the PHYS models are also compared with
their floating-point originals; without it that test is skipped.

The regression suite (`tests/regress.c`) renders every engine and preset and compares a
hash of each render with `tests/golden.txt`; it also checks levels, voices and the CPU
cost (`tests/cpu_baseline.txt`, `tests/target_budget.txt`). After an intended change of
the sound, `GOLDEN_UPDATE=1 sh tests/run_tests.sh` rewrites the hashes; `BUDGET_UPDATE=1`
does the same for the cost files.

## Install

Use the web installer in Chrome or Edge:
<https://keremimo.github.io/melodee/webapp/installer/>. It installs the released package.

From the command line (needs `pip3 install mido python-rtmidi`):

```
python3 tools/fm1_install.py build/melodee.fwsc
python3 tools/fm1_install.py --info          # identity of the connected FM-1
```

Or, to install your own build from the web installer, make a local copy of the site and open it from `localhost`
(Web MIDI needs a secure context):

```
python3 web/make_site.py build/melodee.fwsc dev /tmp/melodee-site
cd /tmp/melodee-site && python3 -m http.server 8000
# open http://localhost:8000/webapp/installer/
```

Installing firmware is at your own risk. If an install fails and the FM-1 no longer
starts, recovery needs [FM-1-transporter](https://github.com/kurogedelic/FM-1-transporter).

## Experimental cache RAM

Builds reclaim seven 4 KiB data-cache ways at `0x01F28000..0x01F2F000`
(`MELODEE_CACHE_RAM=1`, the default while it is being validated on the device; `MELODEE_CACHE_RAM=0 ./build.sh`
turns it off), following the AC79 SDK's flash-execution configuration. All instruction-cache ways remain available.
The first 14 KiB are the UI's (eight undo levels); the rest join the audio allocator, whose worst case still fits
the SRAM banks alone. Without a passed self-test the UI keeps one undo level. Keep USB audio enabled during
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
