# Repatch developer guide

The module builds on **macOS arm64** (clang) and **Windows x64** (MSVC). The
two platforms share every line of the algorithm and the PCL adapter; they
differ only in build tooling, which is why most scripts come in a `.sh` and a
`.ps1` flavour. There is no Intel macOS or Linux target.

## Layout

```
CMakeLists.txt        one build for everything, both platforms (see "Build")
build.sh              macOS: PCL prerequisites + cmake + tests + smoke + verification
build.ps1             Windows: the same, for MSVC
scripts/pcl_env.sh    macOS: PCLDIR / PCLINCDIR / PCLSRCDIR / PCLLIBDIR64 / PCLBINDIR64
scripts/build_pcl.sh  macOS: PCL's 3rd-party libs + libPCL-pxi.a via patched makefile copies
scripts/build_pcl.ps1 Windows: the same via PCL's vc17 projects, or scripts/pcl as a fallback
scripts/pcl/          CMake build of PCL-pxi.lib for trees without src/pcl/windows (see "Build")
scripts/dev_sign_module.sh / .ps1   sign the module (hidden password prompt)
scripts/pi_smoke.js   PJSR smoke test of the installed module (run from the Process Console)
scripts/install_docs.sh  copies doc/tools/Repatch into <PixInsight>/doc/tools (sudo)
scripts/package.sh    assembles dist/Repatch-<version>-<platform>.tar.gz (bin/ + doc/ layout)
scripts/make_repo.sh  writes dist/repo/updates.xri (signed) + packages for the GitHub Pages update repository
.github/workflows/    CI: build + tests + unsigned package on macos-26 (see "Continuous integration")
doc/tools/Repatch/    the in-app documentation page (ready-made HTML + images, no compile step)
src/core/repatch/     the algorithm: pure C++20, no PCL (enforced by a test)
src/module/           the PixInsight adapter (PCL): parameters, process, instance, interface
src/cli/              repatch-cli + a minimal FITS reader/writer
tests/                unit tests (tiny in-repo harness, driven by ctest)
rsc/icons/            process icon (SVG, embedded at build time)
docs/                 user guide, this guide, algorithm notes, superpowers specs/plans
bin/macosx/arm64/     macOS build output (ignored by git)
bin/windows/x64/      Windows build output (ignored by git)
dist/                 packages from scripts/package.sh (ignored by git)
.gitattributes        line endings; .sh must stay LF even on Windows (see "Working on Windows")
```

## Build

One `CMakeLists.txt` covers both platforms. `repatch_core`, `repatch_tests`
and `repatch-cli` are plain C++20 and need no PCL at all — with
`-DREPATCH_BUILD_MODULE=OFF` they configure and build in seconds on either
platform. Only the `Repatch-pxm` target needs PCL, and it is compiled with
PCL's own flags because it is loaded into the PixInsight process and has to
match the ABI PCL was built with.

The core is deliberately **not** compiled with fast-math for the tests and the
CLI (`-O3` without `-ffast-math` on clang, `/fp:precise` on MSVC): the
determinism tests depend on it. The module gets fast-math, as PCL's own builds
do, and the NaN/Inf guards use bit-pattern checks so they survive it.

### macOS arm64

Prerequisites: Xcode Command Line Tools (clang 17+; this repo was built with
Apple clang 21 and CLT only — Xcode.app is not required), CMake ≥ 3.20,
GNU make, a PCL checkout at `../PCL` matching the installed PixInsight core
(1.9.4 Lockhart ↔ PCL 2.10.4, commit `afea714e68`).

`./build.sh` does, in order:

1. `scripts/build_pcl.sh` — builds `libcminpack lcms lz4 RFC6234 zlib zstd`
   and `libPCL-pxi.a` into `$PCLDIR/lib/macosx/arm64` using PCL's own
   `makefile-arm64` files. Those makefiles hard-code Xcode.app's SDK path, so
   the script writes `makefile-arm64.local` copies with the SDK from
   `xcrun --show-sdk-path`; originals are untouched. Skipped when the seven
   `.a` files exist.
