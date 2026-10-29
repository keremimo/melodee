#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kerem Kilic (Ellic Studio)
"""Stack audit must account for callbacks, nesting and reject unknown paths."""
from pathlib import Path
import sys
import tempfile
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import stack_budget

ROOTS = ['fm1_cstart', 'isr_alnk0', 'isr_timer5', 'fm1_core1_main', 'fm1_fault_c']


class StackBudget(unittest.TestCase):
    def analyze(self, bodies=None, extra_dis=''):
        bodies = bodies or {}
        names = list(dict.fromkeys(ROOTS + list(bodies)))
        ir = '%struct.engine_t = type { void (i32)* }\n'
        dis = ''
        for i, name in enumerate(names):
            ir += f'define void @{name}() {{\n{bodies.get(name, "  ret void")}\n}}\n'
            dis += f'{name}:\n {0x1000+i*256:x}: 00 00 \t[--sp] = {{rets,r7-r4}}\n'
            dis += f' {0x1004+i*256:x}: 00 00 \tsp += -100\n'
        dis += extra_dis
        with tempfile.TemporaryDirectory() as directory:
            src = Path(directory)
            (src / 'core.h').write_text('typedef struct { void (*render)(int); } engine_t;')
            (src / 'seq.c').write_text('if (song.playing || cin_left\nif (started) seq_advance(0);')
            (src / 'usb_host.c').write_text('#define UH_HUB_LEVELS 2u\nreturn depth < UH_HUB_LEVELS ? uh_hub_probe\n'
                                            'uh_probe((uint8_t)(level + 1u), m)')
            (src / 'eng_test.c').write_text('.render=renderer,')
            return stack_budget.analyze(ir, dis, src)

    def test_nested_direct_and_tail(self):
        report = self.analyze({'fm1_cstart': '  tail call void @helper()\n  ret void', 'helper': '  ret void'})
        self.assertEqual(report['paths']['fm1_cstart'][0], 240)
        self.assertEqual(report['paths']['fm1_cstart'][1], ['fm1_cstart', 'helper'])
        self.assertEqual(stack_budget.check(report, 3000, 1000, 2000), [])
        self.assertTrue(stack_budget.check(report, 100, 100, 100))
        self.assertTrue(stack_budget.check(report, 3000, 1000, 4096, 512))
        self.assertTrue(stack_budget.check(report, 3000, 1000, 4096, 4000))
        del report['paths']['isr_timer5']
        self.assertTrue(stack_budget.check(report, 3000, 1000, 2000))

    def test_ble_interrupt_nesting(self):
        report = self.analyze({'isr_ble_event': '  ret void', 'isr_ble_rx': '  call void @helper()\n  ret void', 'helper': '  ret void'})
        # BLE's 240 bytes + audio/timer/fault 3*120 + 128 reserve.
        self.assertEqual(stack_budget.check(report, 3000, 728, 2000), [])
        self.assertTrue(stack_budget.check(report, 3000, 727, 2000))
        del report['paths']['isr_ble_rx']
        self.assertTrue(stack_budget.check(report, 3000, 1000, 2000))

    def test_unknown_callback(self):
        with self.assertRaisesRegex(ValueError, 'unresolved stack callbacks'):
            self.analyze({'isr_alnk0': '  call void %callback(i32 1)\n  ret void'})

    def test_engine_callback(self):
        report = self.analyze({'isr_alnk0': '  %a = getelementptr %struct.engine_t, %struct.engine_t* %engine, i32 0, i32 0\n  %b = load void (i32)*, void (i32)** %a\n  call void %b(i32 1)\n  ret void',
                               'renderer': '  ret void'})
        self.assertEqual(report['paths']['isr_alnk0'][0], 240)
        self.assertIn('renderer', report['indirect']['isr_alnk0'])

    def test_unbounded_recursion(self):
        with self.assertRaisesRegex(ValueError, 'recursive stack path'):
            self.analyze({'fm1_cstart': '  call void @fm1_cstart()\n  ret void'})

    def test_bounded_hub_recursion(self):
        report = self.analyze({'fm1_cstart': '  call void @uh_probe()\n  ret void',
                               'uh_probe': '  call void @uh_probe()\n  ret void'})
        self.assertEqual(report['paths']['fm1_cstart'][0], 480)

    def test_bounded_hub_port_helper(self):
        report = self.analyze({'fm1_cstart': '  call void @uh_probe()\n  ret void',
                               'uh_probe': '  call void @uh_hub_port_probe()\n  ret void',
                               'uh_hub_port_probe': '  call void @uh_probe()\n  ret void'})
        self.assertEqual(report['paths']['fm1_cstart'][0], 840)

    def test_idle_hub_port_helper(self):
        report = self.analyze({'fm1_cstart': '  call void @uh_hub_port_probe()\n  ret void',
                               'uh_probe': '  call void @uh_hub_port_probe()\n  ret void',
                               'uh_hub_port_probe': '  call void @uh_probe()\n  ret void'})
        self.assertEqual(report['paths']['fm1_cstart'][0], 720)

    def test_unbounded_port_helper_recursion(self):
        with self.assertRaisesRegex(ValueError, 'recursive stack path'):
            self.analyze({'fm1_cstart': '  call void @uh_hub_port_probe()\n  ret void',
                          'uh_hub_port_probe': '  call void @uh_hub_port_probe()\n  ret void'})

    def test_bounded_recording_reentry(self):
        report = self.analyze({'isr_alnk0': '  call void @input_on()\n  ret void',
                               'input_on': '  call void @seq_advance()\n  ret void',
                               'seq_advance': '  call void @input_on()\n  ret void'})
        self.assertEqual(report['paths']['isr_alnk0'][0], 480)

    def test_dynamic_frame(self):
        with self.assertRaisesRegex(ValueError, 'dynamic stack frames'):
            self.analyze(extra_dis=' 2000: 00 00 \tsp += r0\n')


if __name__ == '__main__':
    unittest.main()
