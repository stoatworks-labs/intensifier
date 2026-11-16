#include "Intensifier.h"

#include "Diag.h"
#include "GLState.h"
#include "Shaders.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <string>

using namespace ffglex;

namespace intensifier
{
namespace
{
/// Frames that must agree before the host's clock unit is settled.
constexpr int kClockVotes = 4;
/// Frames of advancing host time after which an unsettled vote is decided by
/// the size of the host's step alone (UpdateClock).
constexpr int kClockPatience = 30;
/// Host seconds a single frame may advance by.
constexpr double kMaxFrameDelta = 0.1;
/// The reduction's block, each axis.
constexpr int kBlock = 16;

/// Every instance its own sparks: a process-wide counter.
std::atomic< uint32_t > g_nextSeed { 0 };

double wallSeconds()
{
	using namespace std::chrono;
	static const steady_clock::time_point start = steady_clock::now();
	return duration_cast< duration< double > >( steady_clock::now() - start ).count();
}

std::string glStringOrUnknown( GLenum name )
{
	const GLubyte* value = glGetString( name );
	return value ? reinterpret_cast< const char* >( value ) : "unknown";
}

int ceilDiv( int a, int b )
{
	return ( a + b - 1 ) / b;
}
} // namespace

static_assert( PT_COUNT - PT_ABOUT_TEXT == stoatworks::about::kParamCount, "the About run no longer matches StoatworksAbout.h" );

//---------------------------------------------------------------------------
IntensifierPlugin::IntensifierPlugin()
{
	SetMinInputs( 1 );
	SetMaxInputs( 1 );

	//The phosphor's decay and the ABC's lag are in seconds, and each frame
	//counts photoelectrons over the frame's length.
	SetTimeSupported( true );

	seed = ++g_nextSeed;

	for( unsigned int id = 0; id < PT_ABOUT_TEXT; ++id )
		params[ id ] = InfoOf( id ).defaultValue;

	for( unsigned int id = 0; id < PT_ABOUT_TEXT; ++id )
	{
		const ControlInfo& info = InfoOf( id );
		switch( info.kind )
		{
		case Kind::Standard: SetParamInfo( id, info.name, FF_TYPE_STANDARD, params[ id ] ); break;
		case Kind::Option:
			SetOptionParamInfo( id, info.name, static_cast< unsigned int >( info.optionCount ), params[ id ] );
			for( int i = 0; i < info.optionCount; ++i )
				SetParamElementInfo( id, static_cast< unsigned int >( i ), info.options[ i ], static_cast< float >( i ) );
			break;
		}
		SetParamGroup( id, info.group );
	}

	// The About block, last. Inline rather than through a helper: SetParamInfo
	// is protected on CFFGLPlugin, so nothing outside the class can call it.
	SetParamInfo( PT_ABOUT_TEXT, "About", FF_TYPE_TEXT, stoatworks::about::defaultText() );
	{
		FFUInt32 aboutId = PT_ABOUT_TEXT + 1;
		for( const auto& b : stoatworks::about::buttons() )
			SetParamInfo( aboutId++, b.label, FF_TYPE_EVENT, false );
	}
	for( FFUInt32 i = PT_ABOUT_TEXT; i < PT_COUNT; ++i )
		SetParamGroup( i, "About" );
}

//---------------------------------------------------------------------------
GLint IntensifierPlugin::where( const FFGLShader& shader, const char* name )
{
	//Keyed by the literal's address: every name here is a string literal.
	const auto key = std::make_pair( shader.GetGLID(), name );
	for( const auto& entry : uniformCache )
		if( entry.first == key )
			return entry.second;
	const GLint location = glGetUniformLocation( shader.GetGLID(), name );
	uniformCache.emplace_back( key, location );
	return location;
}

//---------------------------------------------------------------------------
FFResult IntensifierPlugin::InitGL( const FFGLViewportStruct* vp )
{
	diag::init();
	diag::info( std::string( "GL vendor=" ) + glStringOrUnknown( GL_VENDOR ) + " renderer=" + glStringOrUnknown( GL_RENDERER )
	            + " version=" + glStringOrUnknown( GL_VERSION ) );

	struct Stage
	{
		FFGLShader* shader;
		shaders::Pass pass;
		const char* name;
	};
	const Stage stages[] = {
		{ &rateShader, shaders::Pass::Rate, "rate" },
		{ &reduceShader, shaders::Pass::Reduce, "reduce" },
		{ &controlShader, shaders::Pass::Control, "control" },
		{ &haloShader, shaders::Pass::Halo, "halo" },
		{ &countShader, shaders::Pass::Count, "count" },
		{ &phosphorShader, shaders::Pass::Phosphor, "phosphor" },
		{ &resampleShader, shaders::Pass::Resample, "resample" },
		{ &compositeShader, shaders::Pass::Composite, "composite" },
	};
	for( const Stage& stage : stages )
	{
		const std::string vertex   = shaders::AssembleVertex( stage.pass );
		const std::string fragment = shaders::Assemble( stage.pass );
		if( stage.shader->Compile( vertex.c_str(), fragment.c_str() ) )
			continue;
		//Returning FF_FAIL is invisible to the operator: the plugin simply
		//does nothing in Resolume. These lines are the only record of which
		//pass it was.
		diag::error( std::string( "the " ) + stage.name + " shader failed to compile - the plugin will do nothing" );
		FFGLLog::LogToHost( "Intensifier: shader failed to compile" );
		DeInitGL();
		return FF_FAIL;
	}

	//A core profile draws nothing without a vertex array bound; the
	//triangle needs no attributes.
	glGenVertexArrays( 1, &vao );
	haveControl = false;
	diag::info( "initialised" );
	return CFFGLPlugin::InitGL( vp );
}

//---------------------------------------------------------------------------
Settings IntensifierPlugin::CurrentSettings() const
{
	Settings s;
	s.sceneLux     = LightFromParam( params[ PT_LIGHT ] );
	s.photocathode = OptionIndex( params[ PT_PHOTOCATHODE ], 2 );
	s.gainFLfc     = GainFromParam( params[ PT_GAIN ] );
	s.phosphor     = OptionIndex( params[ PT_PHOSPHOR ], 2 );
	s.gap          = GapFromParam( params[ PT_GAP ] );
	s.fixedPattern = FixedFromParam( params[ PT_FIXED ] );
	return s;
}

//---------------------------------------------------------------------------
bool IntensifierPlugin::ensureBuffers( int width, int height )
{
	using S = PassBuffer::Sampling;
	bool ok = rate.Ensure( width, height, GL_R32F, S::Nearest );

	//The reduction: 16 x 16 blocks down to one texel.
	levels = 0;
	int w = width, h = height;
	while( ( w > 1 || h > 1 ) && levels < kMaxLevels )
	{
		w = ceilDiv( w, kBlock );
		h = ceilDiv( h, kBlock );
		ok = ok && reduce[ levels ].Ensure( w, h, GL_R32F, S::Nearest );
		++levels;
	}

	const int f = frame.haloFactor;
	if( f > 1 )
		ok = ok && coarse.Ensure( ceilDiv( width, f ), ceilDiv( height, f ), GL_R32F, S::Nearest );
	ok = ok && halo.Ensure( ceilDiv( width, f ), ceilDiv( height, f ), GL_R32F, S::Nearest );
	ok = ok && control[ 0 ].Ensure( 1, 1, GL_RGBA32F, S::Nearest ) && control[ 1 ].Ensure( 1, 1, GL_RGBA32F, S::Nearest );
	ok = ok && counts.Ensure( width, height, GL_RGBA32F, S::Nearest );
	return ok;
}

void IntensifierPlugin::draw( PassBuffer& target )
{
	glBindFramebuffer( GL_FRAMEBUFFER, target.GetGLID() );
	glViewport( 0, 0, static_cast< GLsizei >( target.GetWidth() ), static_cast< GLsizei >( target.GetHeight() ) );
	glDrawArrays( GL_TRIANGLES, 0, 3 );
}

//---------------------------------------------------------------------------
void IntensifierPlugin::render( const FFGLTextureStruct* input, const GLint* hostViewport, GLuint hostFBO, int width, int height )
{
	using S = PassBuffer::Sampling;
	if( !ensureBuffers( width, height ) )
	{
		diag::error( "could not allocate the passes' buffers" );
		return;
	}
	glBindVertexArray( vao );
	const int f       = frame.haloFactor;
	const int coarseW = ceilDiv( width, f ), coarseH = ceilDiv( height, f );

	//The phosphor's store, carried across a change of raster: resampled into
	//a fresh buffer before the old one is released (photofinish's trap).
	if( storeWidth > 0 && ( storeWidth != width || storeHeight != height ) )
	{
		if( hooks.resizeResets )
		{
			for( PassBuffer& b : store )
				b.Destroy();
			storeWidth = storeHeight = 0;
			haveControl              = false;
		}
		else
		{
			const int spare = ( storeCurrent + 1 ) % 3;
			if( store[ spare ].Ensure( width, height, GL_RG32F, S::Nearest ) )
			{
				glUseProgram( resampleShader.GetGLID() );
				bindUnit( 0, store[ storeCurrent ].TextureID() );
				glUniform1i( where( resampleShader, "OldStore" ), 0 );
				glUniform2i( where( resampleShader, "OldSize" ), storeWidth, storeHeight );
				glUniform2f( where( resampleShader, "Ratio" ), static_cast< float >( static_cast< double >( storeWidth ) / width ),
				             static_cast< float >( static_cast< double >( storeHeight ) / height ) );
				draw( store[ spare ] );
				storeCurrent = spare;
			}
		}
	}
	const int next = ( storeCurrent + 1 ) % 3;
	if( !store[ storeCurrent ].Ensure( width, height, GL_RG32F, S::Nearest ) || !store[ next ].Ensure( width, height, GL_RG32F, S::Nearest ) )
	{
		diag::error( "could not allocate the phosphor's store" );
		return;
	}
	storeWidth  = width;
	storeHeight = height;

	//1. rate
	glUseProgram( rateShader.GetGLID() );
	bindUnit( 0, input->Handle );
	glUniform1i( where( rateShader, "InputTexture" ), 0 );
	glUniform1i( where( rateShader, "Premultiply" ), hooks.premultiply ? 1 : 0 );
	draw( rate );

	//2. reduce, to one texel
	glUseProgram( reduceShader.GetGLID() );
	glUniform1i( where( reduceShader, "Source" ), 0 );
	glUniform1i( where( reduceShader, "Block" ), kBlock );
	glUniform1f( where( reduceShader, "Scale" ), 1.0f );
	{
		GLuint source = rate.TextureID();
		int w = width, h = height;
		for( int level = 0; level < levels; ++level )
		{
			bindUnit( 0, source );
			glUniform2i( where( reduceShader, "SourceSize" ), w, h );
			draw( reduce[ level ] );
			source = reduce[ level ].TextureID();
			w      = static_cast< int >( reduce[ level ].GetWidth() );
			h      = static_cast< int >( reduce[ level ].GetHeight() );
		}
	}
	const GLuint total = levels > 0 ? reduce[ levels - 1 ].TextureID() : rate.TextureID();

	//3. control
	const int previous = controlCurrent;
	controlCurrent     = 1 - controlCurrent;
	glUseProgram( controlShader.GetGLID() );
	bindUnit( 0, total );
	bindUnit( 1, control[ previous ].TextureID() );
	glUniform1i( where( controlShader, "Total" ), 0 );
	glUniform1i( where( controlShader, "Previous" ), 1 );
	glUniform1f( where( controlShader, "PixelCount" ), static_cast< float >( static_cast< double >( width ) * height ) );
	glUniform1f( where( controlShader, "LuxPerY" ), static_cast< float >( frame.luxPerY ) );
	glUniform1f( where( controlShader, "EbiLux" ), static_cast< float >( frame.ebiLux ) );
	glUniform1f( where( controlShader, "SensitivityArea" ), static_cast< float >( frame.sensitivity * tube::kTubeArea ) );
	glUniform1f( where( controlShader, "CurrentLimit" ), static_cast< float >( tube::kCathodeCurrentLimit ) );
	glUniform1f( where( controlShader, "MinDuty" ), static_cast< float >( tube::kMinDuty ) );
	glUniform1f( where( controlShader, "Gain" ), static_cast< float >( frame.gain ) );
	glUniform1f( where( controlShader, "MaxLuminance" ), static_cast< float >( tube::kMaxLuminance ) );
	glUniform1f( where( controlShader, "AbcDecay" ), static_cast< float >( frame.abcDecay ) );
	glUniform1i( where( controlShader, "Fresh" ), haveControl ? 0 : 1 );
	glUniform1i( where( controlShader, "AbcOn" ), hooks.abcOn ? 1 : 0 );
	glUniform1i( where( controlShader, "BspOn" ), hooks.bspOn ? 1 : 0 );
	draw( control[ controlCurrent ] );
	haveControl = true;

	//4. halo: the coarse grid's block means, then the gather
	GLuint coarseTexture = rate.TextureID();
	if( f > 1 )
	{
		glUseProgram( reduceShader.GetGLID() );
		bindUnit( 0, rate.TextureID() );
		glUniform2i( where( reduceShader, "SourceSize" ), width, height );
		glUniform1i( where( reduceShader, "Block" ), f );
		glUniform1f( where( reduceShader, "Scale" ), static_cast< float >( 1.0 / ( static_cast< double >( f ) * f ) ) );
		draw( coarse );
		coarseTexture = coarse.TextureID();
	}
	glUseProgram( haloShader.GetGLID() );
	bindUnit( 0, coarseTexture );
	glUniform1i( where( haloShader, "Coarse" ), 0 );
	glUniform2i( where( haloShader, "CoarseSize" ), coarseW, coarseH );
	glUniform1i( where( haloShader, "Radius" ), kernel.radius );
	glUniform1fv( where( haloShader, "Weights" ), static_cast< GLsizei >( kernel.weights.size() ), kernel.weights.data() );
	draw( halo );

	//5. count
	glUseProgram( countShader.GetGLID() );
	bindUnit( 0, rate.TextureID() );
	bindUnit( 1, halo.TextureID() );
	bindUnit( 2, control[ controlCurrent ].TextureID() );
	glUniform1i( where( countShader, "Rate" ), 0 );
	glUniform1i( where( countShader, "Halo" ), 1 );
	glUniform1i( where( countShader, "Control" ), 2 );
	glUniform2i( where( countShader, "HaloSize" ), coarseW, coarseH );
	glUniform1f( where( countShader, "HaloFactor" ), static_cast< float >( f ) );
	glUniform1f( where( countShader, "RateScale" ), static_cast< float >( frame.rate ) );
	glUniform1f( where( countShader, "EbiRate" ), static_cast< float >( frame.ebiRate ) );
	glUniform1f( where( countShader, "HaloFraction" ), hooks.haloOn ? static_cast< float >( tube::kHaloFraction ) : 0.0f );
	glUniform1i( where( countShader, "SamplerModel" ), hooks.samplerModel );
	glUniform1i( where( countShader, "GainModel" ), hooks.gainModel );
	glUniform1i( where( countShader, "RngModel" ), hooks.rngModel );
	glUniform1ui( where( countShader, "FrameIndex" ), frameIndex );
	glUniform1ui( where( countShader, "Seed" ), seed );
	draw( counts );

	//6. phosphor
	glUseProgram( phosphorShader.GetGLID() );
	bindUnit( 0, counts.TextureID() );
	bindUnit( 1, store[ storeCurrent ].TextureID() );
	glUniform1i( where( phosphorShader, "Counts" ), 0 );
	glUniform1i( where( phosphorShader, "Store" ), 1 );
	glUniform4f( where( phosphorShader, "Decay" ), static_cast< float >( frame.decayA ), static_cast< float >( frame.oneMinusA ),
	             static_cast< float >( frame.decayB ), static_cast< float >( frame.oneMinusB ) );
	draw( store[ next ] );
	storeCurrent = next;

	//7. composite, into the host's framebuffer and viewport
	glBindFramebuffer( GL_FRAMEBUFFER, hostFBO );
	glViewport( hostViewport[ 0 ], hostViewport[ 1 ], hostViewport[ 2 ], hostViewport[ 3 ] );
	glUseProgram( compositeShader.GetGLID() );
	bindUnit( 0, input->Handle );
	bindUnit( 1, store[ storeCurrent ].TextureID() );
	bindUnit( 2, control[ controlCurrent ].TextureID() );
	glUniform1i( where( compositeShader, "InputTexture" ), 0 );
	glUniform1i( where( compositeShader, "Store" ), 1 );
	glUniform1i( where( compositeShader, "Control" ), 2 );
	glUniform2i( where( compositeShader, "ViewOrigin" ), hostViewport[ 0 ], hostViewport[ 1 ] );
	glUniform2i( where( compositeShader, "Size" ), width, height );
	float spot[ 5 ];
	SpotWeights( frame.sigmaPx, spot );
	glUniform1fv( where( compositeShader, "Spot" ), 5, spot );
	glUniform1f( where( compositeShader, "DisplayScale" ), static_cast< float >( frame.displayScale ) );
	double rgb[ 3 ];
	PhosphorColour( CurrentSettings().phosphor, rgb );
	if( hooks.whiteScreen )
		rgb[ 0 ] = rgb[ 1 ] = rgb[ 2 ] = 1.0;
	glUniform3f( where( compositeShader, "PhosphorRGB" ), static_cast< float >( rgb[ 0 ] ), static_cast< float >( rgb[ 1 ] ), static_cast< float >( rgb[ 2 ] ) );
	glUniform1f( where( compositeShader, "MixAmount" ), std::clamp( params[ PT_MIX ], 0.0f, 1.0f ) );
	glUniform1i( where( compositeShader, "AlphaModel" ), hooks.alphaModel );
	const double fixedDepth = FixedFromParam( params[ PT_FIXED ] );
	glUniform1f( where( compositeShader, "CellPx" ), static_cast< float >( frame.cellPx ) );
	glUniform1f( where( compositeShader, "BoundaryHalf" ), static_cast< float >( frame.boundaryHalfPx ) );
	glUniform1f( where( compositeShader, "PatternDepth" ), static_cast< float >( fixedDepth * tube::kBoundaryDepth ) );
	glUniform1f( where( compositeShader, "PatternSpread" ), static_cast< float >( fixedDepth * tube::kCellSpread ) );
	glUniform2f( where( compositeShader, "FrameCentre" ), 0.5f * static_cast< float >( width ), 0.5f * static_cast< float >( height ) );
	glDrawArrays( GL_TRIANGLES, 0, 3 );

	unbindTextureUnits( 3 );
	glActiveTexture( GL_TEXTURE0 );
	glUseProgram( 0 );
}

//---------------------------------------------------------------------------
void IntensifierPlugin::UpdateClock()
{
	const double wallNow = wallSeconds();
	if( wallStart < 0.0 )
		wallStart = wallNow;
	const double raw = hostTime;

	//Resolume has been seen sending both seconds and milliseconds through
	//SetTime, and nothing in the call says which. Vote on it against the wall
	//clock for a few frames, then stop asking (ferrofluid's, by way of
	//rosette and millpond).
	if( clockScale == 0.0 && raw >= 0.0 && lastRawTime >= 0.0 && lastWallTime >= 0.0 )
	{
		const double hostDelta = raw - lastRawTime;
		const double wallDelta = wallNow - lastWallTime;
		if( hostDelta > 0.0 && wallDelta >= 0.0005 )
		{
			const double ratio = hostDelta / wallDelta;
			if( ratio > 0.1 && ratio < 10.0 )
				++secondsVotes;
			else if( ratio > 100.0 && ratio < 10000.0 )
				++millisVotes;
			if( secondsVotes >= kClockVotes || millisVotes >= kClockVotes )
			{
				clockScale  = millisVotes > secondsVotes ? 0.001 : 1.0;
				settledJump = true;//wall time to the host's own: no dt across it
			}
		}
		//A host that renders much faster than real time -- an offline render,
		//oxbow's selftest, ittest -- never wins the vote. After kClockPatience
		//frames the step's size decides: a frame is well under a second and
		//well over a millisecond (fountain's).
		if( hostDelta > 0.0 && clockScale == 0.0 && ++clockSamples >= kClockPatience )
		{
			clockScale  = hostDelta < 1.0 ? 1.0 : 0.001;
			settledJump = true;
		}
	}
	if( raw >= 0.0 )
		lastRawTime = raw;
	lastWallTime = wallNow;

	//Double throughout: Resolume's clock has been measured at ~499 million ms,
	//where a float resolves 0.03 s. Only differences are used.
	now = ( raw >= 0.0 && clockScale != 0.0 ) ? raw * clockScale : wallNow - wallStart;
	if( hooks.floatClock )
		now = static_cast< double >( static_cast< float >( now ) );
	if( ++clockFrames == 60 )
		diag::info( "host clock at frame 60: raw=" + std::to_string( raw ) + " scale=" + std::to_string( clockScale )
		            + " seconds=" + std::to_string( now ) );
}

//---------------------------------------------------------------------------
FFResult IntensifierPlugin::ProcessOpenGL( ProcessOpenGLStruct* pgl )
{
	if( pgl == nullptr || pgl->numInputTextures < 1 || pgl->inputTextures[ 0 ] == nullptr )
		return FF_FAIL;
	const FFGLTextureStruct* input = pgl->inputTextures[ 0 ];

	//The host's state, put back however this returns.
	ScopedGLState restore;
	restore.restoreOnExit     = !hooks.leakGLState;
	const GLint* hostViewport = restore.saved.viewport;
	glDisable( GL_BLEND );

	const int width  = static_cast< int >( input->Width );
	const int height = static_cast< int >( input->Height );
	if( width <= 0 || height <= 0 || vao == 0 )
		return FF_FAIL;

	UpdateClock();
	if( settledJump )
	{
		lastNow     = -1.0;
		settledJump = false;
	}
	double dt = lastNow >= 0.0 ? std::clamp( now - lastNow, 0.0, kMaxFrameDelta ) : 0.0;
	lastNow   = now;
	//To the microsecond: a clock at 4.99e8 ms and one from 0 then give the
	//same frame factors to the bit (a frame is never near a half microsecond
	//at any common rate).
	dt = std::round( dt * 1e6 ) * 1e-6;
	//A frame always counts a frame's worth: a host whose clock stands still
	//gets the last real frame's length, so the tube keeps running.
	if( dt > 0.0 )
		countingInterval = dt;

	frame = Compute( CurrentSettings(), width, height, countingInterval, hooks.tube );
	if( kernel.factor != frame.haloFactor || kernel.radiusPx != frame.haloRadiusPx )
		kernel = BuildHaloKernel( frame.haloRadiusPx, frame.haloFactor );

	render( input, hostViewport, pgl->HostFBO, width, height );
	++frameIndex;
	return FF_SUCCESS;
}

//---------------------------------------------------------------------------
FFResult IntensifierPlugin::DeInitGL()
{
	uniformCache.clear();
	for( FFGLShader* s : { &rateShader, &reduceShader, &controlShader, &haloShader, &countShader, &phosphorShader, &resampleShader, &compositeShader } )
		s->FreeGLResources();
	if( vao != 0 )
		glDeleteVertexArrays( 1, &vao );
	vao = 0;
	rate.Destroy();
	for( PassBuffer& b : reduce )
		b.Destroy();
	coarse.Destroy();
	halo.Destroy();
	for( PassBuffer& b : control )
		b.Destroy();
	counts.Destroy();
	for( PassBuffer& b : store )
		b.Destroy();
	storeWidth = storeHeight = 0;
	haveControl              = false;
	return FF_SUCCESS;
}

//---------------------------------------------------------------------------
FFResult IntensifierPlugin::SetTime( double time )
{
	hostTime = time;
	return FF_SUCCESS;
}

char* IntensifierPlugin::GetTextParameter( unsigned int index )
{
	if( index == PT_ABOUT_TEXT )
	{
		static const std::string text = stoatworks::about::textParam( 0 );
		return const_cast< char* >( text.c_str() );
	}
	return CFFGLPlugin::GetTextParameter( index );
}

FFResult IntensifierPlugin::SetTextParameter( unsigned int index, const char* value )
{
	if( index == PT_ABOUT_TEXT )
		return FF_SUCCESS;
	return CFFGLPlugin::SetTextParameter( index, value );
}

FFResult IntensifierPlugin::SetFloatParameter( unsigned int index, float value )
{
	if( index >= PT_COUNT )
		return FF_FAIL;

	// The About buttons open a browser and store nothing.
	if( index >= PT_ABOUT_TEXT )
		return stoatworks::about::handleParam( index - PT_ABOUT_TEXT, value ) ? FF_SUCCESS : FF_FAIL;

	params[ index ] = value;
	return FF_SUCCESS;
}

float IntensifierPlugin::GetFloatParameter( unsigned int index )
{
	return index < PT_COUNT ? params[ index ] : 0.0f;
}

} // namespace intensifier
