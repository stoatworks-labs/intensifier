// The checks. Included into main.cpp's anonymous namespace; see main.cpp for
// the Rig and the pictures.

//===========================================================================
// The harness's own physics, restated from SPEC-intensifier.md and not from
// the plugin's source. Every constant is given again with its source.
//===========================================================================
constexpr double hPi       = 3.14159265358979323846;
constexpr double hE        = 1.602176634e-19;        ///< C, exact (2019 SI)
constexpr double hDiameter = 18e-3;                  ///< m, the frame's height (an 18 mm tube)
constexpr double hTubeArea = 2.54e-4;                ///< m^2, Photek DS001 (18 mm: 2.54 cm^2)
constexpr double hOptics   = 0.9 / ( 4.0 * 1.2 * 1.2 );///< tau / 4N^2: f/1.2, 0.9 (the spec's assumption)
constexpr double hS[ 2 ]   = { 280e-6, 1500e-6 };    ///< A/lm, Hamamatsu TII0007E typ (Gen 2, Gen 3)
constexpr double hEbi[ 2 ] = { 1e-7, 2e-7 };         ///< lux, Hamamatsu TII0007E typ
constexpr double hEta      = 0.20 * ( 1.0 - 0.63 );  ///< US 9,177,764 x US 8,135,253 (the spec's reading)
constexpr double hDecade[ 2 ] = { 1.2e-3, 1.5e-3 };  ///< s per decade: P43 Photek DS001, P45 AST
constexpr double hLmax     = 3.0;                    ///< cd/m^2 (chosen)
constexpr double hTauAbc   = 0.2;                    ///< s (chosen)
constexpr double hIlim     = 80e-9;                  ///< A (chosen)
constexpr double hCell     = 0.45e-3;                ///< m, multifibre flat to flat (derived)
constexpr double hBoundary = 8e-6;                   ///< m, a multifibre boundary's width: one fibre pitch (the spec)
constexpr double hBoundaryDepth = 0.30;              ///< the boundary's loss at Fixed Pattern 1 (chosen)
constexpr double hCellSpread    = 0.04;              ///< a cell's transmission, uniform within +-, at Fixed Pattern 1 (chosen)

double hClamp01( float v )
{
	return std::clamp( static_cast< double >( v ), 0.0, 1.0 );
}
/// The controls, 0..1 to physical, the harness's own (the spec's ranges).
double hLux( float v )
{
	return 1e-5 * std::pow( 1e7, hClamp01( v ) );
}
double hGainCd( float v )
{
	return 1000.0 * std::pow( 100.0, hClamp01( v ) ) / hPi;//fL/fc -> (cd/m^2)/lx
}
double hGap( float v )
{
	return 50e-6 * std::pow( 10.0, hClamp01( v ) );
}
/// The counting interval at a frame rate: the frame, to the microsecond (the spec).
double hFrame( double fps )
{
	return std::round( 1e6 / fps ) * 1e-6;
}
double hPitch( int height )
{
	return hDiameter / height;
}
/// Photoelectrons a pixel expects per lux on the photocathode in one frame.
double hElectronsPerLux( int gen, int height, double fps )
{
	const double p = hPitch( height );
	return hS[ gen ] * p * p * hFrame( fps ) / hE;
}
/// The linear display value per unit of emitted charge, at full gain.
double hDisplayScale( int gen, int height, double fps, float gainParam )
{
	return hGainCd( gainParam ) / ( hElectronsPerLux( gen, height, fps ) * 4.0 * hLmax );
}
double hTau( int phosphor )
{
	return hDecade[ phosphor ] / std::log( 10.0 );
}
/// The halo's radius, px, and the coarse grid's factor the spec gives it.
double hHaloRadius( float gapParam, int height )
{
	return 2.0 * hGap( gapParam ) / hPitch( height );
}
int hHaloFactor( double radiusPx )
{
	return std::max( 1, static_cast< int >( std::ceil( radiusPx / 6.0 - 1e-9 ) ) );
}
/// The display's spot: a Gaussian whose MTF exp( -2 pi^2 sigma^2 f^2 ) is 5%
/// at 64 lp/mm (Photek DS001's limiting resolution, the spec's reading),
/// sigma in px; and the share of a spot's light its centre pixel keeps, of
/// the 5 x 5 the plugin draws (pixel-integrated, the taps renormalised).
double hSpotSigma( int height )
{
	return std::sqrt( std::log( 20.0 ) / ( 2.0 * hPi * hPi ) ) / 64e3 / hPitch( height );
}
double hCentreShare( int height )
{
	const double s = hSpotSigma( height ) * std::sqrt( 2.0 );
	auto cell      = [ & ]( int i ) { return 0.5 * ( std::erf( ( i + 0.5 ) / s ) - std::erf( ( i - 0.5 ) / s ) ); };
	double all     = 0.0;
	for( int i = -2; i <= 2; ++i )
		all += cell( i );
	const double w0 = cell( 0 ) / all;
	return w0 * w0;
}

//===========================================================================
// Statistics, the harness's own.
//===========================================================================
/// Regularised incomplete gamma: P by its series below a + 1, Q by Lentz's
/// continued fraction above (Numerical Recipes, 6.2).
double gammaSeriesP( double a, double x )
{
	double sum = 1.0 / a, term = sum, ap = a;
	for( int n = 0; n < 1000000; ++n )
	{
		ap += 1.0;
		term *= x / ap;
		sum += term;
		if( std::fabs( term ) < std::fabs( sum ) * 1e-15 )
			break;
	}
	return sum * std::exp( -x + a * std::log( x ) - std::lgamma( a ) );
}
double gammaFractionQ( double a, double x )
{
	const double tiny = 1e-300;
	double b = x + 1.0 - a, c = 1.0 / tiny, d = 1.0 / b, h = d;
	for( int i = 1; i < 1000000; ++i )
	{
		const double an = -i * ( i - a );
		b += 2.0;
		d = an * d + b;
		if( std::fabs( d ) < tiny )
			d = tiny;
		c = b + an / c;
		if( std::fabs( c ) < tiny )
			c = tiny;
		d              = 1.0 / d;
		const double e = d * c;
		h *= e;
		if( std::fabs( e - 1.0 ) < 1e-15 )
			break;
	}
	return std::exp( -x + a * std::log( x ) - std::lgamma( a ) ) * h;
}
double gammaQ( double a, double x )
{
	if( x <= 0.0 )
		return 1.0;
	return x < a + 1.0 ? 1.0 - gammaSeriesP( a, x ) : gammaFractionQ( a, x );
}
double gammaP( double a, double x )
{
	if( x <= 0.0 )
		return 0.0;
	return x < a + 1.0 ? gammaSeriesP( a, x ) : 1.0 - gammaFractionQ( a, x );
}
/// P( chi^2_df >= chi2 ).
double chiSquareP( double chi2, int df )
{
	return df <= 0 ? 1.0 : gammaQ( 0.5 * df, 0.5 * chi2 );
}
double logPoisson( double k, double lam )
{
	return -lam + k * std::log( lam ) - std::lgamma( k + 1.0 );
}
/// P( N <= k ), N ~ Poisson( lam ).
double poissonCdf( double k, double lam )
{
	return k < 0.0 ? 0.0 : gammaQ( k + 1.0, lam );
}

/// Composite Simpson on [ a, b ], n intervals (made even).
template< typename F >
double integrate( F f, double a, double b, int n )
{
	n += n & 1;
	const double h = ( b - a ) / n;
	double sum     = f( a ) + f( b );
	for( int i = 1; i < n; ++i )
		sum += ( i & 1 ? 4.0 : 2.0 ) * f( a + i * h );
	return sum * h / 3.0;
}

/// A chi-square of observed counts against expected ones, adjacent bins
/// merged until each expects at least 5 (the usual rule).
struct ChiSquare
{
	double chi2 = 0.0;
	int df      = 0;
	double p    = 1.0;
};
ChiSquare chiSquare( const std::vector< double >& observed, const std::vector< double >& expected )
{
	std::vector< double > o, e;
	double co = 0.0, ce = 0.0;
	for( size_t i = 0; i < observed.size(); ++i )
	{
		co += observed[ i ];
		ce += expected[ i ];
		if( ce >= 5.0 )
		{
			o.push_back( co );
			e.push_back( ce );
			co = ce = 0.0;
		}
	}
	if( ce > 0.0 || co > 0.0 )
	{
		if( e.empty() )
		{
			o.push_back( co );
			e.push_back( ce );
		}
		else
		{
			o.back() += co;
			e.back() += ce;
		}
	}
	ChiSquare r;
	for( size_t i = 0; i < o.size(); ++i )
		r.chi2 += ( o[ i ] - e[ i ] ) * ( o[ i ] - e[ i ] ) / e[ i ];
	r.df = static_cast< int >( o.size() ) - 1;
	r.p  = chiSquareP( r.chi2, r.df );
	return r;
}

/// The checks' significance: a correct model fails one comparison in a
/// million (about 5 sigma, two-sided).
constexpr double kAlpha = 1e-6;
constexpr double kSigmas = 5.0;

/// The larger of two errors, NaN winning: std::max( worst, NaN ) keeps
/// worst, so a model that produced NaN would pass every "worst <= tolerance"
/// (a mutant that divides by alpha did exactly that).
double worse( double a, double b )
{
	return ( a != a || b != b ) ? std::numeric_limits< double >::quiet_NaN() : std::max( a, b );
}
template< typename... T >
double worse( double a, double b, T... rest )
{
	return worse( worse( a, b ), rest... );
}

/// Counts, against Poisson( lam ): the mean, the variance (with the sample
/// variance's own standard error, from Poisson's fourth central moment), and
/// a chi-square on the pmf with exact tails.
struct PoissonFit
{
	double n = 0, mean = 0, var = 0, meanZ = 0, varZ = 0;
	ChiSquare chi;
};
PoissonFit fitPoisson( const std::map< long, double >& histogram, double lam )
{
	PoissonFit f;
	double s1 = 0.0, s2 = 0.0;
	long kmin = histogram.empty() ? 0 : histogram.begin()->first, kmax = histogram.empty() ? 0 : histogram.rbegin()->first;
	for( const auto& h : histogram )
	{
		f.n += h.second;
		s1 += h.second * static_cast< double >( h.first );
	}
	f.mean = s1 / f.n;
	for( const auto& h : histogram )
		s2 += h.second * ( h.first - f.mean ) * ( h.first - f.mean );
	f.var             = s2 / ( f.n - 1.0 );
	f.meanZ           = ( f.mean - lam ) / std::sqrt( lam / f.n );
	const double mu4  = lam * ( 1.0 + 3.0 * lam );
	const double varS = mu4 / f.n - lam * lam * ( f.n - 3.0 ) / ( f.n * ( f.n - 1.0 ) );
	f.varZ            = ( f.var - lam ) / std::sqrt( varS );
	//Bins: every k from a few sigma below to a few above, the tails folded
	//into the end bins exactly.
	const double sd = std::sqrt( lam );
	const long lo   = std::min( kmin, static_cast< long >( std::max( 0.0, std::floor( lam - 8.0 * sd - 5.0 ) ) ) );
	const long hi   = std::max( kmax, static_cast< long >( std::ceil( lam + 8.0 * sd + 5.0 ) ) );
	//Counts nowhere near lambda (a broken model): no pmf to bin, and binning
	//every k between them would take hours. Rejected outright.
	if( hi - lo > 4000000 )
	{
		f.chi.chi2 = HUGE_VAL;
		f.chi.df   = 0;
		f.chi.p    = 0.0;
		return f;
	}
	std::vector< double > obs, exp;
	for( long k = lo; k <= hi; ++k )
	{
		const auto it = histogram.find( k );
		obs.push_back( it == histogram.end() ? 0.0 : it->second );
		double p;
		if( k == lo )
			p = poissonCdf( static_cast< double >( k ), lam );
		else if( k == hi )
			p = 1.0 - poissonCdf( static_cast< double >( k - 1 ), lam );
		else
			p = std::exp( logPoisson( static_cast< double >( k ), lam ) );
		exp.push_back( p * f.n );
	}
	f.chi = chiSquare( obs, exp );
	return f;
}

