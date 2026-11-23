// AudioWorklet: runs a firmware's simulator (sim.wasm, built by scripts/build-sim.sh) and streams its audio.
// Messages in: {module, seed} | {t:"key",k,d} | {t:"btn",id,d} | {t:"enc",role,d} | {t:"midi",pkt}. Out: {ready} | {state:[...], say, fb?}. fb: the screen (240 x 240 RGB565, byte-swapped) when it changed, for a sim with sim_fb.
class FM1Sim extends AudioWorkletProcessor {
  constructor() {
    super();
    this.x = null; this.calls = 0; this.view = null; this.lastSay = -1; this.lastBlit = -1;
    this.port.onmessage = (e) => {
      const m = e.data;
      if (m.module) {
        WebAssembly.instantiate(m.module, {}).then((inst) => { this.x = inst.exports; this.x.sim_init(m.seed >>> 0); this.port.postMessage({ ready: true }); });
      } else if (this.x) {
        const x = this.x;
        if (m.t === "key") x.sim_key(m.k, m.d); else if (m.t === "btn") x.sim_button(m.id, m.d);
        else if (m.t === "enc") x.sim_enc(m.role, m.d); else if (m.t === "midi") x.sim_midi(m.pkt >>> 0);
      }
    };
  }
  process(_in, outs) {
    const out = outs[0], x = this.x, n = out[0].length;
    if (!x) return true;
    x.sim_render(n);
    if (!this.view || this.view.buffer !== x.memory.buffer) this.view = new Float32Array(x.memory.buffer, x.sim_buf(), 8192);
    for (let i = 0; i < n; i++) { out[0][i] = this.view[2 * i]; if (out[1]) out[1][i] = this.view[2 * i + 1]; }
    if (++this.calls % 8 === 0) {
      const state = []; for (let k = 0; k < 13; k++) state.push(x.sim_get(k));
      const msg = { state };
      if (state[11] !== this.lastSay) {
        this.lastSay = state[11];
        const mem = new Uint8Array(x.memory.buffer); let p = x.sim_say(), s = "";
        while (mem[p]) s += String.fromCharCode(mem[p++]);
        msg.say = s;
      }
      if (x.sim_fb && state[12] !== this.lastBlit) {
        this.lastBlit = state[12];
        msg.fb = new Uint16Array(x.memory.buffer, x.sim_fb(), 240 * 240).slice();
        this.port.postMessage(msg, [msg.fb.buffer]);
      } else this.port.postMessage(msg);
    }
    return true;
  }
}
registerProcessor("fm1-sim", FM1Sim);
