# SLOOP master punch FX

Melodee's master effects adapt SLOOP's GPL-3.0-only `firmware/src/punch.c`
from [isod89/sloop-fm1 at 4ec1ac8](https://github.com/isod89/sloop-fm1/blob/4ec1ac829fd2bdf2c571ea215fb3d62477d01220/firmware/src/punch.c).
Source history identifies isod89's original punch implementation in `a3f9fd0`
and its 27-effect expansion in `d30d655`. The retained band-kill SVF equations
come from Felucca DSP by Leo Kuroshita. These notices are preserved in the port.
Melodee integration, ownership, arena handling, UI and reverb changes are
Copyright (C) 2026 Kerem Kilic (Ellic Studio). A shared licence or API was not
used as evidence of authorship.

## Controls

Tap FX: track rack; on SONG, the section's saved punches using the same visual selector.
Hold FX: silent effect keys. Double tap: lock the layer;
tap again: unlock. Last pressed key wins; its release fades dry in 64 samples.
A previously held musical key remains a musical key. Closing the layer while
an effect key is still down leaves that effect active until its key releases.
Stop, project changes and modal cancellation end a request.

| White key | Effect | Black key | Effect |
| --- | --- | --- | --- |
| F3 | LOOP 1/4 | F#3 | KILL LOW |
| G3 | LOOP 1/8 | G#3 | KILL MID |
| A3 | LOOP 1/16 | A#3 | KILL HIGH |
| B3 | LOOP 1/32 | C#4 | REWIND |
| C4 | STUTTER | D#4 | OCT UP |
| D4 | REVERSE | F#4 | FLANGER |
| E4 | STOP | G#4 | AUTO-PAN |
| F4 | HALF | A#4 | FREEZE |
| G4 | LOW PASS | C#5 | LOOP 1/8T |
| A4 | HIGH PASS | D#5 | MONO |
| B4 | PHONE | F#5 | FADE OUT |
| C5 | CRUSH | | |
| D5 | ALIAS | | |
| E5 | GATE | | |
| F5 | ECHO | | |
| G5 | WOBBLE | | |

## Audio and memory

The stereo master sum, including shared returns, enters punch DSP before the
DC blocker and limiter. Master volume attenuates their finished output, so held
loops, freeze and other sounding tails follow the volume knob, including silence
at zero. Stereo USB capture receives that master;
USB stems remain individual tracks. Buffered SLOOP effects intentionally use a
mono PCM16 history, as upstream; filters/kills retain independent stereo state.
Loops/gate align to local or externally clocked transport phase, and loop
regions are protected from being overwritten while held.

The 16,384-sample history (32 KiB) is optional working memory acquired when
FX is armed, not a permanent reservation taken from voices at startup. It
captures from layer entry: hold FX briefly before reverse/rewind to give them
past audio. It is released when the layer, effect keys and wet fade have ended.
Under memory pressure, buffered effects bypass and show FX MEMORY; unbuffered
effects keep working. Allocation is attempted once per layer session, with
normal retry on reopen. Existing engine allocations are never stolen. Several
large reverb slots and the punch history may exceed available SRAM.

FREEZE uses SLOOP's four-line stereo reverb. The same optional history
allocation is reused for its tank: 8,192 samples (186 ms) of master audio feed
the diffusers, then the unmodulated feedback network holds at unity gain,
without damping. It needs no rack reverb and no second punch allocation.

The rack reverb is extracted from SLOOP fx.c at the same source revision.
History identifies `f2b44c2` (isod89) as the stereo Hadamard network and input
allpass update, and `d30d655` as the freeze extension. The original Felucca
input-diffuser work retains Leo Kuroshita's copyright. Rack/arena, input
headroom, wet-only return and bounded arithmetic adaptations are Melodee work.
The native-rate tank uses 19,396 bytes, replacing the removed Echoverb kernel.
Effect ID 8 selects REVERB, with SIZE/DAMP/TONE controls; other IDs stay fixed.

SLOOP insert processors from `d30d655` (after Felucca 1.5, Discussions #78/#177)
are ported in fx_sloop_insert.h, retaining their actual third-party notices.
The new effect IDs append at 12–19. A compact per-channel state is always
allocated; the 256-sample delay per side is present only for flanger/chorus.
Controls and wet/dry fades stay in Melodee's racks. Swept auxiliary returns
remove the original pedal's dry half; insert pedals retain it. Drum/synth
stereo is preserved. Existing PHASER and CHORUS are not silently replaced.

## Validation

`tests/punch_master_test.c` covers every effect's audio, long-held captured
loops, effect transitions, exact dry bypass, key ownership, transport phase,
memory rejection, freeze sustain and the completed stereo master/return sum.
`tests/sloop_fx_test.c` checks stereo reverb decay and controls, all eight
insert processors, mono/stereo transitions, wet-only returns and rack storage. UI renders cover both
fonts, crowded recording headers, lock, memory status and SLOOP Reverb and inserts.
Physical musical feel, output sound and stressed device deadlines require
hardware listening/measurement; builds and simulated audio alone do not prove them.

## Compact history budget

The master history now uses 16,384 mono samples (32 KiB, 372 ms at 44.1 kHz),
down from 32,768 samples (64 KiB). This recovers 32 KiB while PUNCH is armed;
it does not change idle RAM. Replay spans clamp to 15,360 samples (348 ms)
and echo delays to 16,320 samples (370 ms). Long beat divisions therefore use
a shorter captured span at slower tempos. Shorter divisions, stereo filters,
FREEZE's four-line layout and the 64-sample transition remain unchanged.