//===========================================================================
// Rigs and pictures.
//===========================================================================
/// Every check sets every control it depends on (honeydew's trap).
void defaults( Rig& rig )
{
	rig.Set( PT_LIGHT, ParamFromLight( 0.01 ) );
	rig.Set( PT_PHOTOCATHODE, static_cast< int >( kGen3 ) );
	rig.Set( PT_GAIN, ParamFromGain( 40000.0 ) );
	rig.Set( PT_PHOSPHOR, static_cast< int >( kP43 ) );
	rig.Set( PT_GAP, ParamFromGap( 175e-6 ) );
	rig.Set( PT_FIXED, 0.0f );
	rig.Set( PT_MIX, 1.0f );
}

bool startRig( Rig& rig, int w, int h, const Perturb& perturb, const Floats* picture = nullptr, double fps = 60.0 )
{
	rig.fps = fps;
	if( !rig.Init( w, h, picture ) )
		return false;
	rig.plugin.HooksForTest() = perturb;
	defaults( rig );
	return true;
}

Floats grey( int w, int h, double code, double alpha = 1.0 )
{
	return flatCard( w, h, static_cast< float >( code ), static_cast< float >( code ), static_cast< float >( code ), static_cast< float >( alpha ) );
}

/// A flat field that gives about `target` photoelectrons a pixel a frame:
/// the Light Level and the clip's code value, and the lambda the harness's
/// chain expects from what was actually set (float parameter, float code).
struct FlatPlan
{
	int gen     = kGen3;
	float code  = 1.0f;
	float light = 0.0f;
	double lambda = 0.0, duty = 1.0;
};
FlatPlan planFlat( double target, int height, double fps, int gen )
{
	FlatPlan p;
	p.gen          = gen;
	const double k = hElectronsPerLux( gen, height, fps );
	const double e = target / k - hEbi[ gen ];//the scene's share on the photocathode, lux
	double lux = 1.0, code = 1.0;
	if( e <= 0.0 )
	{
		code = 0.0;
		lux  = 1e-5;
	}
	else
	{
		lux = e / hOptics;
		if( lux < 1e-5 )
		{
			lux  = 1e-5;
			code = codeOf( e / ( 1e-5 * hOptics ) );
		}
		lux = std::min( lux, 100.0 );
	}
	p.code             = static_cast< float >( code );
	p.light            = ParamFromLight( lux );
	const double y     = linearOf( p.code );
	const double ePc   = y * hLux( p.light ) * hOptics + hEbi[ gen ];
	p.duty             = std::min( 1.0, hIlim / ( hS[ gen ] * ePc * hTubeArea ) );
	p.lambda           = k * ePc * p.duty;
	return p;
}

/// Pixels at least `margin` from every edge.
template< typename F >
void forInterior( int w, int h, int margin, F f )
{
	for( int y = margin; y < h - margin; ++y )
		for( int x = margin; x < w - margin; ++x )
			f( x, y, static_cast< size_t >( y ) * w + x );
}

//===========================================================================
// --poisson
//===========================================================================
int runPoisson( const Perturb& perturb )
{
	std::printf( "\n=== poisson: event counts are Poisson in the photon rate -- flat fields from lambda 0.02 to 5000 (both sides of the sampler's 1e-3 and 10),\n"
	             "    counts from the count buffer; the mean against the harness's own photon chain, the variance against its standard error, a chi-square\n"
	             "    on the pmf (p >= 1e-6); and a dark quarter under the gate's duty for the tiny rates\n" );
	const double targets[] = { 0.02, 0.5, 3.0, 9.5, 10.5, 40.0, 400.0, 5000.0 };
	for( const Raster& raster : kRasters )
	{
		for( const double target : targets )
		{
			const FlatPlan plan = planFlat( target, raster.h, 60.0, kGen3 );
			const Floats card   = grey( raster.w, raster.h, plan.code );
			Rig rig;
			if( !startRig( rig, raster.w, raster.h, perturb, &card ) )
				return 1;
			rig.Set( PT_LIGHT, plan.light );
			rig.Set( PT_GAP, ParamFromGap( 50e-6 ) );
			const double R   = hHaloRadius( ParamFromGap( 50e-6 ), raster.h );
			const int margin = static_cast< int >( std::ceil( R ) ) + 2 * hHaloFactor( R ) + 3;
			const double npx = static_cast< double >( raster.w - 2 * margin ) * ( raster.h - 2 * margin );
			const int frames = std::clamp( static_cast< int >( std::ceil( std::max( 2e6 / npx, 4e4 / ( plan.lambda * npx ) ) ) ), 2, 200 );
			if( !rig.Render( 2 ) )
				return 1;
			std::map< long, double > histogram;
			for( int f = 0; f < frames; ++f )
			{
				if( !rig.Render( 1 ) )
					return 1;
				const Floats c = rig.Counts();
				forInterior( raster.w, raster.h, margin, [ & ]( int, int, size_t p ) { histogram[ std::lround( c[ 4 * p ] ) ] += 1.0; } );
			}
			const PoissonFit fit = fitPoisson( histogram, plan.lambda );
			Check( std::fabs( fit.meanZ ) <= kSigmas && std::fabs( fit.varZ ) <= kSigmas && fit.chi.p >= kAlpha,
			       fmt( "%4dx%-4d lambda %9.4g (%3d frames, n %.3g): mean %.6g (%+.2f sigma), variance %.6g (%+.2f sigma, dispersion %.5f), chi2 %.1f on %d df, "
			            "p %.3g",
			            raster.w, raster.h, plan.lambda, frames, fit.n, fit.mean, fit.meanZ, fit.var, fit.varZ, fit.var / fit.mean, fit.chi.chi2, fit.chi.df,
			            fit.chi.p ) );
		}

		//The tiny rates: Gen 2 at 240 fps, three quarters of the frame bright
		//at 100 lux so the gate's duty falls to ~0.1, and the dark quarter
		//sees only the gated EBI -- below 1e-3 at both rasters.
		{
			const double fps = 240.0;
			Floats card      = grey( raster.w, raster.h, 1.0 );
			const int dark   = raster.w / 4;
			for( int y = 0; y < raster.h; ++y )
				for( int x = 0; x < dark; ++x )
					for( int c = 0; c < 3; ++c )
						card[ ( static_cast< size_t >( y ) * raster.w + x ) * 4 + c ] = 0.0f;
			Rig rig;
			if( !startRig( rig, raster.w, raster.h, perturb, &card, fps ) )
				return 1;
			rig.Set( PT_PHOTOCATHODE, static_cast< int >( kGen2 ) );
			rig.Set( PT_LIGHT, ParamFromLight( 100.0 ) );
			rig.Set( PT_GAP, ParamFromGap( 50e-6 ) );
			const double lit  = static_cast< double >( raster.w - dark ) / raster.w;
			const double mean = lit * hLux( ParamFromLight( 100.0 ) ) * hOptics + hEbi[ kGen2 ];
			const double duty = std::min( 1.0, hIlim / ( hS[ kGen2 ] * mean * hTubeArea ) );
			const double lam  = duty * hElectronsPerLux( kGen2, raster.h, fps ) * hEbi[ kGen2 ];
			const double R    = hHaloRadius( ParamFromGap( 50e-6 ), raster.h );
			const int margin  = static_cast< int >( std::ceil( R ) ) + 2 * hHaloFactor( R ) + 3;
			const double npx  = static_cast< double >( dark - 2 * margin ) * ( raster.h - 2 * margin );
			const int frames  = std::clamp( static_cast< int >( std::ceil( 600.0 / ( lam * npx ) ) ), 4, 400 );
			if( !rig.Render( 2 ) )
				return 1;
			std::map< long, double > histogram;
			for( int f = 0; f < frames; ++f )
			{
				if( !rig.Render( 1 ) )
					return 1;
				const Floats c = rig.Counts();
				for( int y = margin; y < raster.h - margin; ++y )
					for( int x = margin; x < dark - margin; ++x )
						histogram[ std::lround( c[ 4 * ( static_cast< size_t >( y ) * raster.w + x ) ] ) ] += 1.0;
			}
			const PoissonFit fit = fitPoisson( histogram, lam );
			Check( lam < 1e-3 && std::fabs( fit.meanZ ) <= kSigmas && std::fabs( fit.varZ ) <= kSigmas && fit.chi.p >= kAlpha,
			       fmt( "%4dx%-4d lambda %9.4g (the gated EBI; duty %.4f; %d frames, n %.3g): mean %.4g (%+.2f sigma), variance %.4g (%+.2f sigma), chi2 %.1f on "
			            "%d df, p %.3g",
			            raster.w, raster.h, lam, duty, frames, fit.n, fit.mean, fit.meanZ, fit.var, fit.varZ, fit.chi.chi2, fit.chi.df, fit.chi.p ) );
		}
	}
	return Verdict();
}

//===========================================================================
// --sampler
//===========================================================================
int runSampler( const Perturb& perturb )
{
	std::printf( "\n=== sampler: on the card, halo and all, every count is Poisson in the plugin's own lambda -- a randomised probability-integral transform\n"
	             "    of 20000 pixels a frame is uniform (chi-square on 20 bins, p >= 1e-6), at three Light Levels\n" );
	std::mt19937_64 random( 20261116u );
	std::uniform_real_distribution< double > unit( 0.0, 1.0 );
	for( const Raster& raster : kRasters )
	{
		std::vector< double > bins( 20, 0.0 );
		double n = 0.0, lamMax = 0.0;
		for( const double lux : { 1e-4, 1e-2, 1.0 } )
		{
			Rig rig;
			if( !startRig( rig, raster.w, raster.h, perturb ) )
				return 1;
			rig.Set( PT_LIGHT, ParamFromLight( lux ) );
			if( !rig.Render( 2 ) )
				return 1;
			for( int f = 0; f < 2; ++f )
			{
				if( !rig.Render( 1 ) )
					return 1;
				//20000 pixels a frame, drawn by the harness's own generator: the
				//exact CDF at lambda ~ 1e5 costs a few thousand terms a pixel.
				const Floats c = rig.Counts();
				std::uniform_int_distribution< size_t > pick( 0, c.size() / 4 - 1 );
				for( int s = 0; s < 20000; ++s )
				{
					const size_t p = pick( random );
					const double k = c[ 4 * p ], lam = c[ 4 * p + 2 ];
					lamMax         = std::max( lamMax, lam );
					const double below = poissonCdf( k - 1.0, lam );
					const double at    = std::exp( logPoisson( k, lam ) );
					const double u     = std::clamp( below + unit( random ) * at, 0.0, 1.0 - 1e-12 );
					bins[ static_cast< size_t >( u * 20.0 ) ] += 1.0;
					n += 1.0;
				}
			}
		}
		std::vector< double > expected( 20, n / 20.0 );
		const ChiSquare chi = chiSquare( bins, expected );
		Check( chi.p >= kAlpha, fmt( "%4dx%-4d %.3g counts, lambda up to %.4g: PIT chi2 %.1f on %d df, p %.3g (bins %.4f .. %.4f of n/20)", raster.w, raster.h, n,
		                             lamMax, chi.chi2, chi.df, chi.p, *std::min_element( bins.begin(), bins.end() ) / ( n / 20.0 ),
		                             *std::max_element( bins.begin(), bins.end() ) / ( n / 20.0 ) ) );
	}
	return Verdict();
}

