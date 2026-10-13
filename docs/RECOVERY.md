# FM-1 USB recovery

This command-line tool recovers an FM-1 **already in ROM UBOOT** over a normal USB
data cable. It does not force a running device into recovery. USB identity must be
`4c4a:8057`, with inquiry `WL82 / UBOOT1.00`. If you see a **Melodee Update** MIDI
port, resume installation with the web installer instead.

Download the archive for your computer from the Melodee 1.3.1 release and extract it.
Windows and Linux builds are x64; macOS has separate Intel/x64 and Apple Silicon/arm64
downloads. The executable includes Python, PyUSB, libusb and the RAM recovery helper.
It does not require a Python installation or the JieLi SDK. These are unsigned CLI
executables; use Terminal or PowerShell from the extracted directory.

Windows needs the **WinUSB driver for 4c4a:8057**. Use Zadig to install that driver
only for the ROM recovery device, not Melodee's normal MIDI/audio device. Linux may
need a udev rule or `sudo`. On macOS, USB access denial may require `sudo`.

Choose a `.fwsc` firmware package from the release downloads. The recovery tool
does not select or download firmware for you. To investigate a startup regression,
use a previously working version. Compare the file with the release's `SHA256SUMS`.

The corrected tool also supports the unmodified **official M-VAVE V15 `FM-1.fwsc`**
(identity `FM-1_015`). Download it from M-VAVE; it is not included in the recovery
archive. The complete file must have SHA-256
`db1642b2b6fa5c2cccb11ffd13878068bb28601678d3644049f99dc40e7edb8a`.
Modified V15 packages are refused before USB access. The recovery archives on the
1.3.1 release were corrected on 2026-10-13 to accept V15's metadata layout. If an
older download rejects V15, download the archive again.

For official recovery, substitute `FM-1.fwsc` in the commands below. Keep the full
backup: recovery retains the bootloader and saved-data bytes, but official firmware
may overwrite Melodee's songs, sounds or settings after it boots.

On macOS/Linux:

```sh
./fm1-recover --info
./fm1-recover /path/to/working.fwsc --check
./fm1-recover /path/to/working.fwsc
./fm1-recover /path/to/working.fwsc --write
```

On Windows PowerShell:

```powershell
.\fm1-recover.exe --info
.\fm1-recover.exe C:\path\to\working.fwsc --check
.\fm1-recover.exe C:\path\to\working.fwsc
.\fm1-recover.exe C:\path\to\working.fwsc --write
```

`--info` identifies the ROM device without changing RAM or flash. `--check` validates
the package and bundled RAM helper without accessing USB. With a package and no
`--write`, the tool loads a temporary RAM helper and saves a full flash backup; it
does not change flash. `--write` makes a fresh backup, restores changed application
sectors, and verifies the whole flash. Keep the USB connection stable until it ends.

Backups go into unique directories under `fm1-recovery-backups/`; use `--backup-dir`
to choose another parent. Keep `full-flash-before.bin`, `backup.json` and
`retained-ram.bin`. The complete backup and manifest are synced to disk before the
first erase. The bootloader below `0x4000` and current saved data at/above `0x93000`
are retained. Legacy Prophet banks at `0x89000` are ignored and replaced by firmware;
they survive in the full backup, but are not relocated or loaded by current firmware.

Success prints **RESTORE VERIFIED** and creates `recovery-result.json`. Power-cycle
the FM-1 after recovery or a backup-only session. The report verifies flash contents;
check that the chosen firmware boots, plays audio, and responds to controls afterward.
If a write fails or USB disconnects, retain the original backup, reconnect in UBOOT
and retry. Completed sectors are skipped. Failed runs do not create a success report.

Connect one FM-1 at a time. `--bus` and `--address` select a device when necessary;
`--wait` waits up to 60 seconds for ROM USB. `--libusb PATH` can replace the bundled
library. `--loader PATH` can override the RAM helper with the same verified SDK file.
`--help` lists all options.

The direct-USB protocol has prior successful macOS recovery evidence. Each executable
is built and smoke-tested on its native OS; automated tests model interruptions and
protected flash regions. Those checks do not establish physical USB recovery on
every OS or prove that removing bank relocation fixes the 1.3.0 reports.

Source: https://github.com/keremimo/melodee (tag v1.3.1).
Protocol reference: https://github.com/kurogedelic/FM-1-transporter.
Licences and corresponding libusb source are included in the archive.
