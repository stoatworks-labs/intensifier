// GENERATED from source/Shaders.cpp by demo/tools/check_shaders.py --write.
// Do not edit: tools/verify.sh fails if a character of this differs from the
// plugin's. The one escape allowed is \` for a backtick inside a comment.

// kVersion, source/Shaders.cpp
export const VERSION = '#version 410 core\n';

// kQuadVertex, source/Shaders.cpp
export const QUAD_VERTEX = `
void main()
{
	vec2 p = vec2( float( ( gl_VertexID << 1 ) & 2 ), float( gl_VertexID & 2 ) );
	gl_Position = vec4( p * 2.0 - 1.0, 0.0, 1.0 );
}
`;

// kCommon, source/Shaders.cpp
export const COMMON = `
vec3 toLinear( vec3 c )
{
	c       = max( c, vec3( 0.0 ) );
	vec3 lo = c / 12.92;
	vec3 hi = pow( ( c + 0.055 ) / 1.055, vec3( 2.4 ) );
	return mix( lo, hi, step( vec3( 0.04045 ), c ) );
}
vec3 toCode( vec3 c )
{
	c       = clamp( c, 0.0, 1.0 );
	vec3 lo = c * 12.92;
	vec3 hi = 1.055 * pow( c, vec3( 1.0 / 2.4 ) ) - 0.055;
	return mix( lo, hi, step( vec3( 0.0031308 ), c ) );
}
`;

// kRandom, source/Shaders.cpp
export const RANDOM = `
uvec4 pcg4d( uvec4 v )
{
	v = v * 1664525u + 1013904223u;
	v.x += v.y * v.w;
	v.y += v.z * v.x;
	v.z += v.x * v.y;
	v.w += v.y * v.z;
	v ^= v >> 16u;
	v.x += v.y * v.w;
	v.y += v.z * v.x;
	v.z += v.x * v.y;
	v.w += v.y * v.z;
	return v;
}
vec4 toUniform( uvec4 h )
{
	return ( vec4( h >> 8u ) + 0.5 ) * ( 1.0 / 16777216.0 );
}
`;

// kRateFragment, source/Shaders.cpp
export const RATE_FRAGMENT = `
uniform sampler2D InputTexture;
uniform int Premultiply;// 1 the plugin; 0 the negative control
out vec4 FragColor;
void main()
{
	vec4 src  = texelFetch( InputTexture, ivec2( gl_FragCoord.xy ), 0 );
	float lum = dot( toLinear( src.rgb ), vec3( 0.2126, 0.7152, 0.0722 ) );
	FragColor = vec4( Premultiply == 1 ? lum * clamp( src.a, 0.0, 1.0 ) : lum );
}
`;

// kReduceFragment, source/Shaders.cpp
export const REDUCE_FRAGMENT = `
uniform sampler2D Source;
uniform ivec2 SourceSize;
uniform int Block;
uniform float Scale;
out vec4 FragColor;
void main()
{
	ivec2 origin = ivec2( gl_FragCoord.xy ) * Block;
	float total  = 0.0;
	for( int y = 0; y < Block; ++y )
		for( int x = 0; x < Block; ++x )
		{
			ivec2 q = origin + ivec2( x, y );
			if( q.x < SourceSize.x && q.y < SourceSize.y )
				total += texelFetch( Source, q, 0 ).r;
		}
	FragColor = vec4( total * Scale );
}
`;

// kControlFragment, source/Shaders.cpp
export const CONTROL_FRAGMENT = `
uniform sampler2D Total;   // the frame's sum of Y x alpha, one texel
uniform sampler2D Previous;// last frame's state
uniform float PixelCount;
uniform float LuxPerY;     // photocathode lux per unit Y x alpha
uniform float EbiLux;
uniform float SensitivityArea;// S x the tube's area, A per lux
uniform float CurrentLimit;   // A
uniform float MinDuty;
uniform float Gain;           // (cd/m^2)/lx
uniform float MaxLuminance;   // cd/m^2
uniform float AbcDecay;       // exp( -dt / tau )
uniform int Fresh;            // 1: no history, start settled
uniform int AbcOn;
uniform int BspOn;
out vec4 FragColor;
void main()
{
	float mean    = texelFetch( Total, ivec2( 0 ), 0 ).r / PixelCount * LuxPerY + EbiLux;
	float current = SensitivityArea * mean;
	float duty    = BspOn == 1 ? clamp( CurrentLimit / max( current, 1e-30 ), MinDuty, 1.0 ) : 1.0;
	float screen  = Gain * duty * mean;
	float target  = AbcOn == 1 ? min( 0.0, log( MaxLuminance ) - log( max( screen, 1e-30 ) ) ) : 0.0;
	float logGain = target;
	if( Fresh == 0 )
		logGain = target + ( texelFetch( Previous, ivec2( 0 ), 0 ).a - target ) * AbcDecay;
	FragColor = vec4( exp( logGain ), duty, mean, logGain );
}
`;

