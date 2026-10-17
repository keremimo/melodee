# CPU savings plan for more polyphony

Increase the number of simultaneous instrument voices by reducing the work
needed to complete each audio buffer. Preserve musical timing, key response,
existing patches, sound quality and project compatibility. Evaluate all six
paths below, retain measured improvements, then recalibrate the shared voice
budget against the combined result.

Planning baseline: `experiment/recording-release-queue`, commit `b296f57`,
2026-10-16. The recording release queue has passed host tests and a target
build; its CPU benefit still needs device measurement. Implementation has
started; completed work and remaining validation are distinguished below.

## Progress as of 2026-10-16

Implementation is on `experiment/recording-release-queue`; detailed evidence
and prepared/installed distinctions are maintained in the progress log:

- `70c3baf`: guarded CPU baseline tooling, bounded timing histograms and panel
  cost spans, with source/package manifests and host-memory runtime restoration.
- `875332f`: retain the in-memory original after restoration failure, support
  interactive recovery and reject transfers if the persistence guard is lost.
- `292aa13`: independently switchable modulation-routing cache, disabled by
  default and enabled in the installed comparison build.
- `dc7c5ce`: cache destination layouts independently of live source selections;
  reduce source-change cost and cache memory to 256 B. Prepared, not installed.
- `05ace19`: screen the larger statistics/histogram timing bound, rank inclusive
  spans and exercise engine destinations with optional host-paced source changes.
- `8c1f104`: retry timed-out idempotent runtime reads without retrying writes.
- `0a6efbb`: opt-in centered-bend fast path; reject a general pitch cache after churn measurements.
- `f109b21`: per-core/preparation/merge profiling and sample-job ranking tools.
- `f5602f9`: count actual displaced notes without counting same-circuit drum retriggers.
- `db1efe6`: centered mono-send fast path, host/state parity and prepared target builds; default off, not installed.
- `a60f2a9`: exact Chorus dormancy with kernel and insert/CHAIN/SPLIT trace parity, host/target checks and prepared packages; default off, not installed.
- `8c3e668`: compact overview pitch index with independent scanner parity, measured sparse/dense/rebuild costs and prepared target packages; default off, not installed.
- `2a6f18a`: normal-build ten-minute stress observer, host-tested; hardware execution and additional workload fixtures remain pending.
- `01fb13b`: exact zero-wheel Prophet candidate with full part-state parity at 1x/2x, measured static/changing costs and target checks; default off, not installed.
- `5da9f0d`: fixed column-word table, no added RAM, unchanged shift-kernel assembly; brightness cache rejected after sparse regressions; default off, not installed.
- `0c9fa79`: exact FM6 Modern zero-level preparation, full voice-state parity and static/changing cost checks; default off, not installed.
- `6516ee6`: exact CZ native window-4 kernel, exhaustive phase-function and full-renderer state/PCM parity including ring/noise and partial blocks; default off, not installed.
- `8e5ce97`: exact 808 zero-transpose and 909 unity-pitch metal preparation candidates, independent numerical and full-state/render comparisons; default off, not installed.
- Exact SID filter-output specialization avoids unused high-pass arithmetic with full state/PCM parity and host/target checks; default off, not installed.
- Guarded lab-only native transport/REC control and internal-clock constructed playback; review fixed a nested interrupt-mask defect and track-in-use checks; fresh packages prepared, not installed.
- Bounded live REC/overdub take runner with native-replay comparison, failure cleanup and guarded restoration; host-tested, not run on hardware.
- Exact capture-allocation candidate (`MELODEE_CAPTURE_FAST`): owner-list prune/seal/replacement plus early lowest-free slot, full-state parity; 1.6–3.4x fewer host instructions per recorded note; default off, not installed.
- Resident/cache layout: eight undo levels in validated cache, one SRAM level on failed cache startup, reclaiming 11,256 B; unchanged 48 KiB engine minimum. Cache-enabled packages and validation are recorded in the final progress checkpoint; hardware acceptance remains pending.

The full host suites passed in 79 s for the baseline infrastructure, 83 s
with the original cache and 81 s with the revised cache. Validation includes
unchanged golden renders, all 1,296 ordered concurrent engine assignments, reference-state comparisons and
ASan/UBSan. Target lab and normal builds, memory and register checks, and
target instruction checks passed. These results do not establish device CPU
savings, physical input/audio behavior or increased usable polyphony.

