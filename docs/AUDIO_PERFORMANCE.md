# GLO > WEIGHTS and SAFETY (2026-10-16)

The weights described below apply only to builds with `MELODEE_ADMISSION=1`.
Default builds (admission off) cost a Prophet voice 2 points while the second
core is online (3 in serial fallback), an FM6 voice 1, and CZ, SID and DRUM 2.
GLO > WEIGHTS (P5, CZ-1, FM6, SID) and WEIGHTS 2 (DRUM, BUDGET, SAFETY) let the
player replace each engine's cost with 1–15 points; AUTO keeps the built-in rule
in either build. SAFETY defaults OFF; an explicitly saved ON choice is retained. SAFETY OFF ignores overload cut requests (`shed_voice`):
releasing voices are no longer cut, late buffers and deadline misses are still
counted, and a sustained overload can starve the main loop until its 8-s
watchdog restarts the device. These are device settings in PER5 favorites row 8
bytes 8–15 ('W', version 1; untagged bytes are the defaults), not project data.
No weight here is calibrated against measured device cost.

# Revision: shared workload budget

The user rejected the initial fixed one-voice Prophet/FM6 and two-hit DRUM
limits as musically unusable. They are superseded in the revised source. The
old results below remain evidence for the earlier `audio3` package, not a
certification of the new shared-budget policy.

GLO > GLOBAL knob 2 now controls BUDGET, a device-wide allowance of 8–32 weighted
points. The performance profile defaults to 16. Native engine capacities are
retained: Prophet 8 (5 in serial fallback), FM6 16, CZ 8, SID 4, DRUM 8. Idle
tracks reserve no sounding slots. At the default budget a solo Prophet track
can admit five voices, FM6 eight, CZ five, SID four, and a drum track up to eight
slots (existing lane reuse/choke semantics still apply). Multiple active Prophet
tracks cost four points per voice; one active Prophet track costs three; FM6,
SID and DRUM cost two, CZ three. These weights/default have limited device validation; further matrix testing
was stopped at the user's request. BUDGET is an allowance, not a literal voice count.

Normal pool exhaustion keeps existing capacity-stealing priorities and the
lowest held POLY note. The legacy explicit-fade counter remains compatible;
capacity fades also increment `capacity_steals`. Emergency CPU cuts remain
`voices_shed`; normal budget fades are `voices_given_up - voices_shed`. Lowering
the setting affects new allocation; already-held notes are not abruptly killed.
The existing protected-note floor still guarantees one lead per active part.

The setting is appended at global ID 41 (`G_COUNT=42`), with a virtual device
value like A4. It is not stored in projects/templates. Tagged high bits of PER5
`ext.usb_off` persist it without changing the 604-byte record or low USB flags;
old/unknown records use the profile default. Unchanged defaults do not rewrite
the original settings. Automatic settings writes are deferred while any voice
or USB audio stream is active, protecting live budget edits from flash pauses.
The web editor/mock exposes the same control and preserves it on project loads.

Idle Prophet wheel work now advances its exact noise sequence and LFO clock,
while skipping only unused stateless modulation buffers. The corrected host
suite passed in 68 seconds: all 335 golden renders unchanged, concurrent PCM
parity retained, and new shared allocation/solo chord/drum/settings/UI/web tests
passed. The new matrix fixture `tools/audio_shared_contract.json` describes
measured-workload targets within the shared pool; it does not impose per-track
runtime caps. Higher budget settings require their own timing evidence and do
not inherit the old capped package's deadline guarantee.

The revised `v1.3.1-budget4` lab package sustained five Prophet voices on one
track at budget 16 for 10 seconds with bidirectional USB audio, four modulation
slots and fully wet shared Chorus. The complete interrupt upper bound peaked at
2141 us (26.2% headroom), with no deadline, shedding, worker, resource or USB
errors. Original runtime and all 27 persistent objects were restored and
verified afterward. This single case does not certify every engine combination
or higher budget setting. No further matrix tests were run after the user's
stop instruction.

---

## Earlier fixed-cap candidate evidence

Local implementation based on `next` / `7025143`; no commit, push or publication.
Final evidence is recorded below. Production package uses admission and SRAM
audio, with laboratory profiling and benchmark bypass compiled out.
No backup is created or refreshed. The integer/buffer experiment worktrees and
all comparison artifacts remain untouched. No regression suite, numerical
check, listening comparison or new device benchmark ran during implementation.

