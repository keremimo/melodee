# Timestamped recording releases

Branch: `experiment/recording-release-queue`, based on the tested cursor
experiment `e8754ca`. This is a further performance experiment, not a new
project format. Free timing, separate velocities and releases, repeated hits,
QNT at punch-out, undo, editing and the 1,024-event limit are retained.

## Playback

The ordered onset cursor remains. Sounding notes now have absolute release
deadlines in an indexed minimum heap for each track, rather than a countdown
visited on every control tick. The next due release is checked directly;
inserting, cancelling or retriggering a gate takes O(log N) work. Retriggering
replaces the indexed entry, so a previous loop's release cannot end a new gate.
Release deadlines may cross loop zero and unsigned clock rollover. Existing
per-pitch reference counts protect overlapping notes of the same pitch.

The internal release message uses seven bits for pitch and bit 7 for a pending
note-off. The bit is masked before dispatch; editable note pitches and saved
notes remain ordinary MIDI pitches. This flag does not encode a gate length
or reduce saved project size by itself.

Only notes currently held during capture retain a per-tick duration update.
The playback deadline clock advances once per track. Existing internal versus
external clock units, gate rounding and block dispatch order are preserved:
old gates expire before newly due onsets in the same block. Gate deadlines
are fixed at firing, as the previous countdown was, while new onsets/gates use
updated tempo/division settings.

Live editing requires a small extra check: only overview cells containing
sounding recordings are inspected (at most 64 per track). Overwriting a cell
or changing LEN cancels buried releases immediately. Timing/overview rebuilds
refresh this ownership index. This avoids a full sounding-event scan in the
steady tick, while preserving edit behavior. Bank/index rebuilds, full-store
capture allocation, loop heard-flag resets and the existing once-per-step
recording ownership scan remain; this is not a claim of zero recording CPU.

There is no new audio allocation or copied upstream DAW code. The design uses
the same general ordered-onset/pending-release approach as open-source DAWs,
implemented independently for Melodee's bounded C event store.

## Memory

The queue trades RAM for CPU. Relative to `e8754ca`, it adds 11,776 bytes in
the pool (fixed heaps, packed positions, messages and overview counts), plus
80 bytes of data/BSS bookkeeping. Each track can own all 1,024 shared slots;
heap capacity does not restrict the existing per-track event capacity.

Target build:

- data/BSS: 95,888 of 98,304 bytes;
- pool: 178,776 of 344,064 bytes;
- audio arena: 153,056 bytes, above the required 142,336-byte minimum.

## Host performance

`tests/recording_bench.c`, `cc -O2`, 2026-10-16. The comparator uses the
unmodified `e8754ca` firmware/test sources with the updated benchmark copied
in, compiled against the same generated tables. Capture a 64-step phrase,
warm up 1,024 control ticks, then measure 50,000 ticks. Synthesis rendering is
excluded. Counts cover the whole sequencer advance, not just heap operations.

QNT OFF, extended gates of nearly two loops:

| Tracks | Pitches per step | Events | Tested cursor instructions/tick | Release queue instructions/tick | Reduction |
| --- | --- | --- | --- | --- | --- |
| 1 | 1 | 64 | 3,926 | 2,165 | 44.9% |
| 1 | 4 | 256 | 11,526 | 2,222 | 80.7% |
| 4 | 1 | 256 | 12,100 | 5,122 | 57.7% |
| 4 | 4 | 1,024 | 42,667 | 5,528 | 87.0% |

QNT 1/16 is similar. With the original short captured gates, the new scheduler
is roughly unchanged (about 1% lower to 2% higher instructions/tick), retaining
the previous cursor's large advantage over the original full scanner. Capture
is not accelerated: these fixtures measure about 2–12% more instructions per
note than the preceding branch, including the first cold capture fixture.
Every complete canonical recording array hashes identically in all 16
short/sustained, free/quantized comparisons.

These are host sequencer measurements, not overall FM-1 CPU percentages.
Physical audio, MIDI feel and hardware load still require device testing.

## Storage measurements only

`tools/recording_storage_probe.py` is a host-only lossless codec prototype. It
uses bank counts, exact signed onset deltas, seven-bit pitch with an optional attribute
flag, changed velocity/duration values, and nondefault rounding/drum attributes.
It encodes canonical notes, not duplicate note-on/note-off playback streams.
Neither the firmware loader nor saver calls it.

Measurements use captured benchmark fixtures, not a sample of user projects:

| QNT OFF fixture | Events | Existing occupied recording bytes | Prototype bytes | Reduction |
| --- | --- | --- | --- | --- |
| 1 track, 1 pitch/step | 64 | 556 | 297 | 46.6% |
| 1 track, 4 pitches/step | 256 | 2,092 | 1,192 | 43.0% |
| 4 tracks, 1 pitch/step | 256 | 2,092 | 1,080 | 48.4% |
| 4 tracks, 4 pitches/step | 1,024 | 8,236 | 4,660 | 43.4% |

The existing figures include the 44-byte bank directory; the prototype includes
its own header/counts. QNT 1/16 gives 43.1–48.8% reduction. All decoded note
fields match exactly, including exponent, original timing, rounding, pitch and
drum articulation. Events retain their existing within-bank order, including equal-time notes;
unused canonical slot indices are not persisted by either format.

Compression is content-dependent. A seeded randomized 1,024-event example
(seed 713, independently randomized onset/duration/pitch/velocity/owner/drum
attributes) expands to 10,816 bytes versus 8,236. Any future save format should
choose the smaller encoding and have a versioned bounded decoder.

Current projects reserve **8,236 bytes for recordings regardless of use**.
Those bytes, project slot allocation and the project format are unchanged in
this experiment. Real capacity gains require a separate storage-layout change;
the percentages above are not reductions in whole project size.

Reproduce:

```sh
cc -O2 -w -Ibuild/gen -Ifirmware/src tests/recording_bench.c -lm -o build/host/recording-bench-queue
mkdir -p build/recording-storage-fixtures
build/host/recording-bench-queue short build/recording-storage-fixtures
build/host/recording-bench-queue sustained build/recording-storage-fixtures
python3 tools/recording_storage_probe.py build/recording-storage-fixtures
```

`MELODEE_RECORD_QUEUE=0` retains the previous countdown cursor for direct
same-source comparisons; `MELODEE_RECORD_CURSOR=0` selects the reference scanner.

## Verification

The firmware build and complete host suite passed (see the final result
below): `ALL HOST TESTS PASSED in 79 s`. The suite includes the release queue, countdown and scanner
oracles. The oracle compares held time, remaining gates, flags and pitch
ownership with the original scan transitions through 64/1,024 events, long
gates, loops, swing, tempo/division changes, quantization settings, edits,
large clock batches, reindexing, slot reuse and STOP. Separate queue cases
cover pitch 0/127 tags, deadline zero, rollover, replacement deadlines, dense
cancellation, LEN edits, moved overview ownership and independent tracks.
AddressSanitizer/UndefinedBehaviorSanitizer cover both sets of scheduler cases.
The storage probe roundtrips capture fixtures and randomized/boundary data,
and rejects truncation/malformed input. Existing storage tests still exercise
the unchanged actual project format.

No flash is performed as part of implementing this branch.
