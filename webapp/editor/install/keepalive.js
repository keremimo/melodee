// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
//
// Keeps an install going while it runs, as far as a page can:
//   - timers from a worker: Chrome's background-tab throttling (1 s alignment, then one wake-up a minute) is
//     applied to the page's own timers, which fm1ota.js waits on between steps; a dedicated worker's timers are
//     scheduled apart from them. setTimeout/clearTimeout are routed through it until release. A browser that slows
//     the worker too (a hidden embedded view does) loses nothing: the transfer itself waits on no timer.
//   - one install at a time: a Web Lock that a second installer tab cannot take (two tabs answering the same
//     FM-1 corrupt each other's transfer).
//   - a screen wake lock: the display and the computer stay awake while the page is in front (taken again
//     whenever the page comes back).
//   - leaving: the browser asks before the page is closed or reloaded.
//   - the page's visibility, reported to the caller (the warning when the tab is switched).
// Each part is skipped where the browser lacks it; none is needed for the transfer itself (transfer.js).

const LOCK = "melodee-fm1-install";
const BASE_ID = 1 << 30;                          // worker timer ids, apart from the browser's own

let worker = null, nextId = BASE_ID;
const pending = new Map();

function workerTimers() {
  if (worker !== null) return worker;
  worker = false;
  try {
    const src = "const t=new Map();onmessage=({data:[on,id,ms]})=>{if(on)t.set(id,setTimeout(()=>{t.delete(id);" +
      "postMessage(id)},ms));else{clearTimeout(t.get(id));t.delete(id)}}";
    const url = URL.createObjectURL(new Blob([src], { type: "text/javascript" }));
    let w;
    try { w = new Worker(url); } finally { URL.revokeObjectURL(url); }
    w.onmessage = ({ data: id }) => { const f = pending.get(id); pending.delete(id); if (f) f(); };
    worker = w;                                   // kept for the page's life: a timer set before release still fires
  } catch (_) { /* no workers here: the page's timers, then */ }
  return worker;
}

function routeTimers() {
  const w = workerTimers();
  if (!w) return () => {};
  const nativeSet = globalThis.setTimeout, nativeClear = globalThis.clearTimeout;
  globalThis.setTimeout = (fn, ms = 0, ...args) => {
    if (typeof fn !== "function") return nativeSet(fn, ms, ...args);
    const id = nextId++;
    pending.set(id, () => fn(...args));
    w.postMessage([1, id, Math.max(0, +ms || 0)]);
    return id;
  };
  globalThis.clearTimeout = (id) => {
    if (typeof id === "number" && id >= BASE_ID) { if (pending.delete(id)) w.postMessage([0, id]); }
    else nativeClear(id);
  };
  return () => { globalThis.setTimeout = nativeSet; globalThis.clearTimeout = nativeClear; };
}

// the single-install lock: resolves to a release function, or null when another tab holds it
async function takeLock() {
  if (!navigator.locks) return () => {};
  let release;
  const held = new Promise((r) => { release = r; });
  const got = await new Promise((answer) => {
    navigator.locks.request(LOCK, { ifAvailable: true }, (lock) => {
      answer(!!lock);
      return lock ? held : undefined;
    }).catch(() => answer(false));
  });
  return got ? release : null;
}

export async function busyElsewhere() {
  if (!navigator.locks || !navigator.locks.query) return false;
  try { return (await navigator.locks.query()).held.some((l) => l.name === LOCK); } catch (_) { return false; }
}

// starts holding; -> release(), or null when another tab is installing. onHidden() / onVisible(ms away)
export async function hold({ leaveText = "", onHidden, onVisible } = {}) {
  const unlock = await takeLock();
  if (!unlock) return null;
  const undo = [unlock, routeTimers()];
  let wake = null, waking = false, hiddenAt = 0, done = false;
  const awake = async () => {
    if (done || wake || waking || document.visibilityState !== "visible" || !navigator.wakeLock) return;
    waking = true;
    try {
      const granted = await navigator.wakeLock.request("screen");
      if (done) { granted.release().catch(() => {}); return; }
      wake = granted;
      wake.addEventListener("release", () => { wake = null; });
    } catch (_) { wake = null; }
    finally { waking = false; }
  };
  const vis = () => {
    if (document.visibilityState === "hidden") {
      hiddenAt = Date.now();
      if (onHidden) onHidden();
    } else {
      awake();
      if (hiddenAt && onVisible) onVisible(Date.now() - hiddenAt);
      hiddenAt = 0;
    }
  };
  const leave = (e) => { e.preventDefault(); e.returnValue = leaveText; return leaveText; };
  document.addEventListener("visibilitychange", vis);
  window.addEventListener("beforeunload", leave);
  undo.push(() => {
    document.removeEventListener("visibilitychange", vis);
    window.removeEventListener("beforeunload", leave);
    if (wake) wake.release().catch(() => {});
  });
  awake();
  return () => {
    if (done) return;
    done = true;
    undo.reverse().forEach((f) => f());
  };
}
