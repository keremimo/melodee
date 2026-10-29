# Recording and performance controls

REC taps arm or disarm the selected track. They leave the transport alone.
PLAY starts/stops playback. A track armed before PLAY records from the first
step. While stopped and armed, playing the first key or MIDI note also starts
playback and recording immediately at step zero, skipping the configured
count-in. The triggering note is included in the take. PLAY still uses count-in
normally, including its held-note pickup behavior.
Arming while playing waits for that track's next pattern start; REC
breathes while waiting and stays lit during recording. Keys and MIDI audition
while waiting, and notes still held at the start enter the take there.
Explicit REC arming stays active across pattern loops, including notes held
across loop zero. Tap REC again to finish the take, or tap it while waiting to
cancel. STOP finishes the take and leaves the track armed for the next start.
On **SEQ > PATTERNS** (hold SEQ to enter), unarmed keys select tracks and
patterns without sounding. Selecting an empty pattern, or double-tapping an
occupied one, arms recording and immediately restores normal playing. Keys
stay playable while stopped, during count-in, while waiting for the loop and
through the take. Disarming returns them to track/pattern selection. SELECT
still navigates to SONG; holding SEQ gives the selection shortcuts on other pages.
Pattern-key shortcuts arm one complete pattern pass, then
automatically disarm at its end while playback continues. Notes still held have
their recorded lengths finished at that boundary; their live sound continues
until release. Selecting an empty pattern preserves existing explicit REC
arming. Notes and knob motion share these boundaries, with either internal or
external MIDI clock and any QNT.

Hold EDIT to undo the latest recording take in the selected pattern. Hold EDIT
and tap OCT+ to redo it (EDIT + OCT- also undoes). One slot preserves the whole
pattern's notes and motion together, including overwritten events and exact
note timing, velocity and duration. The next nonempty take replaces that slot;
arming and cancelling without recording leaves it available. Finish or disarm
the take before undoing; playback can continue. Other tracks and banks stay
unchanged. Changing patterns or loading a project invalidates the slot. A
subsequent edit must be undone first, or the take undo becomes unavailable.
If working memory is exhausted, the device shows **NO RECORD UNDO MEMORY**;
recording and audio take priority over keeping this snapshot.

**SEQ > TEMPO > QNT** is one project-wide recording grid, shared by all audio
and MIDI tracks and saved with the project or template. It chooses an automatic
quantization pass when recording finishes, including an early STOP or REC disarm.
OFF keeps the captured timing; a division snaps only the new take's onsets to
that swung grid. Independent note lengths, velocities and drum properties stay intact, and the piano roll
shows the committed timing. Changing QNT later affects the next take only.
An occupied manual step prevents the pass: the take is retained unchanged and
the device shows **QNT STEP OCCUPIED**. Existing recorded notes and other banks
are preserved. Older projects with different track QNT values use the selected
track's value for the whole project when loaded.
REC + PLAY explicitly arms and starts in one gesture (or waits for the next
pattern start if already playing), keeping recording active across loops.
Holding REC opens the mixer.

Take Taker pauses collection on **all tracks** while any track is actively
recording. It continues collecting while a take waits for its loop boundary,
then resumes as soon as the last active take finishes or is disarmed. A
latched take keeps it paused across loops. Notes captured before the pause
still retain their actual release lengths. Applying a Take Taker capture is
also refused while any track records; saved patterns continue playing.

Hold HOME to open the menu. The following device preferences also appear in
the web editor's settings and survive power-off:

- **AUDIO CLICK:** OFF, REC (while a track is armed and playback runs), or ON.
- **CLICK LEVEL:** 0..100 % (KNOB 1 steps 5 %), its own level apart from MASTER and the mixer; 100 % peaks at
  full scale. The first beat of each four-beat bar is accented.
  This sets both metronome and count-in volume relative to mix MASTER;
  MASTER at zero silences both. The click goes to headphones/line out and
  speaker, after effects and USB capture, so recordings contain no click.
- **COUNT-IN:** OFF, 1 BAR, 2 BARS. Starting an armed track with internal clock
  counts four or eight beats before the first step. Count-in clicks sound even
  with AUDIO CLICK off. PLAY cancels it. External clock and song-chain starts
  follow their existing transport timing without a count-in. Notes pressed in
  the final half-beat and still held at the downbeat enter step one without
  retriggering their live sound; earlier presses are for monitoring only.
- **NOTE PREVIEW:** OFF/ON. Moving the stopped STEP/NOTES cursor auditions its
  note or drum hits. NOTES selects an individual note; STEP previews a chord.
  Preview lasts about 167 ms, does not record or transmit MIDI, and respects
  notes already held by the player or arpeggiator.
- **CHORD ENTRY:** HOLD keeps the existing simultaneous-key entry and automatic
  advance. ADD lets separate taps add/remove pitches at the same cursor.
  Up to four pitches fit a polyphonic step; a full chord shows CHORD FULL.
  Mono voice modes keep one note. Cursor movement and length editing remain
  available; each completed tap can be undone.

DRUM has **LANES** and **LANES2** pages in EDIT, with individual KICK, SNARE,
CLAP, HATCL, HATOP, TOM, RIM and BELL levels. Zero silences a lane and 100%
retains its original level. These levels save in sounds, projects, templates
and backups and can be recorded as motion.

Hold **LFO** while turning sound/effect knobs to make temporary changes.
Release LFO to restore the values; press **OCT+** while holding it to keep them.
This works on HOME, sound pages, effect settings and native FM6/CZ-1/PROPHET
pages. Temporary changes are excluded from motion recording. Changing tracks,
loading/saving a sound/project, opening the menu, or receiving an editor sound
edit restores temporary values first. Navigation, storage actions and shared
scale settings keep their ordinary behavior.

USB and TRS MIDI share these controls, using the existing channel routing:

| CC | Control |
| --- | --- |
| 5 | Glide (FM6 also retains its native portamento-time mapping) |
| 7 | Track level |
| 10 | Pan; 64 is centre |
| 71 | Resonance, or Q where the engine exposes it |
| 72 / 73 / 75 | Release / attack / decay |
| 74 | Engine cutoff or brightness |

Cutoff/brightness uses PHASE DCW, SID CUT, DRUM TONE, NOISE FREQ,
FM6 MLVL and PROPHET CUT. CZ-1 uses its filter-envelope amount. Engines without
a resonance control ignore CC71. Native FM6 envelope messages change all six
operators' corresponding rates. CZ-1 changes both amplitude envelopes' first,
second or END rate; PROPHET changes its amplitude envelope. Those native edits
save with the native sound, as panel edits do; common controls and engine
macros record as motion when the selected track is recording. Existing wheel,
expression, sustain, bend, RPN and panic controls remain available.

New data uses 100 parameters (engine parameters start at 92), FUN15 projects,
FBKI pattern banks and TPLD templates. Older layouts import with drum levels
at 100%, retaining native voices, notes, timing and automation. Recording
preferences use tagged spare bits in PER5's unused zoom word; older settings
load as click OFF, level MID, count-in OFF, preview OFF and chord entry HOLD.