2. `cmake -S . -B build -DREPATCH_BUILD_MODULE=ON -DPCL_DIR=$PCLDIR` and a
   build. Targets:
   - `repatch_core` — static lib from `src/core`, `-Wall -Wextra`, **no**
     `-ffast-math` (the determinism tests depend on it).
   - `repatch_tests`, `repatch-cli` — link the core only; configure and build
     in seconds with `-DREPATCH_BUILD_MODULE=OFF`.
   - `Repatch-pxm` — the module. Compiles `src/module/*.cpp` plus the core
     sources with PCL's exact flags (copied from
     `PCL/src/modules/processes/CloneStamp/macosx/g++/makefile-arm64`):
     `-D__PCL_MACOSX -O3 -ffast-math -fvisibility=hidden -std=c++20
     -stdlib=libc++ …`, links the seven PCL libs and AppKit, sets
     `-install_name @executable_path/Repatch-pxm.dylib`, ad-hoc codesigns,
     and copies the result to `bin/macosx/arm64/`. `rsc/icons/Repatch.svg` is
     embedded through `configure_file` → `build/generated/RepatchIcon.h`.
3. `ctest` plus a CLI smoke run on a synthetic image (`repatch-cli --synth`).
4. Verifies the dylib is arm64 and exports `InstallPixInsightModule`.

Useful flags: `--no-module` (core/tests/CLI only), `--no-tests`, `--debug`,
`--clean`, `--pcl-path=DIR`.

### Windows x64

Prerequisites: Visual Studio 2022 with the **MSVC v143 x64/x86 build tools**
component (`Microsoft.VisualStudio.Component.VC.Tools.x86.x64`) and a Windows
SDK, CMake ≥ 3.20, and a PCL tree.

The compiler component is worth naming explicitly: adding the *Desktop
development with C++* workload **without** `--includeRecommended` does not
install `cl.exe`, because the compiler is a *recommended*, not a *required*,
component of that workload. Installing the component by id avoids the trap:

```powershell
& "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\setup.exe" `
  modify --installPath "C:\Program Files\Microsoft Visual Studio\2022\Community" `
  --add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
  --add Microsoft.VisualStudio.Component.Windows11SDK.22621 --passive --norestart
```

Then:

```powershell
.\build.ps1 -PclPath E:\PCL
```

The switches mirror `build.sh`: `-NoModule`, `-NoTests`, `-DebugBuild`,
`-Clean`, `-PclPath`. It verifies the result with `dumpbin`, checking that the
DLL is x64 and exports `InstallPixInsightModule`, and copies it to
`bin/windows/x64/`.

**Where to get PCL.** Two sources, and they are not equivalent:

| Source | Version | `src/pcl/windows/vc17/PCL.vcxproj` |
| --- | --- | --- |
| An installed PixInsight (`C:\Program Files\PixInsight`) | whatever that core was built from | yes |
| PCL's git repository | the pinned commit | **no** |

PCL's repository ships `windows/vc17` projects for the six third-party
libraries but not for the main library — `src/pcl/windows/` simply does not
exist there. `scripts/build_pcl.ps1` handles both: it uses PCL's own project
when the tree has one, and otherwise builds `PCL-pxi.lib` through
`scripts/pcl/CMakeLists.txt`, which transcribes the `Release|x64`
configuration of PCL's project (that project lists exactly `src/pcl/*.cpp`
with no exclusions and no per-file settings, so a glob reproduces it for any
version). Note that it also strips CMake's `/DNDEBUG`: neither of PCL's own
builds defines `NDEBUG` — the Windows project uses the differently spelled
`_NDEBUG` — so `assert()` is live in an official PCL and must stay that way.

A tree copied out of `C:\Program Files` must be copied somewhere **writable**:
the MSBuild projects put object files inside `src/.../windows/vc17/x64/Release`.

