# Two Bluetooth links at once (BLE_LINKS 2)

Melodee's BLE stack (`firmware/src/ble/`, Optimist's, [BLE_MIDI.md](BLE_MIDI.md)) held one link: advertising, scanning,
initiating or one connection ([BLE-STACK.md](BLE-STACK.md) §9, [BLE-HW-FACTS.md](BLE-HW-FACTS.md) §21.5-21.8). With
`BLE_LINKS 2` it holds two: a **peripheral link** (link 0: the computer or phone that connects to the FM-1) and a **central
link** (link 1: a BLE-MIDI keyboard or controller the FM-1 connects out to), each on its own instance of the radio engine.
So a BLE keyboard can play Melodee while an iPad or a Mac is connected to it. Upstream Optimist has no multi-link code
(checked 2026-11-06 against `w0ts/optimist` `8a91efd6`: "One link at a time", "Not built: two links at once"), so this is
new work, and **none of it has been on a device yet**.

- Build: `MELODEE_BLE_LINKS=2 ./build.sh` (FM-1 or `--board smk37`); the default (unset or `1`) is one link.
- Code: the classic stack `firmware/src/ble/` and `firmware/hal/fm1_ble.h`, the glue `firmware/src/midi_ble.c`.
  `platform/ble/` (the parked fm1-base port's copy) carries the same stack changes and stays tested beside it.
- `BLE_LINKS 1` is the stack as it was in behaviour: one bank, both roles on link 0, every link argument 0. Its code was
  reorganised for the banks, so the image is not byte-identical to nightly (below) and needs its own device check.

## Roles are links

| | link 0 `BLE_LINK_PERIPH` | link 1 `BLE_LINK_CENTRAL` |
|---|---|---|
| role | advertise, then slave of the computer | scan, initiate, then master of the device |
| LL states | `LL_OFF` `LL_ADV` `LL_CONN` | `LL_OFF` `LL_SCAN` `LL_INIT` `LL_CONN` |
| engine | instance 0, control block at +0x40 | instance 1, its own control block and RX / TX buffers |
| host | ATT server, SMP responder | GATT client, SMP initiator, `ble_central.c` |
| Melodee | Mode VISIBLE | Mode CONNECT OUT, Last device, the found list, relink |

## How the stack tells the links apart

One context: every call into the stack comes from the BLE interrupts, or from the main loop with them held
([BLE-STACK.md](BLE-STACK.md) §1). So per-link state is **banked behind a selector**, not passed through every function:

- `ble_ll.c`: `llg` (address, advertisement, the host's wishes: shared) and `ll_b[BLE_LINKS]` (one `struct ll_link` per link:
  state, connection, TX queue, encryption). `bll` is the selected bank (`ll_p`); `ll_enter(n)` selects, `ll_leave(was)` gives
  the previous one back. **Every public entry point selects its link first and restores the previous one**, so outside the
  stack `bll` is link 0: the no-argument getters (`ble_connected`, `ble_ll_interval`, `ble_att_mtu`, `ble_midi_ready` ...) still
  mean the computer's link, as they always did.
- `ble_host.c` (`bhs_b`), `ble_att.c` (`att_st_b`), `ble_smp.c` (`bsmp_b`): per-link arrays, indexed by `ble_ll_link()`
  (the selected link). `ble_gattc.c`, `ble_central.c`, `llc` and `bscan` are the central role's alone and are not banked.
- Per-link answers for the main loop: `ble_ll_link_connected(link)`, `ble_midi_ready_link(link)`, `ble_ll_scanning()` /
  `ble_ll_initiating()` (the central link's), `ble_central_*` (the central link's, `ble_central_cancel` ends only it).
- The firmware's hooks (`ble_app_midi_peek` / `pop` / `in`, `ble_app_state`) are called under the link they belong to:
  **`ble_ll_link()` inside a hook says which**, so a firmware keeps one MIDI queue pair per link without a signature change.
- Driver interface ([`ble_hw.h`](../firmware/src/ble/ble_hw.h)): `ble_hw_conn_stop` / `conn_update` / `chmap_update` /
  `set_lengths` / `tx_kick` and the reports `ble_ll_hw_rx` / `tx` / `tx_pending` / `tx_acked` / `event_end` take the link. The
  roles' set-up calls (`adv_*`, `conn_start`, `connect_ind`, `scan_*`, `init_*`, `adv_report`, `master_start`) are bound to
  their role's link.
- `BLE_STATE_MEMORY` (`ble_cfg.h`, empty by default) places the banked state; Melodee's two-link build puts it in `.pool`
  (below).

## The idle policy

Advertising, scanning and initiating are the activities of a link with no connection. `BLE_IDLE_EXCL` (1, the default) keeps
them as they were: **only one runs at a time** (a scan or an attempt to connect pauses advertising). Connected links go on
beside whichever one runs. That is what the radio has to share, and the part the engine is *not* known to do:

- link 0 connected, link 1 scans or initiates: a connection event and a scan window share the radio by RFPRIO (28 / 30 against
  17 / 26, [BLE-HW-FACTS.md](BLE-HW-FACTS.md) §21.5-21.8)
- link 1 connected (master), link 0 advertises: the same, the other way round

`BLE_IDLE_EXCL 0` lets advertising and scanning run side by side too (HW §21.9 C9-C12 open; not used by any build).

## The driver (`ble_hw_wl82*.c`)

- `struct ble_lk` = control block + 2 RX + 2 TX buffers per link; `bb.lk[BLE_LINKS]` after the instance table, link 0's block
  still at +0x40 (`bb` is `BLE_DMA_MEMORY`, `.pool`).
- `hw_n` is the link the running call works on (`hw_enter` / `hw_leave`, like `ll_enter`); `CB`, `LK`, `drv` and `HW_LINK`
  follow it (pointers `lk_p` / `drv_p`, so the single-link driver code is unchanged).
- `ble_wl82_rx_irq` / `ble_wl82_event_irq` serve **every link whose enable and pending bits are both set** (HW §8: the vendor's
  handlers loop over links); with one link they serve link 0 as before.
- `fm1_ble_link_irqs_on` read-modify-writes the second-group enables (`0x2804C`), so starting link 1 does not clear link 0's.
- A stop waits for the engine's busy flag (`0x28038` bit1) at most `HW_STOP_SHARED_US` (2 ms) while the other link has
  something running, instead of the 3-40 ms it waits alone: the flag may stay set for the other link's events. A guess.
- The window widening and supervision follow w0ts/optimist#17 (ceil widening with a 16 us floor, supervision on CRC-good
  packets only, the widening in the update and steady windows: nightly `dd17bc2c`), per link (`drv.widening`). The classic
  driver is the platform copy's but for the two header lines.