## Contract and timing

128-frame half-buffers and 32-sample controls remain unchanged. The DMA clock
uses 544 TIMER4 ticks per frame, with TIMER4 at 24 MHz: the complete deadline is
2901.33 us; the initial 80% target is 2321.07 us. `FS=44100` is the nominal DSP
rate; older comparisons used its approximate 2902.49-us deadline.

Supported limits are defined in total simultaneous sounding voices, including
release tails, retrigger/choke transitions and overlapping hits. They are not
counts of held keys alone. `MELODEE_ADMISSION=1` selects the 1296-row reservation table in
`tools/audio_voice_contract.json`, indexed by ordered tracks in the category
order PROPHET, FM6, CZ1, SID, 808, 909. Prophet and FM6 reserve one slot per
track; CZ/SID reserve one or two depending on the full assignment. Every 808
and 909 track reserves two overlapping circuits. Four CZ tracks reserve
1/2/1/2 slots. The table reserves 4 to 8 total slots, with no idle-track borrowing.
This is a substantial polyphony tradeoff: Prophet/FM6 chords and multi-voice
UNISON replace occupied slots. Tail overlap consumes those same reservations.
Native patches remain readable and their DSP precision is retained; reduced
polyphony changes music that previously exceeded these limits. The default
macro is zero, preserving the legacy-capacity build option; the measured
performance package explicitly enables admission.

The intended supported contract requires zero observed DMA misses, zero
elapsed-deadline violations, zero emergency shedding requests/cuts, successful
allocations, healthy cores and complete timing below the deadline. The 80% target is reported separately; it is not claimed
for every adversarial patch. Notes beyond
capacity may use normal musical voice stealing. `capacity_steals` counts
restarted occupied slots; `voices_given_up` counts explicit cross-track fades;
`voices_shed` counts emergency CPU-overload fades. These are separate from
`audio_late` (DMA advancement) and `audio_deadline_misses` (elapsed C deadline).
CPU1 retirement is a safe serial fallback, not the supported dual-core capacity.

External MIDI drains at most 64 messages per control block without reordering;
excess traffic waits for the next block. Musical timing within the documented
burst limit keeps the original timestamps and one-half lookback. Sequencer,
keyboard and native note preparation are profiled separately. Final supported
attack/control/FX bounds must be established by measurement, not inferred from
this queue limit.

## Changes and rationale

* The mixer prepares per-track modulation, envelopes and engine controls on
  CPU0, preserving the parameters used by both synthesis and FX. A bounded
  eight-descriptor pipeline distributes independent sample work across tracks
  as well as within a track. The first complete descriptor starts CPU1 while
  CPU0 prepares the remaining tracks. Measured engine weights and a CPU0
  interrupt allowance balance ownership; publication uses memory barriers. Each job owns its PCM. CPU0 merges results in original voice
  order before DC filtering, spread, insert/shared effects and output processing.
  The common path avoids copying completed track PCM into another workspace.
* Prophet retains fp32 SSI/Curtis filtering, interpolation, saturation, sync,
  audio-rate Poly-Mod and its output DC filter. Disabled oscillator waveforms
  skip waveform evaluation; stateful triangle processing still runs when enabled.
  Normalized native controls and derived track coefficients are cached by raw
  patch bytes and engine macros, including imported values above 127.
* FM6 retains all 32 algorithms and MODERN/MARK I/OPL arithmetic. Track-wide
  LFO/controller modulation is shared; unchanged exact logarithmic frequency
  inputs reuse their phase increments. Inactive ghost voices share the track's
  quiet-state analysis; oscillator phases and next-note state keep advancing.
* CZ caches decoded eight-stage tone definitions once per active part, instead
  of rebuilding 48 rates and targets per voice per control block. Key-follow is
  applied to the current DCA segment. Dual lines, ring/noise modulation, window
  evaluation, pitch and DC filtering retain fp32 precision and native tone bytes.
  Exact per-line pitch keys include note, fine tuning and A4; continuously
  changing pitch/vibrato misses that cache correctly. Unheard envelopes and
  vibrato advance; only their stateless sample preparation is skipped.
