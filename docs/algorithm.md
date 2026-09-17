# Repatch algorithm notes

This is what `repatch::PatchBackend::Fill` does, in the order it does it.
Symbols: `r = patchSize / 2`, `H` the hole, `S` the synthesis region, `T` the
target patch centres, `V` the valid source centres.

## 1. Regions

- `H` — pixels whose mask value is above the threshold.
- `S = { p : dist(p, H) ≤ feather }` (Euclidean). These are the only pixels
  the output changes.
- `T = S ⊕ square(r)` — every centre whose patch overlaps `S` needs a match.
- `V` — centres whose patch lies fully inside the image and does not touch
  `H`; if `sampleRing > 0`, additionally `dist(q, H) ≤ max(sampleRing, r+1)`.
- If `searchRadius > 0`, a source `q` for target `p` must satisfy
  `|q − p|∞ ≤ searchRadius`. Feasibility is checked up front (every target
  must reach some valid source), otherwise the fill fails with a message.

When sampling is bounded (`sampleRing` or `searchRadius` > 0) the whole
computation runs on `bbox(H)` grown by `feather + 2r + max(ring, radius) + 1`
— nothing outside can influence the result, and it keeps large images fast.

## 2. Pyramid

Levels: `pyramidLevels` if given, else `clamp(1 + floor(log2(min(bboxW, bboxH) /
patchSize)), 1, 8)`; then reduced until the coarsest level is at least
`2·patchSize` on both sides. Images are downsampled with a 3×3 binomial kernel
and decimated; a coarse mask pixel is a hole if *any* fine pixel in its 3×3
support is (so coarse "known" pixels are never contaminated by hole values).
The patch size stays constant across levels; ring and radius scale by 2⁻ᵏ.

## 3. Matching image

With `matchSpace = Stretched`, per-channel parameters are computed once from
the known pixels of the full image: shadows clip `c0 = max(0, median −
2.8·1.4826·MAD)` and a midtones balance `m` such that the median maps to 0.25
(`m = MTF(0.25, (median − c0)/(1 − c0))`). Patches are compared on `M =
MTF(m, (I − c0)/(1 − c0))`; votes always average the linear image `I`, and
`M` is refreshed pointwise inside the hole after every vote. `Linear` uses
`M = I`.

## 4. Coarsest level

The hole is initialised by solving the Laplace equation with the known pixels
as boundary (red-black SOR, ω = 1.8, until the max update < 1e-6 of the known
range or 5000 sweeps). The NNF gets an independent uniformly random valid
source per target.

## 5. Per level, `iterations` times

**PatchMatch pass** (Barnes et al. 2009): distances are refreshed (the vote
changed the targets), then in scan order — forward on even passes, backward on
odd — each target tries (a) the offsets proposed by its two already-visited
neighbours (a known neighbour proposes the identity), and (b) random
candidates around its current best with a window that halves from the search
radius (or the image size) down to 1 pixel. Candidates outside `V` or the
radius are rejected in O(1); distances are SSDs over all channels of `M`,
with early termination.

**Vote** (Wexler et al. 2007): every hole pixel becomes the plain mean of the
pixels proposed for it by all target patches that cover it. Because source
patches never overlap `H`, the vote reads only known or already-voted-elsewhere
pixels and can run in place.

## 6. Between levels

The NNF is upsampled (coordinates and offsets ×2, snapped to `V` with a random
valid fallback) and a vote initialises the finer hole before its first pass.

## 7. Compositing

At level 0, if `feather > 0`, a final vote over `S` is blended into the image
with weight `w = 1 − dist(p, H)/(feather + 1)` outside `H` (1 inside).
Only `S` is written back to the caller's buffer.

## Determinism

Propagation runs in fixed 16-row bands that never read across their edges
within a pass (odd passes shift the bands by 8 rows); each band draws from its
own PCG32 stream seeded by `hash(seed, level, pass, band)`. Everything else is
per-row independent with a fixed reduction order. Hence the same seed gives
the same bytes on 1 or 64 threads.

## References

- C. Barnes, E. Shechtman, A. Finkelstein, D. B. Goldman, *PatchMatch: A
  Randomized Correspondence Algorithm for Structural Image Editing*, SIGGRAPH
  2009.
- Y. Wexler, E. Shechtman, M. Irani, *Space-Time Completion of Video*, PAMI
  2007 (the multi-scale vote/reconstruction scheme).
- P. Felzenszwalb, D. Huttenlocher, *Distance Transforms of Sampled
  Functions*, 2012 (the Euclidean distance transform).
- github.com/Karbo123/content-aware-fill — a compact C++ reference of the same
  pipeline.