## Melodee's glue (`firmware/src/midi_ble.c`)

- `struct mb_link` per link in `mb` (`.pool`): its in ring (TIMER5 drains it into `midi_enqueue`, source 3), its out ring, its
  clock / transport ring, `ready`, `connected`, `closing`, `lost`, `overflow`. The hooks pick theirs with `MBL`
  (`&mb.ln[ble_ll_link()]`).
- **MIDI out goes to every link that is up**, admitted whole: `mel_ble_room()` is true only when every link that is up has
  room, so a retry never duplicates a message on one link (as USB and BLE already were). Clock and transport: each link admits
  them alone (a stalled link costs the other nothing).
- **MIDI in from both links** is source 3 (Bluetooth). `midi_ble_lost` (a link went, or its ring overflowed) ends every
  Bluetooth note: seq.c knows one Bluetooth source, so a keyboard leaving also releases a note the computer holds, and a
  Bluetooth clock source stops. Rare, kept simple.
- **MIDI THRU** (the menu's, `midi_thru.c`) sends with link masks: `mel_ble_emit_to(pkt, links)`,
  `mel_ble_sync_to(pkt, pulse, links)`, `mel_ble_room_for(n, links)`; a Bluetooth message goes to `mel_ble_other_links()`, the
  links it did not come in on (0 with one link: as before, not back to Bluetooth). So the keyboard's notes and, with CLK BLE,
  its clock reach the iPad, the iPad's reach the keyboard, and a full queue on a link a message is not for holds nothing up.
- **Mode** keeps its meaning, applied to the link that has nothing to do: VISIBLE = link 0 advertises (no page scan),
  CONNECT OUT = link 1 scans on the page. **Both relink the last device** (Kerem 2026-11-06; one link: CONNECT OUT only): a
  relink round in VISIBLE scans 10 s every 30 s while the device is away, and advertising pauses for those 10 s
  (`BLE_IDLE_EXCL`); the status line says `RECONNECTING` during a round and `VISIBLE 1A2B` between them. A link that is up
  stays up through a Mode change. The one-link gates "a link is up: no scan / no connect / no relink" became "**the
  device's** link is up". So:
  - a saved VISIBLE at power-on: the keyboard relinks and the iPad finds Melodee between rounds (once the keyboard is back,
    advertising runs all the time);
  - keyboard first: CONNECT OUT, connect it; then Mode VISIBLE: the iPad connects beside it;
  - iPad first: VISIBLE, the iPad connects; then Mode CONNECT OUT: the page scans and connects the keyboard beside it;
  - "Last device" works in either Mode (it pauses advertising while it initiates).
- An attempt out that times out ends only link 1 (`mb_drop_out`, `ble_central_cancel`); **Disconnect** and **Forget** end both.
- Status (header, Bluetooth page): `COMPUTER + DEVICE` with both up, `BLE CONNECTED` with one (unless link 1 is scanning or
  connecting: then that), the rest as before.
- Not done: a page scan in Mode VISIBLE (needs `BLE_IDLE_EXCL 0` or time-slicing), per-link note ownership.

## What it costs (classic builds of 2026-11-06, nightly `dd17bc2c` as the baseline, `./build.sh` default settings)

Limits, from the build itself: the app body (image minus the 3,808-B stub) ends below the commit marker's sector,
`BODY_MAX` 573,440 B (`tools/fm1pkg_make.py`; both boards); `.data` + `.bss` in the 96 KiB `RAM` region (`firmware/app.ld`);
the audio working arena (what `.pool`, the resident audio code and CPU1's stacks leave of `POOL`) at least `_resource_minimum`, 48 KiB with resident audio (the
default) (`app.ld`, checked by the linker and by `tools/build.py`); 8 KiB of pool headroom (`build.py`).

| build | image | body free | RAM `.data`+`.bss` (of 98,304) | pool | audio arena (min 49,152) | all audio resources |
|---|---|---|---|---|---|---|
| FM-1 nightly | 570,932 B | 6,316 B | 97,480 | 133,028 | 78,784 | 81,024 |
| FM-1 `BLE_LINKS 1` | 568,320 B | 8,928 B | 97,496 | 133,028 | 78,816 | 81,072 |
| FM-1 `BLE_LINKS 2` | 571,340 B | 5,908 B | 95,256 | 139,556 | 72,192 | 76,688 |
| SMK-37 Elite nightly | 567,244 B | 10,004 B | 98,024 | 134,648 | 73,856 | 75,456 |
| SMK-37 Elite `BLE_LINKS 1` | 568,532 B | 8,716 B | 98,056 | 134,648 | 73,792 | 75,360 |
| SMK-37 Elite `BLE_LINKS 2` | 571,904 B | 5,344 B | 95,864 | 141,176 | 67,136 | 70,896 |

- **Where the room is:** the classic FM-1 body has 6.3 KB free on nightly (the fm1-base image had 0.3 KB), so the code
  (+3,020 B against `BLE_LINKS 1`, +408 B against nightly) fits with 5.9 KB to spare. `.data` + `.bss` is the tight one (824 B
  free on nightly), so the banked state of both links goes to `.pool` (`BLE_STATE_MEMORY`), which comes off the audio arena;
  the arena keeps 23.0 KB over its 48 KiB floor. Net SRAM cost of the second link: 4,336 B of audio resources
  (81,024 -> 76,688). The SMK-37 Elite the same way: 5.3 KB of body left, `.data` + `.bss` 2,440 B free (280 on nightly),
  arena 18.0 KB over the floor. (The fm1-base figures that looked like "no room", an FM-1 image 2 KB over and an SMK-37 arena under 48 KiB, were the
  base port's own layout; the classic build has neither problem.)
- RAM moved or added by `BLE_LINKS 2`: link 1's engine block 1,444 B (`bb`), its MIDI rings 1,188 B (`mb`), the link layer
  banks 936 B and TX rings 1,024 + 512 B (`BLE_LL_TX_RING_CENTRAL` 512, `midi_ble.h`), L2CAP 536 B, ATT 560 B, SMP 352 B,
  driver 280 B; link 0's share of these left `.bss` (-2,224 B).
- Code (the breakdown measured on `351d77eb`): BLE +2,744 B (the interrupt handlers' link loops `ble_wl82_event_irq` +560 B, `ble_wl82_rx_irq` +326 B, the
  pointer-selected banks and wrappers), +286 B elsewhere (the TIMER5 drain over two links, `fm1_main` inlining); the VISIBLE relink added 24 B after these were taken.
  Stack audit: `isr_ble_event` 744 B, `isr_ble_rx` 780 B (one link: 712 / 752).
- `BLE_LINKS 1` against nightly: image -2,612 B on the FM-1, +1,288 B on the SMK-37 (first measured on `351d77eb`: FM-1 BLE code
  -212 B and the rest -2.2 KB, SMK-37 BLE code -1,136 B and the rest +2.1 KB).
  The rest is code generation, not source: LLVM's global merging regrouped once `bll` / `batt` / `bhs` / `bsmp` / `drv`
  became bank arrays, and the same untouched functions move both ways (`mix_block` -138 B on the FM-1, +78 B on the SMK-37;
  `ui_input` -74 / +88 ...). No function was dropped or added beyond the renames (symbol tables compared).

## What is not known (hardware)

Everything above is tested against a fake engine and the host build only. These are the questions a unit has to answer, in the
order a bring-up meets them (the checklist below):

1. Does instance 1 start (scan, state 1) while instance 0 is a connection (state 7)? Does link 0 keep its anchors while link 1
   scans / initiates?
2. Does the `0x28038` bit1 stay set with a link live? If it does, the 2 ms bound is wasted time on every stop; if the engine is
   not idle at 2 ms the control block of the stopping link may still be in use.
3. The master's first anchor beside a live peripheral link: `hw_first_anchor` corrects a late event interrupt; two links make
   that interrupt late more often.
4. Interval clash: a computer at 7.5 ms and a keyboard at 11.25 ms collide on some events. The engine's RFPRIO arbitration
   decides which is dropped (master and slave both 28 with data queued: not tuned); the supervision timeouts (720 ms - 2 s)
   tolerate a few. fm1-base issue #10 saw the JieLi stack fail one order (computer first, then the keyboard: 0x3E, the service
   search timing out). Both orders are tested in `tests/ble_multilink_test.c`, but only on the fake.
5. ISR time with two links and software AES-CCM on both (the central pairs; the computer bonds), against the audio.

## Bring-up checklist

The classic build has no BLE counters to read (`BLE_DIAG 0`, no console in the USB audio build), so a bring-up watches the
device's status line (header and Settings > Bluetooth), the peers (Audio MIDI Setup / a MIDI monitor on the Mac, the iPad
app), the Mac's `bluetoothd` log (`log show --predicate 'process == "bluetoothd"'`, "LE ConnManager disconnection", reason =
700 + the HCI code: 0x08 supervision timeout, 0x3E failed to establish, 0x22 LL timeout) and the audio load (editor
command 33, AUDIO_STATS). Images (2026-11-06, nightly `dd17bc2c` merged in, FM-1 identity `FM-1_900`, in the worktree's
`build/ble-multilink/`):
`melodee-fm1-links1.fwsc` (the default, one link) and `melodee-fm1-links2.fwsc`; the SMK-37 Elite's beside them.
`tools/flash.sh build/ble-multilink/<package>`; the resources are nightly's (crc `e637e329`), nothing to push again.

