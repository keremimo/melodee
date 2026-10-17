# Melodee on the SMK-37 (beta)

Melodee also builds for the SMK-37, a 37-key controller with the same chip as the FM-1. The FM-1 build is
unchanged; everything for the SMK-37 is behind `BOARD_SMK37`.

## Supported models

- **SMK-37 Pro**: the default build (package identity `SMK-37 Pro_900`).
- **SMK-37 Elite**: `MODEL=elite` (identity `SMK-37 Elite_900`).
- **MKE-P37**: `MODEL=mke` (identity `MKE-P37_900`).

All of them are the same hardware. Each build carries its model's name, so the stock updater still takes that
unit's stock package later: install the package for your model. The installers take a package only for exactly
the connected model (an Elite package is refused on a Pro, an FM-1 package on any SMK, an SMK package on an FM-1);
the unit's own stock package is the same model, so the way back to stock is always open. The firmware itself
refuses a package that is not laid out for the SMK-37 (its flash.bin is not the SMK's app area, 0x9D000 bytes, or,
with the stock loader, its region table or USR image differ) before anything is erased: the screen shows
FAILED -15, -16 or -17.

## Controls

The FM-1's manual still applies; the SMK-37's controls map onto the FM-1's like this.

| SMK-37 | Melodee |
| --- | --- |
| Keys F3..G5 (the middle 27) | the FM-1's 27 keys, with velocity |
| Keys C3..E3 and G#5..C6 (the outer 10) | more notes for the selected track (its key map, scale, chord and octave), with velocity |
| 16 pads | the 16 white keys F3..G5, with velocity, low notes on the bottom row; where the keys are a grid (SEQ held: the patterns, the DRUM grid, NAME, the layers) steps 1-8 are the top row and 9-16 the bottom row |
| Pad lights | every pad lit, in three colours: off dark orange nothing there, on light blue something there (a key in the layout), a brighter blue a pattern or step holding notes, active bright happening now (held, playing); a pattern waiting for the bar pulses orange. While the transport runs: the selected track's steps on the page that plays |
| K1..K4 | KNOB 1..4 (the screen calls them K1..K4) |
| K5 | MASTER (volume) |
| K6 / K7 / K8 | SELECT / ALGORITHM / PRESETS |
| FX, SCALE, CHORD, NOTE REPEAT, PARA, GLOBE | FX, SCL, ENV, LFO, EDIT, GLO |
| PATCH / BT | HOME / SAVE |
| ARP / SEQ | ARP / SEQ |
| SEQ PLAY / SEQ REC | PLAY / REC |
| OCTAVE- / OCTAVE+ | OCT- / OCT+ |
| KNOB BANK | as HOME held: opens or closes the menu (lit while the menu is open) |
| < / > | one step of SELECT: the previous / next page, pattern, row or note, wherever SELECT turns |
| FADER BANK | what the faders set (lit off bank A): A the levels of tracks 1..4; B the selected track's filter (PROPHET: CUTOFF, RESONANCE, ENV AMOUNT, filter DECAY; other engines their four HOME knobs); C its amp envelope (A D S R); D its FX rack (the MIX of slots 1-3, the SEND to the shared FX) |
| Faders 1..4 | as FADER BANK says; a fader takes over only once it moves |
| Pitch wheel | pitch bend of the selected track (2 semitones; PROPHET its BEND, FM6 its DX7 range) |
| Mod wheel | the selected track's mod wheel (CC1, the MOD matrix's MODW source; PROPHET and FM6 their own wheel); engines without one get a vibrato unless a MOD slot uses MODW |
| OCTAVE- + OCTAVE+ held at power-on | HARDWARE CALIBRATION: teach the buttons and knobs again |
| OCTAVE- held at power-on | the boot stub's rescue: update mode |

Project slots A and B are stored on the SMK-37; C and D have no flash there ("SMK: PROJECTS A, B ONLY"). The
SMK-37's TRS jack is a MIDI output and this build does not use it; there is no TRS MIDI IN. MIDI works over USB.

## Build

See [BUILDING.md](BUILDING.md) for the toolchain.

```sh
BOARD=smk37 ./build.sh                 # build/melodee-smk37.fwsc (SMK-37 Pro)
BOARD=smk37 MODEL=elite ./build.sh     # build/melodee-smk37-elite.fwsc
BOARD=smk37 MODEL=mke ./build.sh       # build/melodee-smk37-mke.fwsc
./build.sh                             # the FM-1 build again before tests/run_tests.sh (the tests use its files)
```

## Install

Connect the keyboard by USB and run, with the package for your model:

```sh
python3 tools/fm1_install.py build/melodee-smk37.fwsc
```

`fm1_install.py` needs `mido` and `python-rtmidi` (`pip3 install mido python-rtmidi`). It checks that the package
is for the connected model.

## Going back to the stock firmware

Install the manufacturer's firmware package for your model with the manufacturer's updater. It works directly, because
the installed package keeps the keyboard's model name. `tools/fm1_install.py` can also install the stock SMK-37 Pro
V016 package.

## Safety

- Back up first: have the stock package for your model and the manufacturer's updater ready before you install.
- Keep the USB cable connected until the install has finished and the keyboard has restarted.
- The first start clears the stock firmware's settings, and any other firmware's data, in Melodee's store.
- The stock firmware's key, knob and fader mapping (in USRFLASH) is kept while it is intact. Before Melodee first
  writes there, it releases the stock firmware's marker for that area (one sector, 0xEA000, which never holds
  Melodee's data), so after going back to stock the stock firmware rebuilds its factory mapping instead of reading
  Melodee's data.
- The firmware never writes the keyboard's calibration, Bluetooth pairing, USR or key areas; the flash map refuses
  them at compile time and again in the flash driver.
- Installing other firmware is at your own risk.
