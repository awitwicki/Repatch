#pragma once
#include "Image.h"
#include "Mask.h"

namespace repatch {

// Replaces hole pixels of every channel with the harmonic interpolation of
// the surrounding known pixels (Laplace equation, Dirichlet boundary),
// using red-black successive over-relaxation. Deterministic for any thread
// count. Returns the number of sweeps performed (0 if the hole is empty).
int DiffusionFill( Image& img, const Mask& hole, int threads,
                   int maxSweeps = 5000, float relTol = 1e-6f );

} // namespace repatch
