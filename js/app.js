// The flasher page. Protocol: fm1ota.js / fm1ports.js (from fm1-base). Packages: package.js. Firmwares: firmware/firmwares.json.
import { Updater, hasLoader } from "./fm1ota.js";
import { openPackage, isStock } from "./package.js";
import { Simulator } from "./simulator.js";

const $ = (id) => document.getElementById(id);
const h = (tag, attrs = {}, ...kids) => {
  const e = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs)) k === "class" ? (e.className = v) : e.setAttribute(k, v);
  for (const k of kids.flat()) e.append(k instanceof Node ? k : document.createTextNode(k));
  return e;
};
const isOta = (id) => /^ota-/i.test(id.model);

const state = { access: null, updater: null, id: null, busy: false };

// ------------------------------------------------------------------ support
const supported = !!navigator.requestMIDIAccess && window.isSecureContext;
if (!supported) { $("unsupported").classList.remove("hidden"); $("connect").disabled = true; }

// ------------------------------------------------------------- connecting
async function connect() {
  try {
    state.access = await navigator.requestMIDIAccess({ sysex: true });
  } catch (e) {
    setDevice("bad", "MIDI access was refused. Allow MIDI devices and system-exclusive messages for this site, then try again.");
    return;
  }
  state.updater = new Updater(state.access);
  let timer;
  state.access.onstatechange = () => { clearTimeout(timer); timer = setTimeout(() => { if (!state.busy) scan(); }, 800); };
  $("connect").classList.add("hidden");
  $("rescan").classList.remove("hidden");
  await scan();
}

async function scan() {
  setDevice("", "Looking for the FM-1…");
  state.id = null; refreshButtons();
  const dev = await state.updater.findAny();
  if (!dev) {
    const names = state.updater.ports();
    setDevice("bad", names.length ? "The FM-1 was not found. Is another app using it?" : "No MIDI device found. Check the USB data cable.");
    fillPorts(names);
    return;
  }
  dev.link.close();
  $("portpick").classList.add("hidden");
  state.id = dev.id;
  setDevice("ok", describe(dev.id));
  refreshButtons();
}

function describe(id) {
  if (isOta(id)) return `FM-1 found in update mode (${id.text}): an earlier install was cut off. Press Install on the firmware you were installing to finish it.`;
  if (id.open) return `FM-1 found, running M-VAVE's Open firmware (${id.text}).`;
  return `FM-1 found, running ${id.text}.`;
}

function setDevice(kind, text) {
  $("dot").className = "dot " + kind;
  $("devtext").textContent = text;
}

function fillPorts(names) {
  const sel = $("portlist");
  sel.replaceChildren(...names.map((n) => h("option", { value: n }, n)));
  $("portpick").classList.toggle("hidden", !names.length);
}

$("connect").onclick = connect;
$("rescan").onclick = scan;
$("tryport").onclick = async () => {
  const name = $("portlist").value;
  setDevice("", `Asking ${name}…`);
  const id = await state.updater.probe(name);
  if (id) { state.id = id; setDevice("ok", describe(id)); $("portpick").classList.add("hidden"); refreshButtons(); }
  else setDevice("bad", `${name} did not answer like an FM-1.`);
};

// ---------------------------------------------------------------- firmware list
let firmwares = [];
async function loadList() {
  const box = $("list");
  try {
    const r = await fetch("firmware/firmwares.json?t=" + Date.now(), { cache: "no-store" });
    if (!r.ok) throw new Error(r.status);
    firmwares = (await r.json()).firmwares;
  } catch (e) {
    box.replaceChildren(h("div", { class: "banner bad" }, "Could not load the firmware list. If you opened this page as a file, serve it instead: python3 -m http.server, then open http://localhost:8000."));
    return;
  }
  renderLists();
}

