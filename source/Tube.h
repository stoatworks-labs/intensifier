#pragma once

#include "Controls.h"

#include <vector>

/**
    The tube, on the CPU: every constant with its source, and the factors a
    frame's passes need. Nothing here touches GL; the passes are in
    Shaders.cpp and the plugin.

    The model (SPEC-intensifier.md has the sources in full):

    - The clip is the scene's reflectance: linear luminance Y x alpha, lit by
      Light Level. An f/1.2 objective of transmission 0.9 (assumed) puts
      E = Y E_scene tau / (4 N^2) on the photocathode.
    - The frame's height spans the photocathode's 18 mm, so a pixel is
      18 mm / H square and expects lambda = S E (18 mm / H)^2 dt / e
      photoelectrons in a frame of dt seconds, plus the EBI's.
    - The bright-source protection gates the photocathode to a duty D that
      holds its current at I_lim; the ABC holds the screen's mean luminance at
      L_max by a lag in log gain. Both sense the frame's mean (on the GPU).
    - 7.4% of photoelectrons back-scatter off the plate's web and land up to
      twice the gap away: a gathered halo, so the count stays Poisson.
    - The plate multiplies each event by an exponential gain of mean 1; the
      phosphor is one exponential, integrated exactly over each frame.
*/
namespace intensifier
{

constexpr double kPi = 3.14159265358979323846;

namespace tube
{
/// The elementary charge, C: exact in the 2019 SI.
constexpr double kElementaryCharge = 1.602176634e-19;
/// The photocathode's diameter, m, which the frame's height spans. The usual
/// 18 mm tube (Hamamatsu's Phi18 types; Photek DS001's 18 mm tube).
constexpr double kPhotocathodeDiameter = 18e-3;
/// The photocathode's active area, m^2: Photek DS001 table 2, 18 mm, 2.54 cm^2.
constexpr double kTubeArea = 2.54e-4;
/// The objective: f/1.2, transmission 0.9. ASSUMED (typical of night-vision
/// objectives; no source checked).
constexpr double kFNumber              = 1.2;
constexpr double kObjectiveTransmission = 0.9;
/// Luminous sensitivity, A/lm, typical: Hamamatsu TII0007E (GaAs V7090/V9569
/// 1500 uA/lm; multialkali V6886U 280 uA/lm). Indexed by Photocathode.
constexpr double kSensitivity[ 2 ] = { 280e-6, 1500e-6 };
/// Equivalent background input, lux, typical: Hamamatsu TII0007E
/// (multialkali 1.0e-11 lm/cm2, GaAs 2.0e-11 lm/cm2; 1 lm/cm2 = 1e4 lux).
constexpr double kEbi[ 2 ] = { 1.0e-7, 2.0e-7 };
/// Back-scatter off the plate's input face: ~20% of the electrons that
/// impinge on it (US 9,177,764); the plate's open area 63% (US 8,135,253).
/// READ as 20% of the 37% that strike the web: a decision, said in the spec.
constexpr double kBackscatter = 0.20;
constexpr double kOpenArea    = 0.63;
constexpr double kHaloFraction = kBackscatter * ( 1.0 - kOpenArea );
/// The phosphors' decay, s per decade: P43 1.2 ms/decade, a true exponential
/// (Photek DS001 table 1); P45 1.5 ms to 10% (the AST phosphor table).
constexpr double kDecayPerDecade[ 2 ] = { 1.2e-3, 1.5e-3 };
/// Limiting resolution, line pairs per m: 64 lp/mm typ, one plate (Hamamatsu
/// TII0007E), read as a Gaussian MTF falling to 5% there (Photek's
/// definition of limiting resolution).
constexpr double kLimitingResolution = 64e3;
constexpr double kContrastAtLimit    = 0.05;
/// The ABC's held mean screen luminance, cd/m^2, and its lag, s. CHOSEN: no
/// checked figure for either was reached.
constexpr double kMaxLuminance = 3.0;
constexpr double kAbcTime      = 0.2;
/// The gated photocathode's current limit, A. CHOSEN: -800 V across 10 GOhm,
/// the middle of US 5,949,063's resistor range. The duty's floor, 1e-7, is
/// US 4,882,481's range.
constexpr double kCathodeCurrentLimit = 80e-9;
constexpr double kMinDuty             = 1e-7;
/// The honeycomb: hexagonal multifibres 0.45 mm flat to flat (DERIVED from
/// "several thousand" fibres at a 6-8 um pitch); boundaries one fibre pitch
/// (8 um) wide passing 30% less light at Fixed Pattern 1, cells differing by
/// up to +-4% (both CHOSEN: "faint" is the only word a source gives).
constexpr double kMultifibre     = 0.45e-3;
constexpr double kBoundaryWidth  = 8e-6;
constexpr double kBoundaryDepth  = 0.30;
constexpr double kCellSpread     = 0.04;
/// CIE 1931 x, y: US 4,694,217 (P43 0.334, 0.561; Y2O2S:Tb 0.269, 0.311).
constexpr double kPhosphorXY[ 2 ][ 2 ] = { { 0.334, 0.561 }, { 0.269, 0.311 } };
/// The halo is gathered on a grid coarse enough that its radius is at most
/// this many cells: 13 x 13 taps.
constexpr int kMaxHaloCells = 6;
/// The counting interval before the host's clock has given one.
constexpr double kDefaultFrame = 1.0 / 60.0;
} // namespace tube

/// What the controls ask for, in physical units.
struct Settings
{
	double sceneLux    = 0.01;
	int photocathode   = kGen3;
	double gainFLfc    = 40000.0;
	int phosphor       = kP43;
	double gap         = 175e-6;
	double fixedPattern = 0.5;
};

/// The harness's negative controls that live on the CPU side.
struct TubeHooks
{
	bool phosphorPerFrame = false;///< the phosphor's factors for 1/60 s whatever the frame
	double tauScale       = 1.0;  ///< the decay time x this
	bool abcPerFrame      = false;///< the ABC's lag stepped per frame, not per second
	double haloScale      = 1.0;  ///< the halo's radius x this (0.5: radius = gap)
	double cellScale      = 1.0;  ///< the honeycomb drawn this much larger
	double spotScale      = 1.0;  ///< the display's spot this much wider
};

/// Everything one frame's passes need.
struct Frame
{
	double pitch           = 0.0;///< m of photocathode one pixel spans
	double electronsPerLux = 0.0;///< photoelectrons a pixel expects in the frame per lux on the photocathode
	double luxPerY         = 0.0;///< photocathode lux per unit of the clip's luminance x alpha
	double rate            = 0.0;///< photoelectrons per pixel per frame per unit of luminance x alpha
	double ebiLux          = 0.0;
	double ebiRate         = 0.0;///< photoelectrons per pixel per frame from the EBI
	double sensitivity     = 0.0;///< A/lm
	double gain            = 0.0;///< the tube's luminous gain, (cd/m^2)/lx
	double displayScale    = 0.0;///< linear display value per photoelectron's worth of light, at full gain
	double decayA = 0.0, oneMinusA = 0.0, decayB = 0.0, oneMinusB = 0.0;
	double abcDecay        = 0.0;
	double haloRadiusPx    = 0.0;///< twice the gap, in pixels
	int haloFactor         = 1;  ///< pixels per halo cell
	double cellPx          = 0.0;///< the multifibre, flat to flat, px
	double boundaryHalfPx  = 0.0;
	double sigmaPx         = 0.0;///< the spot's Gaussian sigma, px
	double tau             = 0.0;///< the phosphor's decay time, s
};

/// The frame's factors. `dt` is the counting interval, s (never 0).
Frame Compute( const Settings& settings, int width, int height, double dt, const TubeHooks& hooks = TubeHooks {} );

/// The halo's cell weights: the exact integral of the landing density over
/// each cell of a ( 2 radius + 1 )^2 window, the target at the centre cell's
/// centre. `rawSum` is their sum before normalising (1 up to the quadrature).
struct HaloKernel
{
	double radiusPx = -1.0;
	int factor      = 0;
	int radius      = 0;
	double rawSum   = 0.0;
	std::vector< float > weights;
};
HaloKernel BuildHaloKernel( double radiusPx, int factor );

/// The landing radius's CDF in u = r / R: 1 - sqrt( 1 - u^2 ) (Lambertian
/// back-scatter, elastic, one bounce).
double HaloCdf( double u );

/// Five pixel-integrated taps of the spot's Gaussian, normalised.
void SpotWeights( double sigmaPx, float out[ 5 ] );

/// The phosphor's colour in linear sRGB, normalised to Rec. 709 luminance 1.
void PhosphorColour( int phosphor, double rgb[ 3 ] );

/// The decay time, s, of a phosphor.
double DecayTime( int phosphor );

} // namespace intensifier
