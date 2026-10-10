#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Inject CPU1 failures into a MELODEE_CORE1_TEST=1 build and check the FM-1 carries on with one core.

Some FM-1 units' CPU1 misreads shared SRAM (keremimo/melodee#17); an idle unit then crashed.
Editor command 79 recreates each failure on a healthy unit:
  misread  CPU1 sees a request that is not there (0x00200000, as measured): counted, never run;
           the first raise the core supply (SYSVDD 15) and keep CPU1, later ones retire it
  null     CPU1 runs a job at address 0 (the idle crash: pc 00000002): its fault is caught
  stall    CPU1 never finishes a job: the 10 ms join timeout
Each must end with the worker retired (CPU1 held, serial rendering) and the audio still running.
Test builds also report and set the supply rails (power, sysvdd=N, vdc14=N, count-only).
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
POWER_FIELDS = ('boot_sysvdd', 'boot_vdc14', 'boot_vddio', 'sysvdd', 'vdc14', 'vddio', 'sys_div', 'clk_con0',
                'clk_con1', 'clk_con2', 'clk_con3', 'misread', 'faults', 'cpu_ticks', 'rejected', 'online')
RAILS = {'sysvdd': 0, 'vdc14': 1, 'vddio': 2}
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
    volts = lambda rail, l: {0: 0.93 + 0.03 * l, 1: 1.25 + 0.05 * l, 2: 2.8 + 0.1 * l}[rail]
    print(f"rails: SYSVDD {r['sysvdd']} ({volts(0, r['sysvdd']):.2f} V, boot {r['boot_sysvdd']}) "
          f"VDC14 {r['vdc14']} ({volts(1, r['vdc14']):.2f} V, boot {r['boot_vdc14']}) "
          f"VDDIO {r['vddio']} ({volts(2, r['vddio']):.1f} V, boot {r['boot_vddio']})")
    print(f"clock: SYS_DIV {r['sys_div']:08X} CLK_CON0..3 {r['clk_con0']:08X} {r['clk_con1']:08X} {r['clk_con2']:08X} "
          f"{r['clk_con3']:08X}")
    print(f"cpu: {4000 * 64 * 24 / max(r['cpu_ticks'], 1):.1f} dependent adds/us; "
          f"worker online {r['online']} rejected {r['rejected']} (last {r['misread']:08X}) faults {r['faults']}")


def reboot(name, settle):
    command(name, 4)
    time.sleep(2)
    deadline = time.monotonic() + 30
    while True:
        try:
            r = command(name, 0)
            break
        except (TimeoutError, OSError):
            if time.monotonic() > deadline:
                raise
            time.sleep(0.5)
    print(f'rebooted; waiting {settle} s for the boot guard')
    time.sleep(settle)
    return r


def run(name, mode, settle, do_reboot):
    print(f'== {mode}')
    before = reboot(name, settle) if do_reboot else command(name, 0)
    show('before', before)
    if not before['online']:
        raise SystemExit('CPU1 is not online before the injection: reboot it, or build with MELODEE_DUAL_CORE=1')
    boosted = True
    if mode == 'misread':                   # the first misreads raise the core supply and keep CPU1
        first = command(name, MODES[mode])
        show('inject', first)
        time.sleep(0.5)
        power = command(name, 5)
        show_power(power)
        boosted = first['rc'] == 0 and power['online'] == 1 and power['sysvdd'] == 15
        print(f"  first misreads: {'kept CPU1, SYSVDD 15' if boosted else 'NOT ANSWERED'}")
    hit = command(name, MODES[mode])
    show('inject', hit)
    time.sleep(1.0)
    after = command(name, 0)
    show('after ', after)
    ok = after['audio_halves'] > hit['audio_halves'] and not after['online'] and hit['rc'] == 0
    if mode == 'misread':
        ok = ok and boosted and after['rejected'] > 0 and not after['faults'] and not after['timeouts']
    elif mode == 'null':
        ok = ok and (after['faults'] > 0 or after['timeouts'] > 0)
        print('  fault caught by ' + ('CPU1 itself' if after['faults'] and after['fault_cpu'] else
                                      'CPU0 (debug-unit exception)' if after['faults'] else
                                      'nobody: the join timeout'))
    elif mode == 'stall':
        ok = ok and after['timeouts'] == 1
    if mode in ('null', 'stall'):           # a retired worker leaves CPU0 on the raised supply
        time.sleep(0.2)
        power = command(name, 5)
        show_power(power)
        ok = ok and power['sysvdd'] == 15
    print(f"{mode}: {'PASS' if ok else 'FAIL'}: audio {'running' if after['audio_halves'] > hit['audio_halves'] else 'STOPPED'}, "
          f"worker {'retired' if not after['online'] else 'STILL ONLINE'}")
    return ok


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('modes', nargs='*', default=['misread', 'null', 'stall'],
                        choices=[*MODES, 'reboot', 'power', 'count-only', 'retire-on-misread', *(f'{r}={l}' for r in RAILS for l in range(16))])
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
        elif mode == 'power':
            show_power(command(args.port, 5))
        elif mode in ('count-only', 'retire-on-misread'):
            show_power(command(args.port, 7, args=(int(mode == 'count-only'),)))
        elif '=' in mode:
            rail, level = mode.split('=')
            r = command(args.port, 6, args=(RAILS[rail], int(level)))
            if r['rc']:
                raise SystemExit(f'{mode}: refused')
            show_power(r)
        else:
            results.append(run(args.port, mode, args.settle, not args.no_reboot))
    if results and not all(results):
        raise SystemExit(1)


if __name__ == '__main__':
    main()