The user-requested flash installed `build/melodee-1.4.0-cpu-mod.fwsc`, SHA256
`6908de941a24344b27810be3979452139acc61c57a5d3804fb65d20e05a46625`.
Post-install reads verified `MELODEE v1.4.0-cpu-mod`, model `FM-1_91040`,
360 MHz, 128-frame halves and budget 16. The modulation cache, profiling and
benchmark support are compiled in; the fixed-benchmark bypass and persistence
guard are inactive. `MELODEE_RAM_AUDIO=0` and `MELODEE_ADMISSION=0` preserve the
actual baseline layout and weight policy. Controlled candidate-only screens
have started; baseline/candidate device comparisons remain pending. No voice
allowance or production default has increased. That installation is historical: Claude later reported installing the twelve-candidate
flash-layout `FM-1_900` before its final GUARD label rebuild. This continuation's
read-only device query confirms `FM-1_900` and cache RAM disabled; it does not
independently establish the installed source hash. See the final progress
checkpoint for the resident/cache build set and its explicit cache-failure
undo-depth tradeoff. The user subsequently requested and received the normal `ram12` package; post-install reads confirm its identity, cache self-test ready and 69,240 B audio resource capacity. Profiling/reference packages remain uninstalled; musical and additional-voice validation remains pending.

Detailed measurements, package hashes, reproduction commands and known limits
are recorded in [POLYPHONY_CPU_PROGRESS.md](POLYPHONY_CPU_PROGRESS.md).

## Execution order and experiment boundaries

| Stage | Path | Current status | Next deliverable |
| --- | --- | --- | --- |
| 0 | Current device baseline | 3 x 60-s candidate-only screen and ranked spans complete for one workload; comparison pending | Baseline/candidate runs, profiling-cost measurement and broader workloads |
| 1 | Panel scanning | Fixed column-word candidate host/target tested; brightness cache rejected; SPI2 routing appears feasible | Target fixed-word comparison and electrical timing acceptance |
| 2 | Control preparation | Routing revision and centered-bend fast path host-tested; default off; general pitch cache rejected | Device comparisons; remaining control preparation and structural churn |
| 3 | Core scheduling | Per-core/preparation/merge spans and host job-ranking tooling tested; policy unchanged | Target class measurements, bounded estimates and assignment comparison |
| 4 | FX and mixing | Centered mono-send and exact Chorus dormancy candidates host/target-tested; all-effect routing trace parity | Device comparisons, physical tails/wakeup and further measured pass work |
| 5 | Sequencer indexing | Compact active-bank pitch and exact capture-allocation candidates host/target-tested | Target query/rebuild/edit and live-take timing, external-clock and physical validation |
| 6 | Engine fast paths | Prophet/FM6/CZ, 808/909 preparation and SID filter candidates tested, default off | Target patch ranking/comparisons and deeper sample-path evaluation |
| 7 | Polyphony calibration | Not started; allowances unchanged | Validated additional voices and combined stress results |

Start each candidate from a recorded, tested parent and keep it separately
switchable for host comparisons. Commit each accepted change separately; run
the combined firmware after each integration. Investigate panel hardware
feasibility early, but continue the software paths if that investigation needs
hardware work. Production defaults change only after the relevant device
checks pass. Firmware installation remains a separate requested action.

## Stage 0 Establish the current device baseline

Implemented: 64-bin/64-us assembly-envelope histograms, seven panel subspans
and `tools/audio_cpu_device.py`. The runner uses explicit shared-budget voice
counts, bounded in-memory runtime/preferences, blocked persistence, read-only
committed-object verification and recovery after a failed restore. Profiling
windows are limited to 120 s to avoid 32-bit span-total wrap.
`tools/audio_cpu_stress.py` now provides continuous ten-minute observation of
a user-supplied performance on normal firmware, with native driver stderr
and callback evidence. Actual execution and broader workload fixtures remain
pending. The runner now ranks inclusive spans and covers voice/engine modulation
routes plus host-paced source changes. The screening and normal observers now support exact stereo/four-stem USB
capture with final host-driver evidence. Constructed 64/256/1,024-event playback
fixtures and measured host-paced USB clock are host-tested; device execution
is unrun. Bounded live REC/overdub takes from host-paced USB MIDI are compared
against the firmware's own replay of the same schedule (host-tested, unrun).
TRS-clock fixtures and controlled physical input remain open. Actual stem
capture is unrun.
Guarded native transport/REC control (lab-only editor 101) and an
internal-clock option for constructed playback are host-tested, with fresh
`cpu-lab11`/`cpu-ref9`/`cpu-lab0` prepared packages; internal-clock playback is
unrun on hardware and is not live recording acceptance.
The uncached `1.4.0-cpu-stage0` package is a historical prepared artifact.