// kHaloFragment, source/Shaders.cpp
export const HALO_FRAGMENT = `
uniform sampler2D Coarse;
uniform ivec2 CoarseSize;
uniform int Radius;
uniform float Weights[ 169 ];
out vec4 FragColor;
void main()
{
	ivec2 c     = ivec2( gl_FragCoord.xy );
	int side    = 2 * Radius + 1;
	float total = 0.0;
	for( int j = -Radius; j <= Radius; ++j )
		for( int i = -Radius; i <= Radius; ++i )
		{
			ivec2 q = c + ivec2( i, j );
			if( q.x >= 0 && q.y >= 0 && q.x < CoarseSize.x && q.y < CoarseSize.y )
				total += Weights[ ( j + Radius ) * side + ( i + Radius ) ] * texelFetch( Coarse, q, 0 ).r;
		}
	FragColor = vec4( total );
}
`;

// kSamplers, source/Shaders.cpp
export const SAMPLERS = `
uniform uint FrameIndex;
uniform uint Seed;
uniform int RngModel;// 0 the plugin; 1 the frame left out; 2 a seed of x + y; 3 every instance the same
uvec4 hashAt( ivec2 p, uint call )
{
	uint seed = RngModel == 3 ? 0u : Seed;
	uvec4 key = uvec4( uint( p.x ), uint( p.y ), RngModel == 1 ? 0u : FrameIndex, seed * 256u + call );
	if( RngModel == 2 )
		key = uvec4( uint( p.x + p.y ), 0u, FrameIndex, seed * 256u + call );
	return pcg4d( key );
}
vec4 uniformsAt( ivec2 p, uint call )
{
	return toUniform( hashAt( p, call ) );
}

const float kLogFactorial[ 16 ] = float[ 16 ]( 0.0, 0.0, 0.693147181, 1.79175947, 3.17805383, 4.78749174, 6.57925121, 8.52516136,
                                               10.6046029, 12.8018275, 15.1044126, 17.5023078, 19.9872145, 22.5521639, 25.1912212, 27.8992714 );

// ln( e^-lam lam^k / k! ). Above 15, Stirling's series with
// k ln( lam / k ) + k - lam written as -lam h( t ), k = lam ( 1 + t ),
// h( t ) = ( 1 + t ) ln( 1 + t ) - t: a series near t = 0.
float logPoissonPmf( float k, float lam )
{
	if( k < 16.0 )
		return -lam + k * log( lam ) - kLogFactorial[ int( k ) ];
	float t = ( k - lam ) / lam;
	float h;
	if( abs( t ) < 0.25 )
		h = t * t * ( 0.5 + t * ( -1.0 / 6.0 + t * ( 1.0 / 12.0 + t * ( -1.0 / 20.0 + t * ( 1.0 / 30.0 + t * ( -1.0 / 42.0 + t * ( 1.0 / 56.0 - t / 72.0 ) ) ) ) ) ) );
	else
		h = ( 1.0 + t ) * log( 1.0 + t ) - t;
	float ik = 1.0 / k;
	return -lam * h - 0.5 * log( 6.28318531 * k ) - ik * ( 1.0 / 12.0 - ik * ik * ( 1.0 / 360.0 - ik * ik / 1260.0 ) );
}

float poissonTiny( float lam, ivec2 p )
{
	// P( N >= 1 ) = -expm1( -lam ), compared against a whole 32-bit hash.
	uvec4 h      = hashAt( p, 0u );
	float pAny   = lam * ( 1.0 - lam * ( 0.5 - lam / 6.0 ) );
	uint against = uint( pAny * 4294967296.0 );
	if( h.x >= against )
		return 0.0;
	// Given N >= 1: P( N = k ) = lam^k / k! / expm1( lam ).
	float u   = ( float( h.y >> 8u ) + 0.5 ) * ( 1.0 / 16777216.0 );
	float em1 = lam * ( 1.0 + lam * ( 0.5 + lam / 6.0 ) );
	float pk  = lam / em1;
	float cdf = pk;
	float k   = 1.0;
	while( u > cdf && k < 8.0 )
	{
		k += 1.0;
		pk *= lam / k;
		float next = cdf + pk;
		if( next == cdf )
			break;
		cdf = next;
	}
	return k;
}

float poissonInvert( float lam, ivec2 p )
{
	float u   = uniformsAt( p, 1u ).x;
	float pk  = exp( -lam );
	float cdf = pk;
	float k   = 0.0;
	while( u > cdf && k < 64.0 )
	{
		k += 1.0;
		pk *= lam / k;
		float next = cdf + pk;
		if( next == cdf )
			break;
		cdf = next;
	}
	return k;
}

float poissonPtrs( float lam, ivec2 p )
{
	float slam     = sqrt( lam );
	float b        = 0.931 + 2.53 * slam;
	float a        = -0.059 + 0.02483 * b;
	float invalpha = 1.1239 + 1.1328 / ( b - 3.4 );
	float vr       = 0.9277 - 3.6224 / ( b - 2.0 );
	float k        = floor( lam );
	for( uint call = 0u; call < 32u; ++call )
	{
		vec4 r = uniformsAt( p, 2u + call );
		for( int pair = 0; pair < 2; ++pair )
		{
			float U  = ( pair == 0 ? r.x : r.z ) - 0.5;
			float V  = pair == 0 ? r.y : r.w;
			float us = 0.5 - abs( U );
			k        = floor( ( 2.0 * a / us + b ) * U + lam + 0.43 );
			if( us >= 0.07 && V <= vr )
				return k;
			if( k < 0.0 || ( us < 0.013 && V > us ) )
				continue;
			if( log( V ) + log( invalpha ) - log( a / ( us * us ) + b ) <= logPoissonPmf( k, lam ) )
				return k;
		}
	}
	return max( k, 0.0 );
}

float poisson( float lam, ivec2 p )
{
	if( lam < 1e-3 )
		return poissonTiny( lam, p );
	if( lam < 10.0 )
		return poissonInvert( lam, p );
	return poissonPtrs( lam, p );
}

float standardNormal( ivec2 p, uint call )
{
	vec4 r = uniformsAt( p, call );
	return sqrt( -2.0 * log( r.x ) ) * cos( 6.28318531 * r.y );
}

// Gamma( alpha, 1 ), alpha >= 1.
float gammaMT( float alpha, ivec2 p )
{
	float d = alpha - 1.0 / 3.0;
	float c = inversesqrt( 9.0 * d );
	float v = 1.0;
	for( uint call = 0u; call < 16u; ++call )
	{
		vec4 r  = uniformsAt( p, 40u + call );
		float x = sqrt( -2.0 * log( r.x ) ) * cos( 6.28318531 * r.y );
		float w = c * x;
		if( w <= -1.0 )
			continue;
		v        = ( 1.0 + w ) * ( 1.0 + w ) * ( 1.0 + w );
		float x2 = x * x;
		if( r.z < 1.0 - 0.0331 * x2 * x2 )
			return d * v;
		// 0.5 x^2 + d ( 1 - v + ln v ), with the x^2 terms cancelled exactly.
		float rhs;
		if( abs( w ) < 0.1 )
			rhs = d * w * w * w * w * ( -0.75 + w * ( 0.6 + w * ( -0.5 + w * ( 3.0 / 7.0 - 0.375 * w ) ) ) );
		else
			rhs = 0.5 * x2 + d * ( 1.0 - v + log( v ) );
		if( log( r.z ) < rhs )
			return d * v;
	}
	return d * v;
}

// The plate's charge for n photoelectrons, in units of the mean gain.
uniform int GainModel;// 0 the plugin (exponential); 1 a saturated plate (Polya, shape 4); 2 a constant gain
float charge( float n, ivec2 p )
{
	if( n <= 0.0 )
		return 0.0;
	if( GainModel == 2 )
		return n;
	if( GainModel == 1 )
		return gammaMT( 4.0 * n, p ) * 0.25;
	if( n <= 4.0 )
	{
		vec4 r  = uniformsAt( p, 40u );
		float q = -log( r.x );
		if( n > 1.0 )
			q -= log( r.y );
		if( n > 2.0 )
			q -= log( r.z );
		if( n > 3.0 )
			q -= log( r.w );
		return q;
	}
	return gammaMT( n, p );
}
`;

