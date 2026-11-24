// "Try it" panel: the firmware's own code, compiled to WebAssembly, playing through Web Audio, with the FM-1's controls on screen.
// The firmware entry in firmwares.json has a "sim" block: { wasm, knobs:[{label,enc,get}], buttons:[{label,id,get?}], keys, firstNote, status }
// or, for a firmware whose sim draws its own screen, { wasm, panel: true, keys, firstNote, status }: the FM-1's whole panel
// (the screen, the 7 encoders, the 14 buttons and the keys, lit as the firmware lights them: sim_get 7..10).
const SR = 44100;
const NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];
export const noteName = (n) => (n < 0 ? "-" : NAMES[n % 12] + (Math.floor(n / 12) - 1));
const WHITE = [0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26];
const BLACK = [1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25];
const KEYMAP = "awsedftgyhujkolp;'";            // computer keyboard -> keys from index 7 (C4)
// the FM-1's panel, in plat.h's order (B_FX .. B_OCTUP, EN_SELECT .. EN_K4)
const BUTTONS = ["FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC", "OCT-", "OCT+"];
const ENCODERS = ["SELECT", "ALGO", "PRESET", "K1", "K2", "K3", "K4"];

const h = (tag, attrs = {}, ...kids) => {
  const e = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs)) k === "class" ? (e.className = v) : e.setAttribute(k, v);
  for (const k of kids.flat()) e.append(k instanceof Node ? k : document.createTextNode(k));
  return e;
};

export class Simulator {
  constructor(root, fw, base = "firmware/") {
    this.root = root; this.fw = fw; this.cfg = fw.sim; this.base = base;
    this.ctx = null; this.node = null; this.state = [];
    this.knobEls = []; this.btnEls = []; this.keyEls = []; this.down = new Set(); this.dragging = new Set();
  }

  async start() {
    this.root.replaceChildren(h("p", {}, "Loading…"));
    const bytes = await (await fetch(this.base + this.cfg.wasm + "?v=" + String(this.fw.sha256 || "").slice(0, 12))).arrayBuffer();
    const module = await WebAssembly.compile(bytes);
    const ctx = this.ctx = new AudioContext({ sampleRate: SR, latencyHint: "interactive" });
    await ctx.audioWorklet.addModule("js/sim-worklet.js");
    const node = this.node = new AudioWorkletNode(ctx, "fm1-sim", { numberOfInputs: 0, outputChannelCount: [2] });
    this.vol = ctx.createGain(); this.vol.gain.value = 0.5;
    this.an = ctx.createAnalyser(); this.an.fftSize = 1024;
    node.connect(this.vol); this.vol.connect(this.an); this.an.connect(ctx.destination);
    const ready = new Promise((r) => { node.port.onmessage = (e) => { if (e.data.ready) r(); else this.onState(e.data); }; });
    node.port.postMessage({ module, seed: (Math.random() * 1e9) | 0 });
    await ready;
    this.build();
    this.keyHandlers();
    this.draw();
  }

  stop() {
    this.stopped = true;
    for (const [t, f] of this.listeners || []) window.removeEventListener(t, f);
    try { this.node?.disconnect(); } catch (_) {}
    this.ctx?.close();
  }

  send(m) { this.node.port.postMessage(m); }

