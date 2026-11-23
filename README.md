# FM-1 Firmware Flasher

A static website (no build step) that installs firmware on an M-VAVE FM-1 over USB-MIDI from Chrome/Edge, and lets you
try a firmware in the browser first. Host it anywhere static (GitHub Pages works: serve the folder as the site root).

Run locally: `python3 -m http.server 8000` in this folder, open http://localhost:8000 (Web MIDI needs https or localhost).

## Add a firmware
1. Copy the `.fwsc` to `firmware/<id>/` and add an entry to `firmware/firmwares.json` (`id`, `name`, `version`,
   `product` = the package identity, e.g. `FM-1_5800`, `file`, `sha256`, `description`, optional `controls`, `preview`).
   `sha256sum file.fwsc` gives the hash; the page refuses a file whose hash or identity differs from the list.
2. Optional "Try in browser": build the firmware's simulator with `scripts/build-sim.sh <app source dir> <sim.c> <out.wasm>`
   (see `firmware/abyssmael/sim/sim.c`: the app's own app.c and fm1-base's gfx.c against a small host platform; a sim with a screen needs `SIM_MEM=4194304`; Abyssmael builds from `src/` with `SIM_UNITS="core.c seq.c engines.c fx.c"`, see `src/MODULES.md`), then add a `sim` block
   to its entry (knobs, buttons, keys, status values). The page builds the controls from it.

## Safety checks (same as fm1_install.py)
SHA-256 and identity of the package against the list; model must match the device; a custom package must carry the
Felucca update loader (or be M-VAVE's own); the device checks the package again before erasing anything; an
interrupted install resumes (device left in update mode); SAFE MODE and "Back to stock (V15)" are reachable.

## Tests
`npm test` (node 18+): runs the real install code against an emulated FM-1 (classic and fast install).

## Credits
`js/fm1ota.js` and `js/fm1ports.js` are copied unchanged from fm1-base (MPL-2.0, Charles Vestal, Leo Kuroshita and
contributors). Stock V15 package: M-VAVE, fm1_open (Apache-2.0).

Live site: https://xc3n.github.io/abyssmael/ (deployed by .github/workflows/pages.yml on every push to main).