Use the same clock, audio configuration, patches, input traffic and voice
counts for each before/after comparison. Keep 128-frame halves, 32-sample
control blocks and the current sample rate. Record the exact source revision,
package hash, build flags, device identity, budget and engine assignment.

1. Make a profiling build using the existing audio spans. Measure complete
   interrupt time including nested input, MIDI and USB service; instrument
   controls; each engine; mixer/FX/output; and worker execution and waiting.
   Keep inclusive spans separate so nested work and concurrent cores are not
   added together as if they were serial work.
2. Repeat representative cases in an interleaved baseline/candidate order.
   Record steady load, simultaneous attacks, releases, retriggers, patch/control
   changes, maximum observed time and high-percentile buffer times. Add bounded
   histogram support if required, and measure its own cost.
3. Cover solo instruments and mixed tracks, short and long release tails,
   free and quantized recording, 64/256/1,024 recorded events, overdubbing,
   modulation, insert/shared FX and internal/external clock. Repeat with USB
   audio inactive, stereo streaming and stem capture, plus panel/encoder input.
4. Measure the additional buffer time for one extra voice under each important
   engine/patch combination. This supplies the conversion from saved time to
   useful polyphony; host instruction counts alone do not supply that conversion.
5. Repeat acceptance cases in a normal build with profiling and benchmark
   bypass disabled, ordinary admission active and emergency shedding active.

The hardware deadline is `128 * 544 / 24 = 2,901.33 microseconds`. Use 80%
(`2,321.07 microseconds`) as the target for workloads being qualified for more
voices. Report target failures separately from deadline failures; preserve the
existing overload protection. Historical capped-package results in
[AUDIO_PERFORMANCE.md](AUDIO_PERFORMANCE.md) do not certify this shared-budget
branch or a higher voice allowance.

Use the new shared-budget runner for the implemented screening cases. The
older performance harness still requires an existing backup directory and
retains fixed-reservation assumptions; it has not been converted into the new
runner. Keep temporary runtime fixtures and restoration in memory, verify
persistent data by reads, and avoid autosaves during setup. Creating a backup
is not a prerequisite. The serialized restore clears undo and running engine/
FX state, so start stopped with all voices released; it preserves project
content rather than an interrupted performance's sample-exact state.

**Exit:** a reproducible baseline, a ranked time breakdown and a list of
workloads whose next additional voice is currently too expensive.

## Stage 1 Reduce panel scanning overhead

Owners: `firmware/hal/fm1_input.h`, `firmware/hal/fm1_led_pwm.h`,
`firmware/hal/fm1_gpio.h` and `firmware/src/main.c`.

Initial feasibility: SDK SPI2 port C maps CLK/DO to PA3/PA4, matching the
shift-register wiring. SPI0 serves flash and SPI1 serves the LCD. SPI2's DI
route is PA2, also the LCD backlight, so a prototype must stay transmit-only
and preserve that pad and unrelated PWM/IOMAP ownership. No peripheral scan
path has been implemented or electrically validated. A separately switchable
fixed column-word table removes repeated word construction; default off. The
shift routine, GPIO edge operations and blanking waits are unchanged. A
brightness-cache probe was rejected after sparse patterns regressed by up to
60%; it remains a host benchmark only.

An older device trace measured about 10 microseconds per input tick. At
10 kHz this is a material fixed cost on CPU0. Remeasure it on the current
firmware and separate shifting, GPIO reads, LED/PWM setup, debounce, encoders
and diagnostic bookkeeping.

1. Check the board wiring, pin multiplexing, available peripherals and shared
   bus ownership for hardware-assisted shift-register transmission. Include
   latch timing, LED blanking and completion timing in the feasibility check.
2. If hardware shifting is practical, prototype the same 16-bit column word
   and latch sequence through that peripheral. Keep the existing path as the
   comparator. Bound any completion wait inside the input interrupt.
3. Independently precompute column/LED values when their inputs change, reduce
   repeated address/mask arithmetic and remove redundant register operations
   whose ordering is not required by the board.
4. Keep the 10 kHz scan cadence, debounce thresholds, encoder interpretation
   and LED brightness behavior. The current shift routine documents that
   excessively short write-only edges selected neighboring columns; preserve
   its electrical timing until measurements establish a valid replacement.