**Module flags** come from `src/pcl/windows/vc17/PCL.vcxproj`: `/EHa`,
`/fp:fast`, `/arch:AVX2`, `/GS-`, `/GR`, `/Zc:__cplusplus`, `/permissive-`, the
`MultiThreadedDLL` runtime, and `/OPT:REF /OPT:ICF` in place of the macOS
`-dead_strip`. `/EHsc` is stripped from the global CMake flags and the
exception model set per target, so the module does not raise a D9025
"overriding /EHsc with /EHa" on every translation unit. Beyond CMake's default
Windows libraries the module needs exactly two: `dbghelp` (PCL's
`Win32Exception` stack traces) and `userenv` (`GetUserProfileDirectoryW`, used
by `pcl::File::HomeDirectory`).

### Which PixInsight core a build will load into

The hard compatibility gate is in PCL itself, `src/pcl/API.cpp`:

```cpp
if ( apiVersion < PCL_API_Version )
   throw Error( ... "Unsupported API version %X (expected >= %X)" );
```

`PCL_API_Version` (`include/pcl/api/APIInterface.h`) is baked into the module
when it is compiled; `apiVersion` is what the core passes at load time. **A
module therefore requires a core no older than the PCL it was built against**,
and any newer core is fine.

| PCL | `PCL_API_Version` | Oldest core that loads it |
| --- | --- | --- |
| 2.10.4 | `0x0187` | older than 1.9.4 build 1696; the exact build is not recorded in the headers |
| 2.10.8 | `0x0188` | 1.9.4 Lockhart **build 1696** |

The build number comes from the single annotation that exists in all of PCL
2.10.8 — `ProcessInterface.h`, "available since core version 1.9.4 Lockhart
build 1696 (API version 0x188)". It matters for releases because
`updates.xri` expresses core versions as `X.Y.Z` only, with no build number:
a module built against 2.10.8 and advertised for "1.9.4" would also be offered
to 1.9.4 builds older than 1696, where it fails to load. Hence the Windows
package is advertised for 1.9.5 only — see "Releases and the update
repository".

Keeping the platforms on the same PCL commit avoids the whole question and is
worth doing before a release that matters.

## Working on Windows

**Two shells, and they are not interchangeable.** Git Bash (MSYS2) and WSL
both run the `.sh` scripts, but they differ in the mount prefix (`/c` vs
`/mnt/c`) and in how a path must be translated for a native Windows program.
`scripts/package.sh` and `scripts/make_repo.sh` detect which one they are
running under and convert paths with `cygpath -w` or `wslpath -w` before
handing them to `PixInsight.exe` — which, being a native program, would not
understand `/mnt/e/...` at all. Relying on MSYS's automatic translation is not
enough: an option joined with `=`, as all of PixInsight's are, defeats it.

**`PixInsight.exe` is a GUI-subsystem binary.** It writes no diagnostics to
the console, returns a nonzero exit code even when it has succeeded, and can
write its output file a moment *after* the process exits. Every script that
calls it therefore judges success by the file it was supposed to produce, never
by the exit code, and polls briefly for it. PowerShell adds a trap of its own:
the call operator `&` does **not** wait for a GUI-subsystem process, so
`Start-Process -Wait` is used instead.

**Line endings.** `.gitattributes` pins `*.sh` to LF, because with
`core.autocrlf=true` a Windows clone otherwise gets `#!/bin/bash\r`, which
fails outright. It also pins `*.xri` and `*.xsgn` to LF, since PixInsight signs
those over their exact bytes — the `gh-pages` branch carries its own
`.gitattributes` for the same reason. If a clone predates that file, refresh
the working copy with `git add --renormalize .` and re-check out the scripts.

## Signing and installing

`scripts/dev_sign_module.sh` (macOS) and `scripts/dev_sign_module.ps1`
(Windows) read the `.xssk` password with the echo disabled, call
`PixInsight --sign-module-file=… --xssk-file=… --xssk-password=…` and check
that `Repatch-pxm.xsgn` was produced. The password is never stored. Close
PixInsight first.

Both judge the result by the signature file, not by the exit code, for the
reasons in "Working on Windows"; the PowerShell version additionally requires
the file to be *newer* than the moment it started, so it does not have to
delete an existing signature first and a failed run leaves the previous one
intact.

