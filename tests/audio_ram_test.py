#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kerem Kilic (Ellic Studio)
"""Verify closure, ABI and safe branches across XIP/SRAM for the IR pass."""
import sys
import unittest
import tempfile
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from audio_ram import relocate, FUNCTION, function_type, engine_roots

IR = '''define void @fm1_alnk0_irq() {
  call fastcc void @render(i32 32)
  store void (i8*)* @worker, void (i8*)** @callback
  ret void
}
define internal fastcc void @render(i32 %n) {
  call void @hot(i32 %n)
  ret void
}
define void @worker(i8* %context) {
  ret void
}
define void @hot(i32 %n) section ".dsp_text" {
  ret void
}
define void @boot() {
  call fastcc void @render(i32 32)
  ret void
}
define void @accept(void (i8*, i32)* %job, i8* %context) {
  ret void
}
@callback = global void (i8*)* null
'''

class AudioRamTest(unittest.TestCase):
    def test_closure_and_crossing(self):
        output, selected, far = relocate(IR, {'fm1_alnk0_irq'})
        self.assertEqual(set(selected), {'fm1_alnk0_irq','render','hot','worker'})
        self.assertEqual(far,['render'])
        self.assertIn('call fastcc void %audio_far_', output)
        self.assertIn('call void @hot(i32 %n)',output)
        self.assertIn('call fastcc void @render(i32 32)',output)
        self.assertIn('@__audio_far_render = internal global void (i32)* @render',output)
        functions={m[2]:m for m in FUNCTION.finditer(output)}
        self.assertIn('section ".audio_text"',functions['worker'][4])
        self.assertIn('section ".dsp_text"',functions['hot'][4])
        self.assertNotIn('section',functions['boot'][4])

    def test_lowered_memory_intrinsics(self):
        extra = """define i8* @memcpy(i8* %d, i8* %s, i32 %n) {
  ret i8* %d
}
define i8* @memset(i8* %d, i32 %c, i32 %n) {
  ret i8* %d
}
define void @copy_boot(i8* %d, i8* %s, i8 %fill) {
  tail call void @llvm.memcpy.p0i8.p0i8.i32(i8* %d, i8* getelementptr ([2 x i8], [2 x i8]* @bytes, i32 0, i32 0), i32 2, i32 4, i1 false)
  call void @llvm.memset.p0i8.i32(i8* %d, i8 %fill, i32 2, i32 4, i1 false)
  ret void
}
@bytes = global [2 x i8] zeroinitializer
"""
        source = IR.replace('  call fastcc void @render(i32 32)',
            '  call void @llvm.memcpy.p0i8.p0i8.i32(i8* null, i8* null, i32 0, i32 4, i1 false)\n'
            '  call void @llvm.memset.p0i8.i32(i8* null, i8 0, i32 0, i32 4, i1 false)', 1) + extra
        output, selected, far = relocate(source, {'fm1_alnk0_irq'})
        self.assertTrue({'memcpy', 'memset'} <= set(selected))
        functions = {m[2]: m for m in FUNCTION.finditer(output)}
        self.assertIn('section ".audio_libc"', functions['memcpy'][4])
        self.assertTrue({'memcpy', 'memset'} <= set(far))
        boot = next(m[5] for m in FUNCTION.finditer(output) if m[2] == 'copy_boot')
        self.assertNotIn('@llvm.mem', boot)
        self.assertRegex(boot, r'%audio_copy_\d+ = tail call i8\* %audio_far_')
        self.assertIn('getelementptr ([2 x i8], [2 x i8]* @bytes, i32 0, i32 0)', boot)
        self.assertIn('zext i8 %fill to i32', boot)
        with self.assertRaisesRegex(ValueError, 'Unsupported cross-region'):
            relocate(source.replace('i1 false', 'i1 true'), {'fm1_alnk0_irq'})

    def test_callback_abi(self):
        fn=next(m for m in FUNCTION.finditer(IR) if m[2]=='accept')
        self.assertEqual(function_type(fn),'void (void (i8*, i32)*, i8*)')

    def test_missing_isr_rejected(self):
        with self.assertRaises(ValueError):relocate('', {'fm1_alnk0_irq'})

    def test_indirect_engine_callbacks(self):
        src=Path(__file__).resolve().parents[1]/'firmware/src'
        roots=engine_roots(src)
        self.assertIn('fm6_key',roots)
        self.assertNotIn('drum_keys',roots)      # (DRUM's engine is its module's: firmware/modules/drum)
        self.assertNotIn('p5_preset_make',roots)

    def test_new_callback_requires_classification(self):
        with tempfile.TemporaryDirectory() as directory:
            src=Path(directory)
            (src/'core.h').write_text('typedef struct { void (*new_hook)(void); } engine_t;')
            with self.assertRaisesRegex(ValueError,'Unclassified engine callbacks'):
                engine_roots(src)

if __name__=='__main__':unittest.main()
