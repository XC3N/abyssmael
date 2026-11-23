// Runs the real Updater (js/fm1ota.js) against an emulated FM-1 over a fake Web MIDI: node test/mock-device.test.mjs
import { Updater, pack7, unpack7 } from "../js/fm1ota.js";
import { openPackage } from "../js/package.js";
import { readFileSync } from "node:fs";
import assert from "node:assert";

const manifest = JSON.parse(readFileSync(new URL("../firmware/firmwares.json", import.meta.url)));
const fw = manifest.firmwares[0];
const pkg = await openPackage(readFileSync(new URL("../firmware/" + fw.file, import.meta.url)), fw);
assert.equal(pkg.product, "FM-1_5800");
// a tampered file must be refused
await assert.rejects(openPackage(Buffer.concat([readFileSync(new URL("../firmware/" + fw.file, import.meta.url)), Buffer.from([0])]), fw), /SHA-256/);

const ident = (text) => {
  const d = new Array(34).fill(0); d[1] = 0x59; d[2] = 0x11; d[3] = 28;
  for (let i = 0; i < text.length; i++) d[6 + i] = text.charCodeAt(i);
  return [0xF0, ...pack7(d).slice(0), 0xF7];
};
// pack7 of 34 bytes isn't 34 again; unpack7 on it returns 34 + padding zero bytes? build exact:
const identity = (text) => { const p = pack7(Object.assign(new Array(34).fill(0), { 1: 0x59, 2: 0x11, 3: 28 }, Object.fromEntries([...text].map((c, i) => [6 + i, c.charCodeAt(0)])))); return [0xF0, ...p, 0xF7]; };
const frameOf = (cmd, payload) => { let s = 0; for (const b of payload) s += b; return [0xF0, ...pack7([0, 0x59, cmd, payload.length & 255, payload.length >> 8, 0, ...payload, ~s & 255]), 0xF7]; };
const req = (addr, len) => { const b = [0, 0x59, 0x30, 8, 0, 0, 0, addr & 255, (addr >>> 8) & 255, (addr >>> 16) & 255, (addr >>> 24) & 255, len & 255, len >> 8, 0]; let s = 0; for (let i = 6; i < 14; i++) s += b[i]; return [0xF0, ...pack7([...b, ~s & 255]), 0xF7]; };

function makeDevice(fast) {
  const dev = { mode: "run", text: "FM-1_5700", written: null, port: null };
  const inp = { id: "i", name: "FM-1", state: "connected", open: async () => {}, onmidimessage: null, addEventListener() {}, removeEventListener() {} };
  const emit = (a) => setTimeout(() => inp.onmidimessage?.({ data: Uint8Array.from(a) }), 1);
  let pending = null;       // the read in flight
  const ask = (addr, len) => { pending = { addr, len }; emit(req(addr, len)); };
  let phase = null, pos = 0, got = [], total = pkg.image.length, sectors = 0, order = [];
  const out = {
    id: "o", name: "FM-1", state: "connected", open: async () => {},
    send(a) {
      const b = [...a];
      if (b.join() === [0xF0, 0, 0x32, 0x45, 0, 0, 0, 0x40, 0x7F, 0xF7].join()) return emit(identity(dev.mode === "loader" ? "ota-" + dev.text : dev.text));
      if (b[1] === 0x00 && b[2] === 0x32 && b[4] === 0x08) return;            // HS_FULL: not answered
      if (b[1] === 0x7D) return;
      if (b.join() === "240,34,36,53,127,247") {                                // UPGRADE
        phase = dev.mode === "run" ? "check" : "write"; pos = 0; got = [];
        if (phase === "check") ask(0, 512);
        else if (fast) { phase = "table"; sectors = Math.floor(total / 4096); ask(0xD0000000, sectors * 6 > 4096 ? 4096 - 4096 % 6 : sectors * 6); }
        else ask(0, 512);
        return;
      }
      const u = unpack7(b.slice(1, -1));
      if (u[2] === 0x31 && dev.mode === "loader" && fast) return emit(frameOf(0x31, [1, 0, 16]));
      if (u[2] !== 0x30 || !pending) return;
      const len = u[11] | (u[12] << 8), addr = (u[7] | u[8] << 8 | u[9] << 16 | u[10] << 24) >>> 0;
      const data = u.slice(14, 14 + len); pending = null;
      if (addr >= 0xE0000000) {                                                // finish ack
        if (phase === "check") { dev.mode = "boot"; setTimeout(() => { dev.mode = "loader"; inp.state = "connected"; }, 100); }
        else { dev.written = Uint8Array.from(got); dev.mode = "boot"; setTimeout(() => { dev.mode = "run"; dev.text = pkg.product; }, 100); }
        return;
      }
      if (phase === "table") { phase = "write"; ask(0, 4096); pos = 0; return; }
      if (phase === "write" && fast) {
        got.push(...data); pos += data.length; const sz = Math.min(4096, total - pos);
        return pos >= total ? ask(0xF0000000, 0) : ask(pos, Math.min(4096, total - pos));
      }
      got.push(...data); pos += len;
      const fin = phase === "check" ? 0xE0000000 : 0xF0000000;
      pos >= total ? ask(fin, 0) : ask(pos, Math.min(512, total - pos));
    },
  };
  // boot gap: while rebooting nothing answers
  const realSend = out.send; out.send = (a) => { if (dev.mode === "boot") throw new Error("gone"); return realSend(a); };
  dev.access = { inputs: new Map([["i", inp]]), outputs: new Map([["o", out]]) };
  return dev;
}

for (const fast of [false, true]) {
  const dev = makeDevice(fast);
  const up = new Updater(dev.access); up.pause = 0;
  const steps = [];
  const t0 = Date.now();
  const res = await up.install(pkg.image, pkg.product, (k, ...a) => steps.push(k), pkg.sha256);
  assert.equal(res, "FM-1_5800");
  assert.ok(dev.written, "device wrote nothing");
  assert.equal(Buffer.compare(Buffer.from(dev.written.slice(0, pkg.image.length)), Buffer.from(pkg.image)), 0, "written image differs");
  assert.ok(steps.includes("done"));
  console.log(`${fast ? "fast" : "classic"} install ok (${((Date.now() - t0) / 1000).toFixed(1)}s, ${steps.length} steps)`);
}
console.log("all passed");
