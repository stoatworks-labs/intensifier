#pragma once

#include "Controls.h"
#include "PassBuffer.h"
#include "Tube.h"

#include <FFGLSDK.h>
// After FFGLSDK.h, which is where FFUInt32 comes from.
#include "StoatworksAboutParams.h"

#include <cstdint>
#include <utility>
#include <vector>

namespace intensifier
{

/// The harness's negative controls that reach the shaders or the plugin's
/// own bookkeeping. All off in the plugin.
struct PluginHooks
{
	int samplerModel   = 0;   ///< 1 a rounded normal; 2 at most one event; 3 the rate jittered per pixel
	int gainModel      = 0;   ///< 1 a saturated plate (Polya, shape 4); 2 a constant gain
	int rngModel       = 0;   ///< 1 the frame left out of the seed; 2 a seed of x + y; 3 every instance alike
	int alphaModel     = 0;   ///< 1 the clip's alpha passed through
	bool premultiply   = true;///< false: the clip's light not weighted by its alpha
	bool abcOn         = true;
	bool bspOn         = true;
	bool haloOn        = true;
	bool resizeResets  = false;///< a new raster clears the phosphor and the ABC
	bool floatClock    = false;///< the host's clock read in single precision
	bool leakGLState   = false;///< the host's GL state not put back
	bool whiteScreen   = false;///< the screen drawn in sRGB white, not the phosphor's colour
	bool boundariesOn  = true; ///< false: the honeycomb's cells keep their spread, the boundaries are not drawn
	TubeHooks tube;
};

/**
    The plugin, `SW Intensifier`: the clip is the scene a night-vision image
    intensifier looks at. Every frame: the clock, the tube's factors on the
    CPU, then the passes of Shaders.h -- rate, reduce, control, halo, count,
    phosphor, composite.

    Host indices are the ParamIds, About last.
*/
class IntensifierPlugin : public CFFGLPlugin
{
public:
	IntensifierPlugin();

	FFResult InitGL( const FFGLViewportStruct* viewport ) override;
	FFResult ProcessOpenGL( ProcessOpenGLStruct* input ) override;
	FFResult DeInitGL() override;

	FFResult SetFloatParameter( unsigned int index, float value ) override;
	float GetFloatParameter( unsigned int index ) override;
	char* GetTextParameter( unsigned int index ) override;
	/// The base class's stub fails, and a failed default deletes the instance
	/// -- so without this no real host can load the plugin.
	FFResult SetTextParameter( unsigned int index, const char* value ) override;
	FFResult SetTime( double time ) override;

	void SetById( unsigned int id, float value )
	{
		SetFloatParameter( id, value );
	}
	float GetById( unsigned int id ) const
	{
		return id < PT_COUNT ? params[ id ] : 0.0f;
	}

	/// The controls in physical units.
	Settings CurrentSettings() const;

	//-------------------------------------------------------------------
	// For the harness. Nothing in the plugin's own operation calls these.
	//-------------------------------------------------------------------
	PluginHooks& HooksForTest()
	{
		return hooks;
	}
	void SetSeedForTest( uint32_t s )
	{
		seed = s;
	}
	uint32_t Seed() const
	{
		return seed;
	}
	void SetClockScaleForTest( double scale )
	{
		clockScale = scale;
	}
	/// The last frame's factors, as the passes were given them.
	const Frame& LastFrame() const
	{
		return frame;
	}
	const HaloKernel& Kernel() const
	{
		return kernel;
	}
	/// r N, g Q, b lambda: the last frame's counts, full raster.
	GLuint CountTextureID() const
	{
		return counts.TextureID();
	}
	/// r the store, g the light emitted over the last frame.
	GLuint StoreTextureID() const
	{
		return store[ storeCurrent ].TextureID();
	}
	/// One texel: r g, g D, b the mean photocathode lux, a ln g.
	GLuint ControlTextureID() const
	{
		return control[ controlCurrent ].TextureID();
	}
	double HostNow() const
	{
		return now;
	}
	double ClockScale() const
	{
		return clockScale;
	}
	/// The interval the last frame counted over, s.
	double CountingInterval() const
	{
		return countingInterval;
	}
	uint32_t FrameIndex() const
	{
		return frameIndex;
	}

private:
	void UpdateClock();
	bool ensureBuffers( int width, int height );
	void draw( PassBuffer& target );
	void render( const FFGLTextureStruct* input, const GLint* hostViewport, GLuint hostFBO, int width, int height );

	GLint where( const ffglex::FFGLShader& shader, const char* name );

	float params[ PT_COUNT ] = {};
	PluginHooks hooks;
	uint32_t seed       = 0;
	uint32_t frameIndex = 0;

	Frame frame;
	HaloKernel kernel;

	ffglex::FFGLShader rateShader, reduceShader, controlShader, haloShader, countShader, phosphorShader, resampleShader, compositeShader;
	GLuint vao = 0;
	std::vector< std::pair< std::pair< GLuint, const char* >, GLint > > uniformCache;

	static constexpr int kMaxLevels = 8;
	PassBuffer rate;
	PassBuffer reduce[ kMaxLevels ];
	int levels = 0;
	PassBuffer coarse, halo;
	PassBuffer control[ 2 ];
	int controlCurrent = 0;
	bool haveControl   = false;
	PassBuffer counts;
	PassBuffer store[ 3 ];
	int storeCurrent = 0;
	int storeWidth = 0, storeHeight = 0;

	// The clock (ferrofluid's vote on seconds or milliseconds, fountain's
	// offline fallback).
	double hostTime     = -1.0;
	double clockScale   = 0.0;
	double lastRawTime  = -1.0;
	double lastWallTime = -1.0;
	double wallStart    = -1.0;
	int secondsVotes    = 0;
	int millisVotes     = 0;
	int clockSamples    = 0;
	int clockFrames     = 0;
	bool settledJump    = false;
	double now          = 0.0;
	double lastNow      = -1.0;
	double countingInterval = tube::kDefaultFrame;
};

} // namespace intensifier
