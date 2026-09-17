# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Repatch is a PixInsight process module (macOS / Apple Silicon only) that does
content-aware fill of a masked region using multi-scale PatchMatch. It ships
with a healing-brush interface, a mask-image mode and a CLI harness. A
`Neural` mode is planned but disabled. `docs/developer-guide.md` is the
authoritative long-form reference; this file is the short version.

## Build and test

```bash
./build.sh                    # PCL libs (once) + cmake + core/tests/CLI/module + ctest + CLI smoke run
./build.sh --no-module        # core, tests and CLI only — no PCL checkout needed, builds in seconds
./build.sh --no-tests | --debug | --clean | --pcl-path=DIR
```

The module target needs a PCL checkout (default `../PCL`, or `PCLDIR` env /
`-DPCL_DIR=`) at the commit pinned as `PCL_SHA` in
`.github/workflows/build.yml` (PCL 2.10.4 ↔ PixInsight 1.9.4). Without PCL,
plain `cmake -S . -B build` still configures with the module skipped.

```bash
cmake -S . -B build -DREPATCH_BUILD_MODULE=OFF && cmake --build build -j
cd build && ctest --output-on-failure          # 10 test groups + the no_pcl_deps guard
./build/repatch_tests stroke_                  # one group: cases whose name starts with the prefix
./build/repatch_tests random_hashseed          # any prefix works, so a single case can be selected
./build/repatch-cli --synth 256 256 in.fits mask.fits
./build/repatch-cli in.fits mask.fits out.fits --patch 11 --ring 40 --seed 1
```

If `build/` was configured from a different checkout location, ctest reports
every test as "Not Run"; use `./build.sh --clean`.

Tests use the tiny harness in `tests/testing.h` (`TEST_CASE`, `CHECK`,
`REQUIRE`, `CHECK_NEAR`). Case names must start with their group prefix
(`mask_`, `parallel_`, `random_`, `pyramid_`, `stretch_`, `diffusion_`,
`patchmatch_`, `backend_`, `fits_`, `stroke_`); a new group needs an
`add_test` line in `CMakeLists.txt`.

## Sign, package, release

CI (`.github/workflows/build.yml`) builds and tests every push on an Apple
Silicon runner and uploads an *unsigned* package; PixInsight signatures can
only be produced locally by the PixInsight core with the developer's `.xssk`
key, so releases are made on a developer machine:

```bash
scripts/dev_sign_module.sh            # prompts for the .xssk password, writes bin/macosx/arm64/Repatch-pxm.xsgn
scripts/package.sh                    # dist/Repatch-<version>-macosx-arm64.tar.gz + .sha1 (bin/ + doc/ layout)
scripts/make_repo.sh --notes=notes.html   # dist/repo/updates.xri (signed) + package for the GitHub Pages update repo
scripts/install_docs.sh               # copies doc/tools/Repatch into <PixInsight>/doc/tools (sudo)
```

The version comes from `MODULE_VERSION_*` / `MODULE_RELEASE_*` in
`src/module/RepatchModule.cpp` (or an exact `vX.Y.Z` tag). Bumping PCL means
changing `PCL_SHA` in the workflow, the version notes in README and the
developer guide, and the local `../PCL` checkout together.

## Architecture

Three layers with one hard boundary:

- `src/core/repatch/` — the algorithm. Pure C++20, **no PCL includes**
  (the `no_pcl_deps` ctest greps for `#include <pcl/`), no exceptions across
  the public API. `FillBackend.h` is the only header consumers need:
  `FillRequest` (planar float image, hole mask, optional per-pixel weight,
  `FillParams`) → `FillBackend::Fill` → `FillResult`. `Fill` never throws
  and leaves the image untouched on error/abort. `Stroke.h` adds the brush
  layer on top (`RasterizeStroke`, `StrokeRoi`, `DeriveStrokeSeed`,
  `FillStroke`). One responsibility per file: `Mask`, `Pyramid`, `Stretch`,
  `Diffusion`, `PatchMatch`, `PatchBackend` (orchestration), `Parallel`,
  `Random` (PCG32).
- `src/module/` — the PCL adapter: `RepatchParameters` (Meta* parameter
  classes, Sandbox/CloneStamp pattern), `RepatchProcess`, `RepatchInstance`
  (`ExecuteOn`), `RepatchInterface` (dynamic brush interface), `RepatchModule`
  (version). The core sources are compiled into the module a second time with
  PCL's flags (`-O3 -ffast-math`, hidden visibility); the icon SVG is embedded
  via `configure_file` → `build/generated/RepatchIcon.h`.
- `src/cli/` — `repatch-cli` plus a minimal FITS reader/writer (also linked
  into the tests).

### Determinism contract (read before touching the core)

Output is byte-identical for a given seed regardless of thread count. The
core is compiled **without** `-ffast-math` for tests and CLI; the invariants
that make this hold:

- Work is split into row bands (`kBandHeight = 16`, `Parallel.h`) whose layout
  depends only on the row count and pass parity; PatchMatch propagation never
  reads across a band within a pass, odd passes shift the grid by 8 rows.
- Every random stream is a `Pcg32` seeded by `HashSeed(seed, level, pass, band)`
  (or row) and consumed sequentially inside its band/row.
- Vote, distance transforms, pyramid filters and diffusion (red-black SOR)
  are per-row independent with fixed summation order; reductions go through
  per-row buffers, never shared accumulators.
- NaN/Inf guards use a bit-pattern check (`IsFiniteSample`) so they survive
  the module's `-ffinite-math-only`.

### Brush sessions

`RepatchInterface` is a dynamic PCL interface modelled on CloneStamp. The
first click selects the session target and freezes `randomSeed == 0` into a
concrete seed; each released stroke is filled immediately through
`RepatchInstance::FillStroke` with `before`/`after` tiles kept for undo/redo.
**✔** pastes the `before` tiles back, then launches an instance carrying the
stroke table with `isInterfaceInstance` set, and `ExecuteOn` only restores
the `after` tiles — nothing is recomputed. Replaying that instance from an
icon or script (`isInterfaceInstance == false`) refills the strokes in order
with `DeriveStrokeSeed(seed, k)` and reproduces the same pixels because both
paths share `FillStroke`, `StrokeRoi` and the seeds. Brush-mode special
cases: Feather is ignored, Sample ring 0 means `max(32, 3 × radius)`.

### In-app documentation page

`doc/tools/Repatch/Repatch.html` is **hand-written in the output format of
PixInsight's Documentation Compiler** (no `.pidoc` source, no compile step)
so that CI can package it as is. When editing: `xmllint --html --noout
doc/tools/Repatch/Repatch.html` must print nothing, every `href="#…"` needs a
matching `id`, and the content must stay in step with `docs/user-guide.md`
and the interface controls. Copy constructs from an installed compiled page
(e.g. `<PixInsight>/doc/tools/Crop/Crop.html`) rather than inventing markup.

## Conventions

- `src/module`: PCL style — 3-space indent, `p_` instance members, `e_` event
  handlers, `The<Name>Parameter` globals, `Foo_Control` GUI members. Even patch
  sizes are rounded up in `ValidateParameter`.
- `src/core`: plain modern C++20; keep `ThresholdMask` shared between CLI and
  module so both threshold identically.
- No third-party code is vendored. A future Neural backend must keep the core
  PCL-free and keep the ONNX dependency out of the tests target (link it into
  `Repatch-pxm` only).
