# Recording playback cursor experiment

Branch: `experiment/recording-playback-cursor`, based on nightly `1139a6f`.

The subsequent [release queue experiment](RECORDING_RELEASE_QUEUE_EXPERIMENT.md)
removes sounding-note countdown scans; the measurements below describe this
original cursor implementation.

This experiment keeps Melodee's original recording behavior. Every note is
captured independently, including repeated hits within a step, individual
velocities, overlapping chord releases and free timing. QNT still commits only
the new take's onsets when recording ends. QNT OFF retains the captured timing.
Jam capture, arpeggiator recording, recording latch/pass behavior, undo and the
normal project format remain available. There is no project quantization lock.

The SLOOP step-nudge experiment remains on its separate branch. Its special
nudge saves require that branch; this experiment uses the ordinary project
format and does not migrate those experimental saves.

## Playback changes

The bank's existing linked event index is ordered by captured onset. A cursor
consumes events as they become due, instead of examining every retained event
on every control tick. Due events are dispatched in onset order; equal onsets
keep the existing list order. Events keep their original storage indices.

A separate bounded list visits only notes that are being held during capture
or are currently sounding. Existing per-pitch reference counts still prevent
one overlapping event from releasing another event's note.

Capturing/editing a note inserts one onset into an already ordered index.
Changes to bank or timing rebuild it with an iterative stable merge sort,
without event copies, a scratch array, recursion or a new audio allocation.
Deleting/reusing an event invalidates the affected cursor. Loop zero resets
the onset cursor and heard flags, while sounding notes may continue across it.

The playback cache holds captured onsets throughout a take. Quantization
targets are calculated at punch-out, preflighted using the existing occupied
step rules, then committed. A successful timing change invalidates the index.

Work proportional to the full bank remains at loop reset, timing/index rebuilds
and step-overview dispatch. Capture allocation still searches the bounded pool.
The steady tick visits active notes plus newly due onsets. A sustained phrase
with nearly every event active therefore benefits much less than short notes.

## Memory and storage

The added runtime list occupies 2,048 bytes in the existing pool, plus small
cursor/head bookkeeping in ordinary RAM. The target build reports:

- data/BSS: 95,808 of 98,304 bytes;
- pool: 167,000 of 344,064 bytes;
- on-demand audio arena: 164,832 bytes, above the 142,336-byte minimum.

This is a CPU experiment. It does not reduce RAM or flash storage. The normal
1,024-event limit, eight-byte serialized events and project allocation remain
unchanged. No SLOOP metadata, new save version, voltage or clock changes are used.

## Measurements

Host benchmark: `tests/recording_bench.c`, compiled with `cc -O2`, on
2026-10-16. The baseline was the **unmodified `1139a6f` firmware/test sources**
with this benchmark copied in and `MELODEE_RECORD_CURSOR=0` defined for its
output label. The new variant uses this branch's default cursor implementation.
Both use the same generated tables and capture the same 64-step phrase, then
measure 50,000 control ticks after warmup. Synthesis rendering is excluded.

QNT OFF, original captured short gates:

| Tracks | Pitches per step | Events, both variants | Original instructions/tick | Cursor instructions/tick | Reduction |
| --- | --- | --- | --- | --- | --- |
| 1 | 1 | 64 | 2,280 | 1,414 | 38.0% |
| 1 | 4 | 256 | 4,968 | 1,485 | 70.1% |
| 4 | 1 | 256 | 5,495 | 2,060 | 62.5% |
| 4 | 4 | 1,024 | 16,466 | 2,559 | 84.5% |

QNT 1/16 gives similar reductions (37.9–84.4%). Capture instructions per note
stay within about 4% of the original for QNT OFF and decrease about 5–10% for
the quantized fixture. Each stored event array hashes identically to the
original after capture/commit in all eight comparisons.

The `sustained` benchmark argument extends event durations to nearly two loops.
All eight sustained event arrays also match the original. Instruction savings
are only 1.7–2.9% there; four tracks of four pitches use approximately
43,928 → 42,664 instructions/tick with QNT OFF.

These are **host sequencer instruction counts**, not overall FM-1 CPU savings.
Physical audio, external-clock feel and real device load remain unverified.

For a quick same-branch scanner/cursor comparison:

```sh
cc -O2 -w -Ibuild/gen -Ifirmware/src -DMELODEE_RECORD_CURSOR=0 tests/recording_bench.c -lm -o build/host/recording-bench-scanner
cc -O2 -w -Ibuild/gen -Ifirmware/src tests/recording_bench.c -lm -o build/host/recording-bench-cursor
build/host/recording-bench-scanner
build/host/recording-bench-cursor
build/host/recording-bench-scanner sustained
build/host/recording-bench-cursor sustained
```

The same-branch scanner includes the new raw-onset cache/quantization setup,
so it is a closer performance comparator than the untouched nightly baseline.

## Validation

Firmware build and full host suite passed: `ALL HOST TESTS PASSED in 80 s`.
The existing recording, MIDI timing, note editing, undo, storage and sound
tests use the cursor by default. No legacy recording fixtures are redirected
to the scanner to accommodate behavior changes.

`recording_scheduler_test` checks runtime state against the original scan's
transition rules through unsorted 64/1,024-event banks, loop wrap, swing,
pending quantization, held notes, large batches, manual edits, reindexing,
cross-track slot reuse and STOP. The same oracle also passes against the
reference scanner. AddressSanitizer/UndefinedBehaviorSanitizer pass for these
scheduler cases.

No firmware installation is part of this experiment's implementation task.