A signature is only accepted if the core knows the signing identity's public
key. For a *local* signing identity (an `.xssk` generated by the SigningKeys
script, bound to your PixInsight license) that means a one-time registration
inside PixInsight: **Edit → Local Signing Identity…**, pick the `.xssk`, enter
its password, and tick **Make the local signing identity persistent** so the
encrypted public key is stored in `~/Library/PixInsight/core-001-pxi.settings`
and reloaded at startup. Without it, installation fails with
`Unknown code signing identity '<developerId>'` even though the `.xsgn` is
valid (the `developerId` in the `.xsgn` must match the one in the `.xssk`'s
`<KeyPair>` element). Re-signing does not help; registering the identity does.

Install from PixInsight (Process → Modules → Install Modules…, directory
`bin/macosx/arm64` or `bin/windows/x64`). After the first install, PixInsight
re-loads the module from that directory on every start, so rebuilding +
re-signing is enough to update it (restart PixInsight).

## Command-line harness

`repatch-cli` runs the core on FITS files without PixInsight, which is how the
algorithm is debugged and regression-checked:

```bash
build/repatch-cli --synth 256 256 in.fits mask.fits     # synthesize a test pair
build/repatch-cli in.fits mask.fits out.fits --patch 11 --ring 40 --seed 1
```

On Windows the binary is `build/Release/repatch-cli.exe`, since the Visual
Studio generator is multi-configuration.

`--threads` is the lever for checking the determinism contract: the output must
be byte-identical whatever it is set to.

```bash
for t in 1 2 4 8 16; do build/repatch-cli in.fits mask.fits out-$t.fits --seed 42 --threads $t; done
shasum -a 256 out-*.fits   # all hashes must match
```

Smoke check after install: `scripts/pi_smoke.js`, run from PixInsight's
Process Console with the core `run` command (see the script header). Note that
`-a=` is an option of that console command, not of the PixInsight executable;
`PixInsight -r=script.js -a=...` is rejected with "Unknown string argument".

## Tool documentation

The *Browse Documentation* button of the dialog is PCL's default
`MetaProcess::BrowseDocumentation()`, which opens
`<PixInsight>/doc/tools/<ProcessId>/<ProcessId>.html` and shows "Tool
documentation not available" when that file is missing.

The page lives in the repository ready to install, at
`doc/tools/Repatch/Repatch.html` with its `images/` folder, so the tree
mirrors the PixInsight installation root and a release package is simply
`bin/` + `doc/`. It is **hand-written in the output format of PixInsight's
Documentation Compiler** (`PIToolDoc` class): same `<head>` (relative links to
`../../pidoc/css/*.css` and `../../pidoc/scripts/pidoc-utility.js`, which
every PixInsight installation with the reference documentation has), same
skeleton — `#brief`, `#categories`, `#keywords`, a static numbered `#toc`,
`div.pidoc_section > h3.pidoc_sectionTitle + p.pidoc_sectionToggleButton +
div#<Name>`, `div.pidoc_subsection > h4.pidoc_subsectionTitle`, `dl.pidoc_list`
for parameters, `ol/ul.pidoc_list` with `li.pidoc_spaced_list_item`,
`sup > a.pidoc_referenceTooltip` citations, `#__references__`,
`#__related_tools__`, `#copyright`, `#footer`. Copy a compiled page such as
`<PixInsight>/doc/tools/Crop/Crop.html` when you need a construct that is not
used yet (figures, tables, notes). No `.pidoc` source and no compile step
exist by design: the page is a build input, not a build output, so CI can
package it as is.

When editing, keep it valid: `xmllint --html --noout doc/tools/Repatch/Repatch.html`
must print nothing, every `href="#…"` must have a matching `id`, and the
content must stay in step with `docs/user-guide.md` and the interface (every
control of every section bar, including the brush-mode special cases —
Feather ignored, Sample ring 0 = `max(32, 3 × radius)`).

Local install for testing: `scripts/install_docs.sh` copies
`doc/tools/Repatch/` into `<PixInsight>/doc/tools/` (with `sudo`, since that
tree is owned by root) and verifies the copy. Reopen the Repatch dialog and
press *Browse Documentation*.

