#pragma once

#include <string>

/**
    The passes, in the order a frame runs them. Every one is a full-raster
    triangle into a buffer of its own; nothing is blended.

    1. **rate** -- the clip's linear luminance x alpha (the scene's
       reflectance, premultiplied: a transparent pixel reflects nothing), one
       float a pixel.
    2. **reduce** -- exact block sums, 16 x 16 at a time, down to one texel:
       the frame's total, for the ABC and the BSP. The same shader, with a
       block of f and a scale of 1/f^2, makes the halo's coarse grid.
    3. **control** -- one texel, ping-ponged: the gate's duty D (the BSP), the
       plate's gain g (the ABC, a lag in log gain), the frame's mean
       photocathode illuminance.
    4. **halo** -- the back-scattered electrons' landing density gathered on
       the coarse grid: sum of cell weights x block means.
    5. **count** -- per pixel, lambda = D [ rate ( (1 - eta) Y + eta halo ) +
       EBI ], N ~ Poisson( lambda ) drawn exactly, and the plate's charge
       Q ~ Gamma( N, 1 ): a sum of N exponential gains. Out: N, Q, lambda.
    6. **phosphor** -- the screen's store and the frame's emitted light, by
       the exact integral of one exponential over the frame. Ping-ponged
       (three buffers, so a resize can resample the store into a fresh one).
    7. **composite** -- to the host: the emitted light through the spot's
       kernel, x the gain, x the honeycomb, in the phosphor's colour.

    No time reaches a shader: the frame's factors arrive as floats computed
    in double on the CPU, and the randomness is pcg4d of the pixel, a frame
    counter and the instance. tools/glslc.sh's ASSEMBLED table mirrors
    Assemble().
*/
namespace intensifier::shaders
{

extern const char* const kVersion;
extern const char* const kQuadVertex;
extern const char* const kCommon;
extern const char* const kRandom;
extern const char* const kRateFragment;
extern const char* const kReduceFragment;
extern const char* const kControlFragment;
extern const char* const kHaloFragment;
extern const char* const kSamplers;
extern const char* const kCountFragment;
extern const char* const kPhosphorFragment;
extern const char* const kResampleFragment;
extern const char* const kCompositeFragment;

enum class Pass
{
	Rate,
	Reduce,
	Control,
	Halo,
	Count,
	Phosphor,
	Resample,
	Composite
};
std::string AssembleVertex( Pass pass );
std::string Assemble( Pass pass );

// The count pass's draws: each pcg4d call hashes ( x, y, frame counter,
// seed x 256 + index ), so no two draws share an input. Indices: 0 the tiny
// rates, 1 inversion, 2..33 PTRS (two tries a call), 40..55 the plate's gain,
// 60 and 61 the negative controls' extra normals.

} // namespace intensifier::shaders