// Builds: an entry's top-level fields are its "main" build. An optional "tests" array lists test builds (profile builds, PR builds):
// each needs file + sha256 + version (and wasm for a player build), anything else it leaves out is the main entry's.
const selected = {};      // firmware id -> "main" or the index of the chosen test build
function buildsOf(fw) {
  const { tests, ...rest } = fw;
  const main = { ...rest, channel: "main", label: "main" };
  const list = (tests || []).filter((t) => t.file && t.sha256).map((t, i) => {
    const b = { ...rest, ...t, channel: "test", label: t.label || "test " + (i + 1), preview: t.preview };
    if (rest.sim || t.sim || t.wasm) b.sim = { ...(rest.sim || {}), ...(t.sim || {}), ...(t.wasm ? { wasm: t.wasm } : {}) };
    if (!b.sim?.wasm) delete b.sim;
    return b;
  });
  return { main, tests: list };
}
function current(fw) {
  const { main, tests } = buildsOf(fw), k = selected[fw.id];
  return k === undefined || k === "main" || !tests[k] ? main : tests[k];
}
// main / test switch (and the list of test builds); only for firmwares that have test builds. `need`: only builds with this field count
function picker(fw, need) {
  const { tests } = buildsOf(fw);
  const usable = tests.map((b, i) => [b, i]).filter(([b]) => !need || b[need]);
  if (!usable.length) return "";
  const k = selected[fw.id] === undefined ? "main" : selected[fw.id];
  const seg = (label, on, fn) => { const b = h("button", { type: "button", class: "seg" + (on ? " on" : ""), role: "radio", "aria-checked": String(on) }, label); b.onclick = fn; return b; };
  const row = h("div", { class: "picker", role: "radiogroup", "aria-label": "Build" },
    seg("main", k === "main", () => { selected[fw.id] = "main"; renderLists(); }),
    seg("test", k !== "main", () => { if (k === "main") { selected[fw.id] = usable[0][1]; renderLists(); } }));
  if (k !== "main") {
    const sel = h("select", { "aria-label": "Test build" }, usable.map(([b, i]) => h("option", { value: String(i) }, b.label + " · " + b.version)));
    sel.value = String(k);
    sel.onchange = () => { selected[fw.id] = +sel.value; renderLists(); };
    row.append(sel);
  }
  return row;
}
function buildInfo(b) {
  if (b.channel !== "test") return "";
  return h("p", { class: "meta" }, `Test build ${b.label}${b.commit ? " · commit " + String(b.commit).slice(0, 7) : ""}${b.built ? " · built " + b.built : ""}. ${b.notes || "Unreleased: it may be unstable."}`);
}
function renderLists() {
  $("list").replaceChildren(...firmwares.map(card));
  $("trylist").replaceChildren(...firmwares.filter((f) => f.sim).map(tryCard));
  refreshButtons();
}

function card(entry) {
  const fw = current(entry);
  const btn = h("button", { "data-install": fw.id }, fw.recovery ? "Restore" : fw.channel === "test" ? "Install test build" : "Install");
  btn.onclick = () => startInstall(fw);
  return h("div", { class: "panel fw" },
    h("div", { class: "row", style: "justify-content:space-between" }, h("h3", {}, fw.name), btn),
    picker(entry, "file"),
    h("div", { class: "meta" }, `${fw.version} · ${fw.product}`),
    buildInfo(fw),
    h("details", {}, h("summary", {}, "About"),
      h("p", {}, fw.description || ""),
      h("div", {}, (fw.tags || []).map((t) => h("span", { class: "tag" }, t))),
      fw.preview ? h("audio", { controls: "", preload: "none", src: "firmware/" + fw.preview + "?v=" + String(fw.sha256 || "").slice(0, 12) }) : "",
      fw.controls ? h("table", {}, fw.controls.map(([a, b]) => h("tr", {}, h("td", {}, a), h("td", {}, b)))) : "",
      fw.credits ? h("p", { class: "meta" }, fw.credits) : ""));
}