Note for scripts in general: the Documentation Compiler and `pi_smoke.js` are
PJSR scripts; in this environment neither `PixInsight -n -r=<script>` (new
instance) nor `PixInsight -x=<script>` (IPC to the running instance) executed
scripts at all, so there is no headless PJSR step anywhere in the build.

## Core API

`src/core/repatch/FillBackend.h` is the only header consumers need:

```cpp
repatch::FillRequest req;
req.image = { width, height, channels /*1 or 3*/, { plane0, plane1, plane2 } };
req.mask = holeBytes;             // width*height, nonzero = fill
req.params.patchSize = 11;        // see FillParams for all fields
repatch::PatchBackend backend;
repatch::FillResult r = backend.Fill( req, []( float fraction, const char* stage ) { return true; } );
```

- Planes are separate contiguous `float` buffers (PixInsight allocates channels
  separately, so the module passes them without copying).
- `Fill` never throws; errors and aborts come back in `FillResult`. On error
  or abort the caller's image is untouched (the core works on a copy and
  writes back only the synthesized region).
- `ThresholdMask` is exported so the CLI and the module threshold identically.
- `PatchBackend::EnableDebugCapture()` exposes the final level-0 NNF and masks
  (`Debug()`); the tests use it to prove that no source patch overlaps the hole
  and that the sampling constraints hold.

Internals (one responsibility per file): `Mask` (bbox, dilation, Euclidean
distance transform, valid-source map), `Pyramid` (binomial downsampling,
conservative mask downsampling, level count), `Stretch` (median/MAD
auto-stretch), `Diffusion` (red-black SOR Laplace fill), `PatchMatch` (`Level`,
`Nnf`, distance, propagation/random search pass, vote, upsampling),
`PatchBackend` (orchestration), `Parallel` (band-parallel `std::thread`
runner), `Random` (PCG32).

## Determinism and threading rules

The core promises byte-identical output for a given seed regardless of thread
count. Keep these invariants when changing it:

- Work is split into row bands of `kBandHeight = 16` rows whose layout depends
  on the row count and the pass parity only (`Parallel.h`).
- PatchMatch propagation never reads across a band boundary within a pass;
  odd passes shift the band grid by 8 rows so information still crosses.
- Every random stream is a `Pcg32` seeded by `HashSeed(seed, level, pass, band)`
  (or row), consumed sequentially inside its band/row.
- Vote, distance transforms, pyramid filters and diffusion (red-black
  ordering) are per-row independent with fixed summation order; reductions
  go through per-row buffers, never through shared accumulators.
- The core is compiled without `-ffast-math` for the tests. The module build
  uses PCL's `-ffast-math` for performance; the core's NaN/Inf guards use a
  bit-pattern check (`IsFiniteSample`) specifically so they aren't affected
  by `-ffinite-math-only`, but floating-point results can otherwise differ
  from the CLI build beyond just the last bit, since PatchMatch's argmin
  comparisons can be sensitive to small numerical differences.

The module honours PixInsight's global parallelism setting via
`Thread::NumberOfThreads()` and passes the count to `FillParams::threads`.

## The PCL adapter

`RepatchInstance::ExecuteOn`:
1. locks the view, rejects complex images and the (disabled) Neural mode;
2. for mask-image mode, `BuildHoleMask` looks up the mask view by id, converts
   it to float, crops it to the preview rectangle when the target is a preview
   and the mask has the main image's size, and thresholds with
   `repatch::ThresholdMask`. For brush mode the hole comes from the stroke
   machinery described in "## The brush (dynamic interface)"; the core is not
   aware of the source difference, only the `FillRequest` mask it receives;
3. 32-bit float images are processed in place; other sample types are
   converted to a temporary `pcl::Image` and back;
4. runs `PatchBackend` with a progress callback that drives a
   `StatusMonitor`, pumps events and honours the console abort button
   (abort → `ProcessAborted`, image untouched).

