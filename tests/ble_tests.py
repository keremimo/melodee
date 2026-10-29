#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kerem Kilic (Ellic Studio)
"""BLE protocol, driver and Melodee routing fixtures; no radio hardware needed."""
import concurrent.futures
import os
import re
import shlex
from pathlib import Path
import subprocess

OUT = Path('build/ble-tests')
CASES = [
    ('ble_prim_test', []),
    ('ble_stack_test', []),
    ('ble_stack_test', ['-DBLE_LL_ENC=1', '-DBLE_SMP_LEGACY=1']),
    ('ble_driver_test', ['-Itests/ble_fake', '-DBLE_DIAG=0']),
    ('ble_driver_test', ['-Itests/ble_fake', '-DBLE_DIAG=0', '-DBLE_LL_ENC=1', '-DBLE_SMP_LEGACY=1']),
    ('ble_central_test', []),
    ('ble_central_test', ['-DBLE_MIDI_INITIAL_READ=1']),
    ('ble_scan_test', []),
    ('ble_scan_driver_test', ['-Itests/ble_fake']),
    ('ble_central_driver_test', ['-Itests/ble_fake']),
    ('ble_vm_test', []),
    ('ble_store_test', []),
    ('ble_settings_test', []),
    ('ble_cal_editor_test', ['-Itests/ble_fake', '-Ibuild/gen', '-Ifirmware/src', '-lm']),
    ('ble_ui_render', ['-Itests/ble_fake', '-Ibuild/gen', '-Ifirmware/src', '-lm']),
    ('ble_ui_render', ['-DMELODEE_USB_HOST=1', '-Itests/ble_fake', '-Ibuild/gen', '-Ifirmware/src', '-lm']),
    ('melodee_ble_test', ['-Itests/ble_fake', '-Ibuild/gen', '-Ifirmware/src', '-lm']),
]


def run(case):
    i, (name, flags) = case
    exe = OUT / f'{name}-{i}'
    cmd = shlex.split(os.environ.get('CC', 'cc')) + ['-O1', '-w', '-fsanitize=address,undefined',
           '-fno-sanitize-recover=all', f'tests/{name}.c', *flags, '-o', str(exe)]
    build = subprocess.run(cmd, capture_output=True, text=True)
    result = build if build.returncode else subprocess.run([str(exe)], capture_output=True, text=True)
    exe.with_suffix('.log').write_text(result.stdout + result.stderr)
    return name, result.returncode, exe.with_suffix('.log')


if __name__ == '__main__':
    subprocess.run(['python3', 'tests/ble_rf_replay_test.py'], check=True)
    subprocess.run(['python3', 'tests/ble_calibration_test.py'], check=True)
    OUT.mkdir(parents=True, exist_ok=True)
    # Compile the actual shipping editor decoder, not a fixture substitute.
    editor = Path('firmware/src/editor.c').read_text()
    decoder = re.search(r'static uint32_t ed_unpack7\([^\n]+\)\n\{.*?\n\}', editor, re.S)
    if not decoder:
        raise RuntimeError('Cannot locate shipping editor payload decoder')
    (OUT / 'editor_decoder.h').write_text(decoder.group(0) + '\n')
    Path('build/ble-ui/ppm').mkdir(parents=True, exist_ok=True)
    with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
        results = list(pool.map(run, enumerate(CASES)))
    for name, rc, log in results:
        print(f'{name}: {"FAILED" if rc else "passed"} ({log})')
        if rc:
            print(log.read_text()[-3500:])
    raise SystemExit(any(rc for _, rc, _ in results))