//===========================================================================
// --charge
//===========================================================================
int runCharge( const Perturb& perturb )
{
	std::printf( "\n=== charge: the plate's charge for N photoelectrons is a sum of N exponential gains, Gamma( N, 1 ) in units of the mean gain --\n"
	             "    from the count buffer's N and Q, the probability-integral transform P( N, Q ) of 20000 counted pixels a frame is uniform\n"
	             "    (chi-square on 20 bins, p >= 1e-6), at three Light Levels on the card (N from 1 to ~1e5)\n" );
	std::mt19937_64 random( 3141592u );
	for( const Raster& raster : kRasters )
	{
		std::vector< double > bins( 20, 0.0 );
		double n = 0.0, nMax = 0.0, small = 0.0;
		for( const double lux : { 1e-4, 1e-2, 1.0 } )
		{
			Rig rig;
			if( !startRig( rig, raster.w, raster.h, perturb ) )
				return 1;
			rig.Set( PT_LIGHT, ParamFromLight( lux ) );
			if( !rig.Render( 2 ) )
				return 1;
			for( int f = 0; f < 2; ++f )
			{
				if( !rig.Render( 1 ) )
					return 1;
				const Floats c = rig.Counts();
				std::vector< size_t > counted;
				for( size_t p = 0; p < c.size() / 4; ++p )
					if( c[ 4 * p ] >= 1.0f )
						counted.push_back( p );
				if( counted.empty() )
					continue;
				std::uniform_int_distribution< size_t > pick( 0, counted.size() - 1 );
				for( int s = 0; s < 20000; ++s )
				{
					const size_t p = counted[ pick( random ) ];
					const double k = c[ 4 * p ], q = c[ 4 * p + 1 ];
					nMax           = std::max( nMax, k );
					small += k <= 4.0;
					const double u = std::clamp( gammaP( k, std::max( q, 0.0 ) ), 0.0, 1.0 - 1e-12 );
					bins[ static_cast< size_t >( u * 20.0 ) ] += 1.0;
					n += 1.0;
				}
			}
		}
		std::vector< double > expected( 20, n / 20.0 );
		const ChiSquare chi = chiSquare( bins, expected );
		Check( chi.p >= kAlpha && small > 1000.0 && nMax > 1000.0,
		       fmt( "%4dx%-4d %.3g counted pixels (%.3g of them with N <= 4, N up to %.4g): PIT chi2 %.1f on %d df, p %.3g (bins %.4f .. %.4f of n/20)", raster.w,
		            raster.h, n, small, nMax, chi.chi2, chi.df, chi.p, *std::min_element( bins.begin(), bins.end() ) / ( n / 20.0 ),
		            *std::max_element( bins.begin(), bins.end() ) / ( n / 20.0 ) ) );
	}
	return Verdict();
}

//===========================================================================
// --rng
//===========================================================================
int runRng( const Perturb& perturb )
{
	std::printf( "\n=== rng: the counts' residuals are uncorrelated frame to frame, across, up, along both diagonals and between two instances:\n"
	             "    |r| <= 5 / sqrt( n ), at lambda ~3 (inversion) and ~30 (PTRS)\n" );
	for( const Raster& raster : kRasters )
		for( const double target : { 3.0, 30.0 } )
		{
			const FlatPlan plan = planFlat( target, raster.h, 60.0, kGen3 );
			const Floats card   = grey( raster.w, raster.h, plan.code );
			Rig a, b;
			if( !startRig( a, raster.w, raster.h, perturb, &card ) || !startRig( b, raster.w, raster.h, perturb, &card ) )
				return 1;
			for( Rig* r : { &a, &b } )
			{
				r->Set( PT_LIGHT, plan.light );
				r->Set( PT_GAP, ParamFromGap( 50e-6 ) );
			}
			if( !a.Render( 3 ) || !b.Render( 3 ) )
				return 1;
			auto residuals = []( const Floats& c ) {
				std::vector< double > z( c.size() / 4 );
				for( size_t p = 0; p < z.size(); ++p )
					z[ p ] = ( c[ 4 * p ] - c[ 4 * p + 2 ] ) / std::sqrt( std::max( static_cast< double >( c[ 4 * p + 2 ] ), 1e-30 ) );
				return z;
			};
			const std::vector< double > z0 = residuals( a.Counts() );
			if( !b.Render( 1 ) )
				return 1;
			const std::vector< double > other = residuals( b.Counts() );
			if( !a.Render( 1 ) )
				return 1;
			const std::vector< double > z1 = residuals( a.Counts() );
			//z1 and other: both instances' 4th frame, the same frame index.
			const int w = raster.w, h = raster.h;
			auto corr = [ & ]( const std::vector< double >& p, const std::vector< double >& q, int dx, int dy ) {
				double sp = 0, sq = 0, spq = 0, spp = 0, sqq = 0, n = 0;
				for( int y = 1; y < h - 1; ++y )
					for( int x = 1; x < w - 1; ++x )
					{
						const double u = p[ static_cast< size_t >( y ) * w + x ], v = q[ static_cast< size_t >( y + dy ) * w + x + dx ];
						sp += u;
						sq += v;
						spq += u * v;
						spp += u * u;
						sqq += v * v;
						n += 1.0;
					}
				const double cov = spq / n - ( sp / n ) * ( sq / n );
				const double r   = cov / std::sqrt( ( spp / n - ( sp / n ) * ( sp / n ) ) * ( sqq / n - ( sq / n ) * ( sq / n ) ) );
				return std::make_pair( r, n );
			};
			const auto rt = corr( z0, z1, 0, 0 ), rx = corr( z0, z0, 1, 0 ), ry = corr( z0, z0, 0, 1 ), rd = corr( z0, z0, 1, 1 ), ra = corr( z0, z0, 1, -1 );
			const auto ri    = corr( z1, other, 0, 0 );
			const double lim = kSigmas / std::sqrt( rt.second );
			const double worst = worse( std::fabs( rt.first ), std::fabs( rx.first ), std::fabs( ry.first ), std::fabs( rd.first ), std::fabs( ra.first ),
			                           std::fabs( ri.first ) );
			Check( worst <= lim, fmt( "%4dx%-4d lambda %.3g: r frame %+.5f, x %+.5f, y %+.5f, diagonal %+.5f, anti-diagonal %+.5f, two instances %+.5f "
			                          "(limit %.5f, n %.0f; seeds %u and %u)",
			                          raster.w, raster.h, plan.lambda, rt.first, rx.first, ry.first, rd.first, ra.first, ri.first, lim, rt.second, a.plugin.Seed(),
			                          b.plugin.Seed() ) );
		}
	return Verdict();
}

//===========================================================================
// --phd
//===========================================================================
int runPhd( const Perturb& perturb )
{
	std::printf( "\n=== phd: the plate's pulse heights are a negative exponential, read off the picture -- isolated EBI events (Gen 2, 240 fps, a black clip),\n"
	             "    each one's light summed over 5 x 5 pixels and three frames, over the harness's own light per photoelectron: mean 1, variance 1,\n"
	             "    chi-square against Exp( 1 ) on 20 equiprobable bins (p >= 1e-6)\n" );
	const double fps = 240.0;
	for( const Raster& raster : kRasters )
	{
		const int w = raster.w, h = raster.h;
		const Floats card = grey( w, h, 0.0 );
		Rig rig;
		if( !startRig( rig, w, h, perturb, &card, fps ) )
			return 1;
		rig.Set( PT_PHOTOCATHODE, static_cast< int >( kGen2 ) );
		rig.Set( PT_LIGHT, ParamFromLight( 1e-5 ) );
		rig.Set( PT_GAIN, ParamFromGain( 1000.0 ) );
		const double scale = hDisplayScale( kGen2, h, fps, ParamFromGain( 1000.0 ) );
		if( !rig.Render( 3 ) )
			return 1;
		//A window of four frames: t - 1 (whose spill lands in t), t, t + 1, t + 2.
		std::vector< std::vector< float > > counts( 4 ), light( 4 );
		auto grab = [ & ]( int slot ) {
			const Floats c = rig.Counts();
			const std::vector< double > y = luminanceOf( rig.Output() );
			counts[ slot ].resize( c.size() / 4 );
			light[ slot ].resize( y.size() );
			for( size_t p = 0; p < y.size(); ++p )
			{
				counts[ slot ][ p ] = c[ 4 * p ];
				light[ slot ][ p ]  = static_cast< float >( y[ p ] );
			}
		};
		for( int s = 0; s < 4; ++s )
		{
			if( !rig.Render( 1 ) )
				return 1;
			grab( s );
		}
		std::vector< double > amplitudes;
		double centreLight = 0.0, allLight = 0.0;
		double gainSeen = 1.0;
		const int maxFrames = h >= 720 ? 120 : 400;
		for( int f = 0; f < maxFrames && amplitudes.size() < 20000; ++f )
		{
			for( int y = 4; y < h - 4; ++y )
				for( int x = 4; x < w - 4; ++x )
				{
					const size_t at = static_cast< size_t >( y ) * w + x;
					if( counts[ 1 ][ at ] != 1.0f )
						continue;
					bool isolated = true;
					for( int dy = -3; dy <= 3 && isolated; ++dy )
						for( int dx = -3; dx <= 3 && isolated; ++dx )
						{
							const size_t q = static_cast< size_t >( y + dy ) * w + x + dx;
							for( int s = 0; s < 4 && isolated; ++s )
								if( counts[ s ][ q ] != 0.0f && !( s == 1 && q == at ) )
									isolated = false;
						}
					if( !isolated )
						continue;
					double sum = 0.0, centre = 0.0;
					for( int s = 1; s < 4; ++s )
					{
						centre += light[ s ][ at ];
						for( int dy = -2; dy <= 2; ++dy )
							for( int dx = -2; dx <= 2; ++dx )
								sum += light[ s ][ static_cast< size_t >( y + dy ) * w + x + dx ];
					}
					amplitudes.push_back( sum / scale );
					centreLight += centre;
					allLight += sum;
				}
			std::rotate( counts.begin(), counts.begin() + 1, counts.end() );
			std::rotate( light.begin(), light.begin() + 1, light.end() );
			if( !rig.Render( 1 ) )
				return 1;
			grab( 3 );
			gainSeen = std::min( gainSeen, rig.Control()[ 0 ] );
		}
		const double n = static_cast< double >( amplitudes.size() );
		double s1 = 0.0, s2 = 0.0;
		for( const double a : amplitudes )
			s1 += a;
		const double mean = s1 / n;
		for( const double a : amplitudes )
			s2 += ( a - mean ) * ( a - mean );
		const double var  = s2 / ( n - 1.0 );
		const double zm   = ( mean - 1.0 ) / std::sqrt( 1.0 / n );
		const double zv   = ( var - 1.0 ) / std::sqrt( 8.0 / n );//Exp(1): mu4 = 9, sigma^4 = 1
		std::vector< double > bins( 20, 0.0 ), expected( 20, n / 20.0 );
		for( const double a : amplitudes )
		{
			const double u = 1.0 - std::exp( -std::max( a, 0.0 ) );
			bins[ std::min< size_t >( 19, static_cast< size_t >( u * 20.0 ) ) ] += 1.0;
		}
		const ChiSquare chi = chiSquare( bins, expected );
		Check( n >= 5000 && gainSeen == 1.0 && std::fabs( zm ) <= kSigmas && std::fabs( zv ) <= kSigmas && chi.p >= kAlpha,
		       fmt( "%4dx%-4d %.0f isolated events (ABC gain %.3g throughout): mean %.4f (%+.2f sigma), variance %.4f (%+.2f sigma), chi2 %.1f on %d df, p %.3g",
		            w, h, n, gainSeen, mean, zm, var, zv, chi.chi2, chi.df, chi.p ) );
		//Every event draws the same spot, so the centre pixels' share of all the
		//events' light is the spot's. Weighted by light, not averaged by event:
		//a faint event's share is swung by the last of an earlier event's
		//spill. 1e-4: the sRGB code's float round trip is ~1e-6, and that
		//spill, at the EBI's rates, ~1e-5 of the light.
		const double share = centreLight / allLight;
		Check( std::fabs( share - hCentreShare( h ) ) <= 1e-4,
		       fmt( "%4dx%-4d the spot: the events' centre pixels keep %.6f of their light (the 64 lp/mm, 5%% spot's %.6f at %.3f px sigma; %+.2e, tolerance 1e-4)",
		            w, h, share, hCentreShare( h ), hSpotSigma( h ), share - hCentreShare( h ) ) );
	}
	return Verdict();
}

