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
    const r = await fetch("firmware/firmwares.json", { cache: "no-cache" });
    if (!r.ok) throw new Error(r.status);
    firmwares = (await r.json()).firmwares;
  } catch (e) {
    box.replaceChildren(h("div", { class: "banner bad" }, "Could not load the firmware list. If you opened this page as a file, serve it instead: python3 -m http.server, then open http://localhost:8000."));
    return;
  }
  box.replaceChildren(...firmwares.map(card));
  $("trylist").replaceChildren(...firmwares.filter((f) => f.sim).map(tryCard));
  refreshButtons();
}

function card(fw) {
  const btn = h("button", { "data-install": fw.id }, fw.recovery ? "Restore" : "Install");
  btn.onclick = () => startInstall(fw);
  return h("div", { class: "panel fw" },
    h("h3", {}, fw.name),
    h("div", { class: "meta" }, `${fw.version} · ${fw.product} · ${fw.license || ""}`),
    h("p", {}, fw.description || ""),
    h("div", {}, (fw.tags || []).map((t) => h("span", { class: "tag" }, t))),
    fw.preview ? h("audio", { controls: "", preload: "none", src: "firmware/" + fw.preview }) : "",
    fw.controls ? h("details", {}, h("summary", {}, "Controls"), h("table", {}, fw.controls.map(([a, b]) => h("tr", {}, h("td", {}, a), h("td", {}, b))))) : "",
    fw.credits ? h("p", { class: "meta" }, fw.credits) : "",
    h("div", { class: "row" }, btn));
}

// the in-browser simulator (js/simulator.js): hear a firmware without flashing it
function tryCard(fw) {
  return h("div", { class: "panel fw" },
    h("h3", {}, fw.name), h("div", { class: "meta" }, fw.version),
    h("p", {}, "Play it here with no FM-1 and nothing to install. Works on phones too."),
    h("div", { class: "row" }, tryBtn(fw)));
}
let sim = null;
function tryBtn(fw) {
  const b = h("button", { class: "ghost", type: "button" }, "Try in browser");
  b.onclick = async () => {
    sim?.stop();
    $("sim-title").textContent = "Try " + fw.name;
    $("sim").showModal();
    sim = new Simulator($("sim-body"), fw);
    try { await sim.start(); } catch (e) { $("sim-body").replaceChildren(h("div", { class: "err" }, "The simulator could not start: " + e.message)); }
  };
  return b;
}
$("sim-close").onclick = () => $("sim").close();
$("sim").addEventListener("close", () => { sim?.stop(); sim = null; $("sim-body").replaceChildren(); });

function refreshButtons() {
  for (const b of document.querySelectorAll("[data-install]")) b.disabled = !state.id || state.busy;
  $("localfile").disabled = !state.id || state.busy;
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
  mismatch: "The install finished, but the FM-1 reports a different firmware than expected.",
  badreq: "The FM-1 asked for data outside the package. The install was stopped.",
  hash: "The downloaded file does not match its checksum, so nothing was sent to the FM-1.",
  badpkg: "This file is not a valid FM-1 package.",
};
const resumable = new Set(["stopped", "lost", "noloader", "noreturn"]);

async function startInstall(fw) {
  if (state.busy || !state.id) return;
  let pkg;
  try {
    const r = await fetch("firmware/" + fw.file);
    if (!r.ok) throw new Error(`could not download ${fw.file} (${r.status})`);
    pkg = await openPackage(await r.arrayBuffer(), fw);   // checks SHA-256 and identity against the list
  } catch (e) { return showFailure(fw.name, e); }
  confirmAndRun(fw.name, pkg, { recovery: !!fw.recovery, verified: true });
}

$("localfile").onchange = async (ev) => {
  const f = ev.target.files[0]; ev.target.value = "";
  if (!f || state.busy || !state.id) return;
  try {
    const pkg = await openPackage(await f.arrayBuffer());
    if (!hasLoader(pkg.image) && !isStock(pkg.product)) {
      throw Object.assign(new Error(`${f.name}: no Felucca update loader in this package, and it is not M-VAVE's firmware. It cannot be installed from here.`), { code: "badpkg" });
    }
    confirmAndRun(f.name, pkg, { recovery: isStock(pkg.product), verified: false });
  } catch (e) { showFailure(f.name, e); }
};

function confirmAndRun(name, pkg, opt) {
  const id = state.id;
  const model = pkg.product.split("_")[0];
  const inLoader = isOta(id);
  if (!inLoader && !id.open && id.model !== model) {
    return showFailure(name, Object.assign(new Error(`the connected device is ${id.text}, the package is for ${pkg.product}`), { code: "model" }));
  }
  const body = $("c-body");
  body.replaceChildren(
    h("p", {}, h("strong", {}, name), ` (${pkg.product})`),
    h("p", { class: "mono" }, "SHA-256 " + pkg.sha256),
    h("p", {}, opt.verified ? "The file matches the checksum in the firmware list." : "This file is not from the firmware list: its checksum was not compared with anything."),
    h("p", {}, inLoader ? `The FM-1 is waiting in update mode (${id.text}); the write will be finished.` : `The FM-1 now runs ${id.text}; it will be replaced.`),
    opt.recovery ? "" : h("p", {}, "If the new firmware misbehaves, 'Back to stock' on this page restores M-VAVE's."));
  $("c-title").textContent = opt.recovery ? "Restore firmware" : "Install firmware";
  $("c-ok").checked = false; $("c-go").disabled = true;
  $("c-ok").onchange = () => { $("c-go").disabled = !$("c-ok").checked; };
  $("c-cancel").onclick = () => $("confirm").close();
  $("c-go").onclick = () => { $("confirm").close(); run(name, pkg, inLoader); };
  $("confirm").showModal();
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
        setProgress(15 + pct * 0.8, `Writing… ${Math.floor(pct)}%. Do not unplug.`);
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
    resumable.has(code) ? h("p", {}, "If the FM-1 is now in update mode, it is safe: unplug and replug it, press Scan again, and press Install on the same firmware to finish the write. Two failed starts in a row bring up SAFE MODE, which can always be installed over.") : "");
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