1. **One link, regression first** (`melodee-fm1-links1.fwsc`: the stack was reorganised for banks, never run on a unit):
   VISIBLE + a Mac connects, MIDI both ways; CONNECT OUT + a keyboard (a WIDI, the OXI), notes in, Melodee's MIDI out to it;
   Disconnect, Forget, a power cycle with relink. Anything different from nightly here is a refactor bug, not a two-link one.
2. **Two-link image, one link at a time** (`melodee-fm1-links2.fwsc`): the same as 1. Same behaviour expected (link 0 alone,
   link 1 alone).
3. **Computer, then a scan** (question 1): VISIBLE, the Mac connects and plays; Mode CONNECT OUT on the page: does the list
   fill while the Mac stays connected and its notes keep playing for a minute? A Mac drop here (0x08) or an empty list = the
   second instance does not run beside a live connection.
4. **Computer, then the keyboard** (questions 3, 4): pick the keyboard from the list. The status must reach `COMPUTER + DEVICE`;
   keyboard -> sound, Mac -> sound, the sequencer's MIDI out on both. MIDI THRU on (menu): the keyboard's notes arrive at the
   Mac's MIDI monitor, the Mac's at the keyboard (if it takes MIDI in), nothing comes back to where it was played. Repeated 0x3E / FINDING MIDI that times out with the
   Mac up, but not alone, is fm1-base#10's trap (the master's first anchor or an interval clash).