//===========================================================================
// --halo
//===========================================================================
/// The azimuthal mean of `y` about the pixel centre ( cx, cy ), 0.5 px bins.
std::vector< double > radialProfile( const std::vector< double >& y, int w, int h, int cx, int cy, double maxR )
{
	const int bins = static_cast< int >( std::ceil( maxR * 2.0 ) ) + 1;
	std::vector< double > sum( static_cast< size_t >( bins ), 0.0 ), count( static_cast< size_t >( bins ), 0.0 );
	for( int py = std::max( 0, cy - static_cast< int >( maxR ) - 1 ); py <= std::min( h - 1, cy + static_cast< int >( maxR ) + 1 ); ++py )
		for( int px = std::max( 0, cx - static_cast< int >( maxR ) - 1 ); px <= std::min( w - 1, cx + static_cast< int >( maxR ) + 1 ); ++px )
		{
			const double r = std::hypot( px - cx, py - cy );
			const int b    = static_cast< int >( r * 2.0 );
			if( b < bins )
			{
				sum[ static_cast< size_t >( b ) ] += y[ static_cast< size_t >( py ) * w + px ];
				count[ static_cast< size_t >( b ) ] += 1.0;
			}
		}
	for( size_t b = 0; b < sum.size(); ++b )
		sum[ b ] = count[ b ] > 0.0 ? sum[ b ] / count[ b ] : 0.0;
	return sum;
}

int runHalo( const Perturb& perturb )
{
	std::printf( "\n=== halo: the halo's rim is at twice the gap, read off the picture -- one lit pixel on black, the azimuthal profile of what lands round it,\n"
	             "    the rim as its outer half-maximum: within f + 0.5 px of 2d (f the coarse grid's cell, px), at three gaps a raster; the ring must stand\n"
	             "    10 sigma clear of the background's noise\n" );
	for( const Raster& raster : kRasters )
	{
		const int w = raster.w, h = raster.h;
		const double gaps720[] = { 100e-6, 200e-6, 400e-6 }, gaps180[] = { 200e-6, 300e-6, 500e-6 };
		for( int g = 0; g < 3; ++g )
		{
			const double gap  = h >= 720 ? gaps720[ g ] : gaps180[ g ];
			const float gp    = ParamFromGap( gap );
			const double R    = hHaloRadius( gp, h );
			const int f       = hHaloFactor( R );
			//The source at the centre of a coarse cell where f is odd.
			const int cx = ( w / 2 / f ) * f + ( f - 1 ) / 2, cy = ( h / 2 / f ) * f + ( f - 1 ) / 2;
			Floats card  = grey( w, h, 0.0 );
			for( int c = 0; c < 3; ++c )
				card[ ( static_cast< size_t >( cy ) * w + cx ) * 4 + c ] = 1.0f;
			Rig rig;
			if( !startRig( rig, w, h, perturb, &card ) )
				return 1;
			rig.Set( PT_GAP, gp );
			rig.Set( PT_GAIN, ParamFromGain( 1000.0 ) );
			//Bright enough that the halo's mean shows at ~0.02 of the display, at most 100 lux.
			const double ring = hEta / ( hPi * R * R );
			const double lux  = std::min( 100.0, 0.02 * 4.0 * hLmax / ( hGainCd( ParamFromGain( 1000.0 ) ) * ring * hOptics ) );
			rig.Set( PT_LIGHT, ParamFromLight( lux ) );
			if( !rig.Render( 2 ) )
				return 1;
			const int frames = h >= 720 ? 30 : 60;
			std::vector< double > mean( static_cast< size_t >( w ) * h, 0.0 );
			for( int fr = 0; fr < frames; ++fr )
			{
				if( !rig.Render( 1 ) )
					return 1;
				const std::vector< double > y = luminanceOf( rig.Output() );
				for( size_t p = 0; p < y.size(); ++p )
					mean[ p ] += y[ p ] / frames;
			}
			//The background: everything beyond R + 3f + 4 within twice that.
			const double outer = R + 3.0 * f + 4.0;
			const std::vector< double > profile = radialProfile( mean, w, h, cx, cy, 2.0 * outer );
			double bg = 0.0, bg2 = 0.0, nb = 0.0;
			for( size_t b = static_cast< size_t >( outer * 2.0 ); b < profile.size(); ++b )
			{
				bg += profile[ b ];
				bg2 += profile[ b ] * profile[ b ];
				nb += 1.0;
			}
			bg /= nb;
			const double noise = std::sqrt( std::max( bg2 / nb - bg * bg, 0.0 ) );
			size_t peak = 3;//from 1.5 px out
			for( size_t b = 3; b < static_cast< size_t >( outer * 2.0 ); ++b )
				if( profile[ b ] > profile[ peak ] )
					peak = b;
			const double top = profile[ peak ] - bg;
			double rim       = -1.0;
			for( size_t b = peak; b + 1 < profile.size(); ++b )
				if( profile[ b + 1 ] - bg < 0.5 * top )
				{
					const double a = profile[ b ] - bg, c = profile[ b + 1 ] - bg;
					rim            = 0.5 * ( b + 0.5 ) + 0.5 * ( a - 0.5 * top ) / ( a - c );
					break;
				}
			const double tolerance = f + 0.5;
			Check( top > 10.0 * noise && rim > 0.0 && std::fabs( rim - R ) <= tolerance,
			       fmt( "%4dx%-4d gap %3.0f um: 2d = %6.2f px (f %d); rim at %6.2f px (%+.2f, tolerance %.1f); ring %.3g over a background %.3g +- %.2g "
			            "(%.0f lux, %d frames)",
			            w, h, gap * 1e6, R, f, rim, rim - R, tolerance, top, bg, noise, lux, frames ) );
		}
	}
	return Verdict();
}

//===========================================================================
// --abc
//===========================================================================
int runAbc( const Perturb& perturb )
{
	std::printf( "\n=== abc: the ABC holds the screen's mean at L_max (0.25 of the display) within 1%% before and after a 100x step, read off the picture;\n"
	             "    it settles in the same time at 30, 60 and 120 fps and its gain agrees at common times; a torch on 1%% of the frame dims the\n"
	             "    background by the ratio of the frame means\n" );
	const double knee = hLmax / ( hGainCd( ParamFromGain( 40000.0 ) ) * linearOf( 0.46f ) * hOptics );//scene lux at the knee for code 0.46
	for( const Raster& raster : kRasters )
	{
		const int w = raster.w, h = raster.h;
		const Floats card = grey( w, h, 0.46 );
		struct Run
		{
			double fps;
			std::vector< double > logGain;//at each frame
			double before = 0, after = 0, settle = -1;
		};
		std::vector< Run > runs = { { 30.0, {} }, { 60.0, {} }, { 120.0, {} } };
		for( Run& run : runs )
		{
			Rig rig;
			if( !startRig( rig, w, h, perturb, &card, run.fps ) )
				return 1;
			rig.Set( PT_LIGHT, ParamFromLight( 2.0 * knee ) );
			const int perSecond = static_cast< int >( run.fps );
			for( int f = 0; f < 6 * perSecond; ++f )
			{
				if( f == 3 * perSecond )
					rig.Set( PT_LIGHT, ParamFromLight( 200.0 * knee ) );
				if( !rig.Render( 1 ) )
					return 1;
				run.logGain.push_back( rig.Control()[ 3 ] );
				const bool lastTen = ( f >= 3 * perSecond - 10 && f < 3 * perSecond ) || f >= 6 * perSecond - 10;
				const bool afterStep = f >= 3 * perSecond;
				if( lastTen || ( afterStep && run.settle < 0.0 ) )
				{
					const std::vector< double > y = luminanceOf( rig.Output() );
					double m = 0.0;
					for( const double v : y )
						m += v;
					m /= static_cast< double >( y.size() );
					if( lastTen )
						( afterStep ? run.after : run.before ) += m / 10.0;
					if( afterStep && run.settle < 0.0 && std::fabs( m / 0.25 - 1.0 ) <= 0.1 )
						run.settle = ( f - 3 * perSecond + 1 ) / run.fps;
				}
			}
		}
		for( const Run& run : runs )
			Check( std::fabs( run.before / 0.25 - 1.0 ) <= 0.01 && std::fabs( run.after / 0.25 - 1.0 ) <= 0.01 && run.settle > 0.0,
			       fmt( "%4dx%-4d %3.0f fps: mean display %.5f settled at 2x the knee, %.5f at 200x (L_max is 0.25; tolerance 1%%); within 10%% %.4f s after the step",
			            w, h, run.fps, run.before, run.after, run.settle ) );
		//Frame-rate independence: the settling time to a frame of 30 fps, and
		//ln g at the times all three share (every 1/30 s), to three float
		//roundings a frame at |ln g| <= ln 200, accumulated over the 120 fps
		//run's frames.
		double worstLog = 0.0;
		const int shared = static_cast< int >( runs[ 0 ].logGain.size() );
		for( int i = 0; i < shared; ++i )
		{
			const double a = runs[ 0 ].logGain[ static_cast< size_t >( i ) ];
			const double b = runs[ 1 ].logGain[ static_cast< size_t >( 2 * i + 1 ) ];
			const double c = runs[ 2 ].logGain[ static_cast< size_t >( 4 * i + 3 ) ];
			worstLog       = worse( worstLog, std::fabs( a - b ), std::fabs( a - c ) );
		}
		//Two terms. Float: three roundings a frame at |ln g| <= ln 200, over
		//the 120 fps run's 720 frames. The clock: each frame is rounded to the
		//microsecond, so 2 x 1/60 and 4 x 1/120 differ from 1/30 by a few
		//microseconds a step; that lag in time, at the ABC's rate of change
		//(ln 100 e^(-t/tau) / tau after the step), bounded over the run.
		const double drift = std::max( std::fabs( hFrame( 30.0 ) - 2.0 * hFrame( 60.0 ) ), std::fabs( hFrame( 30.0 ) - 4.0 * hFrame( 120.0 ) ) );
		double clockTerm   = 0.0;
		for( int j = 0; j <= 90; ++j )
			clockTerm = std::max( clockTerm, std::log( 100.0 ) * std::exp( -j / 30.0 / hTauAbc ) * ( j + 1 ) * drift / hTauAbc );
		const double logTolerance = 720.0 * 3.0 * std::ldexp( std::log( 200.0 ), -24 ) + clockTerm;
		const double spread = std::max( { runs[ 0 ].settle, runs[ 1 ].settle, runs[ 2 ].settle } ) - std::min( { runs[ 0 ].settle, runs[ 1 ].settle, runs[ 2 ].settle } );
		Check( spread <= 1.0 / 30.0 + 1e-9 && worstLog <= logTolerance,
		       fmt( "%4dx%-4d settling times %.4f / %.4f / %.4f s (spread %.4f, tolerance one 30 fps frame); ln g at shared times differs by %.3g at most "
		            "(tolerance %.3g)",
		            w, h, runs[ 0 ].settle, runs[ 1 ].settle, runs[ 2 ].settle, spread, worstLog, logTolerance ) );

		//The torch: a background of linear 0.001 at twice its knee, then a
		//square of linear 1 on 1% of the frame.
		{
			const double yb = 0.001, yt = 1.0;
			const double bkCode = codeOf( yb );
			Floats plain        = grey( w, h, bkCode );
			Floats torch        = plain;
			const int side      = static_cast< int >( std::lround( std::sqrt( 0.01 * w * h ) ) );
			const int x0 = w / 2 - side / 2, y0 = h / 2 - side / 2;
			for( int y = y0; y < y0 + side; ++y )
				for( int x = x0; x < x0 + side; ++x )
					for( int c = 0; c < 3; ++c )
						torch[ ( static_cast< size_t >( y ) * w + x ) * 4 + c ] = static_cast< float >( codeOf( yt ) );
			Rig rig;
			if( !startRig( rig, w, h, perturb, &plain ) )
				return 1;
			const double lux = 2.0 * hLmax / ( hGainCd( ParamFromGain( 40000.0 ) ) * yb * hOptics );
			rig.Set( PT_LIGHT, ParamFromLight( lux ) );
			const double R   = hHaloRadius( ParamFromGap( 175e-6 ), h );
			const int margin = static_cast< int >( std::ceil( R ) ) + 2 * hHaloFactor( R ) + 4;
			auto background  = [ & ]( double& mean, double& sd, double& n ) {
				const std::vector< double > y = luminanceOf( rig.Output() );
				double s = 0, s2 = 0;
				n        = 0;
				for( int py = margin; py < h - margin; ++py )
					for( int px = margin; px < w - margin; ++px )
					{
						if( px >= x0 - margin && px < x0 + side + margin && py >= y0 - margin && py < y0 + side + margin )
							continue;
						const double v = y[ static_cast< size_t >( py ) * w + px ];
						s += v;
						s2 += v * v;
						n += 1.0;
					}
				mean = s / n;
				sd   = std::sqrt( std::max( s2 / n - mean * mean, 0.0 ) );
			};
			if( !rig.Render( 60 ) )
				return 1;
			double before = 0, sdB = 0, nB = 0, after = 0, sdA = 0, nA = 0;
			for( int k = 0; k < 10; ++k )
			{
				double m, s, n;
				if( !rig.Render( 1 ) )
					return 1;
				background( m, s, n );
				before += m / 10.0;
				sdB = s;
				nB += n;
			}
			rig.Upload( torch );
			if( !rig.Render( 180 ) )
				return 1;
			for( int k = 0; k < 10; ++k )
			{
				double m, s, n;
				if( !rig.Render( 1 ) )
					return 1;
				background( m, s, n );
				after += m / 10.0;
				sdA = s;
				nA += n;
			}
			const double share = static_cast< double >( side ) * side / ( static_cast< double >( w ) * h );
			const double eB    = linearOf( static_cast< float >( bkCode ) ) * lux * hOptics + hEbi[ kGen3 ];
			const double eA    = ( ( 1.0 - share ) * linearOf( static_cast< float >( bkCode ) ) + share * linearOf( static_cast< float >( codeOf( yt ) ) ) ) * lux * hOptics
			                  + hEbi[ kGen3 ];
			const double want  = eA / eB;
			const double got   = before / after;
			//The ratio's noise: each mean's standard error, propagated.
			const double rel = std::sqrt( sdB * sdB / ( nB * before * before ) + sdA * sdA / ( nA * after * after ) );
			Check( std::fabs( got / want - 1.0 ) <= kSigmas * rel + 1e-4,
			       fmt( "%4dx%-4d a torch on %.2f%% of the frame: the background dims %.4fx; the frame means' ratio is %.4fx (%+.2e, tolerance %.2e: 5 sigma of "
			            "the two means + 1e-4)",
			            w, h, 100.0 * share, got, want, got / want - 1.0, kSigmas * rel + 1e-4 ) );
		}
	}
	return Verdict();
}

