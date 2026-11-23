// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
// Two renders (interleaved stereo float, 44.1 kHz: legacy_native render, check_native render) compared for the parity
// tests (tests/convert_test.sh): the frames, each one's peak, where they first differ, how much of the time differs (in
// 10 ms blocks), and the difference's energy against the signal's. Exits 1 when the expectation fails:
//   node web/convert/render_cmp.mjs A B identical          every sample the same, and not silence
//   node web/convert/render_cmp.mjs A B until S            the same for the first S seconds (a change 2.0 makes later)
//   node web/convert/render_cmp.mjs A B shifted LAG         every 100 ms the same within float rounding (residual / signal
//                                                          1e-2) at some shift of at most LAG samples: a kit becoming
//                                                          another kit's synth starts at the next block in 2.0
import { readFileSync } from "node:fs";

const [a, b, mode = "identical", arg = "0"] = process.argv.slice(2);
const A = new Float32Array(readFileSync(a).buffer.slice(0)), Bf = new Float32Array(readFileSync(b).buffer.slice(0));
const FS = 44100, BLOCK = 441, n = Math.min(A.length, Bf.length) >> 1;
let peakA = 0, peakB = 0, first = -1, blocks = 0, sig = 0, err = 0, blockDiffers = false;
for (let f = 0; f < n; f++) {
  for (let c = 0; c < 2; c++) {
    const x = A[2 * f + c], y = Bf[2 * f + c];
    peakA = Math.max(peakA, Math.abs(x)); peakB = Math.max(peakB, Math.abs(y));
    sig += x * x; err += (x - y) * (x - y);
    if (!Object.is(x, y) && x !== y) { if (first < 0) first = f; blockDiffers = true; }
  }
  if (f % BLOCK === BLOCK - 1 || f === n - 1) { if (blockDiffers) blocks++; blockDiffers = false; }
}
const same = A.length === Bf.length && first < 0, ms = blocks * 10, ratio = sig ? err / sig : err ? Infinity : 0;
/* shifted: each 100 ms window's residual / signal at its best shift (0 first); the worst, and the windows shifted */
let worst = 0, shifted = 0;
if (mode === "shifted" && !same) {
  const W = FS / 10, LAG = Number(arg);
  const res = (w, lag) => {
    let d = 0, e = 0;
    for (let f = w * W; f < Math.min(n, (w + 1) * W); f++) {
      const g = f + lag, y = g >= 0 && g < n ? Bf[2 * g] : 0, x = A[2 * f];
      d += (x - y) * (x - y); e += x * x;
    }
    return e > 1e-9 ? d / e : d;
  };
  for (let w = 0; w * W < n; w++) {
    let best = res(w, 0), at = 0;
    for (let lag = -LAG; best > 1e-6 && lag <= LAG; lag++) { const r = res(w, lag); if (r < best) { best = r; at = lag; } }
    worst = Math.max(worst, best); if (at) shifted++;
  }
}
const ok = peakA > 0.01 && A.length === Bf.length && (mode === "identical" ? same
  : mode === "until" ? (first < 0 || first >= Number(arg) * FS)
  : mode === "shifted" ? same || worst <= 1e-2 : false);
console.log(`${(n / FS).toFixed(1)} s, peaks ${peakA.toFixed(3)} / ${peakB.toFixed(3)}: ` +
  (same ? "identical" : `first difference at ${(first / FS).toFixed(3)} s, ${ms} ms differ, diff/sig ${ratio.toExponential(2)}`) +
  (mode === "shifted" && !same ? `; ${shifted} window(s) shifted, worst residual ${worst.toExponential(2)}` : "") +
  ` (${mode}${mode === "identical" ? "" : " " + arg})`);
process.exit(ok ? 0 : 1);
