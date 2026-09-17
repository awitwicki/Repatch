# Repatch user guide

## What it does

Repatch replaces the pixels of a *hole* (the region where your mask is above
the threshold) with content synthesized from the rest of the image. It works
coarse-to-fine: at a very low resolution the hole is filled by smooth
diffusion, then at each finer level every patch that overlaps the hole is
matched to the most similar patch elsewhere in the image (PatchMatch), and the
hole pixels are rebuilt by averaging what those matches propose. The result
carries the surrounding noise, gradient and texture into the hole.

It is not magic: it can only reuse what already exists in the image. It is
ideal for removing satellite trails, hot columns, blooming spikes, small
artifacts, or unwanted objects on background; it cannot invent structure that
the surroundings do not contain.

## Workflow with a mask image

Choose *Mask source → Mask image* for this workflow.

1. **Make a mask image.** Same dimensions as the target (or as its main image
   if you will run on a preview). Any pixel with value *strictly greater* than
   the threshold (default 0.5) is filled. Grow the mask a little beyond the
   defect — one or two pixels of margin avoid halos. Tools: PixelMath,
   StarMask, CloneStamp painting white on a black image, or a binarized copy.
2. **Use a preview.** Define a preview around the defect and apply Repatch to
   it. Only the preview is processed, so iterations are fast. When the result
   looks right, apply the same instance to the main image (the mask is used at
   full size).
3. **Commit.** Press **✔** with the view active. The history explorer
   records the instance; Undo restores.
4. **Check the console.** It prints the hole size, the pyramid levels, the
   random seed and the time. With Random seed = 0 a new seed is drawn each run
   and reported — enter it to reproduce a result you like.

## Painting with the brush

The default *Mask source* is **Brush**. With the Repatch window open:

1. **Pick the target** — your first press-and-drag both picks the image you
   want to fix as the session target *and* paints the first stroke (release
   without dragging to paint a single dot). Patch, Sampling and Output
   parameters freeze at that moment, and the status line under the brush
   controls changes from "No target view selected" to "Target: … (N
   stroke(s))". One session works on one view; commit (**✔**) before
   painting elsewhere, or press **✘** — the only way to release a session's
   target without committing anything.
2. **Paint** — press the left button and drag over the defect. The circle
   under the cursor is the brush; the orange band is the region the stroke
   will cover. Release the button and the region is filled right away using
   the surrounding pixels. Each new stroke sees the already-filled image, so
   you can work outwards from a clean area.
3. **Adjust** — *Radius* (`[` / `]`, with Shift ±10), *Softness* (0 = hard
   edge, 1 = the fill fades over the whole radius) and *Opacity* (1 = replace,
   0.5 = half fill, half original) apply to the next stroke. *Undo stroke*
   (`Ctrl+Z`), *Redo* (`Ctrl+Y`) and *Clear* restore pixels exactly; nothing
   is recomputed.
4. **Commit** — press **✔**. The whole session becomes one `Repatch` entry in
   the view's history, with the strokes and the seed as parameters, so the
   history icon can be dragged onto another copy of the image and reproduces
   the same result. **✘** (or closing the window) restores the original pixels.

The Patch, Sampling and Output parameters are captured when the first stroke
of a session is made and are greyed out until the session ends — set them
first. In Brush mode a *Sample ring* of 0 means "automatic": sources come from
a ring of `max(32, 3 × radius)` pixels around each stroke, which keeps large
images responsive; enter a value to override it. *Feather* does not apply to
brush strokes at all — each stroke's own Opacity and Softness already control
its edge and how much of the hole is replaced (see Parameters below).

## Parameters

### Mode

- **Patch** — the PatchMatch algorithm described here. The only mode in this
  release.
- **Neural** — reserved for a future ONNX-based inpainting mode. Disabled.

### Mask source

- **Mask source** — **Brush** (default) paints the region to fill directly on
  the target view; see "Painting with the brush" above. **Mask image** takes
  the region to fill from a separate mask view; see "Workflow with a mask
  image" above and the Mask section below. Switching sources while a brush
  session has strokes discards them, after confirmation.

### Brush

Only used when *Mask source* is **Brush**; see "Painting with the brush"
above for the workflow.

- **Radius** (px, 1–500, default 15) — brush radius. Keyboard `[` / `]`
  change it by 1, with Shift by 10.
- **Softness** (0–1, default 0.5) — 0 replaces the whole disc; higher values
  fade the fill into the original over the outer part of the radius (1 =
  over the whole radius).