// kCountFragment, source/Shaders.cpp
export const COUNT_FRAGMENT = `
uniform sampler2D Rate;
uniform sampler2D Halo;
uniform sampler2D Control;
uniform ivec2 HaloSize;
uniform float HaloFactor;
uniform float RateScale;   // photoelectrons per pixel per frame per unit Y x alpha
uniform float EbiRate;     // photoelectrons per pixel per frame from the EBI
uniform float HaloFraction;// eta
uniform int SamplerModel;  // 0 the plugin; 1 a rounded normal; 2 at most one; 3 the rate jittered
out vec4 FragColor;

// The coarse halo at this pixel, bilinear by hand (the same arithmetic on
// every rasteriser).
float haloAt( ivec2 p )
{
	vec2 c   = ( vec2( p ) + 0.5 ) / HaloFactor - 0.5;
	vec2 f0  = floor( c );
	vec2 t   = c - f0;
	ivec2 i0 = ivec2( f0 );
	ivec2 hi = HaloSize - 1;
	ivec2 a  = clamp( i0, ivec2( 0 ), hi );
	ivec2 b  = clamp( i0 + 1, ivec2( 0 ), hi );
	float h00 = texelFetch( Halo, ivec2( a.x, a.y ), 0 ).r;
	float h10 = texelFetch( Halo, ivec2( b.x, a.y ), 0 ).r;
	float h01 = texelFetch( Halo, ivec2( a.x, b.y ), 0 ).r;
	float h11 = texelFetch( Halo, ivec2( b.x, b.y ), 0 ).r;
	return mix( mix( h00, h10, t.x ), mix( h01, h11, t.x ), t.y );
}

void main()
{
	ivec2 p    = ivec2( gl_FragCoord.xy );
	float duty = texelFetch( Control, ivec2( 0 ), 0 ).g;
	float y    = texelFetch( Rate, p, 0 ).r;
	float lam  = duty * ( RateScale * ( ( 1.0 - HaloFraction ) * y + HaloFraction * haloAt( p ) ) + EbiRate );
	float drawn = lam;
	if( SamplerModel == 3 )
		drawn = lam * max( 0.0, 1.0 + 0.3 * standardNormal( p, 60u ) );
	float n = poisson( drawn, p );
	if( SamplerModel == 1 )
		n = max( 0.0, floor( lam + sqrt( lam ) * standardNormal( p, 61u ) + 0.5 ) );
	if( SamplerModel == 2 )
		n = min( n, 1.0 );
	FragColor = vec4( n, charge( n, p ), lam, 0.0 );
}
`;

