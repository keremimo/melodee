// SPDX-License-Identifier: GPL-3.0-only
// Melodee in an AudioWorklet: melodee.wasm is the firmware's UI and sound (web/emu/melodee_web.c). Each
// render quantum asks it for 128 frames, which runs the device's clock forward; about 30 times a second
// the screen and the LEDs go to the page.

class Melodee extends AudioWorkletProcessor {
  constructor() {
    super();
    this.ex = null;
    this.port.onmessage = (e) => this.onMessage(e.data);
  }

  async onMessage(m) {
    if (m.type === "load") {
      const stub = new Proxy({}, { get: () => () => 0 });   // (WASI: nothing is printed or opened)
      const { instance } = await WebAssembly.instantiate(m.wasm, { env: stub, wasi_snapshot_preview1: stub });
      const ex = instance.exports;
      ex._initialize();
      ex.web_boot(m.master);
      this.mem = ex.memory;
      this.sentFb = -1;
      this.last = 0;
      this.ex = ex;
      return;
    }
    const ex = this.ex;
    if (!ex) return;
    if (m.type === "keys") ex.web_keys(m.mask >>> 0);
    else if (m.type === "buttons") ex.web_buttons(m.mask >>> 0);
    else if (m.type === "encoder") ex.web_encoder(m.id, m.n | 0);
    else if (m.type === "master") ex.web_master(m.value | 0);
    else if (m.type === "midi") ex.web_midi_in(m.data[0], m.data[1] || 0, m.data[2] || 0);
  }

  process(inputs, outputs) {
    const ex = this.ex;
    if (!ex) return true;
    const out = outputs[0], n = out[0].length;
    try {
      ex.web_render(n);
    } catch (err) {
      this.ex = null;
      this.port.postMessage({ type: "crash", message: String(err && err.message || err) });
      return true;
    }
    out[0].set(new Float32Array(this.mem.buffer, ex.web_out_l(), n));
    if (out[1]) out[1].set(new Float32Array(this.mem.buffer, ex.web_out_r(), n));
    if (currentTime - this.last >= 1 / 30) {
      this.last = currentTime;
      const msg = { type: "frame", ms: ex.web_now(), keys: ex.web_led_keys() >>> 0, buttons: ex.web_led_buttons() >>> 0 };
      const gen = ex.web_fb_gen(), transfer = [];
      if (gen !== this.sentFb) {
        this.sentFb = gen;
        msg.fb = new Uint16Array(this.mem.buffer, ex.web_fb(), 240 * 240).slice();
        transfer.push(msg.fb.buffer);
      }
      this.port.postMessage(msg, transfer);
    }
    return true;
  }
}

registerProcessor("melodee", Melodee);