  build() {
    if (this.cfg.panel) return this.buildPanel();
    const c = this.cfg, root = this.root;
    this.say = h("div", { class: "simsay", "aria-live": "polite" }, this.fw.name);
    this.statusEl = h("div", { class: "simstatus" });
    this.canvas = h("canvas", { width: 600, height: 80, class: "scope", "aria-label": "Output waveform" });
    const volume = h("input", { type: "range", min: 0, max: 100, value: 50, "aria-label": "Volume" });
    volume.oninput = () => { this.vol.gain.value = (volume.value / 100) ** 2; };
    this.vol.gain.value = 0.25;
    const knobs = h("div", { class: "simknobs" }, (c.knobs || []).map((k, i) => {
      const r = h("input", { type: "range", min: 0, max: 100, value: 0, "aria-label": k.label });
      r.dataset.last = "0";
      r.oninput = () => {              // the firmware moves 2 per detent: send the detents
        const d = Math.round((r.value - r.dataset.last) / 2);
        if (d) { this.send({ t: "enc", role: k.enc, d }); r.dataset.last = String(+r.dataset.last + 2 * d); }
      };
      r.onpointerdown = () => this.dragging.add(i);
      r.onpointerup = r.onpointercancel = () => this.dragging.delete(i);
      this.knobEls.push(r);
      return h("label", {}, h("span", {}, k.label), r);
    }));
    const btns = h("div", { class: "simbtns" }, (c.buttons || []).map((b, i) => {
      const e = h("button", { class: "ghost", type: "button" }, b.label);
      const press = (d) => (ev) => { ev?.preventDefault?.(); this.send({ t: "btn", id: b.id, d }); };
      e.onpointerdown = (ev) => { e.setPointerCapture?.(ev.pointerId); press(1)(ev); };
      e.onpointerup = e.onpointercancel = press(0);
      e.onkeydown = (ev) => { if ((ev.key === " " || ev.key === "Enter") && !ev.repeat) press(1)(ev); };
      e.onkeyup = (ev) => { if (ev.key === " " || ev.key === "Enter") press(0)(ev); };
      this.btnEls.push(e);
      return e;
    }));
    const nk = c.keys || 0, kb = h("div", { class: "simkeys", role: "group", "aria-label": "Keys" });
    if (nk) {
      WHITE.forEach((k, i) => kb.append(this.key(k, "w", i * (100 / WHITE.length), 100 / WHITE.length)));
      BLACK.forEach((k) => {
        const left = WHITE.findIndex((w) => w > k) - 1;   // white key to its left
        kb.append(this.key(k, "b", (left + 1) * (100 / WHITE.length) - 100 / WHITE.length * 0.3, 100 / WHITE.length * 0.6));
      });
    }
    root.replaceChildren(this.say, this.statusEl, knobs, btns, nk ? kb : "", nk ? h("p", { class: "meta" }, "Play the keys with the mouse or touch, or the computer keyboard (A W S E D F T G Y H U J K…).") : "", this.canvas, h("label", { class: "simvol" }, h("span", {}, "Volume"), volume));
  }

  // the whole panel: screen, encoders (drag, wheel, arrow keys or the - / + buttons), buttons and keys
  buildPanel() {
    const c = this.cfg;
    this.say = h("div", { class: "simsay sr", "aria-live": "polite" }, "");
    this.statusEl = h("div", { class: "simstatus" });
    this.screen = h("canvas", { width: 240, height: 240, class: "simscreen", role: "img", "aria-label": "The FM-1's screen" });
    this.img = this.screen.getContext("2d").createImageData(240, 240);
    const grp = (cls, title, kids) => h("div", { class: "grp " + cls, role: "group", "aria-label": title }, h("span", { class: "grptitle" }, title), h("div", { class: "grpbody" }, kids));
    // like the panel: SELECT, ALGO and PRESET together with the master volume; the four macro knobs apart
    const master = this.masterKnob();
    const nav = h("div", { class: "grp nav", role: "group", "aria-label": "Master, select, preset, algorithm" }, h("div", { class: "grpbody" }, [master, this.encoder("SELECT", 0), this.encoder("PRESET", 2), this.encoder("ALGO", 1)]));   // the panel's order: MASTER SELECT PRESET ALGO
    const macros = grp("macros", "Macros", [3, 4, 5, 6].map((r) => this.encoder(ENCODERS[r], r)));
    const button = (id) => {
      const label = BUTTONS[id];
      const e = h("button", { class: "pbtn", type: "button", "aria-label": label }, label);
      const press = (d) => (ev) => { ev?.preventDefault?.(); if (d === 1 && e.dataset.down === "1") return; e.dataset.down = d ? "1" : ""; this.send({ t: "btn", id, d }); };
      e.onpointerdown = (ev) => { e.setPointerCapture?.(ev.pointerId); press(1)(ev); };
      e.onpointerup = e.onpointercancel = press(0);
      e.onkeydown = (ev) => { if ((ev.key === " " || ev.key === "Enter") && !ev.repeat) press(1)(ev); };
      e.onkeyup = (ev) => { if (ev.key === " " || ev.key === "Enter") press(0)(ev); };
      e.oncontextmenu = (ev) => ev.preventDefault();
      this.btnEls[id] = e;
      return e;
    };
    const bgrp = (ids) => h("div", { class: "bgrp" }, ids.map(button));
    const btns = h("div", { class: "simpanel" }, bgrp([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11]));   // the panel's order, two rows of six: FX SEL ENV LFO EDIT GLO / HOME SAVE ARP SEQ PLAY REC
    const kb = h("div", { class: "simkeys", role: "group", "aria-label": "Keys" });
    WHITE.forEach((k, i) => kb.append(this.key(k, "w", i * (100 / WHITE.length), 100 / WHITE.length)));
    BLACK.forEach((k) => {
      const left = WHITE.findIndex((w) => w > k) - 1;
      kb.append(this.key(k, "b", (left + 1) * (100 / WHITE.length) - 100 / WHITE.length * 0.3, 100 / WHITE.length * 0.6));
    });
    const octs = h("div", { class: "octs" }, h("span", { class: "grptitle" }, "Octave"), button(12), button(13));
    this.canvas = h("canvas", { width: 600, height: 80, class: "scope", "aria-label": "Output waveform" });
    this.vol.gain.value = 0.25;
    this.root.replaceChildren(
      h("div", { class: "simdev" },
        h("div", { class: "simtop" }, this.screen, h("div", { class: "simctl" }, nav, macros, btns)),
        octs, kb),
      this.statusEl, this.say,
      h("p", { class: "meta" }, c.help || "Turn an encoder by dragging it up or down, with the mouse wheel, or with its - and + buttons. Play the keys with the mouse, touch, or the computer keyboard (A W S E D F T G Y H U J K…)."),
      this.canvas);
  }

