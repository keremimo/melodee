// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
// The synth modules in the virtual FM-1 (web/core/melodee_core.c CORE_MODULES): a module file built with
// tools/mod_build.py --wasm carries the module's own source as a WebAssembly side module (its WASM chunk, last). The
// side module is linked into the core's memory and function table, as emscripten's dynamic linking does it: its data
// at a base the core gives (core_mod_alloc), its functions after the table's (the table grows), its stack its own; it
// imports memcpy / memset / memcmp from the core (by address too: GOT.func). Its descriptor goes to the core
// (core_mod_link, by the file's uid: its ID's first 4 bytes), then the device's file (the chunk left out, its CRC
// again) into the pool (core_mod_install): the core's loader binds it to the same ABI 3 host table the device's does
// and plays its engine through it.
//   import { installModules } from "./mod_link.mjs";
//   installModules(core.exports, [fileBytes, ...]) after core_boot(0); then core_boot(1)

const WASM_TAG = 0x4d534157, HEAD = 60, STACK = 65536;
const u32 = (b, o) => (b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24)) >>> 0;
const put32 = (b, o, v) => { b[o] = v & 255; b[o + 1] = (v >>> 8) & 255; b[o + 2] = (v >>> 16) & 255; b[o + 3] = (v >>> 24) & 255; };
const CRC = Array.from({ length: 256 }, (_, n) => { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; return c >>> 0; });
export const crc32 = (b) => { let c = 0xffffffff; for (const x of b) c = CRC[(c ^ x) & 255] ^ (c >>> 8); return (c ^ 0xffffffff) >>> 0; };

/** A module file's parts: the device's file (its WASM chunk left out, the head's total, flags and CRC again) and the
 * WebAssembly (null: the file has none). */
export function splitModule(bytes) {
  const b = Uint8Array.from(bytes), image = u32(b, 12), nrel = u32(b, 20);
  if (b.length < HEAD || u32(b, 0) !== 0x31444d4d) throw new Error("not a module file");
  const chunks = [];
  if (b[7]) for (let at = HEAD + image + 4 * nrel; at + 8 <= b.length; at += 8 + ((u32(b, at + 4) + 3) & ~3)) chunks.push({ tag: u32(b, at), at, n: u32(b, at + 4) });
  const w = chunks.find((c) => c.tag === WASM_TAG);
  if (!w) return { file: b, wasm: null };
  const file = b.slice(0, w.at);
  put32(file, 8, file.length);
  file[7] = chunks.length > 1 ? 1 : 0;
  put32(file, 28, crc32(file.subarray(HEAD)));
  return { file, wasm: b.slice(w.at + 8, w.at + 8 + w.n) };
}

/* a side module's needs (its dylink.0 section: memory size and alignment, table size) */
function dylink(mod) {
  const s = WebAssembly.Module.customSections(mod, "dylink.0")[0];
  if (!s) throw new Error("not a side module (no dylink.0)");
  const b = new Uint8Array(s);
  let i = 0;
  const leb = () => { let r = 0, sh = 0, x; do { x = b[i++]; r |= (x & 127) << sh; sh += 7; } while (x & 128); return r >>> 0; };
  while (i < b.length) {
    const id = b[i++], n = leb(), end = i + n;
    if (id === 1) return { memSize: leb(), memAlign: leb(), tableSize: leb() };
    i = end;
  }
  return { memSize: 0, memAlign: 0, tableSize: 0 };
}

/** A side module linked into the core (x: its exports): its descriptor's address. */
export async function linkModule(x, wasm) {
  const mod = await WebAssembly.compile(wasm), d = dylink(mod), table = x.__indirect_function_table;
  const memBase = x.core_mod_alloc(Math.max(1, d.memSize), 2 ** d.memAlign), stack = x.core_mod_alloc(STACK, 16);
  if (!memBase || !stack) throw new Error("the core has no room for the module");
  const tableBase = table.grow(d.tableSize);
  const i32 = (v, mutable = false) => new WebAssembly.Global({ value: "i32", mutable }, v);
  const env = {
    memory: x.memory, __indirect_function_table: table, __memory_base: i32(memBase), __table_base: i32(tableBase),
    __stack_pointer: i32(stack + STACK, true), memcpy: x.memcpy, memset: x.memset, memcmp: x.memcmp,
  };                                                 /* (a module's data records: its host table's rec_base, the pool) */
  const got = {};                                    /* (a core function whose address the module takes: a slot) */
  for (const im of WebAssembly.Module.imports(mod)) {
    if (im.module !== "GOT.func") continue;
    if (typeof x[im.name] !== "function") throw new Error(`the core has no ${im.name}`);
    const at = table.grow(1);
    table.set(at, x[im.name]);
    got[im.name] = i32(at, true);
  }
  const inst = await WebAssembly.instantiate(mod, { env, "GOT.func": got, "GOT.mem": {} });
  inst.exports.__wasm_apply_data_relocs?.();
  inst.exports.__wasm_call_ctors?.();
  const name = Object.keys(inst.exports).find((k) => k.startsWith("mod_") && inst.exports[k] instanceof WebAssembly.Global);
  if (!name) throw new Error("the module exports no descriptor");
  return memBase + inst.exports[name].value;
}

/** Module files (built --wasm) in the core: each linked and named to the core; with install (after core_boot(0)) put
 * in the pool too (a core booted on a NOR that has them: linked only). -> each one's install result (MS_*: 1 loaded) */
export async function installModules(x, files, install = true) {
  const out = [];
  for (const f of files) {
    const { file, wasm } = splitModule(f);
    if (!wasm) throw new Error("a module file without its WebAssembly (tools/mod_build.py --wasm)");
    if (x.core_mod_link(await linkModule(x, wasm), u32(file, 24))) throw new Error("the core refused the module's descriptor");
    if (!install) continue;
    const s = x.core_scratch();
    new Uint8Array(x.memory.buffer).set(file, s);
    const rc = x.core_mod_install(s, file.length);
    if (rc < 0) throw new Error(`the module did not install (${rc})`);
    out.push(rc);
  }
  return out;
}