Validate with `input_test`, target register/residency checks and device tests:
fast chords, bouncy presses/releases, fast encoder turns, all LED brightness
planes, heavy audio load and concurrent USB/TRS traffic. Measure end-to-end
key latency and missed/duplicate inputs, alongside input time and full-buffer
savings. Host input tests cannot establish shift-register electrical timing.

**Exit:** lower measured input cost without a response or LED regression. If
hardware routing is unsuitable, retain only verified software improvements.

## Stage 2 Prepare controls only when their inputs change

Owners: `firmware/src/mod.c`, `firmware/src/voice.c`,
`firmware/src/mix.c` and the engine control preparation functions.

Implemented candidate: `MELODEE_MOD_CACHE=1` caches merged destinations and
ranges in 256 B across four tracks. Raw destinations, engine and valid-source/
amount-zero membership key the layout. Sources, amounts, polarity, evolving
values and bases stay live. Voice dependencies are rebuilt from those live
sources at their original rate, without rebuilding the destination layout.

The original installed revision used 352 B and regressed source changes by
27–48%. The uninstalled revision uses about 31% fewer host instructions for
four engine destinations, including moving sources and amounts. Voice-only
routing is approximately unchanged or slightly cheaper. These are routing-only
host measurements, not complete device CPU savings. Continuously changing
destinations, engine or amount-zero membership still costs 9–31% more in the
expanded benchmark because it rebuilds the layout. Keep that limit visible.

Reference-state parity, unchanged golden/concurrent tests, sanitizers and lab/
normal target checks pass. The original installed candidate has initial device
screens with ranked spans, including failed screens; the revised candidate has
not been installed or compared. Stage 2 exit criteria remain unsatisfied and
production defaults remain unchanged.

Several engine coefficient caches already exist. Start with repeated work
outside those caches, especially the modulation routing rebuilt in `mod_begin`.

1. Separate static modulation routing from changing source values. Prepare
   validated source/destination mappings, destination ranges, merged parameter
   destinations and per-voice dependencies when the configuration changes.
2. Use a complete small routing key initially. Audit every mutation source
   before relying on generation counters: panel edits, MIDI, automation,
   temporary controls, native editors, loads, engine changes and undo/redo.
3. Cache exact pitch/bend/control results when all their inputs match. Include
   A4 tuning, scale offsets, native patch/function settings, engine macros and
   modulation where relevant. Optimize the quiet and unchanged cases first.
4. Share track-wide results across voices and reduce unnecessary parameter
   snapshots. Preserve private, fully published worker inputs and the existing
   order of modulation, engine controls and FX processing.
5. Keep evolving state running: envelopes, LFO phase/noise, smoothing, glide,
   active automation and FM6 quiet voice state. Cache routing and invariant
   arithmetic without lowering their update rate.

Compare cached and uncached PCM through control edits, automation, imported
native values, tuning, all voice modes, engine changes and undo. Use modulation,
tuning, native-control, engine and concurrent matrix tests. Measure cache hit
rates and costs for both static patches and continuous controller movement.

**Exit:** exact output/state parity and a useful reduction in control time,
with no material penalty in the continuously changing case.

## Stage 3 Improve scheduling across both cores

Owners: `firmware/src/audio_batch.h`, `firmware/src/audio_worker.h`,
engine job descriptors and the mixer preparation pipeline.

The current pipeline already overlaps work across tracks. Its cost table has
one estimate per engine family, including one for all 909 circuits. Improve
assignment within that pipeline rather than adding a second rendering system.

1. Measure sample-job cost by engine, circuit/algorithm and enabled expensive
   features, together with CPU0 control/service cost and descriptor overhead.
2. Build bounded conservative cost classes from those measurements. Avoid
   timing-driven scheduling that changes musical state or adds a profiling
   dependency to production firmware.
3. Compare the existing assignment with the refined estimates. Track the
   completion time of both cores, CPU0 wait, early worker idle time and the
   busiest buffer. Avoid pushing tiny jobs to CPU1 when coordination costs more.
4. Evaluate descriptor preparation/merge overhead and batch boundaries after
   assignment improves. Preserve result accumulation order, private scratch,
   publication barriers and joining before state reuse.
5. Retain worker retirement and serial fallback. A job partially advanced by a
   failed worker must never be replayed as if it had not run.

Use worker fault/wrap tests, genuinely concurrent serial/parallel PCM equality
and the 1,296 ordered engine-assignment host matrix. Device comparisons must
include mixed heavy/light patches and input/USB preemption. Report total work
and elapsed buffer time separately: less waiting can increase usable capacity
without reducing total instructions across both cores.

