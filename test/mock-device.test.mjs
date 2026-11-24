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
import { makeDevice } from "./emulator.mjs";
for (const fast of [false, true]) {
  const dev = makeDevice(pkg, fast);
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
