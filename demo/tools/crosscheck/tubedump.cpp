// The plugin's CPU numbers for one raster and one set of host values, as JSON,
// for crosscheck.py to hold the page's port of Controls.cpp and Tube.cpp to.
//
//   tubedump W H DT LIGHT PHOTOCATHODE GAIN PHOSPHOR GAP FIXED
//
// The six values are the host's 0..1 floats (an option its element index),
// parsed with strtof as ittest's --set parses them, or - for the default. Built by crosscheck.py from
// source/Tube.cpp and source/Controls.cpp alone, with no GL: nothing here is
// the page's.
#include "Controls.h"
#include "Tube.h"

#include <cstdio>
#include <cstdlib>

using namespace intensifier;

int main( int argc, char** argv )
{
	if( argc != 10 )
	{
		std::fprintf( stderr, "tubedump W H DT LIGHT PHOTOCATHODE GAIN PHOSPHOR GAP FIXED\n" );
		return 2;
	}
	const int width  = std::atoi( argv[ 1 ] );
	const int height = std::atoi( argv[ 2 ] );
	const double dt  = std::strtod( argv[ 3 ], nullptr );
	// "-" leaves a control at the plugin's constructor default.
	float p[ 6 ];
	for( int i = 0; i < 6; ++i )
		p[ i ] = argv[ 4 + i ][ 0 ] == '-' && argv[ 4 + i ][ 1 ] == '\0' ? InfoOf( static_cast< unsigned int >( i ) ).defaultValue
		                                                              : std::strtof( argv[ 4 + i ], nullptr );

	// IntensifierPlugin::CurrentSettings.
	Settings s;
	s.sceneLux     = LightFromParam( p[ 0 ] );
	s.photocathode = OptionIndex( p[ 1 ], 2 );
	s.gainFLfc     = GainFromParam( p[ 2 ] );
	s.phosphor     = OptionIndex( p[ 3 ], 2 );
	s.gap          = GapFromParam( p[ 4 ] );
	s.fixedPattern = FixedFromParam( p[ 5 ] );

	const Frame f      = Compute( s, width, height, dt );
	const HaloKernel k = BuildHaloKernel( f.haloRadiusPx, f.haloFactor );
	float spot[ 5 ];
	SpotWeights( f.sigmaPx, spot );
	double rgb[ 3 ];
	PhosphorColour( s.phosphor, rgb );

	std::printf( "{\"settings\":{\"sceneLux\":%.17g,\"photocathode\":%d,\"gainFLfc\":%.17g,\"phosphor\":%d,\"gap\":%.17g,"
	             "\"fixedPattern\":%.17g},\n",
	             s.sceneLux, s.photocathode, s.gainFLfc, s.phosphor, s.gap, s.fixedPattern );
	std::printf( "\"frame\":{\"pitch\":%.17g,\"electronsPerLux\":%.17g,\"luxPerY\":%.17g,\"rate\":%.17g,\"ebiLux\":%.17g,"
	             "\"ebiRate\":%.17g,\"sensitivity\":%.17g,\"gain\":%.17g,\"displayScale\":%.17g,\"decayA\":%.17g,"
	             "\"oneMinusA\":%.17g,\"decayB\":%.17g,\"oneMinusB\":%.17g,\"abcDecay\":%.17g,\"haloRadiusPx\":%.17g,"
	             "\"haloFactor\":%d,\"cellPx\":%.17g,\"boundaryHalfPx\":%.17g,\"sigmaPx\":%.17g,\"tau\":%.17g},\n",
	             f.pitch, f.electronsPerLux, f.luxPerY, f.rate, f.ebiLux, f.ebiRate, f.sensitivity, f.gain, f.displayScale,
	             f.decayA, f.oneMinusA, f.decayB, f.oneMinusB, f.abcDecay, f.haloRadiusPx, f.haloFactor, f.cellPx,
	             f.boundaryHalfPx, f.sigmaPx, f.tau );
	std::printf( "\"kernel\":{\"radius\":%d,\"rawSum\":%.17g,\"weights\":[", k.radius, k.rawSum );
	for( size_t i = 0; i < k.weights.size(); ++i )
		std::printf( "%s%.9g", i ? "," : "", static_cast< double >( k.weights[ i ] ) );
	std::printf( "]},\n\"spot\":[" );
	for( int i = 0; i < 5; ++i )
		std::printf( "%s%.9g", i ? "," : "", static_cast< double >( spot[ i ] ) );
	std::printf( "],\n\"phosphor\":[%.17g,%.17g,%.17g],\"decayTime\":%.17g,\"haloFraction\":%.17g}\n", rgb[ 0 ], rgb[ 1 ],
	             rgb[ 2 ], DecayTime( s.phosphor ), tube::kHaloFraction );
	return 0;
}