  // the master volume: a knob of the same kind (the page's, not the firmware's), 0..20 steps
  masterKnob() {
    this.masterStep = 10;
    const set = () => { this.vol.gain.value = (this.masterStep / 20) ** 2 * 0.5; };
    set();
    return this.encoder("MASTER", -1, (d) => { this.masterStep = Math.max(0, Math.min(20, this.masterStep + d)); set(); });
  }

  encoder(name, role, local) {
    const turn = (d) => { if (d) { if (local) { if (local(d) === false) return; } else this.send({ t: "enc", role, d }); knob.style.setProperty("--a", ((+knob.dataset.a || 0) + d * 15) + "deg"); knob.dataset.a = String((+knob.dataset.a || 0) + d * 15); } };
    const knob = h("div", { class: "knob", tabindex: "0", role: "slider", "aria-label": name + " encoder" });
    let y0 = null, acc = 0;
    knob.onpointerdown = (ev) => { knob.setPointerCapture(ev.pointerId); y0 = ev.clientY; acc = 0; ev.preventDefault(); };
    knob.onpointermove = (ev) => {
      if (y0 === null) return;
      acc += y0 - ev.clientY; y0 = ev.clientY;
      const d = Math.trunc(acc / 6); if (d) { acc -= d * 6; turn(d); }
    };
    knob.onpointerup = knob.onpointercancel = () => { y0 = null; };
    knob.onwheel = (ev) => { ev.preventDefault(); turn(ev.deltaY < 0 ? 1 : -1); };
    knob.onkeydown = (ev) => {
      const d = { ArrowUp: 1, ArrowRight: 1, ArrowDown: -1, ArrowLeft: -1, PageUp: 5, PageDown: -5 }[ev.key];
      if (d) { ev.preventDefault(); turn(d); }
    };
    const step = (d, t) => {
      const b = h("button", { class: "kstep", type: "button", "aria-label": name + (d < 0 ? " down" : " up") }, t);
      let rep = null;
      const stop = () => { clearTimeout(rep); clearInterval(rep); rep = null; };
      b.onpointerdown = (ev) => { ev.preventDefault(); turn(d); stop(); rep = setTimeout(() => { rep = setInterval(() => turn(d), 70); }, 350); };
      b.onpointerup = b.onpointercancel = b.onpointerleave = stop;
      b.onkeydown = (ev) => { if (ev.key === " " || ev.key === "Enter") { ev.preventDefault(); turn(d); } };
      return b;
    };
    return h("div", { class: "enc" }, knob, h("div", { class: "encrow" }, step(-1, "−"), step(1, "+")), h("span", { class: "encname" }, name));
  }