* SID caches unchanged frequency registers, edge reciprocals and filter
  coefficients, primes combined-waveform tables before worker submission, and
  omits edge reciprocals on waveforms that do not need them. Chip envelopes,
  sync, ring, noise registers, DAC behavior and filter/output histories remain.
* 808 scratch buffers are explicit and private to concurrent jobs. Bass-drum
  and tom coefficient preparation is cached until its inputs change. The
  resonators, noise, metal oscillators, envelopes and shaping remain integer.
  909 independent circuits render through the same queue; their shared noise
  is prepared once on CPU0 and remains read-only during voice work. Choke and
  retrigger decisions remain on CPU0. 909 panel preparation defers intermediate
  filter/shaper recalculation until the final trigger parameters are installed;
  public single-control updates retain immediate preparation.
* Tempo-synchronized LFO divisions are cached by cycle length, including
  changes to measured external tempo. Allocation computes microtonal target
  pitch once before scanning candidate slots. DMA and worker scratch are aligned.

The old integer experiment is retained as evidence, not copied wholesale:
39.703 to 33.546 misses/s and 68.201 to 61.165 emergency cuts/s still failed.
256-frame buffers also failed and doubled the designed MIDI/output lookback.
UI sleep showed no material improvement. Source history since `v0.13` includes
fp32 synthesis, SID replacement, recording/modulation additions, SRAM relocation,
weighted budget expansion and nested USB/input servicing. No identical-project
run of complete 0.13 exists, so this audit does not establish its reported speed
advantage or identify a single regression as causal.

## Memory and ownership

All enabled synthesis callbacks, worker callbacks, audio ISR and nested
TIMER5/input/UART/audio-endpoint paths are resident in executable SRAM.
`usb_poll` is deliberately outside this graph: TIMER5 calls it only when audio
is not running. The resident build verifies linked addresses and far-call
boundaries. Tables that already support hot sample work remain in SRAM.

The short worker descriptors occupy scratch space after existing retained
records. They are fully published before read and never treated as retained
music. Existing `.noinit` records stay ahead of them. CZ/FM6/SID/808 caches live
with their engine state and are released with it. No persisted layout changes.

The complete resident graph costs additional SRAM: the linker now reserves
at least 48 KiB of contiguous on-demand arena, with separately checked SRAM
tails added to capacity. The four largest engine states must fit that floor.
Delay-based FX still consume on-demand memory; supported FX limits must satisfy
both the performance and resource-allocation gates. Full UI/FX functionality
remains available, but unlimited simultaneous effects are not a contract.
There is no intentional engine precision/fidelity reduction. Production image
is 497076 B; data+bss 94808/98304 B; pool 185880/344064 B; contiguous on-demand
arena 53952 B and total audio resources 60508 B in four SRAM banks. The build
verified 323 linked resident functions. Retained music addresses do not move.

## Profiling and reproducibility

Build: `MELODEE_RAM_AUDIO=1 MELODEE_AUDIO_PROFILE=1 MELODEE_AUDIO_BENCH=1
PYTHON=build/venv/bin/python ./build.sh --release 1.3.1-perf`.
These laboratory switches are opt-in. Profiling disabled compiles out all scope
measurements. The fixed-voice benchmark bypasses admission and suppresses
emergency fades for 30 seconds, counting every requested fade instead; the tool
refreshes this lease and turns it off in `finally`. Production contract checks
must also run with ordinary admission and shedding enabled.

Editor 98 returns schema 1: inclusive ticks, calls and maxima for complete C
half-buffer processing, nested TIMER5/input/UART/USB, events, allocation, attacks,
envelopes, modulation, track controls, each engine, mixing, FX and output. Worker
job/wait totals accompany these scopes. FX covers inserts, shared preparation
and the worker kernel; output includes master processing and USB/output copy. Scopes are nested and must not be summed.
Worker engine durations are published privately and merged by CPU0 after joining.

Editor 72 flag 32 adds schema 7 complete timing, deadline/admission/steal counters
and fixed-benchmark status; existing flags/schema layouts remain compatible.
`tools/usb_audio_stats.py --complete --voices --cores --memory --parts` reads it.
The assembly envelope captures C entry/exit and nested service. The reported
maximum adds one worst observed TIMER5 span plus 4 us for the short timestamp,
save/restore/return envelope. This is a conservative timing bound, not a claim
that every assembly instruction or physical MIDI latency was directly measured;
its allowance requires nested service to remain bounded below one 100-us tick.