// kPhosphorFragment, source/Shaders.cpp
export const PHOSPHOR_FRAGMENT = `
uniform sampler2D Counts;
uniform sampler2D Store;
uniform vec4 Decay;// a, 1 - a, b, 1 - b
out vec4 FragColor;
void main()
{
	ivec2 p   = ivec2( gl_FragCoord.xy );
	float q   = texelFetch( Counts, p, 0 ).g;
	float s   = texelFetch( Store, p, 0 ).r;
	FragColor = vec4( s * Decay.x + q * Decay.z, s * Decay.y + q * Decay.w, 0.0, 0.0 );
}
`;

// kResampleFragment, source/Shaders.cpp
export const RESAMPLE_FRAGMENT = `
uniform sampler2D OldStore;
uniform ivec2 OldSize;
uniform vec2 Ratio;// old pixels per new pixel, each axis
out vec4 FragColor;
void main()
{
	vec2 p   = floor( gl_FragCoord.xy );
	vec2 lo  = p * Ratio;
	vec2 hi  = ( p + 1.0 ) * Ratio;
	ivec2 i0 = clamp( ivec2( floor( lo ) ), ivec2( 0 ), OldSize - 1 );
	ivec2 i1 = clamp( ivec2( ceil( hi ) ) - 1, ivec2( 0 ), OldSize - 1 );
	i1       = min( i1, i0 + 63 );
	float total = 0.0;
	for( int y = i0.y; y <= i1.y; ++y )
		for( int x = i0.x; x <= i1.x; ++x )
		{
			float ox = min( hi.x, float( x + 1 ) ) - max( lo.x, float( x ) );
			float oy = min( hi.y, float( y + 1 ) ) - max( lo.y, float( y ) );
			total += max( ox, 0.0 ) * max( oy, 0.0 ) * texelFetch( OldStore, ivec2( x, y ), 0 ).r;
		}
	FragColor = vec4( total, 0.0, 0.0, 0.0 );
}
`;