// the in-browser simulator (js/simulator.js): hear a firmware without flashing it
function tryCard(entry) {
  const cur = current(entry), fw = cur.sim ? cur : buildsOf(entry).main;
  return h("div", { class: "panel fw" },
    h("h3", {}, fw.name), picker(entry, "sim"), h("div", { class: "meta" }, fw.version), buildInfo(fw),
    h("div", { class: "row" }, tryBtn(fw)));
}
let sim = null;
function tryBtn(fw) {
  const b = h("button", { class: "ghost", type: "button" }, "Try in browser");
  b.onclick = async () => {
    sim?.stop();
    $("sim-title").textContent = "Try " + fw.name + (fw.channel === "test" ? ` (test: ${fw.label})` : "");
    $("sim").showModal();
    sim = new Simulator($("sim-body"), fw);
    try { await sim.start(); } catch (e) { $("sim-body").replaceChildren(h("div", { class: "err" }, "The simulator could not start: " + e.message)); }
  };
  return b;
}
$("sim-close").onclick = () => $("sim").close();
$("sim").addEventListener("close", () => { sim?.stop(); sim = null; $("sim-body").replaceChildren(); });

function refreshButtons() {
  for (const b of document.querySelectorAll("[data-install]")) b.disabled = state.busy || !supported;
  $("localfile").disabled = state.busy || !supported;
}

// ----------------------------------------------------------------- install
const msgs = {
  notfound: "The FM-1 was not found. Check the cable and close other MIDI apps.",
  model: "This firmware is for a different device.",
  open: "This FM-1 runs M-VAVE's Open firmware. Restore the stock firmware first (the Restore card), then install this.",
  foreign: "The FM-1 is in the update mode of M-VAVE's own firmware and this package is not for it. Nothing was written. Install 'Back to stock' first.",
  stopped: "The FM-1 stopped answering. If nothing was written, you are safe; otherwise it is in update mode and the install can be resumed.",
  lost: "The USB connection dropped.",
  noloader: "The update loader did not appear.",
  noreturn: "The FM-1 did not come back after the install. Power-cycle it (unplug and replug), then press Scan again.",
  mismatch: "The write finished, but the FM-1 reports a different firmware ID than the package. This is not the file checksum: the file was written as downloaded.",
  badreq: "The FM-1 asked for data outside the package. The install was stopped.",
  hash: "The downloaded file does not match its checksum, so nothing was sent to the FM-1. Reload the page (Ctrl+Shift+R) and try again: your browser or the site cache probably has an old copy of the firmware list.",
  badpkg: "This file is not a valid FM-1 package.",
};
const resumable = new Set(["stopped", "lost", "noloader", "noreturn"]);

async function startInstall(fw) {
  if (state.busy) return;
  if (!state.id) {                                  // Install before Connect: connect first
    state.busy = true; refreshButtons();
    try { if (!state.updater) await connect(); else await scan(); } finally { state.busy = false; refreshButtons(); }
    if (!state.id) return showFailure(fw.name, Object.assign(new Error("No FM-1 found. Plug it in with a data cable, close other MIDI apps, and try again."), { code: "notfound" }));
  }
  let pkg;
  try {
    // The URL carries the start of the expected hash, so a changed file is a new URL and never an old cached copy of it.
    // A mismatch is tried once more, bypassing every cache, before it is reported.
    const get = async (retry) => {
      const r = await fetch(`firmware/${fw.file}?v=${fw.sha256.slice(0, 12)}${retry ? "&r=" + Date.now() : ""}`, retry ? { cache: "reload" } : {});
      if (!r.ok) throw new Error(`could not download ${fw.file} (${r.status})`);
      return openPackage(await r.arrayBuffer(), fw);   // checks SHA-256 and identity against the list
    };
    try { pkg = await get(false); } catch (e) { if (e.code !== "hash") throw e; pkg = await get(true); }
  } catch (e) { return showFailure(fw.name, e); }
  confirmAndRun(fw.channel === "test" ? `${fw.name} (test build ${fw.label})` : fw.name, pkg, { recovery: !!fw.recovery, verified: true });
}

$("localfile").onchange = async (ev) => {
  const f = ev.target.files[0]; ev.target.value = "";
  if (!f || state.busy) return;
  if (!state.id) { state.busy = true; try { if (!state.updater) await connect(); else await scan(); } finally { state.busy = false; } }
  try {
    if (!state.id) throw Object.assign(new Error("No FM-1 found."), { code: "notfound" });
    const pkg = await openPackage(await f.arrayBuffer());
    if (!hasLoader(pkg.image) && !isStock(pkg.product)) {
      throw Object.assign(new Error(`${f.name}: no Felucca update loader in this package, and it is not M-VAVE's firmware. It cannot be installed from here.`), { code: "badpkg" });
    }
    confirmAndRun(f.name, pkg, { recovery: isStock(pkg.product), verified: false });
  } catch (e) { showFailure(f.name, e); }
};