Production editor 100 reports clock, maximum TIMER5/assembly ticks, compiled
profiling/benchmark/admission flags, half size and four active reservations.
Every final production row verifies 360 MHz, 128 frames, admission on, both
laboratory features off, matching quotas and TIMER5 below 2400 ticks.

Host: `tests/run_tests.sh` includes existing correctness/numerical/golden, USB,
resource, tuning, event and engine checks plus `audio_matrix_test`: all 1296
ordered assignments of Prophet/FM6/CZ/SID/808/909, fixed held voices, releases,
retriggers, modulation, controls, spread and shared Chorus, compared between
serial and a genuinely concurrent worker. Host time does not establish a device
deadline. The six-category matrix contains both five-family 625-assignment sets.

Device: `build/venv/bin/python tools/audio_performance_device.py --backup
build/ui-load-comparison/original-backup --output build/audio-performance-matrix`.
The helper requires the existing backup, edits live musical runtime, writes fresh
result artifacts, restores runtime/settings and verifies stored CRCs in `finally`.
Existing kept-global autosave can write settings during stopped configuration;
the original settings are restored from the existing artifact if that occurs. It
never calls backup creation. JSONL contains detailed scopes/counters, CSV the
combination matrix, and `restoration.json` the final state verification.

Final validation must also establish adversarial patch/algorithm/note/control,
attack/release/retrigger, supported FX and active USB-streaming bounds; produce
recordings; and distinguish instrument/physical listening from numerical and
recording evidence. The matrix alone does not establish those additional claims.

## Supported workload boundaries

The measured contract uses four warm, stable engine assignments, healthy CPU1,
360 MHz and the reservations in the table. Native program, engine, algorithm,
filter/chip model and KIT changes are configured stopped, followed by all-sound-off (or waiting for release tails to finish);
those cold transitions remain functional but are outside the timing guarantee.
POLY, MONO, LEGATO and UNISON share the same slot reservations. Spread remains
available and has host matrix coverage; final production mode checks add it on
all four tracks. The full matrix uses track LEVEL 35. Maximum LEVEL 127 has additional
production corner and sustained worst-assignment coverage below, rather than
a second exhaustive matrix of gain settings. The listening recording and
higher-level extension used the physical MASTER knob at about halfway.

Device matrix: all ordered six-category assignments, attacks at the full reserved
slot count, held notes, drum retriggers, four modulation slots per track (LFO to
cutoff, ENV to pitch, velocity to shape, key to amplitude, amount 24), stereo USB
capture and playback at 44.1 kHz/PCM24, and one shared Chorus in CHAIN mode with
full sends, MIX/RATE/CHAR/RETURN 127. Track inserts and other shared slots are off.
The timing matrix samples every assignment for at least .24 s, spanning at least
one 75-half-buffer TIMER5 phase cycle; each final burst renders before counters
are sampled. Matrix FM6 retains the existing runtime's MARK I function setting.

The expanded patch checks use both Prophet filter revisions, native sync and
Poly-Mod/full wheel/all LFO waves/extreme cutoff and notes; all 32 FM6 algorithms
in MODERN/MARK I/OPL with feedback 7; all eight CZ waves/windows with dual lines,
actual ring/noise bits, eight-stage envelopes and continuously active vibrato;
both SID chip revisions, combined/noise/sync/ring waveforms and filters; and both
drum models' metal/tom/crash, choke and retrigger lanes. Four-track controller
bursts send one pitch bend plus CC1 per track every 80 ms; releases/retriggers
repeat every other burst. For the tested maximum of eight slots this is at most
24 messages per burst (drum auto-retriggers included), not an unqualified
64-message worst-case guarantee. The MIDI drain bound is independently 64 per
control block; excess remains ordered for the next block.

More FX, SPLIT routing, four-track USB capture, hotter native-engine macro or
algorithm automation, faster attack/controller storms, clocks other than 360 MHz,
CPU1 fallback and legacy extra engine identities retain functionality but are
outside this measured contract. Host tests cover retained FX/routing, native
formats, event ordering and worker fallback. Finite device sampling establishes
observed bounds for these workloads, not a formal proof for arbitrary patches,
external traffic or every hardware/temperature condition.

## Consolidated validation evidence

