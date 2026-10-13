# Melodee

[![License: GPL-3.0-only](https://img.shields.io/badge/license-GPL--3.0--only-blue.svg)](LICENSE)

![Melodee 1.0.0: the Melodee logo and six firmware screens](docs/media/melodee-1.0.0.png)

**TL;DR:** connect your FM-1 to a computer by USB, open the
[web installer](https://keremimo.github.io/melodee/) in Chrome or Edge, and press Install.
No extra hardware is needed. Use at your own risk; M-VAVE's own updater or the installer's
**Return to official V15** takes you back to the official firmware.
It saves a complete backup first. If your firmware cannot export one, you can select
**Skip backup** and confirm that Melodee music, sounds and settings may be lost.
For a black-screen device already in ROM UBOOT, use the
[direct USB recovery tool](BUILDING.md#recover-a-black-screen-fm-1).

Multi-engine synthesizer and sequencer firmware for the M-VAVE FM-1. Current release:
**Melodee 1.3.1** ([what's new](#whats-new-in-131)).

Melodee is a modified version of [Felucca](https://github.com/hugelton/Felucca) by Leo Kuroshita
([@kurogedelic](https://github.com/kurogedelic)), [Hügelton Instruments](https://hugelton.com), and
follows Felucca 1.0's design. Melodee is not affiliated with or endorsed by Hügelton Instruments;
please report Melodee problems here, not to Felucca. The installer also finds an FM-1 that still
runs Felucca.

- Install: [web installer](https://keremimo.github.io/melodee/) (Chrome or Edge, USB), or `tools/fm1_install.py` from a terminal
- Editor: [web editor](https://keremimo.github.io/melodee/webapp/editor/)
- Synth only (optional): a one-track sound module build without the sequencer, for playing and recording
  into a computer; choose **Synth only** in the installer, edit it with its own
  [synth editor](https://keremimo.github.io/melodee/webapp/synth-editor/) ([BUILDING.md](BUILDING.md#synth-only-firmware))
- Build: [BUILDING.md](BUILDING.md)

## What's new in 1.3.1

- **Simpler updates:** the loader no longer scans or moves legacy Prophet banks from
  `0x89000`. That abandoned area is treated as application flash. Current Prophet user
  banks at `0x93000` remain supported. Legacy sounds need an export or backup if you want
  to retain them; they are no longer recovered automatically during an upgrade.
- **UBOOT recovery:** standalone command-line unbrickers for Windows, macOS (Intel and
  Apple Silicon), and Linux are included in the release downloads. They back up the
  device first and require `--write` to restore firmware. See
  [recovery instructions](BUILDING.md#recover-a-black-screen-fm-1).
- **Official V15 recovery:** recovery downloads were corrected on 2026-10-13 to
  accept the unmodified M-VAVE `FM-1.fwsc`, verified by its complete SHA-256.
  Download the recovery archive again if an older copy rejects official firmware.

This removes the relocation step; it does not establish the cause of the reported
1.3.0 startup failures.

## What's new in 1.3.0

This release adds track and shared effects, one-pass recording, selectable USB capture,
and an optional autosave setting. Standard (dual-core) and Single core firmware are
available in the installer and release downloads.

- **Effects:** three inserts per track and a shared three-slot return, with independent
  sends. Choose Phaser, Width, Formant, Tape, Spit, Echo, Room, Reverb or Chorus.
  Effects save with projects and templates; sound changes preserve the track's effects.
- **One-pass recording:** arm a track before PLAY to record from step zero, or arm it
  during playback to record at its next pattern start. Recording ends after one pass.
  While stopped and armed, the first key or MIDI note starts and records immediately.
  SEQ > TEMPO > QNT quantizes the new take when recording ends; OFF keeps played timing.
- **Note editing:** bulk deletion includes recorded notes, and pattern clearing removes
  manual notes, recorded notes and motion together.
- **USB capture:** stereo master capture is the default, including track and shared
  effects. Hold HOME and choose USB INPUT > 4 TRACKS for isolated mono stems.
- **Optional autosave:** hold HOME and set AUTOSAVE to OFF to keep project saves manual.
  It defaults ON, remembers the choice across power-offs, and leaves manual SAVE working.
- **Clock choice:** the HOME menu's OVERCLOCK chooses 320 MHz (default) or 360 MHz for
  the next start. A CPU fault at 360 MHz returns the device to 320 MHz.
- **Installer options:** stable, next nightly and the separate synth-only firmware
  remain selectable, with Standard and Single core variants for each.
- **Compatibility:** earlier Melodee project and template layouts load with their
  effect routing preserved. Back up before upgrading; older firmware may not reproduce
  the new effects and recording behavior.

## What's new in 1.2.1

This release makes room for more voices. External effects are gone, the audio engine runs from fast
internal RAM, and the firmware is about 115 KB smaller. Standard (dual-core) and Single core firmware
are both available in the installer and release downloads.

- **No external effects:** distortion, the SLICER, chorus, delay, reverb and the FX layer (repeat,
  reverse, filter sweeps, tape stop, freeze, harmonizer, flanger, phaser, FX LATCH and punch-in) are
  removed. The panel's FX button has no action and CC91 / CC93 do nothing. Older songs and presets
  still load; their former effect settings and automation are ignored, so they play dry.
- **Fewer cut notes:** the whole audio path runs from internal RAM, and the shared voice budget rises
  from 16 to 18 units while the second core is online. Per-engine caps and overload protection remain.
- **ANALOG and DIGITAL retired:** track 1 now powers on with PROPHET FUNK BASS II. Saved ANALOG and
  DIGITAL sounds keep their place but play silent.
- **Older formats:** projects, templates and presets saved by Melodee 1.0 or later load as before.
  Anything older shows OLD FORMAT and stays untouched until you choose to replace it; the device asks
  first. A backup the device cannot read is refused before anything is written. If you still have
  projects or presets from before 1.0, open and save them with 1.1.0 before updating.
- **Import SysEx:** a DX7, CZ-1, Prophet or Melodee preset sent over USB opens its USER list. Pick the
  destination and choose **Import here**; occupied slots ask before they are replaced.
- **Bulk presets:** hold OCT+ and turn a knob to select a range of USER or STORE slots, then send or
  erase them together.
- **Note tools:** move and copy notes or ranges at quarter-step boundaries, and quantize a pattern or
  a selection to 1, 0.5 or 0.25 step, each with one undo.
- **Fixes:** local PLAY keeps running until MIDI Start, Continue or Stop takes over; no pop at power-on;
  a list picker no longer covers half of a neighbouring value; Prophet parts no longer add a DC offset
  to the mix and USB stems; settings backups with recording preferences restore again.

## What's new in 1.1.0

The minor release carries forward 1.0.2's native synth motion, pattern operations and UI fixes,
with both Standard (dual-core) and Single core firmware available in the installer and release
downloads. Both builds have the same features and project format.

**On-device sound designer:** choose a sound goal or an expert synthesis path, start with INIT
or your current tone, switch on the features the sound needs (Prophet pulse, Poly-Mod and unison,
FM operators, CZ-1 ring and noise), shape the engine's real controls, compare against the original
and name your own preset. Open **Sound designer** from the HOME hold menu
or a sound page's action sheet. [Controls and saving](docs/SOUND_DESIGNER.md).

- **Clearer header:** the four track dots are gone. A REC mark beside the tempo shows recording
  (filled: the selected track is ready/recording; a ring: waiting or another track is armed), the octave shift is a small
  `+2` / `-1` badge, and a page's dots always fit. The bar under a sound page starts with the
  track's number in its colour.
- **Larger small text:** labels, names and values are a pixel larger and easier to read.
- **Short pages become knob pages:** CHORD, ENV DEST, LFO DEST, LFO 2, ARP 2 and DLY 2 put their
  settings on the four knobs. MPC's degree moved into the SCL list (shown with QNT MPC). Number
  settings (GLO DRUM channel, FM6 ALG, CZ-1 SUS and END) step in place instead of opening a picker.
- **Prophet tone controls:** keyed tuning moves in audible semitone steps, controls whose signal
  path is switched off still show, and unison shows the native default voice count.
- **Steadier dim lights:** idle key and button lights are dimmed by hardware PWM. If the startup
  check finds a conflict on the input rows, the earlier frame dimming is used instead.
- **Fixes:** the header BPM no longer freezes mid-roll on fast turns, and a small patch of the
  screen can no longer stay out of date after an update.

## What's new in 1.0.2

- **Two installer builds:** Standard uses both cores. Choose **Single core** if your FM-1 crashes,
  freezes or restarts into update mode with Standard. Single core never starts the second core;
  PROPHET plays up to five voices instead of eight, and heavy FM6 and PROPHET sounds reach the CPU
  limit sooner. Both builds have the same features and project format. Choose Standard and install
  again to return to dual core.
- **Motion records the native synth settings:** Prophet and CZ-1 Stage knobs, their sound pages,
  and FM6 operator and voice pages now record and replay their changes. Arm REC, start playback,
  then turn a sound knob. Turning one while armed but stopped shows **PLAY TO RECORD MOTION**.
  Stopping or bypassing motion restores the original patch; project snapshots keep that original.
  MOTION is hidden until the selected pattern has recorded motion.
- **Init sound starts from scratch:** Prophet, CZ-1, SID and FM6 use their own init patches,
  with effects off. Switching engines in the sound browser starts on that engine's init sound.
- **SCALES stays unobscured:** changing a scale no longer opens a second picker over its list.
- **Copy and delete patterns:** OCT+ on PATTERNS opens the current pattern's menu. Copy notes
  and motion within the track, confirm before replacing a used slot, or delete with confirmation.
  Holding SAVE undoes the action.

Projects with native synth motion require 1.0.2 or later; older firmware cannot load those events.
Existing projects remain readable by 1.0.2. The motion pool still holds 64 events shared across tracks
and patterns; settings represented by multiple patch bytes can use multiple events.

## What's new in 1.0.1

- **Fixes the crashes some FM-1s had with 1.0.0:** a red MELODEE CRASH screen, on some units every
  minute or so, sometimes ending in update mode. On those units the FM-1's second core now and then
  reads memory wrong. Melodee now checks every job it hands to the second core and never runs one it
  read wrong. If the second core misreads, faults or stops answering, it is switched off and the
  first core plays every voice, with no crash or freeze; PROPHET then plays up to five voices
  instead of eight. Such a unit stays on one core until it is switched off and on again. Units
  without the fault keep both cores.
- **No crash loop into update mode:** after a crash in the first 30 seconds, the next start runs
  on one core.

## What's new in 1.0.0

- **A new instrument on the screen:** Stage puts the four tracks, their sounds, patterns and live
  controls on HOME. Rubik type, Melodee's own icons, track colours and NIGHT / DAY / CONTRAST
  palettes carry through the sound browser, mixer, pattern and song views, full-height NOTES and
  drum editors, curve pages and action sheets.
- **One navigation language:** KNOB 1 moves across, KNOB 2 moves down, SELECT changes pages,
  OCT+ enters and OCT− goes back. Named values open pickers; actions open sheets instead of hiding
  behind button combinations. The octave buttons keep their musical job on Stage.
- **Faster display:** the firmware sends only the areas that changed to the LCD. Scrolling NOTES,
  moving its playhead and turning most controls no longer resend the whole screen.
- **More sound and performance:** dual-core rendering is on by default, DSP signal paths use the
  FM-1's native single-precision FPU. Modulation, stereo spread and screen sleep are included;
  the retired hold/latch controls are gone; **FX** opens the selected track's new insert page.
- **808 and 909 kits:** both DRUM kits synthesize every voice in real time. The 909 kick, snare,
  toms, rim and clap use circuit models from
  [fm1-x0x](https://github.com/charlesvestal/fm1-x0x/tree/201e5c5c1ac056a028a006bd1eabb23ac1bb174d);
  hats, crash and ride use metallic oscillator/noise approximations rather than samples. EDIT sets
  each drum's tuning, decay, tone, attack/snappy and level; a recorded hit can keep its own pitch
  and length.
- **Finish the song on the box:** Capture writes the last bars you played, Jam logs pattern changes,
  REC held on SONG turns that jam into an arrangement, and projects save themselves after five idle
  seconds. SONG's four knobs select section, track, pattern and repeats.
- **SID replaces LOFI:** a Commodore MOS 6581 / 8580 for every note, modelled at the register level after
  [reSID](https://github.com/libsidplayfp/resid): three oscillators on the chip's 16-bit frequency register,
  pulse, triangle, sawtooth, the 23-bit noise register, ring modulation and hard sync, and the combined
  waveforms as reSID sampled them from real chips. The chip's own envelope generator (its 16 rates,
  exponential decay and ADSR delay), each revision's DACs, cutoff curve and resonance, the 6581's thump
  through the C64's output stage, and 32 factory presets. MODEL switches between 6581 and 8580.
- **A smaller, focused engine list:** PROPHET, FM6, CZ-1, SID and DRUM are selectable. PHASE,
  VOICE and NOISE join TRIO, WHEEL and PHYS as retired engine IDs; old tracks using them remain
  identifiable but silent. Phrases and their pages are removed.

**Upgrading from 0.13.1:** export important Prophet programs or keep a complete backup.
Current firmware ignores the old Prophet bank location; it no longer moves those banks.
The four legacy project objects are retired and are
not read by 1.0.0. Former effect fields in older saves are ignored.
Tracks and user presets that used LOFI keep their engine number and load as SID with LOFI's control
values, so they sound different; choose a SID preset to start again.

## What's new in 0.13.1

- **PROPHET Lo Freq:** oscillator B's Lo Freq switch drops it seven octaves instead of ten, as measured
  on a Rev 4 recording, and a new note finds it anywhere in its cycle, as on the free-running original.
  Programs that use it as a slow modulator (Pickle Pincher, FLUTES, TRUMPET FLUTE and others) move at
  their intended rate; Pickle Pincher no longer falls silent after the first note
- **PROPHET filter:** smoother saturation at the filter's last stage and output removes a faint grain
  that became audible as the filter closed, for example in Internalized's sustain
- **Web editor:** importing more native .syx programs than there are empty slots offers to overwrite
  them from the selected slot onwards

## What's new in 0.13

- **PROPHET**, a new engine for Sequential Prophet-5 Rev 4 programs: two oscillators with hard sync,
  Poly-Mod, LFO and noise wheel modulation, the SSI (Rev 1/2) and Curtis (Rev 3) four-pole filters,
  separate filter and amplifier envelopes, Vintage, glide, unison and five voices per track.
  **All 200 programs of Sequential's v1.03 factory bank are its presets.** LFO rates, vibrato depth,
  filter tracking and envelope times are fitted to recordings of a Rev 4. Sixteen device pages edit
  every native parameter, and 128 native user slots (P001–P128) start with the first 128 factory
  programs. The web editor imports and exports Prophet-5/10 .syx programs and banks; the device also
  accepts single and edit-buffer dumps over USB. PROPHET takes ANALOG's place in the engine list
- **Note editing:** SEQ > NOTES is now the single editor for entered steps and recorded takes:
  SELECT walks the notes, the knobs change pitch, length and velocity, SCL moves and EDIT deletes;
  hold EDIT while recording to erase as the playhead passes
- **FM6:** DX7 librarians can send 32-voice banks straight to the device over USB, into F001–F032
  or F033–F064 (EDIT > STORE > SLOT), and bank dump requests answer from there; STORE > SEND sends
  the track's voice. The fourth main knob is DTUN
- **Performance:** engine and bank memory is allocated when it is used, idle USB capture and silent
  tracks cost no processing, and the screen draws while the LCD transfers
- **Retired engines:** PHYS, TRIO and WHEEL. Tracks that use them are silent; their saved values are kept

**Upgrading from 0.12:** projects, templates, banks and presets migrate. Projects and templates saved
by 0.13 include their Prophet programs and need 0.13. Installing 0.12 or the official V15 afterwards
erases the Prophet user slots: export them, or keep the installer's backup, first.

## What's new in 0.12

- **Microtonal scales:** 54 new tunings, for 70 scales in total, including equal divisions,
  just intonation, historical temperaments, maqam models and Bohlen–Pierce. The SCALES browser
  groups them by family, shows full names and note counts, and saves device favorites; the web
  editor adds filtering, search and browser favorites. Chords, ARP and recordings retain scale degrees
- **Concert pitch:** set A4 from **400–480 Hz** on GLO > GLOBAL, saved independently of projects;
  440 Hz remains the default and TUNE still adds a fine offset in cents
- **Recording:** retain the original note timing and gate lengths, with **1,024 timed notes across
  all 32 pattern banks**. SEQ > TEMPO > QNT applies an automatic quantization pass when recording ends and defaults OFF
- **Recorded-note editing:** SEQ > NOTES selects individual synth or drum hits, zooms the original
  take, deletes selected hits and supports undo without changing neighbouring notes
- **Native user presets:** 64 FM6 voices and 128 CZ-1 tones in separate collections, alongside 64 general
  user presets. Scroll and favorite them in PRESETS; import/export Dexed/DX7 and Casio .syx in the editor.
  Native slots store only the tone, so loading keeps the track's mix and patterns
- **FM6 diagrams:** MARK I algorithms 4 and 6 show their longer feedback paths

**Upgrading from 0.11 / 0.11.1:** existing projects, templates, settings and presets migrate.
Expanded recordings and new scale IDs need 0.12; older firmware cannot reproduce those additions.
Microtonal MIDI OUT sends degree addresses, so an external synth needs the same tuning and mapping.

## What's new in 0.11.1

- **Web editor connects again:** with 0.11 it stopped after "Reading" the parameters, because the
  device cut the list of CZ-1's 65 preset names short. The device now sends the whole list, and the
  editor also connects to an FM-1 that still runs 0.11 (the last CZ-1 presets show as numbers there)

## What's new in 0.11

- **CZ-1**, a new engine playing native Casio CZ-1 tones: two lines, each with its own eight-step pitch,
  timbre and volume envelopes, the CZ's waveforms and windows, ring and noise modulation, detune, vibrato,
  key follow, line levels and velocity sensitivity (from MIDI)
- **Casio's 64 CZ-1 preset tones** (A-1 BRASS 1 to H-8 TYPHOON SOUND) as CZ-1's factory presets
- **Every tone value on the device:** 38 EDIT pages for the lines, detune, vibrato, windows and the six
  envelopes, and CZ TOOLS (NAME, copy line 1 > 2 or 2 > 1, COMPARE). A value that has no effect on the
  tone as it is (line 2 in LINE1, steps after END, vibrato without DEPTH) is drawn dim
- **Casio SysEx over MIDI:** CZ editors and librarians can send named tones to the CZ-1 user slots and request track tones
- **PHASE** keeps its own six presets and gains LINK / SPLIT: separate native DCO, DCW and DCA envelopes
- **Installer:** **Return to official V15** can skip the backup when the firmware cannot export one
- **Removed:** the SAMPLE, GRAIN and SLICE engines and the custom drum kit (DRUM plays the 808)

**Upgrading from 0.10:** projects, templates, settings and user presets load. Tracks and user presets
that used SAMPLE, GRAIN or SLICE need another sound; a track with the custom drum kit plays the 808.

## What's new in 0.10

Melodee 0.10 is built on Felucca 1.0: its screen design, quick layers, sequencer and file formats.
On top of Felucca 1.0 it adds:

- **FM6** plays DX7 voices using Dexed's algorithms and envelope rules, with up to 16 voices, operator pages on the
  device and DX7 SysEx import. Each track keeps its own function settings (MODERN / MARK I / OPL,
  pitch bend, portamento and controllers), saved with the project
- **KIT 808** on the DRUM engine: TR-808 circuit models on the eight lanes
- **USB audio:** Melodee Out plays the computer through the FM-1, Melodee In records the four tracks
  as separate channels; each can be switched off
- **Patterns:** eight banks per track, with songs that pick a bank for each track
- **Step editing:** set a note's length while its key is held, resize it with ENV, move it with SCL,
  delete it with EDIT, and undo or redo up to eight edits; MIDI step entry; unquantized live recording with optional quantization when the take ends
  and the cursor following playback
- **Startup:** a BOOT project loaded at power-on and a template for new projects; CLK, TUNE, MIDI and
  ROUT kept between starts
- **Panel:** HOME names the notes and chords you play; key lights for the scale and the sounding
  notes (the menu's LIGHTS); REC + PLAY records at once and REC held captures what you just played; SAVE + REC
  saves the project to its slot, and it saves itself when stopped and left alone; BPM and swing on SEQ > TEMPO, saved
  with the project
- **MIDI:** a DRUM channel (10 by default) for the first DRUM track; QNT ALL and MPC pad layouts that
  incoming MIDI follows too; a more reliable TRS input; GLO > SYSTEM shows the input's activity

**Upgrading from an earlier Melodee:** device data starts fresh. Projects, settings, templates and
user presets saved by earlier Melodee versions are not imported, and the user sample slots are gone
(see [Startup and compatibility](#startup-and-compatibility)). Felucca 1.0 projects load.

## Features

- **Five selectable instruments:** PROPHET, FM6, CZ-1, SID and DRUM, each with factory presets;
  DRUM contains synthesized 808 and 909 kits
- **Four tracks**, one instrument and sound per track. FM6 plays up to 16 voices; PROPHET plays up
  to eight voices per track in the default dual-core build (five if CPU1 is unavailable or disabled).
  ALGORITHM selects the track on every page. Running several eight-voice Prophet tracks together
  has not yet been stress-tested on hardware
- **Sequencer:** 64 steps per track with chords, ties, accent, slide and per-step chance; a piano
  roll of the steps; a drum grid (white keys = steps, black keys = lanes); motion recording of knob
  moves; unquantized live loop recording with overdub and optional quantization at take end; MIDI step entry; tied-note length,
  movement and deletion; eight-level manual step undo/redo; divisions from 1/32 to 4 bars;
  loading a sound never touches your patterns
- **Patterns:** eight independent 64-step banks per track, with up to four notes per step, ties,
  chance, drum hits and bank-specific motion (64 automation events shared across the project)
- **Songs:** up to 16 rows, each choosing a bank for every track and repeating 1–16 times
- **Chord keys:** one finger plays an in-key chord (triads or sevenths of the scale, or fixed chord
  shapes), with voicings; on the keys, MIDI in, recording and the arpeggiator
- **Arpeggiator** with REPEAT and a beat LED, 70 scales with a white-key mode, glide,
  MONO / LEGATO / UNISON
- **Modulation matrix:** 4 slots per track, MIDI controllers as sources
- **Dry mixer:** track level, pan and synth voice spread, optional per-track FX, with output protection
- **Quick layers:** hold GLO, SCL or EDIT for shortcuts on the keys and knobs; double-press to lock
  a layer. Multi-level undo (SAVE held); REC on every page; OCT+ confirms, OCT- goes back
- **Presets:** browse by category (BASS, LEAD, PAD, KEYS, ORGAN, STRING, BRASS, WIND, PLUCK, BELL, DRUM, FX), favourites or
  RECENT, with knob acceleration; factory presets, 64 general slots, 64 native FM6 slots, 128 native CZ-1 slots, 128 native Prophet slots and 4 projects, named on the device;
  a startup project and a template for new projects; compatible upstream projects from earlier versions load
- **Screen:** Melodee's own look: Rubik type, icons drawn for Melodee, every track in its own colour (the screen takes the
  selected track's), three palettes: NIGHT, DAY and CONTRAST
- **Stage (HOME):** the selected track's own four knobs (its engine's: the Prophet's cutoff, resonance, filter envelope
  and release, the CZ-1's wave, DCW, detune and vibrato), the notes and chord it plays in front of the live waveform
  (the last voicing stays after release; a DRUM track lights its lanes as they hit), and a lane per track: its sound,
  its pattern (and the next one while it waits for the bar), the bar playing and its level
- **Lights:** one grammar for keys and buttons: dark nothing there, dim something there, bright happening now,
  breathing waiting. The keys that play glow (the scale's notes with QNT OFF, every key of a kit), a key lights up
  while its note sounds, MIDI in too, and the idle buttons glow; REC breathes while armed and waiting for the pattern start, PLAY while
  counting in; the HOME-held menu's LIGHTS sets the level (OFF: only what is pressed, engaged or selectable)
- **USB:** class-compliant MIDI in and out; **Melodee Out** plays the computer through the FM-1,
  **Melodee In** defaults to a stereo copy of the synth master, including track effects, shared effects,
  pan, master level and output protection. The HOME-held menu's **USB INPUT** selects **STEREO** or
  **4 TRACKS** (four isolated mono stems, after track effects/level, before shared effects and master).
  Stereo skips stem collection and halves capture transfer/packing work; no extra effects pass or resampling.
  Computer playback and the monitor click stay out of recordings. Both support 16/24-bit audio at 44.1 kHz;
  each device can be disabled with **USB AUDIO**. Changing USB options reconnects automatically; reselect the
  input in your DAW if needed. No driver needed
- **Overclock:** the HOME-held menu's OVERCLOCK sets the clock the FM-1 starts at: **320 MHz** (the default, JieLi's
  own clock table) or **360 MHz** (M-VAVE's clock, what Melodee ran at before). 360 MHz gives more voices with the
  second core (18 budget units instead of 16). A new clock needs a restart: closing the menu offers one (unsaved song
  changes are lost), or it applies at the next power-on. If a 360 MHz session crashes, hangs or loses its second
  core, the FM-1 goes back to 320 MHz and says **CPU FAULT: 320 MHZ**
- **MIDI:** USB and TRS MIDI in; channels 1–4 play tracks 1–4, channel 10 the first DRUM track (GLO >
  SYSTEM **DRUM**: any channel or OFF; no DRUM track: the selected one), other channels the selected track,
  and the keys send on the track's channel; pitch bend, sustain, panic; clock from internal, USB or TRS;
  GLO > SYSTEM KNOB 1 shows the USB or the TRS input's status (RX while it receives; both always play)
  Local PLAY uses the internal tempo until the selected input sends Start, Continue or Stop. Clock
  pulses alone do not take over. Changing the clock source clears this transport selection.
- **Web:** editor for every parameter (with a 6-operator FM patch editor), step grid, mixer,
  preset library and FM6 voice editor; full backup and restore; return to the official firmware

## Controls

![FM-1 controls](docs/panel.jpg)

- **SELECT** turns the pages of the open section (both ways); **MASTER** sets the volume,
  **ALGORITHM** picks the track (T1–T4), and **PRESETS** browses its sounds. KNOB 1 moves across
  lists and grids, KNOB 2 moves down; on parameter pages KNOB 1–4 edit the four controls
- **BPM**, the song's **SWG** and the selected track's recording **QNT** are on **SEQ > TEMPO**:
  the project's, saved and loaded with it (power-on: the BOOT project's, the template's or 120);
  GLO > GLOBAL keeps CLK and TUNE, the device's
- SCL, ENV, LFO, EDIT, GLO, SAVE, ARP and SEQ open their pages; press again for the next page.
  HOME returns home
- **Held:** GLO, SCL and EDIT open their quick layers; SAVE is undo, HOME the menu, SEQ the PATTERNS grid with
  the song under it, REC captures (below).
- **Undo:** hold SAVE to undo the last sound or pattern load or Capture; hold it again to go further back (up to 8
  levels); while SAVE is held, OCT- undoes and OCT+ redoes. On the step pages SAVE's undo
  covers the step edits first
  When editing synth steps, SCL and EDIT use the editing controls below instead.
- **FX** opens the track's three insert pages; SELECT moves between the slots.
- PLAY starts and stops all four tracks; REC arms or disarms the selected track without starting the
  transport. While playing, REC waits for that track's next pattern start, records one pass and
  automatically disarms at its end while playback continues. While stopped and armed, the first
  key or MIDI note starts playback and records from step zero, skipping count-in.
  **REC + PLAY** arms and starts in one gesture
- On Stage, OCT− / OCT+ shift the octave (both: reset). Everywhere else OCT+ enters, opens or
  confirms and OCT− goes back. During synth step editing they move the step cursor
- Save a sound: stop, tap SAVE, choose USER and a slot, press OCT+ to open its sheet with
  **Save here** selected, then OCT+ again (name it with the keys)
- **Import SysEx:** a valid USB DX7, CZ-1, Prophet or Melodee preset opens its matching USER list,
  preserving the full native name. Choose a destination with KNOB 1, press OCT+ and choose **Import here**.
  A DX7 cartridge needs 32 consecutive slots starting there. Occupied slots ask before replacement;
  saving requires stopped playback. **Cancel import** leaves the slots unchanged. Imports do not load a track.
- **Bulk presets:** hold OCT+ and turn SELECT, KNOB 1 or KNOB 2 to select a contiguous range in USER
  or STORE. Release, then tap OCT+ for **Send selected** over USB SysEx, **Erase selected** (one confirmation),
  or **Clear selection**. Empty slots are skipped. Single-slot **Send** is also available in USER.
- **New song:** SAVE > PROJECT, turn KNOB 1 past TMPL to **NEW**, KNOB 3 to pick it and OCT+ (unsaved changes ask
  first). Set the key (KNOB 1 ROOT, 2 SCALE) and tempo (KNOB 3), OCT+; give each track a role with KNOB 1–4 (KEEP,
  DRUMS, BASS, CHORDS, LEAD, PAD), OCT+ creates it: your template's sounds (none saved: the power-on ones), a role's
  first sound where the template's does not fit it, every pattern empty
- **SAVE + REC** saves the project back to the slot it was loaded from or last saved to (`SAVED B`), stopping
  the transport first; a new project opens SAVE > PROJECT on a free slot. **Autosave:** stopped and untouched for
  5 seconds (no key, button, knob or MIDI), a project that has a slot saves itself there when it changed.
  Hold **HOME** and set **AUTOSAVE** to **OFF** to disable it (default **ON**). The choice survives power-off;
  manual saves still work
- **Capture:** while the transport runs, Melodee keeps what you play on every track that is not recording. Hold
  **REC** to write the selected track's last bars into its pattern, with their timing, velocities and lengths: an
  empty pattern takes 1, 2 or 4 bars (as many as your notes span) and its LEN follows; a pattern with notes takes its
  last LEN steps over what it holds. Hold SAVE to undo

### Browsing sounds

Turn **PRESETS** to open the sound browser. **KNOB 1** chooses the list and **KNOB 2** chooses a
sound: each engine's factory presets in engine order, its native user slots, then the general user presets.
The list can be **FAV**, **RECENT** (sounds browsed since power-on, newest first) or one category: **BASS LEAD PAD KEYS ORGAN STRING
BRASS WIND PLUCK BELL DRUM FX OTHER**. Every factory sound, the Prophet, CZ-1 and FM6 libraries included, has a
category; native and user slots take the category of a factory sound with the same name, otherwise the words in
their name (BASS, PIANO, STRINGS, ...). The chosen category is kept with the device settings.

**KNOB 4** chooses the engine and starts it from its init sound, nothing changed and dry (INIT PROPHET, INIT TONE,
INIT SID, FM6's INIT VOICE; DRUM its 808 kit), for designing a sound from scratch; each engine's init sound comes
first in its list. **Init sound** in the sound's sheet (OCT+ held) and on EDIT held's black key does the same for the
track's engine.

Knobs accelerate: a slow turn moves one step a detent, and a few clicks are one step each however quick. Only a spin
(four detents or more in a row) speeds up, to 3, 5 and then 8 steps a detent on wide values, so a quick half turn
sweeps 0–127; the tempo and lists of more than 256 entries go twice as far, so the end of a 400-sound list is a flick
away. Lists of names (waveforms, modes) never accelerate.
MENU > **KNOB ACCEL** OFF keeps every detent one step. While the list moves fast, the screen follows at once and the
sound loads when the knob rests, so a flick does not load every sound it passes; playing a key or changing the track
loads it at once. Hold **SAVE** to return to the sound you had before browsing.

### Patterns and songs

Hold **SEQ** and press one of the first eight white keys to pick pattern 1–8: the pattern playing is lit, the others
holding notes glow, the one waiting for the loop end breathes. Hold a pattern key and press a second one to copy its
notes, ties, timing, chance, drum hits and automation. **SEQ + SELECT** also chooses a pattern. While playing, each
track changes at its own loop end. Selecting the active pattern cancels a queued change. STOP applies pending choices.

**SEQ > PATTERNS** (or hold SEQ) shows the four tracks' eight patterns at once: the one playing in the track's colour
(with how far it has played), the one waiting outlined, the others holding notes raised; under it, the song's rows
around the one playing (no song yet: the jam's). **KNOB 1–4** pick tracks 1–4's patterns; SELECT goes on to SONG.
**OCT+** opens the selected track's pattern's sheet: **Copy to…** frames the place it goes on the track's row (the
first empty one), any knob moves the frame, OCT+ copies there (over a pattern in use it asks first) and OCT- cancels;
**Delete pattern** asks, then empties the pattern, its notes and automation (hold SAVE to undo).

On **SEQ > SONG**, KNOB 1 chooses the section, KNOB 2 the track, KNOB 3 that track's pattern and
KNOB 4 the section's repeats. **Jam to song:** from PLAY on, every loop of track 1 logs the four tracks'
patterns as a section (the same patterns again: a repeat), so switching patterns while you jam writes a song;
hold **REC** on SONG to turn the logged jam into the arrangement (over a song with sections: confirm).
PLAY runs the arrangement; sections change all four tracks
at track 1's loop boundary and the final section stops. STOP returns to the patterns selected before
SONG. Project saves and complete backups include all 32 banks and the arrangement.

### Note editing

**SEQ > NOTES** is the single editor for entered steps and recorded performances. Press SEQ to
open it; tap SEQ again to reach PATTERN, TEMPO and the other sequence pages. The piano roll
shows recorded notes at their original timing alongside ordinary notes and continuous tie tails.
The highlighted note has a bright outline; the playhead is a separate moving line.

- **SELECT** keeps its global job: turning the SEQ pages. **KNOB 1 STEP** walks every interval,
  including empty ones, and visits separate chord pitches or repeated hits one at a time. Going
  backwards lands on the previous interval's last note.
- **KNOB 2 PITCH** changes the highlighted pitch. On an empty manual step, it inserts a note;
  playing keys or MIDI enters a note or chord, then advances when released. Playing on a recorded
  interval auditions without replacing the take. During live recording, keys and MIDI record normally.
- **KNOB 3 LENGTH** extends or shortens a note at hard quarter-step boundaries (0.25, 0.50, 0.75,
  1.00, 1.25…); hold **ENV** for 1/16-step boundaries. Adjusting an ordinary step note while stopped
  converts its chord and ties into precise timed notes, retaining their pitches and velocities.
  Recorded chord pitches then have independent lengths. Held-key entry and ENV + SELECT retain
  their whole-step tie controls for manual notes.
- **KNOB 4 VEL** adjusts velocity from 1 to 127. Turning it on an accented manual note makes the
  displayed velocity its playback velocity. Hold **ENV** and turn KNOB 4 to switch a manual note's
  **SLIDE** on or off; the card changes to SLIDE while ENV is held.
- **PRESETS** zooms around the selected interval from 16 steps down to one. Selection stays put.
- **OCT+** places a note or drum hit in empty space, or opens the selected note/hit's sheet for
  Length, Velocity, Chance, Slide, Move, Copy and Delete. OCT− closes it or goes back.
- Choose **Move note** from the note's sheet, then turn **KNOB 1** to move it while stopped.
  Note durations and velocities stay intact; moving into an unselected manual note or tie is blocked.
  Hold **ENV** and turn SELECT or KNOB 1 to resize instead.
- Hold **OCT+** and turn SELECT, KNOB 1 or KNOB 2 to select a range; turn **KNOB 1** to move its notes
  at quarter-step boundaries, preserving relative timing, durations and velocities. Tap or hold OCT+ for
  **Copy: KNOB 1**, **Quantize**, or **Clear selection**. A single note's sheet offers **Move note** and **Copy note**;
  choose either, then turn KNOB 1. After copying, KNOB 1 moves the duplicate; OCT- finishes the move.
  Copies and moves wrap around the loop. Hold OCT+ without a range to open the pattern tools: **Quantize** snaps
  the pattern's note onsets, or only the selected range when one exists. On the Quantize row, KNOB 1 chooses
  **1**, **0.5**, or **0.25 step**, and OCT+ applies it. Durations and velocities stay intact; SAVE held undoes it.
- Tap **EDIT** to delete the highlighted pitch. Recorded deletion affects just that hit; manual
  chords retain their other pitches and ties. Deleting the final manual pitch clears its tie tail.
  Using another control while EDIT is held consumes the tap.
- While the selected track is recording and playing, **hold EDIT to erase**. The current step and
  each step the playhead crosses lose their notes, including recorded hits, drum hits and tied
  notes already sounding. The header reads **ERASING**; release EDIT to stop immediately.
  Keys and MIDI still audition while erasing, but do not record new notes. The gesture stays on
  the selected track and bank; leaving NOTES, changing track/bank or stopping recording cancels it.
  This is live recording: it starts a fresh edit history, so the eight-edit undo does not restore
  an erased pass.
- Hold **SAVE** to undo. **EDIT/SAVE + OCT− / OCT+** undo/redo up to eight edits, restoring note data
  and focus. A new edit clears redo. Recording, external edits, changing track, bank or loop length
  start a fresh history. With no manual edit to undo, held SAVE retains sound/pattern-load undo.
  Fractional manual conversions and grouped moves/copies have one complete undo/redo snapshot.
  Starting playback releases that temporary snapshot; these transforms require stopped playback.

Recorded properties and deletion are editable during playback; stop live recording before editing
those notes. Live recording follows the playhead; ordinary playback leaves the editing focus alone.
The screen shows the four knob controls plus selection, zoom, movement and deletion hints.
The DRUM grid retains white-key step entry and black-key lane selection for manual patterns;
recorded drum performances use the same precise NOTES piano roll and individual-hit controls after
recording stops. During live drum recording, the grid and lane keys stay in place.

Armed live recording preserves played timing, velocity and each note's held duration, including
repeated hits within one step and overlapping chord notes with different releases.
**SEQ > TEMPO > QNT** defaults to **OFF**, preserving captured timing. Choose a division to
quantize the new take's onsets to a swung grid when recording finishes. Note lengths and velocities
stay intact; the piano roll and playback use the committed timing. Changing QNT afterward chooses
the next take's grid. An occupied manual step leaves the take intact and shows **QNT STEP OCCUPIED**.
This is independent of scale/key-map QNT on SCL. Track GATE controls manually entered steps.

Project saves, backups and bank copies preserve edited performance events and their stored timing.
User-preset patterns and the ordinary step-edit protocol carry only the grouped step overview;
external overview edits can replace a recording group with ordinary step sequencing.

A project holds **1,024 timed notes shared across all 32 banks**. A chord uses one entry per note.
Distinct repeated hits are separate entries; repeating the same pitch at exactly the same time
replaces that event. **RECORDING FULL** leaves existing recordings intact; clearing or replacing
recorded steps makes space for new takes. Notes held longer than 128
nominal steps are capped at that duration.

Each track has **three serial FX inserts**, all usable together, including repeated
effect types. Press **FX** and turn **SELECT** between **FX 1**, **FX 2** and **FX 3**.
KNOB 1 chooses **OFF, PHASER, WIDTH, FORMANT, TAPE, SPIT, ECHO, ROOM, REVERB, CHORUS**;
KNOB 2 sets **MIX**; KNOB 3 sets rate (Echo's time / Room's and Reverb's size);
KNOB 4 sets depth, focus, jitter, bite, repeats or decay.
The page follows `docs/design`: four rings, a track-colour picture, the normal
name picker, OCT+ sound actions and OCT- back to Stage. FX settings belong to
the track and save with projects and templates. Loading or saving a preset,
changing engines, Init sound and sound undo preserve the track's effects.

Phaser sweeps four all-pass sections per side. Width processes the left side
and keeps the right dry; collapsing it to mono changes its tone. Formant jumps
among five vowel shapes. Tape drifts two parallel crossover filters, smoothly
changes the middle band's polarity, and rolls off the top. JITTER increases the
movement without abrupt filter or phase switches. Its stereo filters run in
one software pass with no delay buffer. Spit abruptly retunes a resonant
band-pass and saturates it. Echo is a short, reduced-rate delay (10–370 ms,
8 KiB per slot); Room keeps its small damped four-comb character (2 KiB).
Reverb adds a diffused stereo eight-delay feedback tank with input and output
allpasses (4 KiB per slot). Its **DEPTH** knob increases the wet body and extends
the tail, from a short space to a deeper wash, with subtle delay drift for
fuller diffusion and a softly bounded wet return. **MIX** sets the balance and
**SIZE** sets the reflection spacing. Filter FX, Echo and Room run at **11.025 kHz**; Reverb runs at **5.5125 kHz**. Synths and the dry path stay at **44.1 kHz**.
Reverb uses an inexpensive averaging input filter and interpolates the wet
return; the rate conversion needs no additional buffer. Turning MIX to zero or
selecting OFF fades to dry and releases the time-effect buffer. An allocation
failure bypasses only that slot and shows MEMORY FULL; bypass and re-enable to retry.

Chorus adds two smoothly modulated stereo delay taps at full **44.1 kHz**, using
**2 KiB per slot** and integer sample processing. **RATE** sets the modulation
speed, **DEPTH** sets pitch movement, and **MIX** blends the delayed signal with
dry audio (start near 50%). Its delay is centred at 5.8 ms with up to +/-2.9 ms
of movement. It has no feedback or extra filters; OFF / MIX 0 releases its buffer.
For several tracks, use one Chorus in the shared send chain to save processing.

SELECT also visits **FX SEND** and **SEND FX 1–3**. SEND is the selected track's post-volume, post-pan amount into one shared stereo chain; RETURN blends that chain into the main output. Set a shared TYPE (for example REVERB), then raise SEND on the tracks that should feed it. Shared MIX defaults fully wet. **FX SEND > MODE** chooses **CHAIN** (the default) or **SPLIT** (separate sends). SPLIT exposes **FX SENDS > SEND 1–3** for each track: each global slot receives its own mix of tracks and its return joins the dry output. SEND 1 shares the existing SEND level; SEND 2 and SEND 3 are remembered when returning to CHAIN. The routing choice and send levels are saved in projects and the boot template; older projects load as CHAIN. Changing MODE fades the return before resetting the global effect tails. Switching the shared chain off fades its return to silence. The three track inserts remain available, and preset changes keep SEND. One shared Reverb uses 4 KiB regardless of the number of sending tracks. Its processing uses CPU1 when available; CPU0 prepares the dry output, then joins before the final limiter. Four mono USB stems remain individual tracks; the stereo master includes the shared return.

Old FX/slicer sends, IDs, motion and key assignments remain inert. Previous
FUN16 projects, FBKK banks, TPLF templates and old user presets load with the new
FX off. FUN17 projects and TPLG templates retain their original effect in slot 1,
with slots 2 and 3 off. FUN18 projects and TPLH templates preserve all three inserts. New FUN19 projects and TPLI templates also store SEND and the shared return, with the shared chain off for older music. Eight-byte packed steps keep the project and bank extents unchanged. Four mono USB
recording stems include the complete chain, averaged after
pan; Width therefore has the same mono tradeoff on a stem.

With **CLK TRS** or **CLK USB**, musical timing follows MIDI clock pulses directly, so tempo
changes do not shift the pattern. DIV sets the pattern's step length, while TEMPO QNT independently
selects the grid committed at recording end. The DRUM grid keeps its white-key step and black-key lane controls.

### Startup and compatibility

On **SAVE > PROJECT**, KNOB 2 **BOOT** selects OFF or project A–D to load at power-on.
KNOB 1 **SLOT** also offers **TMPL**: save your sounds and settings there as the template for new
projects, with empty patterns. BOOT OFF uses the template when one is saved. CLK, TUNE, MIDI
and ROUT persist between starts; loading a project or template applies its own settings. DRUM is the
device's own setting.

For handpans and other instruments with a different concert pitch, open **GLO > GLOBAL** and turn
**KNOB 1 A4**: **400–480 Hz**, in 1 Hz steps, with **440 Hz** as the default. Set **432 Hz** for an
A=432 instrument and leave **TUNE** at 0; TUNE still adds a fine offset in cents. A4 retunes all
pitched engines, including FM6 and CZ-1, and takes effect on held notes. It is saved with device
settings and stays selected when loading projects, templates or presets. MIDI note numbers and
incoming computer audio are unchanged; external MIDI instruments need their own tuning adjustment.

Projects use the FBKG format: all 32 banks, their timing, arrangement and automation. Felucca 1.0
projects load into pattern 1; their old project-based SONG rows are cleared. Pre-1.0 Melodee's
multi-pattern projects/settings/templates and incompatible 58/62-parameter user presets are not imported.
User sample slots USR1–3 and sample uploads are removed; their flash space now stores projects.
SAMPLE, GRAIN, SLICE, OBXF and the custom drum kit are removed; DRUM provides synthesized 808 and
909 kits. Earlier sample data is overwritten as projects
are saved. Complete backups with nonempty user samples require firmware that supports those slots.
FM6 and CZ-1 have independent native user collections: F001–F064 store the 128-byte Dexed/DX7
voice, and Z001–Z128 store the complete 144-byte Casio tone. They appear alongside factory tones
in normal global and engine-specific preset scrolling, and can be favorited. SAVE > USER selects
the current engine's collection; FM6 > STORE uses those same FM6 slots. Native saves exclude
Felucca envelopes, modulation settings and patterns. Loading keeps these track settings;
when changing engines, only engine-specific controls receive their defaults.

The 64 general U01–U64 slots remain available for other engines. First boot copies saved FM6
voices and saved CZ banks into their native collections, imports embedded CZ user tones into free
CZ slots, and releases successfully migrated general slots. If CZ's collection is full, unmatched
legacy tones stay in their general slots. Migration and saves use atomic flash writes; full backups
include both collections. The editor's User presets collection selector imports/exports .syx directly.
Going back to older firmware cannot access these new native FM6 slots or U33–U64.

## Engines

In the order the device lists them:

CZ-1, FM6 and Prophet use the FM-1's native single-precision FPU for continuous
signal arithmetic. Patch bytes and precise integer phase counters remain intact.
CZ-1 evaluates the chip's phase functions continuously; FM6 retains the MARK I
and OPL ROM quantization, with float buses, gains and feedback. Float rounding
and fractional feedback mean FM6 is no longer sample-exact against Dexed.
Prophet's filters, envelopes, oscillators, sync and Poly-Mod now keep fractional
state until the final PCM conversion. Its SSI/Curtis models still require audio
calibration; the FPU conversion does not establish hardware fidelity.

- **PROPHET**: Sequential Prophet-5 Rev 4 programs, eight voices per track with the default
  dual-core build (five if CPU1 is unavailable or disabled): oscillators A and B (saw,
  pulse, B triangle, low-frequency and keyboard switches), hard sync, Poly-Mod, LFO and noise wheel
  modulation, the SSI (Rev 1/2) or Curtis (Rev 3) four-pole filter, filter and amplifier envelopes,
  Vintage, glide and unison. Each voice uses two of the shared voice slots with CPU1 online,
  or three with serial rendering
- **FM6**: classic 6-operator FM (Dexed-based): 32 algorithms, a full patch per track edited in the
  web editor or on the device; operator frequency, levels, envelopes and scaling, pitch envelope,
  LFO, STORE and DX7 SysEx; an algorithm chart on screen. The main knobs control MLVL, MRAT,
  MEG and DTUN. PRESETS selects the operator on operator pages. Algorithms 4 and 6 use three- and two-operator feedback loops in MARK I; MODERN and OPL
  use OP6 self-feedback, so 4 / 6 sound like 3 / 5 in those engines. Set ENGINE to MARK I on
  FM PORTA and raise FB above 0 to hear the longer loops; the algorithm chart follows ENGINE.
  Each track keeps its own patch and function settings (MODERN / MARK I / OPL, pitch bend,
  portamento, wheel, foot, breath and aftertouch), saved with the project and the template
- **CZ-1**: native Casio tones, with separate eight-point pitch, timbre and volume envelopes on each line
- **SID**: a Commodore MOS 6581 / 8580 per note, modelled at the register level after reSID: three oscillators on the 16-bit frequency register (pulse width 12 bits, the 23-bit noise register, ring modulation, hard sync, combined waveforms sampled from real chips), the chip's envelope generator (16 rates, exponential decay, the ADSR delay), each revision's DACs and cutoff curve, the 6581's thump through the C64's output stage; 32 factory presets
- **DRUM**: synthesized TR-808 and TR-909 kits with the General MIDI key map. Every drum has its
  own tuning, decay, tone, attack/snappy and level controls; recorded hits can override pitch and length

ANALOG, DIGITAL, PHASE, VOICE, NOISE, TRIO, WHEEL and PHYS are retired. Their engine IDs, saved edit
values and favourite bits remain reserved; old tracks using these engines are silent. PROPHET replaces
ANALOG in the engine list, and track 1 powers on with PROPHET FUNK BASS II.

PROPHET opens on It's a Prophet 5 (or on user slot P001 once it holds your own program). PRESETS lists
INIT and Sequential's 200 factory programs, then the native user slots P001–P128. In the web editor,
choose PROPHET to edit a track's program field by field, read and send it, import or export .syx
programs and banks in Sequential's format, and manage the user slots. Calibration, storage and format
notes: [docs/PROPHET5_PROTOTYPE.md](docs/PROPHET5_PROTOTYPE.md).

The DIGITAL engine of 0.9 has been replaced by FM6: projects and presets with DIGITAL sounds load
as FM6 sounds converted from them.

CZ-1 opens with a native INIT TONE; PRESETS then lists Casio's 64 CZ-1 preset tones (A-1 BRASS 1 to
H-8 TYPHOON SOUND). The separate 128-slot user collection starts empty. Imported Casio tones use
their original oscillator and six eight-point envelope parameters.

**Native CZ-1 SysEx**: choose CZ-1 in the web editor, then **CZ-1 native patches → Import .syx**.
Select a tone from the imported bank and **Send to track**. **Read track** retrieves its original tone;
**Export .syx** writes a CZ-1-compatible tone. **Add to library** retains it for later use.
In **User presets**, choose **CZ-1**, then import .syx into free native slots or save the current track's
native tone. Export writes the collection as Casio frames. The device scrolls these tones in PRESETS;
there is no BANK / PTCH selection page. Full backups include all 128 slots.
Both lines have separate eight-point DCO/DCW/DCA envelopes, including sustain,
end points, velocity sensitivity and key follow. The original 144-byte CZ-1 tone survives user presets,
projects, templates and library export; it is not reduced to common ADSR values. Compatible 128-byte
CZ tones are accepted with CZ-1 defaults for fields they lack. See [format and fidelity notes](docs/CZ1_SYSEX.md).

Native mode uses the documented uPD933 phase functions, rate law and logarithmic DCA response.
Vibrato timing, key follow, velocity response, DAC behavior and output filtering still need hardware
calibration; this build does not establish near-identical CZ-1 audio.

SAMPLE, GRAIN and SLICE and all recorded sample material have been removed. Engine numbers 4, 8
and 13 remain reserved so existing projects do not accidentally select another instrument. Old tracks
using those engines are silent until another sound is chosen. Historical separate GM drum parts load
as the synthesized 808 kit.


## Scale keyboard

The first **SCL** page is **SCALES**, a browser with full names and note counts.
**KNOB 1** selects a family (or ALL / FAV), **KNOB 2** browses that list,
**KNOB 3** sets ROOT beside SCALE, and **KNOB 4** sets QNT.
The **PRESETS** knob also browses scales on this page. Family changes select the first
matching scale when the current one is outside the family; an empty favorites list keeps
that scale and shows how to add it. Device favorites survive power-off.
Press **SCL** again for the settings page: ROOT, FAV, QNT and TRN. **KNOB 2** there
saves or removes the active scale as a favorite without changing its tuning.
Press **SCL** once more for CHORD.
The web editor adds family filtering, search by full or short name, and favorites saved
in that browser. Its filters keep the active tuning until a scale is selected;
browser favorites and device favorites are separate.

On the **SCL** page, set **QNT** to WHITE to play the selected scale using only the
white keys (SNAP keeps every key and rounds it down to the scale). C4 plays **ROOT**; consecutive white keys play consecutive scale notes
above and below it. Black keys are silent, including during live recording and
step entry. **TRN** transposes the resulting notes; the octave buttons shift them
by full repeat periods (octaves for the original scales). Set QNT to OFF for the normal chromatic keyboard.

Available scales: chromatic (CHR), major (MAJ), natural minor (MIN), Dorian (DOR),
Mixolydian (MIX), major pentatonic (PEN), minor pentatonic (MPEN), harmonic minor
(HARM), Phrygian (PHRY), Lydian (LYD), Locrian (LOC), ascending melodic minor (MEL),
minor blues (BLUES), whole tone (WHOLE), half-whole diminished (DIMHW), and
whole-half diminished (DIMWH). Scales with other than seven notes continue across
the white keys without repeating notes; their roots need not fall on every C key.

The list also includes **54 microtonal selections**, for **70 scales in total**:
selected equal divisions from 5 to 53, just intonation, harmonic scales, Partch, historical temperaments,
maqam models, shruti, gamelan examples and Bohlen–Pierce. See the [complete catalogue and
playing notes](assets/scales/README.md). QNT WHITE, ALL and MPC play consecutive degrees;
SNAP rounds the keyboard and incoming MIDI pitches down to a scale degree. QNT OFF bypasses
microtonal tuning. ROOT and TRN shift pitch in ordinary semitones. The octave buttons,
ARP OCT and chord voicings move by the scale's repeat period (3:1 for Bohlen–Pierce).
Microtonal notes display as degrees, such as D1 and D3+1, and retain separate identities
when several degrees fit inside one semitone. Recordings and projects retain these degrees.
MIDI OUT carries degree addresses; an external synth needs the same tuning and mapping to
reproduce these pitches.

QNT **ALL** plays the next scale note on every key, black keys included. QNT **MPC** maps an
MPC's Bank H pads (MIDI 20–35, H01–H16 of MPC Sample's default map) to successive scale notes;
the **MPC** page in the SCL family sets **DEG**, the degree pad H02 plays. Incoming MIDI follows
WHITE, ALL and MPC the way the keys do (without the octave buttons). SCL, QNT and DEG are shared by
all four tracks; ROOT and TRN stay per track. Drum kits keep their own note mapping.

On the **CHORD** page, CHRD picks the chord keys (OFF, the scale's triads or
sevenths, or a fixed shape) and VOIC the voicing.

## Layout

| Path | What |
| --- | --- |
| `firmware/` | firmware sources: `src/` app, `hal/` hardware layer, `loader/` update loader |
| `tools/` | build script, generators, package maker, installer and sample uploader |
| `assets/` | UI font and icon names |
| `web/` | web installer and editor sources |
| `tests/` | tests that run on the build machine |
| `LICENSES/` | licence texts of the bundled font, icons, ported DSP and SDK files |

## Support

You can leave a tip with [ko-fi](https://ko-fi.com/keremimo).

## Credits

- Melodee by Ellic Studio (Kerem Kilic, [@keremimo](https://github.com/keremimo))
- Logo: Smallcutekitty ([smallcutekitty.net](https://smallcutekitty.net)), Kerem's wife
- Based on [Felucca](https://github.com/hugelton/Felucca) by Leo Kuroshita ([@kurogedelic](https://github.com/kurogedelic)), [Hügelton Instruments](https://hugelton.com)
- **[Hügelton Instruments](https://hugelton.com)** (Leo Kuroshita, [@kurogedelic](https://github.com/kurogedelic)):
  Felucca itself; the PHASE engine's waveforms (a C port of the oscillator of
  [CrispyZebra](https://github.com/hugelton/CrispyZebra), GPL-3.0); the synthesized 808; the [Fukiai](https://github.com/hugelton/Fukiai) icon
  font of the web editor ([MIT](LICENSES/MIT-Fukiai.txt))
- Font: [Rubik](https://github.com/googlefonts/rubik) by The Rubik Project Authors, [SIL OFL 1.1](LICENSES/OFL-Rubik.txt);
  the panel diagram: [Inter Tight](https://github.com/rsms/inter-tight) by The Inter Project Authors, [SIL OFL 1.1](LICENSES/OFL-InterTight.txt)
- VOICE engine: after [klattsch](https://github.com/tgies/klattsch) by Tony Gies (MIT); formant data from Klatt (1980) and Hillenbrand et al. (1995)
- Native CZ engine: uPD933 model by Devin Acker in [MAME](https://github.com/mamedev/mame/blob/master/src/devices/sound/upd933.cpp) ([BSD-3-Clause](LICENSES/BSD-3-Clause-uPD933.txt))
- PHYS engine: models ported from [DaisySP](https://github.com/electro-smith/DaisySP) by Electrosmith and Emilie Gillet ([MIT](LICENSES/MIT-DaisySP.txt)) and from Emilie Gillet's [eurorack](https://github.com/pichenettes/eurorack) code ([MIT](LICENSES/MIT-Rings.txt))
- SID engine: after [reSID](https://github.com/libsidplayfp/resid) by Dag Lem (GPL-2.0-or-later); its samples of a real
  6581's and 8580's combined waveforms are included as data ([assets/resid](assets/resid/README.md))
- FM6 engine: msfa from [Dexed](https://github.com/asb2m10/dexed) by Google Inc. and Pascal Gauthier ([Apache-2.0](LICENSES/Apache-2.0-msfa.txt))
- Package format and boot files: [JieLi AC79 SDK](https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK) ([Apache-2.0](LICENSES/Apache-2.0.txt); three of its files are in every package, none in this tree)
- Contributions: [Charles Vestal](https://github.com/charlesvestal) (knob acceleration and 808/909 models),
  [keremimo](https://github.com/keremimo) (white-key scales, #2), [ChanceTheMaker](https://github.com/ChanceTheMaker)
  (TRS MIDI, bend, sustain and clock, palettes, favourites, editor display settings: #8, #10, #11, #12),
  [andreahaku](https://github.com/andreahaku) (sample recording and trim, #29; SLICE manual slices and tests, #27, #22),
  [johnskiller](https://github.com/johnskiller) (the boot-time SYS clock retune behind OVERCLOCK, keremimo/melodee#27)

## Licence

Free software: [GPL-3.0-only](LICENSE). The bundled font and the
ported DSP keep their own licences ([LICENSES/](LICENSES/)); details in [LICENSING.md](LICENSING.md).

"Felucca" and "Hügelton Instruments" are names of Hügelton Instruments. M-VAVE and FM-1 are
trademarks of their respective owners. Melodee is not affiliated with or endorsed by any of them.

Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments\
Modifications Copyright (C) 2026 Kerem Kilic (Ellic Studio)

See [Recording and performance controls](docs/RECORDING_PERFORMANCE.md) for count-in, metronome, drum levels, temporary knob edits, additive chords and MIDI parameter control.