function confirmAndRun(name, pkg, opt) {
  const id = state.id, model = pkg.product.split("_")[0], inLoader = isOta(id);
  if (!inLoader && !id.open && id.model !== model) {
    return showFailure(name, Object.assign(new Error(`the connected device is ${id.text}, the package is for ${pkg.product}`), { code: "model" }));
  }
  run(name, pkg, inLoader);
}

const guard = (e) => { e.preventDefault(); e.returnValue = ""; };

async function run(name, pkg, inLoader) {
  state.busy = true; refreshButtons();
  $("p-title").textContent = "Installing " + name;
  $("p-end").replaceChildren(); $("p-close").classList.add("hidden");
  setProgress(1, "Starting…");
  $("progress").addEventListener("cancel", (e) => e.preventDefault());
  $("progress").showModal();
  window.addEventListener("beforeunload", guard);
  let wake = null; try { wake = await navigator.wakeLock?.request("screen"); } catch (_) {}
  const len = pkg.image.length;
  const step = (k, a, b, c) => {
    switch (k) {
      case "start": setProgress(3, `Starting the update on ${a}…`); break;
      case "verify": setProgress(Math.min(12, 3 + a / 8), "The FM-1 is checking the package…"); break;
      case "loader": setProgress(13, "Switching to update mode… (the USB device re-appears, this takes a few seconds)"); break;
      case "mode": setProgress(15, k && a === "fast" ? "Writing: only changed sectors (fast install)…" : "Writing the whole package (classic install)…"); break;
      case "write": {
        const pct = c !== undefined ? c : Math.min(99, (b * 100) / (2 * len));
        setProgress(15 + pct * 0.8, `Writing… ${Math.floor(pct)}%.`);
        break;
      }
      case "reboot": setProgress(97, "Restarting the FM-1…"); break;
      case "done": setProgress(100, `Done: the FM-1 runs ${a}.`); break;
    }
  };
  try {
    state.updater.classic = false;
    let ok = true;
    if (inLoader) ok = await state.updater.resume(pkg.image, step, pkg.product);
    else await state.updater.install(pkg.image, pkg.product, step, pkg.sha256);
    if (!ok) throw Object.assign(new Error("the write did not finish"), { code: "stopped" });
    $("p-title").textContent = "Done";
    $("p-end").replaceChildren(h("div", { class: "ok-msg" }, `${name} is installed. The FM-1 restarted on ${pkg.product}.`));
  } catch (e) {
    $("p-title").textContent = "The install did not finish";
    if (e.code === "mismatch") e.message += ` (the package says ${pkg.product}; the FM-1 may be running the new firmware, check its screen)`;
    $("p-end").replaceChildren(failureBox(e));
    setProgress(0, e.message || String(e));
  } finally {
    window.removeEventListener("beforeunload", guard);
    try { await wake?.release(); } catch (_) {}
    state.busy = false;
    $("p-close").classList.remove("hidden");
    $("p-close").onclick = () => { $("progress").close(); scan(); };
  }
}

function setProgress(pct, text) {
  $("p-bar").style.width = Math.max(0, Math.min(100, pct)) + "%";
  if (text) $("p-text").textContent = text;
}

function failureBox(e) {
  const code = e.code || "";
  return h("div", { class: "err" },
    h("strong", {}, msgs[code] || "Something went wrong."), " ",
    e.message && e.message !== msgs[code] ? h("div", { class: "mono" }, e.message) : "",
    resumable.has(code) ? h("p", {}, "Replug the FM-1, press Scan again and Install the same firmware to finish.") : "");
}

function showFailure(name, e) {
  $("p-title").textContent = name;
  setProgress(0, "");
  $("p-text").textContent = "";
  $("p-end").replaceChildren(failureBox(e));
  $("p-close").classList.remove("hidden");
  $("p-close").onclick = () => $("progress").close();
  $("progress").showModal();
}

loadList();