The final host suite ended `ALL HOST TESTS PASSED in 53 s` in
`build/audio-optimization-native-final-validation.log`. There were 335 unchanged
golden renders, zero numerical/health/voice-routing failures and zero CPU budget
failures. Concurrent and serial PCM matched in 1536 FM6 plus 348 Prophet cases,
and in all 1296 six-family mixer assignments with controls, releases, retriggers,
spread and shared Chorus (hash `965b58c26881ac58`). Admission checked all 1296
rows with independent reservations, releases, excess-note replacement and two
overlapping drum circuits. Worker fault tests reject invalid descriptors and
never replay partially advanced state. The four largest engine states use
29424 B; the 48 KiB contiguous floor also leaves room for the supported Chorus.
Production target instruction/residency checks passed independently.

The final instrumented build `1.3.1-perf8` recorded 568 native corner cases in
`build/audio-performance-native-corners/cases.jsonl` and
`build/audio-performance-native-corners-rest/cases.jsonl`. All recorded zero
DMA/elapsed deadline misses, emergency/cross-track cuts, worker/resource failures
and USB errors. The worst conservative bound was 2641 us (909 tom/crash); Prophet
reached 2531 us and CZ 2269 us. 555/568 fit the initial 85% screening bound and
537/568 the 80% target. These numbers include profiling overhead and are not
misrepresented as universal 80% passes. Maximum TIMER5 was 1354 ticks, safely
below the required 2400-tick interval. Cold native patches are configured before
resetting the measured maxima; their sample/control/envelope work and subsequent
simultaneous onsets/retriggers are measured.

The first listening capture had almost-zero MASTER level and only a handful of
PCM levels; amplifying it exposed quantization distortion. It also recorded USB
and DMA errors and is preserved as failed evidence. After MASTER was set halfway,
`build/audio-performance-production-audition` recorded a fresh mixed-engine clip
with zero misses, emergency cuts, worker/resource or USB errors (1536 us complete
upper bound). The user confirmed the new level-adjusted playback clip sounds
clean. Its original PCM24 and 16-bit playback copies are preserved. This confirms
that recording's listening result; physical analogue output/control audition is
separate and has not been inferred from it.


Final production matrix (`1.3.1-audio3`): all 1296 assignments preserved their
reserved 4–8 sounding slots with zero DMA/elapsed misses, emergency/cross-track
cuts, worker/resource failures and USB errors. 1259/1296 met the initial 80%
target. The worst complete conservative bound was 2581 us (11.04% headroom).
There were two retried read-only diagnostics; both are counted in the report.
The matrix was split after row 152 to produce the corrected listening clip;
rows 153–1295 resumed at the exact next assignment. Both parts restored all
27 persistent CRCs and original runtime CRC 1680630693. The combined CSV and
machine-readable summary are in `build/audio-performance-final-report`.

Sustained extension: the ten slowest matrix assignments ran for 10 s at track
LEVEL 127 with rapid controllers/releases/retriggers, USB and Chorus. One first
window overlapped the likely delayed kept-global settings write (CLOCK/ROUTE changes save
after 1500 ms), with 7 DMA misses and USB errors despite a sub-deadline interrupt
body. This failed evidence is retained. Stopped setup now allows deferred
persistence and USB negotiation to settle before measurement. Both slowest
assignments were then rerun for 30 s each with zero misses, emergency cuts or
USB errors, preserving all eight slots; the worst upper bound was 2767 us,
leaving 134.33 us (4.63%). The other nine original 10-s windows were clean.
This is an explicit limit on the claim: 80% headroom is not universal, and
flash writes/USB opening/global configuration are outside the steady-workload
windows. Deliberate flash operations retain existing audio pause/worker-join
safety. The helper does not hide these failed runs or classify DMA misses as
normal admission steals.

Production package `build/melodee-1.3.1-audio3.fwsc` is 609877 B, SHA256
`403502e6658ac55299b2f79208d79f213afb58610f14270fb6afc8d3335f7732`.
It was installed locally and verified as FM-1_91031. Base remains `next` /
`7025143caeabde756ba1099c317f36c3798523c3`, with uncommitted source changes;
there is no new release publication or clean commit identity implied by the
local package version. Existing original runtime/settings files still match
SHA256 `fc1bc9079c5fa665854dd1be72d95eef57454657c6269fdacf1dfb260c65fc9b`
and `e768a607359716357ba36509c7f90b1889b93eeeec3b382a93cce0b341df113d`.
The integer worktree retains its 599/441-line diff; the buffer worktree its
3/3-line diff. No backup was created or refreshed.

