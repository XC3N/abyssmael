# Abyssmael: modules and who owns what

Abyssmael is generative ambient firmware for the M-VAVE FM-1, built on fm1-base (MPL-2.0). All sound comes from our own code.

## Layout

| File | Side | Owns |
|---|---|---|
| `abyssmael.h` | contract | Every shared type, enum, table and prototype. Change it only with a note to the owners of the modules it touches. |
| `app.c` | UI, entry | The platform contract (`app_init`, `app_ui_init`, `app_frame`, `app_expect`) and the unity build of `scene.c`, `ui.c`, `display.c`. |
| `scene.c` | UI | The scene store (`am_store`), the factory scenes, `scene_use`. |
| `ui.c` | UI | Input, pages, the black-key submenu map, MIX, note entry, LEDs. |
| `display.c` | UI | `draw()`: header, pages, SEQ pattern page, the black-key footer. |
| `core.c` | sound, -O2 | `render_block` and `app_render` (the order of the stages), `app_panic`, the scene `sc`. |
| `seq.c` | sound, -O2 | Clock, GEN / EUCL / STEP, ARP, held keys, the key queue, drift. Starts notes; makes no audio. |
| `engines.c` | sound, -O2 | The five instruments (DRONE, PAD, BELL, PLUCK, TEXTURE), voices, envelopes, LFOs, the two-core split. |
| `fx.c` | sound, -O2 | Delay, shimmer reverb, the AGE layer, soft clip, master gain. |
| `fm1.toml`, `words.tsv` | build | Package identity and the screen-reader words. |
| `tests/basic.txt` | test | Host script with `expect` checks (48). |

The audio order inside a block is `seq_block`, `engines_block`, `engines_run`, `fx_block`, `engines_flush`.

## Boundaries

- The UI side writes scene data (`sc`, `ui`) and calls the sound side only through the prototypes in `abyssmael.h`. The sound side never includes or calls UI code and never draws.
- The sound side reads `sc` but does not write it, except through `seq.c` for the sequencer's own state.
- `core.c`, `seq.c`, `engines.c` and `fx.c` are separate -O2 units (`o2 = [...]` in `fm1.toml`), each compiled alone. Each includes `abyssmael.h`; none includes another `.c`. The UI files are one unity unit.
- Shared mutable state used by both cores or by the UI and the render is `volatile` or a plain word written from one side only.

## Rules for audio code

- Functions marked `PLAT_RAM_CODE` (in `fx.c`, `engines.c`) may call only RAM code. Helpers they use (`pct`, `fm_exp2f`, delay reads) must be `always_inline`; under `AMB_ENGINE` `abyssmael.h` makes `inline` mean that. Flash code reaches RAM code through `PLAT_FAR(fn)`. `tools/ramcode.py` checks this in the build.
- RAM code budget: 32 KB (`.ram_text`). Check the build's size lines before adding code there.
- No per-sample `sin`, `exp` or `tanh` in the block loops: modulate once per block and step across it (see `rd_step`, `wow_dstep`).
- Flush one-pole and feedback states to 0 when tiny (`no_denorm`): denormals are slow on the FPU.
- Second core: voices with odd index render on core 2 into `mx[1]`, even on core 1 into `mx[0]`, then `engines_flush` sums them. `plat_cpu2_*` calls are made only from flash code (`engines_run`). With `cpu2_fallback` a dropped job is redone on core 1. Voices must not share writable state across the split.
- Store size must stay within `PLAT_STORE_MAX` (a `_Static_assert` guards it). Change the store magic when the layout changes.

## Build and test

```
cp -r src ~/src/fm1-base/abyssmael
cd ~/src/fm1-base
.venv/bin/python tools/build.py abyssmael
.venv/bin/python tools/build.py abyssmael --host
./abyssmael/build/host/abyssmael_host abyssmael/tests/basic.txt out/
```

The host script supports `wait`, `tap`, `key`/`tapkey` (`b0..b10`, `w0..w15`), `turn`, `spin`, `wav`, `wavstop`, `shot` and `expect`. New features add checks to `tests/basic.txt`.

Browser simulator (needs clang with wasm32):

```
SIM_MEM=4194304 SIM_UNITS="core.c seq.c engines.c fx.c" \
  scripts/build-sim.sh src firmware/abyssmael/sim/sim.c firmware/abyssmael/sim.wasm
```

## Who works where

Parallel threads, one per area: UI (`ui.c`, `display.c`, `scene.c`), sequencers (`seq.c`), engines (`engines.c`), FX (`fx.c`), core/render and CPU (`core.c`). Keep a change inside your files when you can; when you need a new shared field or prototype, add it to `abyssmael.h` in a small commit of its own and rebase often.