Parameters follow the Sandbox/CloneStamp pattern (`RepatchParameters.*`,
`LockParameter`, `AllocateParameter`, `ParameterLength`); even patch sizes are
rounded up in `ValidateParameter`. The interface is a plain `SectionBar`
layout; the `ViewList` lists main views only.

## The brush (dynamic interface)

`RepatchInterface` is a dynamic PCL interface (`Features() =
InterfaceFeature::DefaultDynamic`, `IsDynamicInterface() = true`). The flow
mirrors CloneStamp:

- The first left click on a view makes it the **session target**
  (`SelectTarget`: `View::AddToDynamicTargets()`, snapshot of the instance
  into `m_session`, and `randomSeed == 0` frozen to a concrete seed).
- Dragging collects a dense path (`AppendPathPoint`, one point per
  `radius/3` px); `DynamicPaint` draws the cursor circle and the swept band;
  `RequiresDynamicUpdate`/`UpdateImageRect` keep the overlay invalidations
  small.
- On release, `CommitStroke` copies the stroke's ROI (`CopyTile`), calls
  `RepatchInstance::FillStroke` (the same function the replay path uses),
  copies the result (`after` tile), pushes a `SessionStroke`, and
  `RegenerateImageRect`s the view. Undo/redo paste `before`/`after` tiles.
- **✔** (`Execute`) pastes every `before` tile back (the view holds the
  original again), builds an instance from `m_session` with the strokes table
  (`ExportStrokes`), sets `isInterfaceInstance`, and `LaunchOn`s the window.
  `RepatchInstance::ExecuteOn` sees the flag and only calls
  `RepatchInterface::RestoreView()`, which pastes the `after` tiles — nothing
  is recomputed, and the history record carries the full parameter set.
- From an icon or a script (`isInterfaceInstance == false`) `ExecuteOn`
  replays the strokes in order with `DeriveStrokeSeed(seed, k)`; because the
  live path used the same `FillStroke`, ROI rule (`StrokeRoi`) and seeds, the
  pixels are identical.

Core side: `Stroke.h` holds the profile (`BrushProfile`), rasterization
(`RasterizeStroke` → hole mask + weight map), the ROI rule, seeds and
`FillStroke`; `FillRequest::weight` is the per-pixel compositing weight used
by `PatchBackend` when writing back (`out = a·fill + (1−a)·orig`).

## Continuous integration and packaging

`.github/workflows/build.yml` runs on every push, pull request and tag on
GitHub's Apple Silicon image (`macos-26`; `macos-latest` currently maps to
it). PCL is not on GitHub, so the job fetches the pinned commit from GitLab
with `git fetch --depth 1 origin $PCL_SHA` (52 MB, seconds), then runs
`./build.sh` unchanged — PCL's static libraries, core, tests, CLI, module and
the verification steps — and `scripts/package.sh`. The whole `PCL/` directory
(sources + `lib/macosx/arm64/*.a`, object files trimmed) is cached under a
key made of the PCL commit, Xcode and SDK versions and the PCL build scripts,
so only the first run after one of those changes pays for the PCL build.
The uploaded artifact is `dist/Repatch-<version>-macosx-arm64-unsigned.tar.gz`.

**CI cannot sign.** A `.xsgn` is produced only by the PixInsight core
(`PixInsight --sign-module-file`) with the developer's `.xssk`; PixInsight
does not run on a hosted runner. CI therefore proves that every commit
builds and passes the tests, and gives you a package layout check, while a
release is made locally:

```bash
./build.sh                       # or download the CI artifact's dylib into bin/macosx/arm64/
scripts/dev_sign_module.sh       # writes bin/macosx/arm64/Repatch-pxm.xsgn
scripts/package.sh               # dist/Repatch-<version>-macosx-arm64.tar.gz + .sha1
```

**CI covers macOS only.** There is no Windows job yet; the Windows package is
built and signed on a developer machine with `build.ps1`,
`scripts/dev_sign_module.ps1` and `scripts/package.sh --platform=windows-x64`
(the platform is inferred from the host, so the flag is only needed when
cross-labelling). A Windows job is feasible — it would fetch the pinned PCL
commit from GitLab, which has no `PCL.vcxproj`, and so would go through
`scripts/pcl` exactly as a local build with a git PCL tree does.