  key(k, cls, left, width) {
    const e = h("div", { class: "simkey " + cls, style: `left:${left}%;width:${width}%`, "data-k": k });
    const on = (d) => { if (d === 1 ? !this.down.has(k) : this.down.has(k)) { d ? this.down.add(k) : this.down.delete(k); this.send({ t: "key", k, d }); e.classList.toggle("on", !!d); } };
    e.onpointerdown = (ev) => { e.setPointerCapture(ev.pointerId); on(1); };
    e.onpointerup = e.onpointercancel = () => on(0);
    e.onpointerenter = (ev) => { if (ev.buttons) on(1); };
    e.onpointerleave = () => on(0);
    e.press = on;
    this.keyEls[k] = e;
    return e;
  }

  keyHandlers() {
    const f = (d) => (ev) => {
      if (ev.repeat || ev.ctrlKey || ev.metaKey || ev.altKey || /^(INPUT|SELECT|TEXTAREA)$/.test(ev.target.tagName)) return;
      const i = KEYMAP.indexOf(ev.key.toLowerCase());
      if (i >= 0 && this.keyEls[7 + i]) { ev.preventDefault(); this.keyEls[7 + i].press(d); }
    };
    this.listeners = [["keydown", f(1)], ["keyup", f(0)]];
    for (const [t, fn] of this.listeners) window.addEventListener(t, fn);
  }

  onState(m) {
    if (m.state) this.state = m.state;
    if (m.fb && this.img) {
      const d = this.img.data, fb = m.fb;
      for (let i = 0; i < fb.length; i++) {
        const c = ((fb[i] & 0xff) << 8) | (fb[i] >> 8);
        d[4 * i] = ((c >> 11) & 31) * 255 / 31; d[4 * i + 1] = ((c >> 5) & 63) * 255 / 63; d[4 * i + 2] = (c & 31) * 255 / 31; d[4 * i + 3] = 255;
      }
      this.screen.getContext("2d").putImageData(this.img, 0, 0);
    }
    if (this.cfg.panel) {
      const s = this.state;
      this.btnEls.forEach((e, i) => { e.classList.toggle("lit", !!(s[7] >> i & 1)); e.classList.toggle("dim", !(s[7] >> i & 1) && !!(s[9] >> i & 1)); });
      this.keyEls.forEach((e, k) => { if (e) { e.classList.toggle("lit", !!(s[8] >> k & 1)); e.classList.toggle("dim", !(s[8] >> k & 1) && !!(s[10] >> k & 1)); } });
      if (m.say) this.say.textContent = m.say;
      this.statusEl.replaceChildren(...(this.cfg.status || []).map((st) => {
        const v = s[st.get];
        return h("span", {}, h("b", {}, st.label + " "), st.names ? (st.names[v] ?? String(v)) : st.fmt === "page" ? (Math.floor(v / 10) + "." + (v % 10 + 1)) : String(st.add ? v + st.add : v), " ");
      }));
      return;
    }
    if (m.say) { this.say.textContent = m.say; clearTimeout(this.sayT); this.sayT = setTimeout(() => (this.say.textContent = this.fw.name), 1800); }
    const s = this.state, c = this.cfg;
    (c.knobs || []).forEach((k, i) => {
      const r = this.knobEls[i];
      if (!this.dragging.has(i) && +r.value !== s[k.get]) { r.value = s[k.get]; r.dataset.last = String(s[k.get]); }
    });
    (c.buttons || []).forEach((b, i) => { if (b.get != null) this.btnEls[i].classList.toggle("lit", !!s[b.get]); });
    this.statusEl.replaceChildren(...(c.status || []).map((st) => {
      const v = s[st.get];
      return h("span", {}, h("b", {}, st.label + " "), st.fmt === "note" ? noteName(v) : String(v), " ");
    }));
  }

  draw() {
    if (this.stopped) return;
    const cv = this.canvas, g = cv.getContext("2d"), buf = new Uint8Array(this.an.fftSize);
    this.an.getByteTimeDomainData(buf);
    g.clearRect(0, 0, cv.width, cv.height);
    g.strokeStyle = getComputedStyle(document.body).getPropertyValue("--accent") || "#0f6b66";
    g.lineWidth = 2; g.beginPath();
    for (let i = 0; i < buf.length; i++) {
      const x = (i / buf.length) * cv.width, y = (buf[i] / 255) * cv.height;
      i ? g.lineTo(x, y) : g.moveTo(x, y);
    }
    g.stroke();
    requestAnimationFrame(() => this.draw());
  }
}
