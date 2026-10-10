#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Inject CPU1 failures into a MELODEE_CORE1_TEST=1 build and check the FM-1 carries on with one core.

Some FM-1 units' CPU1 misreads shared SRAM (keremimo/melodee#17); an idle unit then crashed.
Editor command 79 recreates each failure on a healthy unit:
  misread  CPU1 sees a request that is not there (0x00200000, as measured): counted, never run
  null     CPU1 runs a job at address 0 (the idle crash: pc 00000002): its fault is caught
  stall    CPU1 never finishes a job: the 10 ms join timeout
  crash    CPU0 crashes beside a working CPU1 (as the master_out report), late and early
Each must end with the worker retired (CPU1 held, serial rendering), the audio still running and
CPU1 barred until power-off (soft resets keep it out). Test builds also report the supply rails and
the clock, read only (power), and can count misreads without retiring (count-only).
A retired worker stays retired until reset, so every mode reboots first and waits out the
30 s boot guard (two crashes within 30 s of boot enter UBOOT).
"""
import argparse
import time
import mido

HEADER = [0x7D, 0x46, 0x4C, 79]
MODES = {'report': 0, 'misread': 1, 'null': 2, 'stall': 3}
FIELDS = ('online', 'rejected', 'faults', 'fault_cpu', 'fault_dbg', 'fault_pc', 'fault_rets',
          'timeouts', 'audio_halves', 'waited_us', 'fault_emu')
POWER_FIELDS = ('sysvdd', 'vdc14', 'vddio', 'barred', 'boot_failed', 'reserved', 'sys_div', 'clk_con0',
                'clk_con1', 'clk_con2', 'clk_con3', 'misread', 'faults', 'cpu_ticks', 'rejected', 'online')
RC = {0: 'ok', 1: 'no worker (dual core off or already retired)', 2: 'job completed (it should not)'}


def find_port(name):
    names = (name,) if name else ('Melodee', 'Felucca')   # (macOS may keep the old port name)
    return next((p for p in mido.get_input_names() if any(n in p for n in names)), None), \
        next((p for p in mido.get_output_names() if any(n in p for n in names)), None)


def command(name, mode, timeout=3.0, args=()):
    i, o = find_port(name)
    if not i or not o:
        raise TimeoutError(f'no MIDI port matching {name!r}')
    fields = POWER_FIELDS if 5 <= mode <= 8 else FIELDS
    with mido.open_input(i) as incoming, mido.open_output(o) as outgoing:
        time.sleep(0.05)                                    # (CoreMIDI drops a reply sent before the input is live)
        outgoing.send(mido.Message('sysex', data=HEADER + [mode, *args]))
        if mode == 4:
            return None
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            for message in incoming.iter_pending():
                if message.type != 'sysex' or list(message.data[:4]) != HEADER:
                    continue
                data = list(message.data[4:])
                if len(data) != 2 + 5 * len(fields):
                    raise RuntimeError('unexpected reply: is this a MELODEE_CORE1_TEST=1 build?')
                values = {f: sum(data[2 + i * 5 + j] << (7 * j) for j in range(5)) for i, f in enumerate(fields)}
                return {'mode': data[0], 'rc': data[1], **values}
            time.sleep(0.005)
    raise TimeoutError('no reply to command 79: is this a MELODEE_CORE1_TEST=1 build?')


def show(label, r):
    print(f"{label}: rc {r['rc']} ({RC.get(r['rc'], '?')}) online {r['online']} rejected {r['rejected']} "
          f"faults {r['faults']} timeouts {r['timeouts']} halves {r['audio_halves']} waited {r['waited_us']} us")
    if r['faults']:
        print(f"  fault: cpu {r['fault_cpu']} dbg {r['fault_dbg']:08X} emu {r['fault_emu']:08X} "
              f"pc {r['fault_pc']:08X} rets {r['fault_rets']:08X}")


def show_power(r):
    print(f"rails (read only): SYSVDD {r['sysvdd']} ({0.93 + 0.03 * r['sysvdd']:.2f} V) "
          f"VDC14 {r['vdc14']} ({1.25 + 0.05 * r['vdc14']:.2f} V) VDDIO {r['vddio']} ({2.8 + 0.1 * r['vddio']:.1f} V)")
    print(f"clock: SYS_DIV {r['sys_div']:08X} CLK_CON0..3 {r['clk_con0']:08X} {r['clk_con1']:08X} {r['clk_con2']:08X} "
          f"{r['clk_con3']:08X}; cpu {4000 * 64 * 24 / max(r['cpu_ticks'], 1):.1f} dependent adds/us")
    print(f"worker online {r['online']} rejected {r['rejected']} (last {r['misread']:08X}) faults {r['faults']}; "
          f"CPU1 barred until power-off {r['barred']}, boot after an early crash {r['boot_failed']}")


def back(name, wait=2):
    time.sleep(wait)
    deadline = time.monotonic() + 30
    while True:
        try:
            return command(name, 0)
        except (TimeoutError, OSError):
            if time.monotonic() > deadline:
                raise
            time.sleep(0.5)


def reboot(name, settle, keep=False):
    """keep: the supply ladder as it stands (a soft reset), else as at power-on"""
    command(name, 4, args=(1,) if keep else ())
    r = back(name)
    if settle:
        print(f'rebooted; waiting {settle} s for the boot guard')
        time.sleep(settle)
    return r


def ladder(name, settle):
    """CPU0 crashes (the master_out report) with CPU1 working: one core until power-off"""
    def crash():
        i, o = find_port(name)
        with mido.open_output(o) as outgoing:
            outgoing.send(mido.Message('sysex', data=HEADER + [10]))
        return back(name, 8)                # the crash screen shows for 4 s

    def state(label, online, barred, failed=None):
        p = command(name, 5)
        good = p['online'] == online and p['barred'] == barred and (failed is None or p['boot_failed'] == failed)
        print(f"  {label}: online {p['online']} barred {p['barred']} early-crash boot {p['boot_failed']} "
              f"({'ok' if good else 'UNEXPECTED'})")
        return good

    print('== crash')
    reboot(name, settle)
    ok = state('fresh boot', 1, 0)
    crash()                                 # after the boot guard's 30 s
    ok &= state('after a crash beside CPU1: one core', 0, 1, 0)
    reboot(name, 0, keep=True)
    ok &= state('next soft reset: still one core', 0, 1)
    reboot(name, 0)
    ok &= state('power-on again: both cores', 1, 0)
    crash()                                 # within the first 30 s
    ok &= state('after an early crash: one core', 0, 1, 1)
    time.sleep(settle)
    reboot(name, 0)
    ok &= state('power-on again: both cores', 1, 0)
    print(f"crash: {'PASS' if ok else 'FAIL'}")
    return ok


def run(name, mode, settle, do_reboot):
    print(f'== {mode}')
    before = reboot(name, settle) if do_reboot else command(name, 0)
    show('before', before)
    if not before['online']:
        raise SystemExit('CPU1 is not online before the injection: reboot it, or build with MELODEE_DUAL_CORE=1')
    hit = command(name, MODES[mode])
    show('inject', hit)
    time.sleep(1.0)
    after = command(name, 0)
    show('after ', after)
    ok = after['audio_halves'] > hit['audio_halves'] and not after['online'] and hit['rc'] == 0
    if mode == 'misread':
        ok = ok and after['rejected'] > 0 and not after['faults'] and not after['timeouts']
    elif mode == 'null':
        ok = ok and (after['faults'] > 0 or after['timeouts'] > 0)
        print('  fault caught by ' + ('CPU1 itself' if after['faults'] and after['fault_cpu'] else
                                      'CPU0 (debug-unit exception)' if after['faults'] else
                                      'nobody: the join timeout'))
    elif mode == 'stall':
        ok = ok and after['timeouts'] == 1
    time.sleep(0.2)                         # a retired worker bars CPU1 until power-off
    power = command(name, 5)
    reboot(name, 0, keep=True)
    again = command(name, 5)
    carried = power['barred'] == 1 and again['online'] == 0 and again['barred'] == 1
    print(f"  barred, and after a soft reset: {'one core' if carried else 'NOT CARRIED'}")
    ok = ok and carried
    print(f"{mode}: {'PASS' if ok else 'FAIL'}: audio {'running' if after['audio_halves'] > hit['audio_halves'] else 'STOPPED'}, "
          f"worker {'retired' if not after['online'] else 'STILL ONLINE'}")
    return ok


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('modes', nargs='*', default=['misread', 'null', 'stall'],
                        choices=[*MODES, 'crash', 'reboot', 'power', 'count-only', 'retire-on-misread'])
    parser.add_argument('--port', help='MIDI port name (default: Melodee or Felucca)')
    parser.add_argument('--settle', type=float, default=31, help='seconds after a reboot before injecting')
    parser.add_argument('--no-reboot', action='store_true', help='inject into the running session')
    args = parser.parse_args()
    results = []
    for mode in args.modes:
        if mode == 'report':
            show('report', command(args.port, 0))
        elif mode == 'reboot':
            show('report', reboot(args.port, 0))
        elif mode == 'crash':
            results.append(ladder(args.port, args.settle))
        elif mode == 'power':
            show_power(command(args.port, 5))
        elif mode in ('count-only', 'retire-on-misread'):
            show_power(command(args.port, 7, args=(int(mode == 'count-only'),)))
        else:
            results.append(run(args.port, mode, args.settle, not args.no_reboot))
    if results and not all(results):
        raise SystemExit(1)


if __name__ == '__main__':
    main()