`scripts/package.sh` lays the archive out like the PixInsight installation
root (`bin/Repatch-pxm.dylib` or `bin/Repatch-pxm.dll`,
`bin/Repatch-pxm.xsgn`, `doc/tools/Repatch/…`), which is what the PixInsight
updater unpacks for a `type="module"` package. The version comes from
`--version`, an exact `vX.Y.Z` tag, or `MODULE_VERSION_*` in
`src/module/RepatchModule.cpp`; the `.sha1` file holds the value for the
package's `sha1` attribute; the archive is byte-for-byte reproducible for
identical inputs.

Two portability details in that script, both of which bite silently: fixing
the archive's owner ids needs `--owner/--group` on GNU tar but `--uid/--gid`
on the bsdtar that macOS ships, and `mktemp -t prefix` is BSD-only. GNU tar
also gets `--sort=name`, which removes the directory walk order from the
result.

To bump PCL: change `PCL_SHA` in the workflow and the PCL version and commit
notes in this guide and in `CLAUDE.md`, keep the local `../PCL` checkouts on
the same commit, and rebuild. Raising PCL can raise `PCL_API_Version` and so
the oldest core a module loads into — check the table in "Which PixInsight
core a build will load into" and the `--core-versions` used for releases.

## Releases and the update repository

Users install and update the module through a PixInsight *update
repository*: a directory on a web server holding `updates.xri` (the
catalogue) and the packages. PixInsight fetches `<repository URL>updates.xri`,
compares release dates with `etc/update/installed.xri`, downloads the package
from `serverURL` + `fileName`, checks its SHA-1, and unpacks it into the
installation root with the updater's own privileges (so the root-owned `doc/`
tree is no problem). Unsigned repositories are rejected by default
(`Security/AllowUnsignedRepositories`), so `updates.xri` is signed with
`PixInsight --sign-xml-file`, which appends a `<Signature>` element.

Repatch's repository is served by GitHub Pages from the `gh-pages` branch:

```
https://awitwicki.github.io/Repatch/            ← what users enter in PixInsight
https://awitwicki.github.io/Repatch/updates.xri
https://awitwicki.github.io/Repatch/Repatch-0.1.0-macosx-arm64.tar.gz
https://awitwicki.github.io/Repatch/Repatch-0.1.0-windows-x64.tar.gz
```

That branch also carries a `.gitattributes` of its own, which keeps every file
byte-exact on checkout. PixInsight verifies the `<Signature>` over
`updates.xri` and the SHA-1 of each package, so a line-ending conversion would
invalidate both — and with `core.autocrlf=true` a checkout does exactly that
to the text files.

The packages live next to `updates.xri` rather than as GitHub Release
assets on purpose: release-asset URLs answer with an HTTP redirect to
`objects.githubusercontent.com`, and there is no evidence that the core's
downloader follows redirects. A GitHub Release is still made for people who
download by hand.

One-time setup: create the branch and enable Pages.

```bash
git worktree add ../Repatch-pages --detach            # from the Repatch checkout
cd ../Repatch-pages && git checkout --orphan gh-pages && git rm -rf -q . && touch .nojekyll
git add .nojekyll && git commit -m "Update repository" && git push -u origin gh-pages
```

Then on GitHub: **Settings → Pages → Build and deployment → Source: Deploy
from a branch, Branch: `gh-pages` / `(root)`**. The site is live at
`https://awitwicki.github.io/Repatch/` a minute later.

Release procedure (version bumped in `src/module/RepatchModule.cpp`, the
release date too):

On the macOS machine:

```bash
./build.sh
scripts/dev_sign_module.sh             # bin/macosx/arm64/Repatch-pxm.xsgn
scripts/package.sh                     # dist/Repatch-<v>-macosx-arm64.tar.gz + .sha1
```

On the Windows machine:

```powershell
.\build.ps1 -PclPath ..\PCL
scripts\dev_sign_module.ps1            # bin\windows\x64\Repatch-pxm.xsgn
bash scripts/package.sh                # dist/Repatch-<v>-windows-x64.tar.gz + .sha1
```