//===========================================================================
// --bsp
//===========================================================================
int runBsp( const Perturb& perturb )
{
	std::printf( "\n=== bsp: past its knee the gated photocathode's current is the limit's, whatever the light -- from the counts: interior mean x e / dt\n"
	             "    x the tube's area over a pixel's, at 10 and 100 lux on a grey of 0.5 (and below the knee, the ungated current)\n" );
	for( const Raster& raster : kRasters )
	{
		const int w = raster.w, h = raster.h;
		const double code = codeOf( 0.5 );
		const Floats card = grey( w, h, code );
		for( const double lux : { 0.1, 10.0, 100.0 } )
		{
			Rig rig;
			if( !startRig( rig, w, h, perturb, &card ) )
				return 1;
			rig.Set( PT_LIGHT, ParamFromLight( lux ) );
			rig.Set( PT_GAP, ParamFromGap( 50e-6 ) );
			if( !rig.Render( 2 ) )
				return 1;
			const double R   = hHaloRadius( ParamFromGap( 50e-6 ), h );
			const int margin = static_cast< int >( std::ceil( R ) ) + 2 * hHaloFactor( R ) + 3;
			double sum = 0.0, n = 0.0;
			for( int f = 0; f < 4; ++f )
			{
				if( !rig.Render( 1 ) )
					return 1;
				const Floats c = rig.Counts();
				forInterior( w, h, margin, [ & ]( int, int, size_t p ) {
					sum += c[ 4 * p ];
					n += 1.0;
				} );
			}
			const double mean    = sum / n;
			const double pitch   = hPitch( h );
			const double current = mean * hE / hFrame( 60.0 ) * hTubeArea / ( pitch * pitch );
			const double ePc     = linearOf( static_cast< float >( code ) ) * hLux( ParamFromLight( lux ) ) * hOptics + hEbi[ kGen3 ];
			const double free    = hS[ kGen3 ] * ePc * hTubeArea;
			const double want    = std::min( free, hIlim );
			//The mean's relative standard error (Poisson counts), and float: the
			//frame's total is three levels of 256-texel sums, each at most 256
			//roundings of 2^-24.
			const double sigma   = std::sqrt( mean / n ) / mean;
			const double roundoff = 3.0 * 256.0 * std::ldexp( 1.0, -24 );
			Check( std::fabs( current / want - 1.0 ) <= kSigmas * sigma + roundoff,
			       fmt( "%4dx%-4d %5.1f lux: photocathode current %.5g A from the counts (mean %.5g a pixel); ungated it would be %.4g A, the limit %.3g A: "
			            "%+.2e from the expected (tolerance %.1e)",
			            w, h, lux, current, mean, free, hIlim, current / want - 1.0, kSigmas * sigma + roundoff ) );
		}
	}
	return Verdict();
}

//===========================================================================
// --phosphor
//===========================================================================
/// The light a unit of charge, arriving uniformly over [ 0, dt ], emits in
/// frame `k` (0 the arrival frame), for an exponential of time constant
/// tau: by quadrature, the harness's own.
double frameLight( int k, double dt, double tau )
{
	//int_0^dt ( 1 / dt ) [ e^{-( k dt - t' ) / tau} - e^{-( ( k + 1 ) dt - t' ) / tau} ]^+ dt'
	//with the arrival frame's lower edge at t' itself.
	return integrate(
		[ & ]( double t ) {
			const double start = std::max( k * dt, t ), end = ( k + 1 ) * dt;
			return ( std::exp( -( start - t ) / tau ) - std::exp( -( end - t ) / tau ) ) / dt;
		},
		0.0, dt, 4000 );
}

int runPhosphor( const Perturb& perturb )
{
	std::printf( "\n=== phosphor: the light spilling into the frame after a one-frame flash, read off the picture, gives the phosphor's decay time\n"
	             "    (the harness's own quadrature of an exponential over the frames) -- P43 0.521 ms, P45 0.651 ms, to 1e-3, at 30, 60, 120 and 240 fps;\n"
	             "    and the flash's colour is the phosphor's\n" );
	for( const Raster& raster : kRasters )
	{
		const int w = raster.w, h = raster.h;
		const Floats black = grey( w, h, 0.0 ), flash = grey( w, h, 1.0 );
		for( const int phosphor : { static_cast< int >( kP43 ), static_cast< int >( kP45 ) } )
		{
			std::string line;
			double worst = 0.0, colourOff = 0.0;
			bool flat    = true;
			//US 4,694,217's chromaticities (the spec's reading).
			const double kPatentXY[ 2 ][ 2 ] = { { 0.334, 0.561 }, { 0.269, 0.311 } };
			for( const double fps : { 30.0, 60.0, 120.0, 240.0 } )
			{
				Rig rig;
				if( !startRig( rig, w, h, perturb, &black, fps ) )
					return 1;
				rig.Set( PT_PHOSPHOR, phosphor );
				rig.Set( PT_PHOTOCATHODE, static_cast< int >( kGen2 ) );
				rig.Set( PT_GAIN, ParamFromGain( 1000.0 ) );
				//The flash at a tenth of the display: well under the ABC's knee.
				const double lux = 0.1 * 4.0 * hLmax / ( hGainCd( ParamFromGain( 1000.0 ) ) * hOptics );
				rig.Set( PT_LIGHT, ParamFromLight( lux ) );
				auto total = [ & ]() {
					const std::vector< double > y = luminanceOf( rig.Output() );
					double s                      = 0.0;
					for( const double v : y )
						s += v;
					flat = flat && rig.Control()[ 0 ] == 1.0;
					return s;
				};
				if( !rig.Render( 10 ) )
					return 1;
				double base = 0.0;
				for( int k = 0; k < 6; ++k )
				{
					if( !rig.Render( 1 ) )
						return 1;
					base += total() / 6.0;
				}
				rig.Upload( flash );
				if( !rig.Render( 1 ) )
					return 1;
				const double l1 = total() - base;
				{
					//The screen's colour: the frame's mean linear RGB, to xy by the
					//IEC 61966-2-1 matrix (the harness's own).
					const Floats out = rig.Output();
					double r = 0, g = 0, b = 0;
					for( size_t p = 0; p < out.size() / 4; ++p )
					{
						r += linearOf( out[ 4 * p ] );
						g += linearOf( out[ 4 * p + 1 ] );
						b += linearOf( out[ 4 * p + 2 ] );
					}
					const double X = 0.4124 * r + 0.3576 * g + 0.1805 * b, Y = 0.2126 * r + 0.7152 * g + 0.0722 * b, Z = 0.0193 * r + 0.1192 * g + 0.9505 * b;
					colourOff = worse( colourOff, std::fabs( X / ( X + Y + Z ) - kPatentXY[ phosphor ][ 0 ] ), std::fabs( Y / ( X + Y + Z ) - kPatentXY[ phosphor ][ 1 ] ) );
				}
				rig.Upload( black );
				if( !rig.Render( 1 ) )
					return 1;
				const double l2    = total() - base;
				const double s     = l2 / l1;
				const double dt    = hFrame( fps );
				//tau from s by bisection in log tau.
				double lo = std::log( 1e-6 ), hi = std::log( 1.0 );
				for( int it = 0; it < 80; ++it )
				{
					const double mid = 0.5 * ( lo + hi ), t = std::exp( mid );
					( frameLight( 1, dt, t ) / frameLight( 0, dt, t ) < s ? lo : hi ) = mid;
				}
				const double tau = std::exp( 0.5 * ( lo + hi ) );
				const double rel = tau / hTau( phosphor ) - 1.0;
				worst            = worse( worst, std::fabs( rel ) );
				line += fmt( "  %3.0f fps: spill %.5f -> tau %.4f ms (%+.1e)", fps, s, tau * 1e3, rel );
			}
			Check( worst <= 1e-3 && flat, fmt( "%4dx%-4d %s, source tau %.4f ms:%s%s", w, h, phosphor == kP43 ? "P43" : "P45", hTau( phosphor ) * 1e3, line.c_str(),
			                                   flat ? "" : "  (the ABC moved: the check is void)" ) );
			Check( colourOff <= 1e-3, fmt( "%4dx%-4d %s on the screen: the flash frames' mean colour is within %.2g of US 4,694,217's x %.3f y %.3f in xy "
			                               "(tolerance 1e-3, the matrices' four digits)",
			                               w, h, phosphor == kP43 ? "P43" : "P45", colourOff, kPatentXY[ phosphor ][ 0 ], kPatentXY[ phosphor ][ 1 ] ) );
		}
	}
	return Verdict();
}

//===========================================================================
// --fixed
//===========================================================================
/// In-place radix-2 FFT, the harness's own.
void fft( std::vector< std::complex< double > >& a )
{
	const size_t n = a.size();
	for( size_t i = 1, j = 0; i < n; ++i )
	{
		size_t bit = n >> 1;
		for( ; j & bit; bit >>= 1 )
			j ^= bit;
		j ^= bit;
		if( i < j )
			std::swap( a[ i ], a[ j ] );
	}
	for( size_t len = 2; len <= n; len <<= 1 )
	{
		const double angle = -2.0 * hPi / static_cast< double >( len );
		const std::complex< double > wl( std::cos( angle ), std::sin( angle ) );
		for( size_t i = 0; i < n; i += len )
		{
			std::complex< double > w( 1.0, 0.0 );
			for( size_t k = 0; k < len / 2; ++k )
			{
				const std::complex< double > u = a[ i + k ], v = a[ i + k + len / 2 ] * w;
				a[ i + k ]           = u + v;
				a[ i + k + len / 2 ] = u - v;
				w *= wl;
			}
		}
	}
}

