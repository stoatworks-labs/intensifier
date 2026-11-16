#include "Tube.h"

#include <algorithm>
#include <cmath>

namespace intensifier
{

double DecayTime( int phosphor )
{
	//Per decade -> e-folding: tau = t_decade / ln 10.
	return tube::kDecayPerDecade[ phosphor == kP45 ? 1 : 0 ] / std::log( 10.0 );
}

Frame Compute( const Settings& s, int width, int height, double dt, const TubeHooks& hooks )
{
	using namespace tube;
	Frame f;
	const int gen     = s.photocathode == kGen2 ? 0 : 1;
	const double h    = std::max( height, 1 );
	f.pitch           = kPhotocathodeDiameter / h;
	f.sensitivity     = kSensitivity[ gen ];
	f.electronsPerLux = f.sensitivity * f.pitch * f.pitch * dt / kElementaryCharge;
	f.luxPerY         = s.sceneLux * kObjectiveTransmission / ( 4.0 * kFNumber * kFNumber );
	f.rate            = f.electronsPerLux * f.luxPerY;
	f.ebiLux          = kEbi[ gen ];
	f.ebiRate         = f.electronsPerLux * f.ebiLux;
	//1 fL/fc = (1/pi) (cd/m^2)/lx: 1 fL = (1/pi) cd/ft^2, 1 fc = 1 lm/ft^2.
	f.gain         = s.gainFLfc / kPi;
	f.displayScale = f.gain / ( f.electronsPerLux * 4.0 * kMaxLuminance );

	f.tau            = DecayTime( s.phosphor ) * hooks.tauScale;
	const double dtP = hooks.phosphorPerFrame ? kDefaultFrame : dt;
	const double x   = dtP / f.tau;
	f.decayA         = std::exp( -x );
	f.oneMinusA      = -std::expm1( -x );
	f.decayB         = f.oneMinusA / x;
	f.oneMinusB      = 1.0 - f.decayB;

	const double dtA = hooks.abcPerFrame ? kDefaultFrame : dt;
	f.abcDecay       = std::exp( -dtA / kAbcTime );

	f.haloRadiusPx = 2.0 * s.gap * hooks.haloScale / f.pitch;
	f.haloFactor   = std::max( 1, static_cast< int >( std::ceil( f.haloRadiusPx / kMaxHaloCells - 1e-9 ) ) );

	f.cellPx         = kMultifibre * hooks.cellScale / f.pitch;
	f.boundaryHalfPx = 0.5 * kBoundaryWidth / f.pitch;
	//A Gaussian MTF exp( -2 pi^2 sigma^2 f^2 ) at 5% at the limiting resolution.
	const double sigma = std::sqrt( std::log( 1.0 / kContrastAtLimit ) / ( 2.0 * kPi * kPi ) ) / kLimitingResolution;
	f.sigmaPx          = sigma * hooks.spotScale / f.pitch;
	(void)width;
	return f;
}

double HaloCdf( double u )
{
	u = std::clamp( u, 0.0, 1.0 );
	return 1.0 - std::sqrt( 1.0 - u * u );
}

namespace
{
/// Where the ray from the origin at angle theta is inside the square
/// [ cx - 0.5, cx + 0.5 ] x [ cy - 0.5, cy + 0.5 ]: [ enter, exit ], or
/// false.
bool raySquare( double cosT, double sinT, double cx, double cy, double& enter, double& exit )
{
	double lo = 0.0, hi = 1e300;
	const double d[ 2 ] = { cosT, sinT };
	const double c[ 2 ] = { cx, cy };
	for( int k = 0; k < 2; ++k )
	{
		const double a = c[ k ] - 0.5, b = c[ k ] + 0.5;
		if( std::fabs( d[ k ] ) < 1e-300 )
		{
			if( a > 0.0 || b < 0.0 )
				return false;
			continue;
		}
		double t0 = a / d[ k ], t1 = b / d[ k ];
		if( t0 > t1 )
			std::swap( t0, t1 );
		lo = std::max( lo, t0 );
		hi = std::min( hi, t1 );
	}
	enter = lo;
	exit  = hi;
	return hi > lo;
}
} // namespace

HaloKernel BuildHaloKernel( double radiusPx, int factor )
{
	HaloKernel k;
	k.radiusPx = radiusPx;
	k.factor   = std::max( factor, 1 );
	//The disc's radius in cells; a cell [ i - 1/2, i + 1/2 ] meets it while
	//|i| - 1/2 < R.
	const double R = radiusPx / k.factor;
	k.radius       = std::max( 0, static_cast< int >( std::ceil( R + 0.5 ) ) - 1 );
	const int side = 2 * k.radius + 1;
	k.weights.assign( static_cast< size_t >( side * side ), 0.0f );
	std::vector< double > w( k.weights.size(), 0.0 );
	if( R <= 1e-9 )
	{
		w[ static_cast< size_t >( k.radius * side + k.radius ) ] = 1.0;
	}
	else
	{
		//Polar: the share of the landings in a cell is
		//  (1 / 2 pi) int dtheta [ F( exit ) - F( enter ) ],
		//F the radial CDF, over the angles whose ray crosses the cell. F is
		//continuous (the density's 1/sqrt singularity at the rim is
		//integrable), so a midpoint rule in theta converges.
		constexpr int kSamples = 1024;
		for( int j = -k.radius; j <= k.radius; ++j )
			for( int i = -k.radius; i <= k.radius; ++i )
			{
				double lo = 0.0, span = 2.0 * kPi;
				if( i != 0 || j != 0 )
				{
					//The cell's angular extent, from its corners, about the
					//direction of its centre (never more than pi / 2).
					const double mid = std::atan2( static_cast< double >( j ), static_cast< double >( i ) );
					double a0 = 1e9, a1 = -1e9;
					for( int c = 0; c < 4; ++c )
					{
						const double x = i + ( ( c & 1 ) ? 0.5 : -0.5 ), y = j + ( ( c & 2 ) ? 0.5 : -0.5 );
						double a = std::atan2( y, x ) - mid;
						a        = std::remainder( a, 2.0 * kPi );
						a0       = std::min( a0, a );
						a1       = std::max( a1, a );
					}
					lo   = mid + a0;
					span = a1 - a0;
				}
				double sum = 0.0;
				for( int s = 0; s < kSamples; ++s )
				{
					const double theta = lo + span * ( s + 0.5 ) / kSamples;
					double enter = 0.0, exit = 0.0;
					if( !raySquare( std::cos( theta ), std::sin( theta ), i, j, enter, exit ) )
						continue;
					sum += HaloCdf( exit / R ) - HaloCdf( enter / R );
				}
				w[ static_cast< size_t >( ( j + k.radius ) * side + ( i + k.radius ) ) ] = sum * span / kSamples / ( 2.0 * kPi );
			}
	}
	double total = 0.0;
	for( const double v : w )
		total += v;
	k.rawSum = total;
	for( size_t n = 0; n < w.size(); ++n )
		k.weights[ n ] = static_cast< float >( w[ n ] / total );
	return k;
}

void SpotWeights( double sigmaPx, float out[ 5 ] )
{
	//Each tap is the Gaussian's integral over its pixel.
	double w[ 5 ], total = 0.0;
	const double s = std::max( sigmaPx, 1e-6 ) * std::sqrt( 2.0 );
	for( int i = -2; i <= 2; ++i )
	{
		w[ i + 2 ] = 0.5 * ( std::erf( ( i + 0.5 ) / s ) - std::erf( ( i - 0.5 ) / s ) );
		total += w[ i + 2 ];
	}
	for( int i = 0; i < 5; ++i )
		out[ i ] = static_cast< float >( w[ i ] / total );
}

void PhosphorColour( int phosphor, double rgb[ 3 ] )
{
	const double x = tube::kPhosphorXY[ phosphor == kP45 ? 1 : 0 ][ 0 ];
	const double y = tube::kPhosphorXY[ phosphor == kP45 ? 1 : 0 ][ 1 ];
	const double X = x / y, Y = 1.0, Z = ( 1.0 - x - y ) / y;
	//IEC 61966-2-1, XYZ (D65) to linear sRGB.
	rgb[ 0 ] = 3.2406 * X - 1.5372 * Y - 0.4986 * Z;
	rgb[ 1 ] = -0.9689 * X + 1.8758 * Y + 0.0415 * Z;
	rgb[ 2 ] = 0.0557 * X - 0.2040 * Y + 1.0570 * Z;
	const double luminance = 0.2126 * rgb[ 0 ] + 0.7152 * rgb[ 1 ] + 0.0722 * rgb[ 2 ];
	for( int c = 0; c < 3; ++c )
		rgb[ c ] /= luminance;
}

} // namespace intensifier
