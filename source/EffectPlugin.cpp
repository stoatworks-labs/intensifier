#include "Intensifier.h"

/**
    The registration. `intensifier_core` is an OBJECT library so this
    file-scope constructor, which nothing references by name, is linked into
    the bundle (an archive would let the linker drop it, and the bundle would
    load and report no plugins).
*/
static CFFGLPluginInfo PluginInfo(
	PluginFactory< intensifier::IntensifierPlugin >,// Create method
	"IT01",                                         // Plugin unique ID of maximum length 4
	"SW Intensifier",                               // Plugin name
	2,                                              // API major version number
	1,                                              // API minor version number
	0,                                              // Plugin major version number
	1,                                              // Plugin minor version number
	FF_EFFECT,                                      // Plugin type
	"A night-vision image intensifier tube looking at the clip. The photocathode turns its light into a sparse stream of "
	"photoelectrons, the microchannel plate multiplies each by a gain that differs every time, and each burst lights the "
	"phosphor: scintillation that thickens into a picture as the light rises, halos round bright points, the whole frame "
	"dimming when a torch comes in, and the honeycomb of the fibre optics at high light.",
	"Intensifier FFGL effect"// About
);

extern "C" const char* IntensifierBuildStamp()
{
	return "intensifier " INTENSIFIER_VERSION ", built " __DATE__ " " __TIME__;
}