int runFixed( const Perturb& perturb )
{
	std::printf( "\n=== fixed: the honeycomb's period, read off the picture at high light (Fixed Pattern 1): the strongest line of the averaged frame's\n"
	             "    spectrum sits at the triangular lattice's 2 / ( sqrt 3 s ) to one DFT bin, and its three directions 60 degrees apart all show;\n"
	             "    and its depth, everywhere: Fixed Pattern 1 against 0 (same seed, so the same photons) loses the boundary strips' share of the\n"
	             "    light in each third of the frame, across and down\n" );
	for( const Raster& raster : kRasters )
	{
		const int w = raster.w, h = raster.h;
		const double code = codeOf( 0.5 );
		const Floats card = grey( w, h, code );
		Rig rig, plain;
		if( !startRig( rig, w, h, perturb, &card ) || !startRig( plain, w, h, perturb, &card ) )
			return 1;
		const float light = ParamFromLight( 10.0 * hLmax / ( hGainCd( ParamFromGain( 40000.0 ) ) * 0.5 * hOptics ) );
		rig.Set( PT_FIXED, 1.0f );
		rig.Set( PT_LIGHT, light );
		plain.Set( PT_FIXED, 0.0f );
		plain.Set( PT_LIGHT, light );
		if( !rig.Render( 4 ) || !plain.Render( 4 ) )
			return 1;
		const int frames = h >= 720 ? 16 : 64;
		const int N      = h >= 720 ? 512 : 128;
		std::vector< double > mean( static_cast< size_t >( w ) * h, 0.0 );
		//Light summed over each third of the frame: [0..2] across (columns),
		//[3..5] down (rows), with the pattern and without.
		double withPattern[ 6 ] = {}, without[ 6 ] = {}, brightest = 0.0;
		for( int f = 0; f < frames; ++f )
		{
			if( !rig.Render( 1 ) || !plain.Render( 1 ) )
				return 1;
			const std::vector< double > y = luminanceOf( rig.Output() ), y0 = luminanceOf( plain.Output() );
			for( int row = 0; row < h; ++row )
				for( int col = 0; col < w; ++col )
				{
					const size_t p = static_cast< size_t >( row ) * w + col;
					mean[ p ] += y[ p ] / frames;
					brightest = worse( brightest, y[ p ], y0[ p ] );
					for( const int band : { 3 * col / w, 3 + 3 * row / h } )
					{
						withPattern[ band ] += y[ p ];
						without[ band ] += y0[ p ];
					}
				}
		}
		const int x0 = ( w - N ) / 2, y0 = ( h - N ) / 2;
		double avg   = 0.0;
		for( int y = 0; y < N; ++y )
			for( int x = 0; x < N; ++x )
				avg += mean[ static_cast< size_t >( y0 + y ) * w + x0 + x ];
		avg /= static_cast< double >( N ) * N;
		std::vector< std::vector< std::complex< double > > > grid( static_cast< size_t >( N ), std::vector< std::complex< double > >( static_cast< size_t >( N ) ) );
		for( int y = 0; y < N; ++y )
			for( int x = 0; x < N; ++x )
			{
				const double hx = 0.5 - 0.5 * std::cos( 2.0 * hPi * x / N ), hy = 0.5 - 0.5 * std::cos( 2.0 * hPi * y / N );
				grid[ static_cast< size_t >( y ) ][ static_cast< size_t >( x ) ] = ( mean[ static_cast< size_t >( y0 + y ) * w + x0 + x ] - avg ) * hx * hy;
			}
		for( auto& row : grid )
			fft( row );
		for( int x = 0; x < N; ++x )
		{
			std::vector< std::complex< double > > col( static_cast< size_t >( N ) );
			for( int y = 0; y < N; ++y )
				col[ static_cast< size_t >( y ) ] = grid[ static_cast< size_t >( y ) ][ static_cast< size_t >( x ) ];
			fft( col );
			for( int y = 0; y < N; ++y )
				grid[ static_cast< size_t >( y ) ][ static_cast< size_t >( x ) ] = col[ static_cast< size_t >( y ) ];
		}
		auto power = [ & ]( int kx, int ky ) {
			return std::norm( grid[ static_cast< size_t >( ( ky % N + N ) % N ) ][ static_cast< size_t >( ( kx % N + N ) % N ) ] );
		};
		const double sPx = hCell / hPitch( h );
		const double r0  = N * 2.0 / ( std::sqrt( 3.0 ) * sPx );
		double best = 0.0, bestR = 0.0, bestA = 0.0;
		for( int ky = 0; ky <= N / 2; ++ky )
			for( int kx = -N / 2; kx < N / 2; ++kx )
			{
				const double r = std::hypot( kx, ky );
				if( r < 0.5 * r0 || r > 1.5 * r0 )
					continue;
				const double p = power( kx, ky );
				if( p > best )
				{
					best  = p;
					bestR = r;
					bestA = std::atan2( ky, kx ) * 180.0 / hPi;
				}
			}
		//Each of the three directions on its own: the strongest bin within 3
		//degrees of 30, 90 and 150 and half to 1.5 r0 out must sit at r0 to a
		//bin, and carry at least a quarter of the strongest line's power.
		double weakest = 1e300, farthest = 0.0;
		std::string seen;
		for( const double dir : { 30.0, 90.0, 150.0 } )
		{
			double m = 0.0, at = 0.0;
			for( int ky = 0; ky <= N / 2; ++ky )
				for( int kx = -N / 2; kx < N / 2; ++kx )
				{
					const double r = std::hypot( kx, ky ), a = std::atan2( ky, kx ) * 180.0 / hPi;
					if( r >= 0.5 * r0 && r <= 1.5 * r0 && std::fabs( a - dir ) <= 3.0 && power( kx, ky ) > m )
					{
						m  = power( kx, ky );
						at = r;
					}
				}
			weakest  = std::min( weakest, m / best );
			farthest = worse( farthest, std::fabs( at - r0 ) );
			seen += fmt( " %.0f deg at %.2f bins, %.2f of the strongest;", dir, at, m / best );
		}
		Check( std::fabs( bestR - r0 ) <= 1.0 && farthest <= 1.0 && weakest >= 0.25,
		       fmt( "%4dx%-4d multifibre %.2f px: lattice lines expected at %.2f bins of %d; strongest at %.2f bins and %.1f deg;%s each within 1 bin "
		            "of r0 and >= 0.25",
		            w, h, sPx, r0, N, bestR, bestA, seen.c_str() ) );

		//The depth. A strip of width b along every edge of a hexagon s flat
		//to flat covers 1 - ( 1 - 2b / s )^2 of it, and loses hBoundaryDepth
		//of its light there. Tolerances: the cells' spread, uniform within
		//+-hCellSpread (sd hCellSpread / sqrt 3 a cell), over the cells in a
		//third of the frame, to kSigmas; and the box filter across the pixel,
		//which moves a strip's coverage up to half a pixel inside the cell,
		//where the perimeter is shorter by 2t / s: 0.5 / s of the loss.
		const double expectLoss = hBoundaryDepth * ( 1.0 - std::pow( 1.0 - 2.0 * ( 0.5 * hBoundary ) / hCell, 2.0 ) );
		const double cellArea   = 0.5 * std::sqrt( 3.0 ) * sPx * sPx;
		const double sdThird    = hCellSpread / std::sqrt( 3.0 ) / std::sqrt( static_cast< double >( w ) * h / 3.0 / cellArea );
		const double tolLoss    = kSigmas * sdThird + expectLoss * 0.5 / sPx;
		double worstLoss = 0.0;
		std::string losses;
		for( int band = 0; band < 6; ++band )
		{
			const double loss = 1.0 - withPattern[ band ] / without[ band ];
			worstLoss         = worse( worstLoss, std::fabs( loss - expectLoss ) );
			losses += fmt( "%s%.5f", band == 0 ? " across " : band == 3 ? "; down " : " ", loss );
		}
		Check( worstLoss <= tolLoss && brightest < 1.0,
		       fmt( "%4dx%-4d the light lost to the boundaries in each third of the frame:%s; expected %.5f (8 um strips of a %.2f px cell, %.0f%% "
		            "deep), worst %+.5f, tolerance %.5f (%g sd of the cells' spread, %.2e, plus 0.5 / s of the loss for the box filter); brightest "
		            "pixel %.3f (want < 1, nothing clipped)",
		            w, h, losses.c_str(), expectLoss, sPx, 100.0 * hBoundaryDepth, worstLoss, tolLoss, kSigmas, sdThird, brightest ) );
	}
	return Verdict();
}

//===========================================================================
// --alpha
//===========================================================================
int runAlpha( const Perturb& perturb )
{
	std::printf( "\n=== alpha: output alpha = mix( a, 1, Mix ); a transparent pixel sends the photocathode only its EBI; Mix 0 hands the clip through\n" );
	for( const Raster& raster : kRasters )
	{
		const int w = raster.w, h = raster.h;
		//Left half: white at alpha 0. Right half: a grey under an alpha ramp.
		Floats card = grey( w, h, 0.5 );
		for( int y = 0; y < h; ++y )
			for( int x = 0; x < w; ++x )
			{
				float* o = &card[ ( static_cast< size_t >( y ) * w + x ) * 4 ];
				if( x < w / 2 )
					o[ 0 ] = o[ 1 ] = o[ 2 ] = 1.0f, o[ 3 ] = 0.0f;
				else
					o[ 3 ] = std::round( 255.0f * ( 2.0f * x / w - 1.0f ) ) / 255.0f;
			}
		Rig rig;
		if( !startRig( rig, w, h, perturb, &card ) )
			return 1;
		rig.Set( PT_LIGHT, ParamFromLight( 0.05 ) );
		double worst[ 2 ] = {};
		const float mixes[ 2 ] = { 1.0f, 0.5f };
		for( int k = 0; k < 2; ++k )
		{
			rig.Set( PT_MIX, mixes[ k ] );
			if( !rig.Render( 3 ) )
				return 1;
			const Floats out = rig.Output();
			for( size_t p = 0; p < out.size() / 4; ++p )
			{
				const double a = card[ 4 * p + 3 ];
				worst[ k ]     = worse( worst[ k ], std::fabs( out[ 4 * p + 3 ] - ( a + ( 1.0 - a ) * mixes[ k ] ) ) );
			}
		}
		//The EBI alone on the transparent side, far from the lit half's halo.
		const double R    = hHaloRadius( ParamFromGap( 175e-6 ), h );
		const int margin  = static_cast< int >( std::ceil( R ) ) + 2 * hHaloFactor( R ) + 3;
		const Floats c    = rig.Counts();
		const double duty = rig.Control()[ 1 ];
		const double want = duty * hElectronsPerLux( kGen3, h, 60.0 ) * hEbi[ kGen3 ];
		double worstEbi   = 0.0;
		for( int y = margin; y < h - margin; ++y )
			for( int x = margin; x < w / 2 - margin; ++x )
				worstEbi = worse( worstEbi, std::fabs( c[ 4 * ( static_cast< size_t >( y ) * w + x ) + 2 ] / want - 1.0 ) );
		rig.Set( PT_MIX, 0.0f );
		if( !rig.Render( 1 ) )
			return 1;
		const size_t through = differing( rig.Output(), card );
		Check( worst[ 0 ] <= 1e-6 && worst[ 1 ] <= 1e-6 && worstEbi <= 1e-5 && through == 0,
		       fmt( "%4dx%-4d alpha at Mix 1 off mix( a, 1, Mix ) by %.2g, at Mix 0.5 by %.2g (tolerance 1e-6); lambda over white at alpha 0 is the EBI's "
		            "%.4g to %.2g (tolerance 1e-5); Mix 0: %zu values differ from the clip (bit-exact)",
		            w, h, worst[ 0 ], worst[ 1 ], want, worstEbi, through ) );
	}
	return Verdict();
}