// kCompositeFragment, source/Shaders.cpp
export const COMPOSITE_FRAGMENT = `
uniform sampler2D InputTexture;
uniform sampler2D Store;
uniform sampler2D Control;
uniform ivec2 ViewOrigin;
uniform ivec2 Size;
uniform float Spot[ 5 ];
uniform float DisplayScale;
uniform vec3 PhosphorRGB;
uniform float MixAmount;
uniform int AlphaModel;// 0 the plugin; 1 the clip's alpha passed through
uniform float CellPx;
uniform float BoundaryHalf;
uniform float PatternDepth;
uniform float PatternSpread;
uniform vec2 FrameCentre;
out vec4 FragColor;

// The fibre-optic inverter's transmission: a triangular lattice of
// hexagonal multifibres, CellPx flat to flat; the boundary strip, box-filtered
// across the pixel, passes PatternDepth less; each cell differs by up to
// PatternSpread.
float honeycomb( ivec2 at )
{
	if( PatternDepth <= 0.0 && PatternSpread <= 0.0 )
		return 1.0;
	vec2 q      = ( vec2( at ) + 0.5 - FrameCentre ) / CellPx;
	float j     = q.y / 0.866025404;
	float i     = q.x - 0.5 * j;
	vec2 base   = floor( vec2( i, j ) );
	vec2 best   = base;
	float bestD = 1e30;
	for( int k = 0; k < 4; ++k )
	{
		vec2 c  = base + vec2( float( k & 1 ), float( k >> 1 ) );
		vec2 pc = vec2( c.x + 0.5 * c.y, 0.866025404 * c.y );
		float d = dot( q - pc, q - pc );
		if( d < bestD )
		{
			bestD = d;
			best  = c;
		}
	}
	const vec2 kNeighbour[ 6 ] = vec2[ 6 ]( vec2( 1.0, 0.0 ), vec2( -1.0, 0.0 ), vec2( 0.0, 1.0 ), vec2( 0.0, -1.0 ), vec2( 1.0, -1.0 ), vec2( -1.0, 1.0 ) );
	float edge = 1e30;
	for( int k = 0; k < 6; ++k )
	{
		vec2 n  = best + kNeighbour[ k ];
		vec2 pn = vec2( n.x + 0.5 * n.y, 0.866025404 * n.y );
		edge    = min( edge, 0.5 * ( dot( q - pn, q - pn ) - bestD ) );
	}
	float e     = edge * CellPx;
	float cover = clamp( min( e + 0.5, BoundaryHalf ) - max( e - 0.5, -BoundaryHalf ), 0.0, 1.0 );
	uvec4 h     = pcg4d( uvec4( uint( int( best.x ) + 32768 ), uint( int( best.y ) + 32768 ), 0x51ed2705u, 0x0b5e55edu ) );
	float r     = float( h.x >> 8u ) / 8388607.5 - 1.0;
	return ( 1.0 - PatternDepth * cover ) * ( 1.0 + PatternSpread * r );
}

void main()
{
	ivec2 at = ivec2( gl_FragCoord.xy ) - ViewOrigin;
	vec4 src = texelFetch( InputTexture, at, 0 );
	if( MixAmount <= 0.0 )
	{
		FragColor = src;
		return;
	}
	float gain    = texelFetch( Control, ivec2( 0 ), 0 ).r;
	float emitted = 0.0;
	for( int j = -2; j <= 2; ++j )
		for( int i = -2; i <= 2; ++i )
		{
			ivec2 q = clamp( at + ivec2( i, j ), ivec2( 0 ), Size - 1 );
			emitted += Spot[ i + 2 ] * Spot[ j + 2 ] * texelFetch( Store, q, 0 ).g;
		}
	float shown = emitted * DisplayScale * gain * honeycomb( at );
	vec3 rgb    = toCode( shown * PhosphorRGB );
	float outA  = AlphaModel == 1 ? src.a : 1.0;
	FragColor   = vec4( mix( src.rgb, rgb, MixAmount ), mix( src.a, outA, MixAmount ) );
}
`;
