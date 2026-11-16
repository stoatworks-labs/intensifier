#pragma once

#include "StoatworksAboutLinks.h"

/**
    The host's parameters, and what they mean in physical units.

    Every ranged parameter the host sees is 0..1, because `SetParamInfo`
    clamps an `FF_TYPE_STANDARD` default into 0..1 before `SetParamRange`
    could widen it. The conversions live in Controls.cpp, one function per
    control, each with its inverse where a default or the harness needs it.
    Option parameters hold the element index (an option reads back 0..1
    whatever its count: map by index).

    Units: lux for Light Level (the scene's illuminance), the tube's luminous
    gain in fL/fc for Gain, metres for Gap (shown in micrometres), a 0..1
    depth for Fixed Pattern.
*/
namespace intensifier
{

/// Every control, by what it is. The host sees them in this order, with the
/// About block last.
enum ParamId : unsigned int
{
	// -- Scene -------------------------------------------------------------------
	PT_LIGHT = 0,

	// -- Tube --------------------------------------------------------------------
	PT_PHOTOCATHODE,
	PT_GAIN,
	PT_PHOSPHOR,
	PT_GAP,
	PT_FIXED,

	// -- Look --------------------------------------------------------------------
	PT_MIX,

	// -- The Stoatworks About block: a text line, then one button per link.
	PT_ABOUT_TEXT,
	PT_COUNT = PT_ABOUT_TEXT + 1 + stoatworks::about::kButtonCount
};

enum class Kind
{
	Standard,
	Option
};

/// One control as the host is told about it.
struct ControlInfo
{
	unsigned int id;
	const char* name;///< at most 16 characters: FFGL's name field is not terminated
	const char* group;
	Kind kind;
	float defaultValue;///< the host's 0..1, or the element index
	const char* const* options;
	int optionCount;
};

/// The table, indexed by ParamId, for every id below PT_ABOUT_TEXT.
const ControlInfo& InfoOf( unsigned int id );

/// An option's index from the value the host stored.
int OptionIndex( float value, int count );

/// The photocathodes, by the Photocathode option's index.
enum Photocathode
{
	kGen2 = 0,///< multialkali (S-25 type)
	kGen3 = 1 ///< GaAs
};

/// The phosphors, by the Phosphor option's index.
enum Phosphor
{
	kP43 = 0,///< Gd2O2S:Tb, green
	kP45 = 1 ///< Y2O2S:Tb, white
};

//---------------------------------------------------------------------------
// Conversions, 0..1 to physical and back.
//---------------------------------------------------------------------------
double LightFromParam( float v );///< lux on the scene, 1e-5..100, geometric
float ParamFromLight( double lux );
double GainFromParam( float v );///< luminous gain, fL/fc, 1000..100000, geometric
float ParamFromGain( double fLfc );
double GapFromParam( float v );///< cathode-to-plate gap, m, 50..500 um, geometric
float ParamFromGap( double metres );
double FixedFromParam( float v );///< the honeycomb's depth, 0..1, linear

/// The ranges, for the harness and the docs.
constexpr double kLightLow = 1e-5, kLightHigh = 100.0;
constexpr double kGainLow = 1000.0, kGainHigh = 100000.0;
constexpr double kGapLow = 50e-6, kGapHigh = 500e-6;

} // namespace intensifier