//===========================================================================
// --resize
//===========================================================================
int runResize( const Perturb& perturb )
{
	std::printf( "\n=== resize: the ABC's gain and the phosphor's store survive a raster change mid-run -- 1280x720 -> 320x180 in the middle of an ABC\n"
	             "    transition, the frame after a bright one at 240 fps: g follows a run never resized, and the spill's mean on the display matches it\n" );
	const double fps  = 240.0;
	const double code = 0.46;
	const double knee = hLmax / ( hGainCd( ParamFromGain( 40000.0 ) ) * linearOf( static_cast< float >( code ) ) * hOptics );
	Floats bright720 = grey( 1280, 720, code ), black720 = grey( 1280, 720, 0.0 ), black180 = grey( 320, 180, 0.0 );
	Rig moved, kept;
	if( !startRig( moved, 1280, 720, perturb, &bright720, fps ) || !startRig( kept, 1280, 720, perturb, &bright720, fps ) )
		return 1;
	//One seed: up to the resize the two runs draw the same photoelectrons, so
	//what differs after it is the resize's doing, not two samples' noise.
	moved.plugin.SetSeedForTest( kept.plugin.Seed() );
	for( Rig* r : { &moved, &kept } )
	{
		r->Set( PT_PHOTOCATHODE, static_cast< int >( kGen2 ) );
		r->Set( PT_LIGHT, ParamFromLight( 2.0 * knee ) );
	}
	if( !moved.Render( 120 ) || !kept.Render( 120 ) )
		return 1;
	for( Rig* r : { &moved, &kept } )
		r->Set( PT_LIGHT, ParamFromLight( 20.0 * knee ) );
	if( !moved.Render( 24 ) || !kept.Render( 24 ) )
		return 1;
	if( !moved.Resize( 320, 180, &black180 ) )
		return 1;
	kept.Upload( black720 );
	if( !moved.Render( 1 ) || !kept.Render( 1 ) )
		return 1;
	auto meanOf = []( const Floats& out ) {
		const std::vector< double > y = luminanceOf( out );
		double s                      = 0.0;
		for( const double v : y )
			s += v;
		return s / static_cast< double >( y.size() );
	};
	const double spillMoved = meanOf( moved.Output() ), spillKept = meanOf( kept.Output() );
	const double gMoved = moved.Control()[ 3 ], gKept = kept.Control()[ 3 ];
	//The tolerance: the new frame's EBI events, which the two rasters draw
	//differently (5 sigma; an exponential gain doubles a count's variance),
	//the spot's edge taps (the rasters clamp a different share of the frame:
	//the perimeter's share x 0.04, more than the spot puts past one pixel at
	//either raster), and float (the resample adds 16 texels; 1e-5).
	auto ebiSigma = [ & ]( int w, int h, double g ) {
		const double events = hElectronsPerLux( kGen2, h, fps ) * hEbi[ kGen2 ] * w * h;
		return g * hDisplayScale( kGen2, h, fps, ParamFromGain( 40000.0 ) ) * std::sqrt( 2.0 * events ) / ( static_cast< double >( w ) * h );
	};
	const double sigma     = std::hypot( ebiSigma( 320, 180, std::exp( gMoved ) ), ebiSigma( 1280, 720, std::exp( gKept ) ) );
	const double tolerance = kSigmas * sigma + spillKept * ( 2.0 * ( 1280 + 720 ) / ( 1280.0 * 720.0 ) * 0.04 + 1e-5 );
	double worstLog        = std::fabs( gMoved - gKept );
	for( int f = 0; f < 60; ++f )
	{
		if( !moved.Render( 1 ) || !kept.Render( 1 ) )
			return 1;
		worstLog = worse( worstLog, std::fabs( moved.Control()[ 3 ] - kept.Control()[ 3 ] ) );
	}
	const double logTolerance = 1e-4;//three levels of 256 float additions in the frame's sum, each <= 1 ulp
	Check( spillKept > 1e-3 && std::fabs( spillMoved - spillKept ) <= tolerance && worstLog <= logTolerance && std::fabs( gKept ) > 0.1,
	       fmt( "the frame after the resize: spill on the display %.6f (resized) against %.6f (kept) -- %+.2e, tolerance %.2e; ln g %.5f against %.5f, "
	            "worst difference over 60 frames %.2e (tolerance %.0e)",
	            spillMoved, spillKept, spillMoved - spillKept, tolerance, gMoved, gKept, worstLog, logTolerance ) );
	return Verdict();
}

//===========================================================================
// --clock
//===========================================================================
int runClock( const Perturb& perturb )
{
	std::printf( "\n=== clock: a host clock at 4.99e8 ms gives the pictures a clock from 0 s does, bit for bit (same seed, same context), and a clock left\n"
	             "    to settle its own unit settles on the right one\n" );
	for( const Raster& raster : kRasters )
	{
		Rig zero, late;
		if( !startRig( zero, raster.w, raster.h, perturb ) || !startRig( late, raster.w, raster.h, perturb ) )
			return 1;
		late.plugin.SetSeedForTest( zero.plugin.Seed() );
		late.plugin.SetClockScaleForTest( 0.001 );
		late.timeOffset = 4.99e8;
		late.timeScale  = 1000.0;
		size_t differ   = 0, values = 0;
		for( int f = 0; f < 120; ++f )
		{
			if( f == 40 )
				for( Rig* r : { &zero, &late } )
					r->Set( PT_LIGHT, ParamFromLight( 1.0 ) );
			if( !zero.Render( 1 ) || !late.Render( 1 ) )
				return 1;
			if( f % 30 == 29 )
			{
				const Floats a = zero.Output(), b = late.Output();
				differ += differing( a, b );
				values += a.size();
			}
		}
		Check( differ == 0, fmt( "%4dx%-4d 120 frames, the light stepped at frame 40: %zu of %zu values differ at frames 30, 60, 90, 120 (bit-exact)", raster.w,
		                         raster.h, differ, values ) );

		for( const double unit : { 1.0, 1000.0 } )
		{
			Rig r;
			if( !startRig( r, raster.w, raster.h, perturb ) )
				return 1;
			r.plugin.SetClockScaleForTest( 0.0 );
			r.timeOffset = unit == 1.0 ? 12.5 : 4.99e8;
			r.timeScale  = unit;
			if( !r.Render( 60 ) )
				return 1;
			const double want = 1.0 / unit;
			const double got  = r.plugin.ClockScale();
			const double host = ( r.timeOffset + r.timeScale * r.Seconds( r.frame - 1 ) ) * want;
			Check( got == want && std::fabs( r.plugin.HostNow() - host ) < 1e-6 && std::fabs( r.plugin.CountingInterval() - hFrame( 60.0 ) ) < 1e-12,
			       fmt( "%4dx%-4d a host clock in %s at 60 fps, rendered faster than real time: settled on %s by frame 60, reads %.6f s (host %.6f s), counts "
			            "over %.6f s",
			            raster.w, raster.h, unit == 1.0 ? "seconds" : "milliseconds", got == 1.0 ? "seconds" : ( got == 0.001 ? "milliseconds" : "nothing" ),
			            r.plugin.HostNow(), host, r.plugin.CountingInterval() ) );
		}
	}
	return Verdict();
}

//===========================================================================
// --glstate
//===========================================================================
int runGLState( const Perturb& perturb )
{
	std::printf( "\n=== glstate: the host's GL state comes back as it was handed over\n" );
	for( const Raster& raster : kRasters )
	{
		Rig rig;
		if( !startRig( rig, raster.w, raster.h, perturb ) )
			return 1;
		GLuint vao = 0, buffer = 0;
		glGenVertexArrays( 1, &vao );
		glGenBuffers( 1, &buffer );
		int wrong = 0;
		for( int frame = 0; frame < 3; ++frame )
		{
			glBindFramebuffer( GL_FRAMEBUFFER, rig.outputFBO );
			glViewport( 3, 5, raster.w - 7, raster.h - 9 );
			glEnable( GL_BLEND );
			glBlendFuncSeparate( GL_SRC_ALPHA, GL_ONE, GL_ONE, GL_ZERO );
			glClearColor( 0.1f, 0.2f, 0.3f, 0.4f );
			glBindVertexArray( vao );
			glBindBuffer( GL_ARRAY_BUFFER, buffer );
			glActiveTexture( GL_TEXTURE3 );
			rig.plugin.SetTime( rig.Seconds( frame ) );
			if( rig.plugin.ProcessOpenGL( &rig.process ) != FF_SUCCESS )
				return 1;
			GLint vp[ 4 ], src = 0, dst = 0, srcA = 0, dstA = 0, va = 0, ab = 0, at = 0;
			GLfloat cc[ 4 ];
			glGetIntegerv( GL_VIEWPORT, vp );
			glGetIntegerv( GL_BLEND_SRC_RGB, &src );
			glGetIntegerv( GL_BLEND_DST_RGB, &dst );
			glGetIntegerv( GL_BLEND_SRC_ALPHA, &srcA );
			glGetIntegerv( GL_BLEND_DST_ALPHA, &dstA );
			glGetFloatv( GL_COLOR_CLEAR_VALUE, cc );
			glGetIntegerv( GL_VERTEX_ARRAY_BINDING, &va );
			glGetIntegerv( GL_ARRAY_BUFFER_BINDING, &ab );
			glGetIntegerv( GL_ACTIVE_TEXTURE, &at );
			wrong += vp[ 0 ] != 3 || vp[ 1 ] != 5 || vp[ 2 ] != raster.w - 7 || vp[ 3 ] != raster.h - 9;
			wrong += !glIsEnabled( GL_BLEND ) || src != GL_SRC_ALPHA || dst != GL_ONE || srcA != GL_ONE || dstA != GL_ZERO;
			wrong += cc[ 0 ] != 0.1f || cc[ 1 ] != 0.2f || cc[ 2 ] != 0.3f || cc[ 3 ] != 0.4f;
			wrong += va != static_cast< GLint >( vao ) || ab != static_cast< GLint >( buffer ) || at != GL_TEXTURE3;
			wrong += glGetError() != GL_NO_ERROR;
		}
		glBindVertexArray( 0 );
		glDisable( GL_BLEND );
		glActiveTexture( GL_TEXTURE0 );
		glDeleteVertexArrays( 1, &vao );
		glDeleteBuffers( 1, &buffer );
		Check( wrong == 0, fmt( "%4dx%-4d three frames with a host viewport, blend, clear colour, vertex array, buffer and texture unit of its own: %d "
		                        "things not put back, GL errors included",
		                        raster.w, raster.h, wrong ) );
	}
	return Verdict();
}

//===========================================================================
// --tube (offline)
//===========================================================================
/// Gauss-Legendre nodes and weights on [ -1, 1 ], by Newton on P_n.
void gaussLegendre( int n, std::vector< double >& x, std::vector< double >& w )
{
	x.assign( static_cast< size_t >( n ), 0.0 );
	w.assign( static_cast< size_t >( n ), 0.0 );
	for( int i = 0; i < n; ++i )
	{
		double z = std::cos( hPi * ( i + 0.75 ) / ( n + 0.5 ) );
		double dp = 0.0;
		for( int it = 0; it < 100; ++it )
		{
			double p0 = 1.0, p1 = z;
			for( int k = 2; k <= n; ++k )
			{
				const double p2 = ( ( 2.0 * k - 1.0 ) * z * p1 - ( k - 1.0 ) * p0 ) / k;
				p0              = p1;
				p1              = p2;
			}
			dp              = n * ( z * p1 - p0 ) / ( z * z - 1.0 );
			const double dz = p1 / dp;
			z -= dz;
			if( std::fabs( dz ) < 1e-16 )
				break;
		}
		x[ static_cast< size_t >( i ) ] = z;
		w[ static_cast< size_t >( i ) ] = 2.0 / ( ( 1.0 - z * z ) * dp * dp );
	}
}

