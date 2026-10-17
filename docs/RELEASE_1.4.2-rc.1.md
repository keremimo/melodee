Melodee 1.4.2-rc.1 is the first release candidate for 1.4.2, built from the
`nightly` branch. It includes everything in [1.4.1](RELEASE_1.4.1.md), plus:

- **Four tracks to start:** new songs open with four tracks (Prophet, FM6 bass,
  CZ lead and 808 drums), and unused tracks are listed so they can be added back.
- **Chord strums:** voiced chords can be spread over a beat division with down,
  up or alternating strokes (STRUM/STROKE). Recording keeps the played onsets;
  release, stop and panic cancel pending tones. Existing projects, templates and
  presets load with strumming off.
- **Pattern halve and double:** SEQ's pattern dialog halves (C#) or doubles (D#)
  the pattern, and stays open for further steps until SEQ is released.
- **Voice weights:** GLO > WEIGHTS sets budget points per voice for each engine,
  with an optional overload SAFETY (off by default).
- **Audio performance:** the CPU optimizations from nightly are now the default
  in every build: modulation caching, faster control, mixing, panel, recording index and
  capture paths, idle FX sleeping, and engine-specific fast paths for Prophet,
  FM6, CZ, drums and SID. Parts and drums are balanced across both cores, eligible
  adjacent reverbs overlap, and part of the data cache is used as audio RAM.
- **Memory:** inactive pattern banks are stored compressed, FM6 SysEx dumps reuse
  project RAM, and recording release queues share storage. Save formats for
  recordings and projects are otherwise unchanged.
- **Fixes:** each track keeps its own preset list in the browser; Stage drum cards
  no longer flicker under the knobs; the SCL cycle skips the scale picker.
- **CPU1 failure:** this package does not fall back to single-core rendering. If
  CPU1 fails, the firmware waits for the watchdog restart; the boot-loop guard is
  retained.

This is a prerelease for testing, with the same build configuration as the
nightly channel. The full host suite runs against that configuration; physical
testing of this exact package is pending. Projects, templates and presets saved
by this candidate carry the two strum parameters, which 1.4.1 does not read:
back up before installing if you may return to 1.4.1.

Downloads include the firmware package and application binary, native UBOOT
recovery tools for Windows x64, macOS Apple Silicon, macOS Intel and Linux x64,
and licence notices. `BUILD_INFO.json` records the source commit, build switches
and native smoke tests; `SHA256SUMS` covers every asset. The web installer offers
this candidate alongside stable and nightly; stable remains the default.
