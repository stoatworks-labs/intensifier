#include "Controls.h"

#include <algorithm>
#include <cmath>

namespace intensifier
{
namespace
{
double clamp01( float v )
{
	return std::clamp( static_cast< double >( v ), 0.0, 1.0 );
}
double geometric( float v, double low, double high )
{
	return low * std::pow( high / low, clamp01( v ) );
}
float inverseGeometric( double value, double low, double high )
{
	const double lo = std::min( low, high ), hi = std::max( low, high );
	return static_cast< float >( std::log( std::clamp( value, lo, hi ) / low ) / std::log( high / low ) );
}

const char* const kPhotocathodeNames[] = { "Gen 2", "Gen 3" };
const char* const kPhosphorNames[]     = { "P43", "P45" };

ControlInfo standard( unsigned int id, const char* name, const char* group, float value )
{
	return { id, name, group, Kind::Standard, value, nullptr, 0 };
}
ControlInfo option( unsigned int id, const char* name, const char* group, float value, const char* const* names, int count )
{
	return { id, name, group, Kind::Option, value, names, count };
}

//---------------------------------------------------------------------------
// The defaults. Gen 3 at Hamamatsu's typical luminous gain (4.0e4
// (lm/m2)/lx = 40,000 fL/fc), a P43 screen, a 175 um gap (a halo of 0.7 mm,
// the figure quoted for a 4G tube) -- and a Light Level and Fixed Pattern
// set from Resolume's 33 demo clips through --pipe (tools/clips.py
// defaults): at 0.1 lux the median clip's ABC just engages, and at Fixed
// Pattern 0.75 the honeycomb matches one frame's photon noise at 100x that.
//---------------------------------------------------------------------------
const ControlInfo kTable[] = {
	standard( PT_LIGHT, "Light Level", "Scene", ParamFromLight( 0.1 ) ),
	option( PT_PHOTOCATHODE, "Photocathode", "Tube", static_cast< float >( kGen3 ), kPhotocathodeNames, 2 ),
	standard( PT_GAIN, "Gain", "Tube", ParamFromGain( 40000.0 ) ),
	option( PT_PHOSPHOR, "Phosphor", "Tube", static_cast< float >( kP43 ), kPhosphorNames, 2 ),
	standard( PT_GAP, "Gap", "Tube", ParamFromGap( 175e-6 ) ),
	standard( PT_FIXED, "Fixed Pattern", "Tube", 0.75f ),
	standard( PT_MIX, "Mix", "Look", 1.0f ),
};
static_assert( sizeof( kTable ) / sizeof( kTable[ 0 ] ) == PT_ABOUT_TEXT, "every control needs a row in the table" );
} // namespace

const ControlInfo& InfoOf( unsigned int id )
{
	return kTable[ std::min< unsigned int >( id, PT_ABOUT_TEXT - 1 ) ];
}

int OptionIndex( float value, int count )
{
	const long i = std::lround( static_cast< double >( value ) );
	return static_cast< int >( std::clamp< long >( i, 0, count - 1 ) );
}

double LightFromParam( float v )
{
	return geometric( v, kLightLow, kLightHigh );
}
float ParamFromLight( double lux )
{
	return inverseGeometric( lux, kLightLow, kLightHigh );
}
double GainFromParam( float v )
{
	return geometric( v, kGainLow, kGainHigh );
}
float ParamFromGain( double fLfc )
{
	return inverseGeometric( fLfc, kGainLow, kGainHigh );
}
double GapFromParam( float v )
{
	return geometric( v, kGapLow, kGapHigh );
}
float ParamFromGap( double metres )
{
	return inverseGeometric( metres, kGapLow, kGapHigh );
}
double FixedFromParam( float v )
{
	return clamp01( v );
}

} // namespace intensifier