int runTube( const Perturb& perturb )
{
	std::printf( "\n=== tube: the halo kernel's cell weights against the mechanics by Monte Carlo (Lambertian back-scatter, a projectile across the gap's\n"
	             "    retarding field landing at 2d sin 2 theta), chi-square p >= 1e-6; the phosphors' colours back to the patent's chromaticities; the\n"
	             "    controls' conversions round-trip\n" );
	std::mt19937_64 random( 4694217u );
	std::uniform_real_distribution< double > unit( 0.0, 1.0 );
	for( const double radius : { 1.0, 3.5, 14.0, 21.0, 40.0, 60.0 } )
	{
		const int f            = hHaloFactor( radius );
		const HaloKernel k     = BuildHaloKernel( radius * perturb.tube.haloScale, f );
		const int side         = 2 * k.radius + 1;
		const double electrons = 4e6;
		std::map< std::pair< int, int >, double > landed;
		for( int e = 0; e < static_cast< int >( electrons ); ++e )
		{
			const double s = std::sqrt( unit( random ) ), c = std::sqrt( 1.0 - s * s );//Lambert: sin^2 theta uniform
			const double r = radius * 2.0 * s * c;                                     //R = 2d sin 2 theta, radius = 2d
			const double phi = 2.0 * hPi * unit( random );
			const int i = static_cast< int >( std::lround( r * std::cos( phi ) / f ) ), j = static_cast< int >( std::lround( r * std::sin( phi ) / f ) );
			landed[ { i, j } ] += 1.0;
		}
		std::vector< double > obs, exp;
		double outside = electrons;
		for( int j = -k.radius; j <= k.radius; ++j )
			for( int i = -k.radius; i <= k.radius; ++i )
			{
				const auto it = landed.find( { i, j } );
				const double o = it == landed.end() ? 0.0 : it->second;
				obs.push_back( o );
				outside -= o;
				exp.push_back( electrons * k.weights[ static_cast< size_t >( ( j + k.radius ) * side + ( i + k.radius ) ) ] );
			}
		obs.push_back( outside );
		exp.push_back( 0.0 );
		const ChiSquare chi = chiSquare( obs, exp );
		Check( chi.p >= kAlpha && std::fabs( k.rawSum - 1.0 ) <= 1e-3,
		       fmt( "halo radius %5.1f px, %d px a cell, %2d x %-2d window: %.0f simulated landings, chi2 %.1f on %d df, p %.3g; %.0f landed outside the window; "
		            "the quadrature's weights summed to %.6f before normalising",
		            radius, f, side, side, electrons, chi.chi2, chi.df, chi.p, outside, k.rawSum ) );
	}
	//The colours: back to XYZ by the inverse matrix (IEC 61966-2-1).
	const double xy[ 2 ][ 2 ] = { { 0.334, 0.561 }, { 0.269, 0.311 } };
	for( int p = 0; p < 2; ++p )
	{
		double rgb[ 3 ];
		PhosphorColour( p, rgb );
		const double X = 0.4124 * rgb[ 0 ] + 0.3576 * rgb[ 1 ] + 0.1805 * rgb[ 2 ];
		const double Y = 0.2126 * rgb[ 0 ] + 0.7152 * rgb[ 1 ] + 0.0722 * rgb[ 2 ];
		const double Z = 0.0193 * rgb[ 0 ] + 0.1192 * rgb[ 1 ] + 0.9505 * rgb[ 2 ];
		const double x = X / ( X + Y + Z ), y = Y / ( X + Y + Z );
		Check( std::fabs( x - xy[ p ][ 0 ] ) <= 1e-3 && std::fabs( y - xy[ p ][ 1 ] ) <= 1e-3 && std::fabs( Y - 1.0 ) <= 1e-12,
		       fmt( "%s: linear sRGB %.4f %.4f %.4f, luminance %.12f; back to x %.4f y %.4f (US 4,694,217: %.3f, %.3f; tolerance 1e-3, the matrices' four digits)",
		            p == 0 ? "P43" : "P45", rgb[ 0 ], rgb[ 1 ], rgb[ 2 ], Y, x, y, xy[ p ][ 0 ], xy[ p ][ 1 ] ) );
	}
	double worst = 0.0;
	for( int i = 0; i <= 100; ++i )
	{
		const float v = i / 100.0f;
		const double d[] = { std::fabs( static_cast< double >( ParamFromLight( LightFromParam( v ) ) ) - v ),
			                 std::fabs( static_cast< double >( ParamFromGain( GainFromParam( v ) ) ) - v ),
			                 std::fabs( static_cast< double >( ParamFromGap( GapFromParam( v ) ) ) - v ),
			                 std::fabs( static_cast< double >( LightFromParam( v ) ) / hLux( v ) - 1.0 ),
			                 std::fabs( static_cast< double >( GainFromParam( v ) ) / hPi / hGainCd( v ) - 1.0 ),
			                 std::fabs( static_cast< double >( GapFromParam( v ) ) / hGap( v ) - 1.0 ) };
		for( const double x : d )
			worst = worse( worst, x );
	}
	Check( worst <= 1e-6, fmt( "the conversions: round trips and the harness's own ranges agree to %.2g (tolerance 1e-6)", worst ) );
	return Verdict();
}

//===========================================================================
// The registry.
//===========================================================================
struct CheckEntry
{
	const char* flag;
	CheckFn run;
	bool offline;///< needs no GL context
};

const std::vector< CheckEntry >& checks()
{
	static const std::vector< CheckEntry > list = {
		{ "tube", runTube, true },
		{ "poisson", runPoisson, false },
		{ "sampler", runSampler, false },
		{ "charge", runCharge, false },
		{ "rng", runRng, false },
		{ "phd", runPhd, false },
		{ "halo", runHalo, false },
		{ "abc", runAbc, false },
		{ "bsp", runBsp, false },
		{ "phosphor", runPhosphor, false },
		{ "fixed", runFixed, false },
		{ "alpha", runAlpha, false },
		{ "resize", runResize, false },
		{ "clock", runClock, false },
		{ "glstate", runGLState, false },
	};
	return list;
}

bool isOffline( const std::string& flag )
{
	for( const CheckEntry& c : checks() )
		if( flag == c.flag )
			return c.offline;
	return false;
}

//===========================================================================
// --negative
//===========================================================================
int runNegative( bool offlineOnly = false )
{
	struct Case
	{
		const char* name;
		CheckFn check;
		Perturb perturb;
		const char* what;
	};
	std::vector< Case > cases;
	auto add = [ & ]( const char* name, CheckFn fn, const char* what, std::function< void( Perturb& ) > set ) {
		Perturb p;
		set( p );
		cases.push_back( { name, fn, p, what } );
	};
	add( "tube", runTube, "the halo kernel built at half the radius (a rim at the gap)", []( Perturb& p ) { p.tube.haloScale = 0.5; } );
	add( "poisson", runPoisson, "a rounded normal for the count", []( Perturb& p ) { p.samplerModel = 1; } );
	add( "poisson", runPoisson, "at most one event a pixel a frame", []( Perturb& p ) { p.samplerModel = 2; } );
	add( "sampler", runSampler, "the rate jittered 30% per pixel (over-dispersed)", []( Perturb& p ) { p.samplerModel = 3; } );
	add( "rng", runRng, "the frame left out of the seed", []( Perturb& p ) { p.rngModel = 1; } );
	add( "rng", runRng, "a seed of x + y", []( Perturb& p ) { p.rngModel = 2; } );
	add( "rng", runRng, "every instance seeded alike", []( Perturb& p ) { p.rngModel = 3; } );
	add( "phd", runPhd, "a saturated plate: Polya, shape 4", []( Perturb& p ) { p.gainModel = 1; } );
	add( "phd", runPhd, "a constant gain", []( Perturb& p ) { p.gainModel = 2; } );
	add( "phd", runPhd, "the display's spot twice as wide", []( Perturb& p ) { p.tube.spotScale = 2.0; } );
	add( "charge", runCharge, "a saturated plate: Polya, shape 4", []( Perturb& p ) { p.gainModel = 1; } );
	add( "charge", runCharge, "a constant gain", []( Perturb& p ) { p.gainModel = 2; } );
	add( "halo", runHalo, "the halo's radius the gap, not twice it", []( Perturb& p ) { p.tube.haloScale = 0.5; } );
	add( "halo", runHalo, "no back-scatter", []( Perturb& p ) { p.haloOn = false; } );
	add( "abc", runAbc, "no ABC", []( Perturb& p ) { p.abcOn = false; } );
	add( "abc", runAbc, "the ABC's lag stepped per frame", []( Perturb& p ) { p.tube.abcPerFrame = true; } );
	add( "bsp", runBsp, "no gate", []( Perturb& p ) { p.bspOn = false; } );
	add( "phosphor", runPhosphor, "the phosphor stepped per frame (60 fps factors at every rate)", []( Perturb& p ) { p.tube.phosphorPerFrame = true; } );
	add( "phosphor", runPhosphor, "a decay ten times too long", []( Perturb& p ) { p.tube.tauScale = 10.0; } );
	add( "phosphor", runPhosphor, "the screen drawn in sRGB white", []( Perturb& p ) { p.whiteScreen = true; } );
	add( "fixed", runFixed, "the honeycomb drawn 1.25x too large", []( Perturb& p ) { p.tube.cellScale = 1.25; } );
	add( "fixed", runFixed, "the boundaries not drawn, the cells' spread kept", []( Perturb& p ) { p.boundariesOn = false; } );
	add( "alpha", runAlpha, "the clip's alpha passed through", []( Perturb& p ) { p.alphaModel = 1; } );
	add( "alpha", runAlpha, "the clip's light not weighted by its alpha", []( Perturb& p ) { p.premultiply = false; } );
	add( "resize", runResize, "a new raster clears the phosphor and the ABC", []( Perturb& p ) { p.resizeResets = true; } );
	add( "clock", runClock, "the host's clock read in single precision", []( Perturb& p ) { p.floatClock = true; } );
	add( "glstate", runGLState, "the host's GL state not put back", []( Perturb& p ) { p.leakGLState = true; } );

	if( offlineOnly )
		cases.erase( std::remove_if( cases.begin(), cases.end(), []( const Case& c ) { return !isOffline( c.name ); } ), cases.end() );

	int unfalsifiable = 0;
	for( const Case& c : cases )
	{
		std::printf( "\n=== negative control: %s -- %s\n", c.name, c.what );
		const int before = g_failures;
		g_failures       = 0;
		c.check( c.perturb );
		const int observed = g_failures;
		g_failures         = before;
		if( observed > 0 )
			std::printf( "  ok    %s failed %d check%s, as it must\n", c.name, observed, observed == 1 ? "" : "s" );
		else
		{
			std::printf( "  FAIL  %s PASSED against a wrong model -- it cannot fail, so it is not a check\n", c.name );
			++unfalsifiable;
		}
	}
	std::printf( "\nnegative controls: %zu wrong models, %d of them undetected\n", cases.size(), unfalsifiable );
	std::printf( "\n  %s\n", unfalsifiable == 0 ? "PASS" : "FAIL" );
	return unfalsifiable == 0 ? 0 : 1;
}

int runOffline()
{
	int failed = 0;
	for( const CheckEntry& check : checks() )
		if( check.offline )
			failed |= check.run( Perturb {} );
	failed |= runNegative( true );
	std::printf( "\n  offline: the checks that need no GL context. The picture checks were NOT run --\n"
	             "  tools/verify.sh runs them against a real driver, at 1280x720 and 320x180, and again on the software renderer.\n"
	             "\n  %s\n",
	             failed == 0 ? "PASS" : "FAIL" );
	return failed == 0 ? 0 : 1;
}

//===========================================================================
// --bench: GPU time per frame from GL_TIME_ELAPSED.
//===========================================================================
int runBench()
{
	struct Size
	{
		int w, h;
		const char* name;
		int warm, timed;
	};
	const Size sizes[] = { { 1280, 720, "720p", 120, 120 }, { 1920, 1080, "1080p", 120, 120 }, { 3840, 2160, "4K", 30, 60 } };
	std::printf( "\n=== bench: GPU time per frame (GL_TIME_ELAPSED), the defaults on the card (0.1 lux, Gap 175 um, Fixed Pattern 0.75)\n" );
	GLuint query = 0;
	glGenQueries( 1, &query );
	for( const Size& size : sizes )
	{
		Rig rig;
		if( !rig.Init( size.w, size.h ) )
			return 1;
		if( !rig.Render( size.warm ) )
			return 1;
		std::vector< double > gpu;
		for( int f = 0; f < size.timed; ++f )
		{
			glBeginQuery( GL_TIME_ELAPSED, query );
			if( !rig.Render( 1 ) )
				return 1;
			glEndQuery( GL_TIME_ELAPSED );
			GLuint64 ns = 0;
			glGetQueryObjectui64v( query, GL_QUERY_RESULT, &ns );
			gpu.push_back( static_cast< double >( ns ) * 1e-6 );
		}
		std::sort( gpu.begin(), gpu.end() );
		std::printf( "  %-6s halo %5.1f px (cell %d px)  GPU median %6.2f ms, worst %6.2f (%4.1f%% of a 60 fps frame)\n", size.name,
		             rig.plugin.LastFrame().haloRadiusPx, rig.plugin.LastFrame().haloFactor, gpu[ gpu.size() / 2 ], gpu.back(),
		             100.0 * gpu[ gpu.size() / 2 ] / ( 1000.0 / 60.0 ) );
	}
	glDeleteQueries( 1, &query );
	return 0;
}