**Exit:** a shorter completion time for constrained workloads, no material
regression for balanced workloads, and unchanged ownership/fallback behavior.

## Stage 4 Reduce FX and mixer work

Owners: `firmware/src/fx.c`, `firmware/src/mix.c`,
`firmware/src/audio.c` and USB capture paths.

1. Profile dry, insert, shared CHAIN and SPLIT routes independently. Trace
   buffer clears, copies, gain/pan/solo passes, sends, returns and output work.
2. Fuse compatible passes and specialize common zero/unity/default cases.
   Preserve arithmetic order, rounding, clipping and channel routing. Keep
   parallel overlap where merging passes would otherwise increase worker wait.
3. Extend bypasses to selected effects with silent inputs and finished tails.
   Use the effect's actual state and required history; zero wet input alone
   does not establish silence. Continue cheap phase/smoothing work needed for
   the next audible block and wake processing without a discontinuity.
4. Use an exact dormant-state path first. Treat any threshold-based tail
   truncation as a separate sound-affecting proposal rather than silently
   including it in this optimization.
5. Keep limiter/DC protection, stereo spread, solo/mute ramps, metronome and
   USB master/stem semantics intact. Inspect copies within masked-interrupt
   sections as well as copies within the mixer.

Use FX correctness/performance tests, mixer goldens, USB/stem tests and the
concurrent matrix. Device cases include long feedback/reverb tails, silence
followed by an attack, zero/unity gains, hot levels, live sends/routing edits
and multiple effects. Record audible transitions as well as CPU measurements.

**Exit:** fewer measured mixer/FX microseconds with exact retained signal
behavior and correct tails, wakeup and streaming.

## Stage 5 Complete sequencer indexing

Owners: `firmware/src/recording.c`, `firmware/src/seq.c` and recording/edit
operations that change step ownership.

The onset cursor and release queue already handle event timing. Production
`seq_step` still searches the selected bank for ownership each step. The
opt-in `MELODEE_RECORD_INDEX` candidate uses active-bank pitch bitmaps with
coalesced edit invalidation and a sparse-bank fallback. Independent scanner
parity, host query/rebuild measurements and target memory checks pass; device
maintenance-burst and combined-buffer measurements remain pending. Live capture
allocation scanned all 1,024 slots per recorded note; opt-in
`MELODEE_CAPTURE_FAST` keeps the same slot choice and state through one bank-list
walk and an early lowest-free search (full-state parity against the original).
Near-full stores and the once-per-take undo snapshot remain O(stored events).

1. Measure that scan and the release queue's overview checks separately from
   synthesis, capture allocation and ordinary step dispatch.
2. Prepare a compact per-step pitch ownership index for active banks. Query it
   when suppressing duplicate overview notes and drum hits. Prefer a bounded
   representation with explicit RAM accounting.
3. Preserve the distinction between original onset step and rounded overview
   cell, wrapped cells and notes outside LEN. Maintain exact pitch ownership
   when many events share a cell or pitch; deleting one must retain the others.
4. Define invalidation/update behavior for capture/replacement, punch-out
   quantization, deletion, erase passes, copy/double, move, undo/redo, load,
   bank switches and LEN changes. Keep expensive rebuilds out of the steady
   tick and measure the longest rebuild/edit burst.
5. Inspect other repeated scans only if the profile identifies them. Capture
   allocation and background jam capture can be separate follow-ups within
   this path; preserve their existing recording and history semantics.

Extend the independent scanner oracle and recording benchmarks. Exercise all
1,024 events concentrated on one track, repeated pitches, sparse banks,
quantization collisions, overlapping releases, edits and external clock.
Use existing recording, note-editor, erase, undo and persistence tests.

**Exit:** identical event/voice behavior, lower step-dispatch cost and bounded
maintenance spikes that do not erase the timing gain.

## Stage 6 Add exact engine fast paths

Owners: `eng_prophet_test.c`, `eng_fm6.c`, `fm6_core.c`, `cz_native.c`,
`eng_sid.c`, `eng_drum.c` and their underlying drum kernels.

Rank actual instrument costs after the infrastructure changes. Work on the
patches limiting the desired extra voice, with a separate comparator for each
specialization.

- **Prophet:** extend existing disabled-waveform and control caches; inspect
  wheel modulation, invariant oscillator/pitch inputs and inactive routes.
  Preserve sync, audio-rate Poly-Mod, filter histories, drift and noise order.