5. **Keyboard, then the computer** (the other order): CONNECT OUT, keyboard up; Mode VISIBLE: the Mac connects beside it.
6. **Five minutes of both** (questions 4, 5): a pattern playing, CLK INT (clock out to both), keys on the keyboard, notes from
   the Mac. Count drops on either side, stuck notes, audio load and clicks against the same run on one link.
7. **Endings**: switch the keyboard off -> the Mac stays, relink (either Mode) brings the keyboard back within a round (30 s);
   the Mac disconnects -> the keyboard stays; Disconnect ends both; Radio off / on; a power cycle restores the radio and relinks.
8. **Power-on in VISIBLE** with the keyboard remembered: it relinks; the Mac or iPad connects between the relink rounds and,
   once the keyboard is back, at any time. A Mac that cannot connect during a round should retry by itself (CoreBluetooth).
9. **Pairing beside a link**: a device that asks for a passkey (the iPad advertising from Ellic Studio, CONNECT OUT) while
   the Mac is connected: the PIN shows, the pairing completes, the Mac stays (question 5: AES on both links).
10. A stop that disturbs the other link (Disconnect of one, a timed-out attempt beside the Mac) is question 2 (the busy flag).

## Tests

`python3 tests/ble_tests.py` (in `tests/run_tests.sh`): the stack cases run against the classic tree and the parked one
(`BLE_TREES=classic` for the classic alone; the tests include `ble/<file>` and the tree comes in with `-I`), the firmware's
cases against the classic. Two-link cases: `ble_stack_test -DBLE_LINKS=2 -DBLE_CENTRAL=1`, `ble_central_test -DBLE_LINKS=2`
(the whole central suite on link 1), `ble_multilink_test` (a computer on link 0 and a keyboard on link 1: both orders, MIDI
tagged by link, frames in pieces on both, endings), `ble_multilink_driver_test` (the WL82 driver: instance table, per-link
columns and buffers, per-link interrupts, bounded stops, the idle policy) and `melodee_ble_test -DBLE_LINKS=2` (the glue: a
queue pair per link, output to both and admitted whole, clock per link, THRU's mask, input from both, a link going alone,
scanning / relink / a timed-out attempt beside the computer's link, relink in Mode VISIBLE between advertising). The peripheral model the central suite and the two-link
test share is `tests/ble_central_model.h`.