## Reproduction

Use existing original/stress state artifacts, fresh output directories and a
connected 360-MHz FM-1. Install only the exact validated package. Commands below
perform live setup and restore the existing state in `finally`; they create
result recordings/reports, never backups.

```sh
MELODEE_RAM_AUDIO=1 MELODEE_ADMISSION=1 PYTHON=build/venv/bin/python ./build.sh --release 1.3.1-audio3
PATH="$PWD/build/venv/bin:$PATH" tests/run_tests.sh
build/venv/bin/python tools/audio_performance_device.py --backup build/ui-load-comparison/original-backup --output build/NEW-matrix --contract tools/audio_voice_contract.json --normal-admission --production --usb --usb-format int24 --modulation --fx --seconds .24
build/venv/bin/python tools/audio_performance_adversarial.py --backup build/ui-load-comparison/original-backup --output build/NEW-corners --production --level 127
build/venv/bin/python tools/audio_performance_finish.py --backup build/ui-load-comparison/original-backup --stress build/ui-load-comparison/stress-backup --output build/NEW-comparison --seconds 60
```

For an instrumented build add `MELODEE_AUDIO_PROFILE=1 MELODEE_AUDIO_BENCH=1` and
use the laboratory helpers without `--production`; the normal-admission run
still leaves fixed-voice bypass off. Fixed-capacity screening can omit
`--normal-admission` to expose overload without reducing its own workload.
The conservative IRQ bound and health predicates, not a passing average CPU
meter or a short-looking C body alone, determine the reported outcome.


The affected production extension in `build/audio-performance-production-corners`
completed 200 cases at LEVEL 127: all 96 FM6 algorithm/mode combinations loaded
through native RAM-only function fixtures, all 16 Prophet cases, 16 slowest CZ
cases, all 48 SID cases, 12 drum pairs and 12 MONO/LEGATO/UNISON cases with
SPREAD 42. No modified runtime fixture/backup file is written: the existing
FBKK bytes are modified in memory, checksummed and sent to runtime object 0.
All 200 recorded zero DMA/elapsed misses, emergency/cross-track cuts,
worker/resource failures and USB errors. The worst complete bound was 2582 us
(Prophet UNISON). There were 196/200 within the 85% screening bound; MODE/one-shot
cases may intentionally sound fewer notes than their reserved maximum. The
original 568 instrumented cases cover the complete CZ wave/window/ring/noise
cross-product; the production subset repeats its most expensive measured cases.
All final runs verify 27 saved objects and original runtime CRC on exit.


Final same-project comparison: `build/audio-performance-production-comparison`
ran the existing 64-step 180-BPM stress runtime for 60.018 s with stereo USB
playback/capture active, original patches/sequence data and FX off. Admission
held five sounding slots throughout (1 Prophet, 1 FM6, 1 CZ, 2 drum). All DMA/
elapsed misses, emergency/cross-track cuts, worker/resource and USB errors were
zero. Maximum render without nested service was 1500 us; conservative complete
maximum was 1824 us. 7746 occupied-slot replacements were ordinary admission/
retrigger decisions, not emergency CPU shedding. Compared with the existing
FP32 baseline: 39.703 DMA misses/s -> 0, 68.201 emergency cuts/s -> 0, and
3556 -> 1500 us render maximum without nested service. This comparison combines
code improvements with the explicit five-slot cap; the old run sounded 4–11
voices while shedding continuously, so it does not isolate a per-voice speedup
or preserve all chord voices. The matrix/fixed benchmarks supply the separate
capacity evidence. No complete 0.13 comparison was invented.

The final run also recorded another musical clip (1554 us complete upper bound,
all deadline/worker/resource/USB counters zero), restored the existing settings
and runtime, and verified all 27 persistent CRCs plus original runtime CRC
1680630693. Post-run identity is `MELODEE v1.3.1-audio3`, FM-1_91031 running,
360 MHz, CPU1 online, profiling/benchmark support off, admission on, 128-frame
halves and 60508 B audio resource capacity. No new backup, commit, push, tag or
publication occurred. The final report retains the failed startup/persistence
window as a distinct failure and requires zero errors in the accepted rechecks.