- **FM6:** inspect algorithm/operator dependencies and zero-contribution paths.
  Skip sample work only when feedback/routing and future voice state remain
  exact. Retain all algorithms, engine modes, envelope and quiet-state behavior.
- **CZ:** extend existing tone/pitch caches and specialize unchanged line/window
  cases. Preserve both lines, ring/noise behavior, envelopes and DC histories.
- **SID:** inspect waveform combinations and inactive signal paths beyond the
  existing frequency/filter caches. Keep chip counters, sync, ring and noise
  state advancing correctly.
- **808 and 909:** specialize expensive circuits and constant/zero mixes, share
  invariant preparation, and inspect genuinely finished circuits. Preserve
  resonators, noise sequences, choke/retrigger behavior and concurrent scratch.

Use unchanged golden renders, numerical reference tests, native patch/control
coverage, serial/concurrent parity and per-engine health checks. Test extreme
notes and parameters, controller motion, long tails and randomized patches.
Use recordings/listening for transitions and any case not established by exact
comparison. Keep sample-rate, precision, oversampling and envelope-resolution
changes outside these exact optimizations; any quality tradeoff needs a
separate explicit decision.

**Exit:** measured improvement in the limiting instrument cases with preserved
state and sound, including patches that do not take the new fast path.

## Stage 7 Convert savings into tested voice capacity

Re-run the original workloads after every accepted change and on the combined
candidate. Measure interactions: less CPU0 service work can change the best
core assignment, and fused mixing can change the overlap available to FX.

1. Hold voice counts fixed to establish actual time saved. Record the same
   workload at the original source, each accepted stage and the combined result.
2. Increase requested voices incrementally using the current shared budget.
   Test the extra voice under simultaneous attacks and release overlap, not
   just while holding a settled chord. Include native engine caps and the RAM
   needed for extra voice state.
3. Revisit budget weights and allowance only where the measured workload
   supports it. Preserve protected-note/stealing priorities and CPU1 fallback.
   Keep limits conservative enough for the tested patch/control range.
4. Run the broad host suite and build/residency/memory checks. Re-run the full
   relevant device assignment matrix, then longer stress cases for the slowest
   workloads, USB/clock/controller bursts and playing panel gestures. Start
   with 60-second screening and a proposed 10-minute final stress run per
   representative limiting case; finite testing remains workload-specific.
5. Validate the production package with profiling/bypass disabled. Require zero
   observed deadline misses, late buffers, emergency voice shedding, resource
   failures, worker faults and USB stream errors in the qualified workloads.
   Count normal capacity steals separately; the additional requested voices
   must actually remain available in the capacity tests.

For each result, publish before/after buffer time and headroom, worker wait,
RAM use, errors and sustained voices by engine combination. Include cases with
little gain. Do not add overlapping percentage savings from separate stages or
apply old fixed-capacity guarantees to the expanded voice budget.

## Shared constraints and completion criteria

Measured builds retain the same 153,056-B contiguous audio arena and
142,336-B minimum in the baseline layout:

| Build | Data/BSS of 98,304 B |
| --- | ---: |
| Baseline / normal build, cache and profiling off | 95,888 B |
| Profiling baseline, cache off | 96,512 B |
| Installed profiling candidate, original cache | 96,864 B |
| Prepared profiling candidate, revised cache | 96,768 B |

The recorded baseline has FM6 kernels in internal DSP SRAM; full audio-graph
residency was opt-in, not enabled in that package. The attempted full-residency
profiling build failed its separate 49,152-B minimum-arena check. No minimum
was lowered. Resolve that configuration's memory fit separately before using
it in comparisons. Preserve the existing checked SRAM kernels and keep layout,
clock, weight policy and workload identical within each CPU comparison.

New caches, indices and descriptors must fit measured memory limits and the
intended simultaneous engine/FX workloads. No new unbounded wait, allocation,
sort or lock belongs in the steady audio path.

Keep projects, preset formats, free recording timing and independent note
lengths compatible. Retain source provenance and applicable notices when
extracting or moving implementations. This work does not require a new UI.
If a status indicator is needed later, follow the shared-header rules in
[the design guide](design/README.md).

A path is complete when its candidates have been measured, the worthwhile
changes pass their correctness/device checks, and rejected candidates have a
recorded reason. Overall success requires a demonstrated increase in usable
polyphony for stated workloads while retaining the required timing margin
and musical behavior. No numerical savings or extra voice count is promised
before the measurements establish it.
