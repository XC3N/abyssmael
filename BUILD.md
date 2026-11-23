# Building Abyssmael

Every area thread can build and test on its own. Everything runs on Linux x86-64 (macOS needs Docker or podman for the device toolchain; the host simulator needs only clang).

## One-time setup

```
git clone --recursive https://github.com/charlesvestal/fm1-base ~/src/fm1-base
cd ~/src/fm1-base
tools/setup_toolchain.sh        # pinned JieLi toolchain and SDK files into .toolchain/ (or tools/get_toolchain.sh for the quick way)
python3 -m venv .venv && .venv/bin/pip install pillow   # or let setup_toolchain.sh create .venv from uv.lock
```

Check: `ls ~/src/fm1-base/.venv/bin/python` must exist. If fm1-base is elsewhere, set `FM1_BASE=/path/to/fm1-base`.

## Build and test

From the root of this repo:

```
scripts/build-fw.sh host      # host simulator + the 48+ expect checks in src/tests/basic.txt (needs only clang)
scripts/build-fw.sh device    # the real package; checks the RAM code, image and RAM sizes
scripts/build-fw.sh           # both
```

Results go to `build-out/` (git-ignored): `host.log`, `host/*.png` (screens), `host/*.wav` (audio), `device.log`, `abyssmael.fwsc` and its sha256. The script copies `src/` into a throwaway app folder inside fm1-base, so nothing in fm1-base changes.

Pass criteria for a change: "host tests: N ok" with no FAIL, and for anything touching `core.c`, `seq.c`, `engines.c`, `fx.c` the device build's check lines all `ok` (RAM code under 32768 B, RAM under 98304 B).

## Adding tests

`src/tests/basic.txt` is a script for the host simulator: `wait`, `tap`, `key`/`tapkey` (`b0..b10` black, `w0..w15` white), `turn`, `spin`, `wav`, `wavstop`, `shot`, `expect <name> <value>`. Names for `expect` are in `app_expect` in `src/app.c`; add one there when a feature needs a new check. Put new checks in your own script file beside it and run it with the host binary, or append to `basic.txt`.

## Browser simulator

```
SIM_MEM=4194304 SIM_UNITS="core.c seq.c engines.c fx.c" scripts/build-sim.sh src firmware/abyssmael/sim/sim.c /tmp/sim.wasm
```

Needs clang with the wasm32 target. The site owner publishes `firmware/abyssmael/sim.wasm`; area threads don't need to.

## Rules

See `src/MODULES.md` for module ownership and the audio-code rules (RAM code, `always_inline`, second core). Never push to `main` a change that fails `scripts/build-fw.sh`; open a PR from a branch instead.