Then, with both packages in `dist/` on whichever machine has the `.xssk`:

```bash
scripts/make_repo.sh --core-versions=windows-x64=1.9.5:1.9.5 --notes=notes.html

git tag v<v> && git push origin v<v>
gh release create v<v> dist/Repatch-<v>-*.tar.gz dist/Repatch-<v>-*.tar.gz.sha1 \
   --title "Repatch <v>" --notes-file notes.md

cp dist/repo/* ../Repatch-pages/ && cd ../Repatch-pages && git add -A && git commit -m "Repatch <v>" && git push
```

`scripts/make_repo.sh` refuses unsigned packages, verifies each one's `.sha1`
against the file itself, writes a `<platform>` entry per package with the
previous module files in `<remove>`, validates the XML, and signs it (password
prompted, never stored). `--base-url` changes the hosting location;
`--no-sign` is for inspection only.

**`updates.xri` is one catalogue for every platform**, which has a consequence
worth stating plainly: every platform still being offered must be passed on
every run, because a package left out simply disappears from the repository.
Pass them explicitly, or let the default pick up every signed
`dist/Repatch-*.tar.gz`:

```bash
scripts/make_repo.sh \
  --package=dist/Repatch-0.1.0-macosx-arm64.tar.gz \
  --package=dist/Repatch-0.1.0-windows-x64.tar.gz \
  --core-versions=windows-x64=1.9.5:1.9.5 \
  --notes=notes.html
```

A package already published can be fed back in unchanged — fetch the exact
blob out of the branch (`git show origin/gh-pages:<file> > dist/<file>`) so its
`sha1` does not move and installations on that platform see no change.

`--core-versions` takes either a range for everything or `platform=range` for
one platform, repeatable. It exists because the platforms can sit on different
PCL versions and therefore have different core floors: at 0.1.0 the macOS
module was built against PCL 2.10.4 and is offered for `1.9.4:1.9.5`, while the
Windows module was built against 2.10.8, needs API `0x0188`, and is offered for
`1.9.5:1.9.5` only — `.xri` cannot say "1.9.4 build 1696" (see "Which
PixInsight core a build will load into"). The default release notes are
composed from the ranges actually emitted, so the prose cannot drift out of
step with the attributes.

Under Git Bash or WSL, run it from that shell as usual; it finds
`PixInsight.exe` and converts paths itself.

Trust: packages signed with a *local* signing identity install only on
machines where that identity has been registered (Edit → Local Signing
Identity…). Distributing to other users requires a Certified PixInsight
Developer identity from Pleiades Astrophoto; the procedure above does not
change, only the `.xssk`.

## Adding the Neural backend (Phase 2)

1. Implement `class NeuralBackend : public repatch::FillBackend` in
   `src/core` (or a separate library if it needs ONNX Runtime; keep the core
   PCL-free and keep the ONNX dependency out of the tests target).
2. In `RepatchInstance::ExecuteOn`, choose the backend from `p_mode` instead
   of throwing for `Neural`.
3. Enable `Neural_RadioButton` in the interface and add the model-specific
   parameters as new `Meta*` classes.
4. Link the runtime in the `Repatch-pxm` target only.

## Tests

`./build/repatch_tests <prefix>` runs the cases whose names start with the
prefix (`mask_`, `parallel_`, `random_`, `pyramid_`, `stretch_`, `diffusion_`,
`patchmatch_`, `backend_`, `fits_`, `stroke_`); `ctest` runs each group plus the
`no_pcl_deps` guard. Add a `TEST_CASE( group_what_it_checks )` to the matching
`tests/test_*.cpp`; new groups need an `add_test` line in `CMakeLists.txt`.

## Conventions

- `src/module`: PCL style — 3-space indent, `p_` instance members, `e_` event
  handlers, `The<Name>Parameter` globals, `Foo_Control` GUI members.
- `src/core`: plain modern C++20, no exceptions across the public API, no
  PCL includes (a ctest case greps for them).
- No third-party code is vendored.