- **Opacity** (0–1, default 1) — how much of the synthesized content is
  mixed into the hole itself for a stroke: 1 replaces, 0.5 blends half and
  half. This is the brush-mode equivalent of "how much of the hole is
  replaced" — see the Feather entry below for how this differs from Mask
  image mode.
- **strokes** — the table of committed strokes (per-point x/y, plus each
  stroke's radius, softness and opacity), written by painting and read back
  by *Execute* to replay the session exactly; not meant to be edited by
  hand, but a PJSR script can set it directly to generate brush instances.

### Mask

- **Mask image** — the view that defines the hole. Main views only. If the
  target is a preview and the mask has the main image's size, the mask is
  cropped to the preview automatically. A colour mask uses its first channel.
- **Threshold** (0–1, default 0.5) — mask values above this are filled.

### Patch

- **Patch size** (5–41, odd, default 11) — the side of the square patches
  being compared and copied. Larger patches preserve larger structures
  (gradients, big stars) but are slower and can look repetitive; smaller
  patches follow fine texture and noise. Rule of thumb: at least the size of
  the largest feature you want reproduced, and not larger than half the hole.
- **Pyramid levels** (1–8) and **Auto** — how many resolution levels to use.
  Auto picks `1 + log2(hole size / patch size)`, so that at the coarsest level
  the hole is about one patch wide. Set it manually to 1 for tiny holes or
  when you want purely local texture; more levels give more coherent large
  structure. Either way the number is an upper bound: levels whose sample
  ring or search radius leaves no room for a whole source patch at that
  scale are dropped, and the console reports how many were actually used.
- **Iterations** (1–50, default 5) — PatchMatch passes per level. Quality
  saturates quickly; 4–6 is plenty.

### Sampling

- **Search radius** (px, 0 = whole image) — limits how far from a hole pixel
  its source patches may come from. Useful on big images to keep the search
  local and fast.
- **Sample ring** (px, 0 = whole image) — restricts *source* patches to a
  ring of this width around the hole. This is the setting that makes Repatch
  behave for astro backgrounds: with a ring of 30–80 px the fill is built only
  from the immediate surroundings, so the gradient and noise level match.
  If the ring contains stars you do not want copied, widen the mask or use a
  star-free ring. In Brush mode, 0 means something different — see
  "Painting with the brush" above.

### Output

- **Match space** — *Stretched* (default) compares patches on an
  automatically stretched copy (median/MAD screen stretch), which is what you
  want for linear data: faint background structure participates in the match
  instead of being dominated by a few bright pixels. The filled pixels are
  always copied from the original linear data, so the output stays linear.
  *Linear* compares raw values; use it for already stretched or non-linear
  images.
- **Feather** (px, default 3) — width of a soft blend just *outside* the
  hole. It hides the seam between synthesized and original pixels. 0 disables
  it. This applies to Mask image mode only, where the hole itself is always
  fully replaced (feather only affects the ring outside it). Feather does
  not apply to brush strokes at all; in Brush mode a stroke's own Opacity
  controls how much of the hole itself is replaced (see the Brush section
  above).
- **Random seed** — 0 draws a fresh seed each run; any other value gives a
  reproducible result, identical for any number of threads.

## Tips for astronomical images

- Work on linear data with *Stretched* matching; run before stretching.
- Mask generously: include the halo of a trail or the diffraction spikes of a
  removed star, not just the bright core.
- Use a sample ring (30–80 px) on backgrounds with gradients; use the whole
  image for repetitive textures (e.g. a flat nebula field) where more
  candidates help.
- For very large holes (hundreds of pixels), increase the patch size to 15–21
  and let Auto choose the pyramid levels.
- Repatch runs on RGB images as one 3-channel match; for narrowband data with
  very different channel statistics, consider filling each channel separately.

## Troubleshooting

- **"no valid source region"** — at full resolution no patch fits without
  touching the hole: the sample ring is too narrow, the patch is too large,
  or the hole covers almost the whole (preview) image. Increase the ring,
  reduce the patch size, or use a larger preview.
- **"searchRadius too small"** — some hole pixels have no source within the
  radius; increase it or set it to 0.
- **"Mask dimensions do not match"** — the mask must match the target, or the
  main image when the target is a preview.
- **Visible seam** — in Mask image mode, increase Feather or enlarge the mask
  by a pixel or two; in Brush mode, increase Softness (Feather does not
  apply to brush strokes).
- **Repetitive or blocky fill** — reduce the patch size, add pyramid levels,
  or widen the sample ring so more candidates are available.
- **Slow** — use a preview, set a search radius or sample ring, or reduce the
  iterations. The cost scales with the number of hole pixels, not the image.
