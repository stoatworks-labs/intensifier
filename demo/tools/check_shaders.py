"""The demo's shaders must be the plugin's shaders, character for character.

    python3 demo/tools/check_shaders.py           compare; exit 1 on any drift
    python3 demo/tools/check_shaders.py --write   regenerate demo/shaders.js

Called from `tools/verify.sh`. Exit code 1 means a copy has drifted.

------------------------------------------------------------------- why

`demo/shaders.js` holds the GLSL pieces of `source/Shaders.cpp`: kVersion,
kQuadVertex, kCommon (the sRGB pair), kRandom (pcg4d), kSamplers (Poisson and
Gamma) and the eight passes -- rate, reduce, control, halo, count, phosphor,
resample and composite. That is two copies of the same text, and two copies
drift -- quietly, because a tube drawn by a *plausible* sampler scintillates
exactly like one drawn by the right one. The whole claim of the page is that it
runs the plugin's own shaders, so the claim needs something enforcing it.
`ittest` drives the real plugin class and has never heard of this page, and
`tools/glslc.sh` compiles the C++ copies and never looks at the JS one.

------------------------------------------------------------------- what it does

Pulls each piece out of `source/Shaders.cpp` and the matching backtick literal
out of `demo/shaders.js`, and compares them exactly -- no whitespace
normalisation, no comment stripping: a comment updated on one side only is
exactly the drift worth catching. `kVersion`, a plain string, is compared too.
A piece may be several adjacent raw strings (MSVC refuses one literal over
about 16 KB, C2026); this joins them as the compiler does, with nothing
between. None of intensifier's pieces needs that today.

It fails when Shaders.cpp grows a piece this script does not list (a pass the
page would silently lack), and it checks the JOIN as well as the pieces:
AssembleVertex() is kVersion + kQuadVertex for every pass, Assemble() joins
each fragment stage from its pieces, Intensifier.cpp's InitGL compiles those
for all eight passes, and plugin.js must assemble them the same way.

The page is more than the shaders: the orchestration around them -- the
buffers and their formats, the 16 x 16 reduction to one texel, the control
texel's ping-pong, the halo's coarse grid, the phosphor's three stores and the
resample, the clock and the frame length -- is Intensifier.cpp's
ProcessOpenGL and render() translated by hand, and the frame factors, the halo
kernel and the spot are Tube.cpp's, the conversions Controls.cpp's. This
cannot check a translation, but it can trip when its source moves:
ORCHESTRATION lists the C++ lines the port was made from, and a change to any
of them fails here and needs a reader to carry it across.

And it fails when a piece declares a uniform that plugin.js never names (a
uniform the page does not set sits at 0, a control quietly dead on the page and
live in the plugin), or when plugin.js hands an integer uniform -- int, ivec,
uint or a sampler -- to the kit's `.set()`, which always uploads a float and so
is refused by GL without a word. Every piece is checked, not only the
fragments.

The one transformation is a decode, not a normalisation: a backtick cannot
appear raw inside a template literal, so --write would escape one as \\`.
Shaders.cpp has none today. This undoes that escape and REJECTS any other
backslash or any `${` on the JS side; there are none in the C++ bodies, so
either could only be somebody hiding a difference.

`--write` produces shaders.js from the C++ by exactly that escape, so the copy
is spliced by this script and never typed.

------------------------------------------------------------------- what it cannot

Nothing here checks the PORTED half: Controls.cpp (every conversion and
default), Tube.cpp (Compute, BuildHaloKernel, SpotWeights, PhosphorColour) and
the CPU half of Intensifier.cpp (the clock, the frame length, the passes and
every uniform) in demo/plugin.js are a hand translation.
demo/tools/crosscheck/ renders ittest and the page from the same frames with
the same seed and compares the pictures, the control texel and the CPU
numbers; it needs Chrome and a GPU, so it is run by hand, not by verify.sh.
When you change one of those, change the port too.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.dont_write_bytecode = True

# JS constant, C++ symbol (all in source/Shaders.cpp), in the order they are
# declared there. kVersion, a plain string, is handled on its own.
SHADERS = [
    ("QUAD_VERTEX", "kQuadVertex"),
    ("COMMON", "kCommon"),
    ("RANDOM", "kRandom"),
    ("RATE_FRAGMENT", "kRateFragment"),
    ("REDUCE_FRAGMENT", "kReduceFragment"),
    ("CONTROL_FRAGMENT", "kControlFragment"),
    ("HALO_FRAGMENT", "kHaloFragment"),
    ("SAMPLERS", "kSamplers"),
    ("COUNT_FRAGMENT", "kCountFragment"),
    ("PHOSPHOR_FRAGMENT", "kPhosphorFragment"),
    ("RESAMPLE_FRAGMENT", "kResampleFragment"),
    ("COMPOSITE_FRAGMENT", "kCompositeFragment"),
]

# The pieces whose uniforms plugin.js must set (by name, quoted).
UNIFORM_PIECES = [symbol for _, symbol in SHADERS]

# How the pieces are joined, on each side. Each entry is (file, text that must
# appear in it verbatim). The C++ lines are the plugin's assembly; the JS lines
# are plugin.js's copy of it. A change to either is a change to what runs.
ASSEMBLY = [
    ("source/Shaders.cpp",
     "std::string AssembleVertex( Pass )\n{\n"
     "\treturn std::string( kVersion ) + kQuadVertex;\n}"),
    ("source/Shaders.cpp",
     "std::string Assemble( Pass pass )\n{\n"
     "\tswitch( pass )\n"
     "\t{\n"
     "\tcase Pass::Rate: return std::string( kVersion ) + kCommon + kRateFragment;\n"
     "\tcase Pass::Reduce: return std::string( kVersion ) + kReduceFragment;\n"
     "\tcase Pass::Control: return std::string( kVersion ) + kControlFragment;\n"
     "\tcase Pass::Halo: return std::string( kVersion ) + kHaloFragment;\n"
     "\tcase Pass::Count: return std::string( kVersion ) + kRandom + kSamplers + kCountFragment;\n"
     "\tcase Pass::Phosphor: return std::string( kVersion ) + kPhosphorFragment;\n"
     "\tcase Pass::Resample: return std::string( kVersion ) + kResampleFragment;\n"
     "\tcase Pass::Composite: return std::string( kVersion ) + kCommon + kRandom + kCompositeFragment;\n"
     "\t}\n"
     "\treturn std::string();\n}"),
    ("source/Intensifier.cpp", "const std::string vertex   = shaders::AssembleVertex( stage.pass );"),
    ("source/Intensifier.cpp", "const std::string fragment = shaders::Assemble( stage.pass );"),
    ("source/Intensifier.cpp", "if( stage.shader->Compile( vertex.c_str(), fragment.c_str() ) )"),
    ("source/Intensifier.cpp", '{ &rateShader, shaders::Pass::Rate, "rate" },'),
    ("source/Intensifier.cpp", '{ &reduceShader, shaders::Pass::Reduce, "reduce" },'),
    ("source/Intensifier.cpp", '{ &controlShader, shaders::Pass::Control, "control" },'),
    ("source/Intensifier.cpp", '{ &haloShader, shaders::Pass::Halo, "halo" },'),
    ("source/Intensifier.cpp", '{ &countShader, shaders::Pass::Count, "count" },'),
    ("source/Intensifier.cpp", '{ &phosphorShader, shaders::Pass::Phosphor, "phosphor" },'),
    ("source/Intensifier.cpp", '{ &resampleShader, shaders::Pass::Resample, "resample" },'),
    ("source/Intensifier.cpp", '{ &compositeShader, shaders::Pass::Composite, "composite" },'),
    ("demo/plugin.js", "const vertex = S.VERSION + S.QUAD_VERTEX;"),
    ("demo/plugin.js", "rate: S.VERSION + S.COMMON + S.RATE_FRAGMENT,"),
    ("demo/plugin.js", "reduce: S.VERSION + S.REDUCE_FRAGMENT,"),
    ("demo/plugin.js", "control: S.VERSION + S.CONTROL_FRAGMENT,"),
    ("demo/plugin.js", "halo: S.VERSION + S.HALO_FRAGMENT,"),
    ("demo/plugin.js", "count: S.VERSION + S.RANDOM + S.SAMPLERS + S.COUNT_FRAGMENT,"),
    ("demo/plugin.js", "phosphor: S.VERSION + S.PHOSPHOR_FRAGMENT,"),
    ("demo/plugin.js", "resample: S.VERSION + S.RESAMPLE_FRAGMENT,"),
    ("demo/plugin.js", "composite: S.VERSION + S.COMMON + S.RANDOM + S.COMPOSITE_FRAGMENT,"),
    ("demo/plugin.js", "programs[name] = new Program(gl, vertex, fragment, name);"),
]

# The C++ the page's CPU half (plugin.js, between its PORT and FRAME markers)
# was ported from, line by line. Not a proof of the port: a tripwire for its
# source.
ORCHESTRATION = [
    # Controls: the table and the conversions.
    ("source/Controls.cpp", 'standard( PT_LIGHT, "Light Level", "Scene", ParamFromLight( 0.1 ) ),'),
    ("source/Controls.cpp",
     'option( PT_PHOTOCATHODE, "Photocathode", "Tube", static_cast< float >( kGen3 ), kPhotocathodeNames, 2 ),'),
    ("source/Controls.cpp", 'standard( PT_GAIN, "Gain", "Tube", ParamFromGain( 40000.0 ) ),'),
    ("source/Controls.cpp",
     'option( PT_PHOSPHOR, "Phosphor", "Tube", static_cast< float >( kP43 ), kPhosphorNames, 2 ),'),
    ("source/Controls.cpp", 'standard( PT_GAP, "Gap", "Tube", ParamFromGap( 175e-6 ) ),'),
    ("source/Controls.cpp", 'standard( PT_FIXED, "Fixed Pattern", "Tube", 0.75f ),'),
    ("source/Controls.cpp", 'standard( PT_MIX, "Mix", "Look", 1.0f ),'),
    ("source/Controls.cpp", "return low * std::pow( high / low, clamp01( v ) );"),
    ("source/Controls.cpp",
     "return static_cast< float >( std::log( std::clamp( value, lo, hi ) / low ) / std::log( high / low ) );"),
    ("source/Controls.cpp", "const long i = std::lround( static_cast< double >( value ) );"),
    ("source/Controls.h", "constexpr double kLightLow = 1e-5, kLightHigh = 100.0;"),
    ("source/Controls.h", "constexpr double kGainLow = 1000.0, kGainHigh = 100000.0;"),
    ("source/Controls.h", "constexpr double kGapLow = 50e-6, kGapHigh = 500e-6;"),
    # Tube: the constants and the frame's factors.
    ("source/Tube.h", "constexpr double kElementaryCharge = 1.602176634e-19;"),
    ("source/Tube.h", "constexpr double kPhotocathodeDiameter = 18e-3;"),
    ("source/Tube.h", "constexpr double kTubeArea = 2.54e-4;"),
    ("source/Tube.h", "constexpr double kFNumber              = 1.2;"),
    ("source/Tube.h", "constexpr double kObjectiveTransmission = 0.9;"),
    ("source/Tube.h", "constexpr double kSensitivity[ 2 ] = { 280e-6, 1500e-6 };"),
    ("source/Tube.h", "constexpr double kEbi[ 2 ] = { 1.0e-7, 2.0e-7 };"),
    ("source/Tube.h", "constexpr double kBackscatter = 0.20;"),
    ("source/Tube.h", "constexpr double kOpenArea    = 0.63;"),
    ("source/Tube.h", "constexpr double kDecayPerDecade[ 2 ] = { 1.2e-3, 1.5e-3 };"),
    ("source/Tube.h", "constexpr double kLimitingResolution = 64e3;"),
    ("source/Tube.h", "constexpr double kContrastAtLimit    = 0.05;"),
    ("source/Tube.h", "constexpr double kMaxLuminance = 3.0;"),
    ("source/Tube.h", "constexpr double kAbcTime      = 0.2;"),
    ("source/Tube.h", "constexpr double kCathodeCurrentLimit = 80e-9;"),
    ("source/Tube.h", "constexpr double kMinDuty             = 1e-7;"),
    ("source/Tube.h", "constexpr double kMultifibre     = 0.45e-3;"),
    ("source/Tube.h", "constexpr double kBoundaryWidth  = 8e-6;"),
    ("source/Tube.h", "constexpr double kBoundaryDepth  = 0.30;"),
    ("source/Tube.h", "constexpr double kCellSpread     = 0.04;"),
    ("source/Tube.h", "constexpr double kPhosphorXY[ 2 ][ 2 ] = { { 0.334, 0.561 }, { 0.269, 0.311 } };"),
    ("source/Tube.h", "constexpr int kMaxHaloCells = 6;"),
    ("source/Tube.h", "constexpr double kDefaultFrame = 1.0 / 60.0;"),
    ("source/Tube.cpp", "f.pitch           = kPhotocathodeDiameter / h;"),
    ("source/Tube.cpp", "f.electronsPerLux = f.sensitivity * f.pitch * f.pitch * dt / kElementaryCharge;"),
    ("source/Tube.cpp", "f.luxPerY         = s.sceneLux * kObjectiveTransmission / ( 4.0 * kFNumber * kFNumber );"),
    ("source/Tube.cpp", "f.rate            = f.electronsPerLux * f.luxPerY;"),
    ("source/Tube.cpp", "f.ebiRate         = f.electronsPerLux * f.ebiLux;"),
    ("source/Tube.cpp", "f.gain         = s.gainFLfc / kPi;"),
    ("source/Tube.cpp", "f.displayScale = f.gain / ( f.electronsPerLux * 4.0 * kMaxLuminance );"),
    ("source/Tube.cpp", "f.decayA         = std::exp( -x );"),
    ("source/Tube.cpp", "f.oneMinusA      = -std::expm1( -x );"),
    ("source/Tube.cpp", "f.decayB         = f.oneMinusA / x;"),
    ("source/Tube.cpp", "f.oneMinusB      = 1.0 - f.decayB;"),
    ("source/Tube.cpp", "f.abcDecay       = std::exp( -dtA / kAbcTime );"),
    ("source/Tube.cpp", "f.haloRadiusPx = 2.0 * s.gap * hooks.haloScale / f.pitch;"),
    ("source/Tube.cpp",
     "f.haloFactor   = std::max( 1, static_cast< int >( std::ceil( f.haloRadiusPx / kMaxHaloCells - 1e-9 ) ) );"),
    ("source/Tube.cpp", "f.cellPx         = kMultifibre * hooks.cellScale / f.pitch;"),
    ("source/Tube.cpp", "f.boundaryHalfPx = 0.5 * kBoundaryWidth / f.pitch;"),
    ("source/Tube.cpp",
     "const double sigma = std::sqrt( std::log( 1.0 / kContrastAtLimit ) / ( 2.0 * kPi * kPi ) ) / kLimitingResolution;"),
    ("source/Tube.cpp", "return 1.0 - std::sqrt( 1.0 - u * u );"),
    ("source/Tube.cpp", "k.radius       = std::max( 0, static_cast< int >( std::ceil( R + 0.5 ) ) - 1 );"),
    ("source/Tube.cpp", "constexpr int kSamples = 1024;"),
    ("source/Tube.cpp", "a        = std::remainder( a, 2.0 * kPi );"),
    ("source/Tube.cpp", "const double theta = lo + span * ( s + 0.5 ) / kSamples;"),
    ("source/Tube.cpp", "sum += HaloCdf( exit / R ) - HaloCdf( enter / R );"),
    ("source/Tube.cpp",
     "w[ static_cast< size_t >( ( j + k.radius ) * side + ( i + k.radius ) ) ] = sum * span / kSamples / ( 2.0 * kPi );"),
    ("source/Tube.cpp", "w[ i + 2 ] = 0.5 * ( std::erf( ( i + 0.5 ) / s ) - std::erf( ( i - 0.5 ) / s ) );"),
    ("source/Tube.cpp", "rgb[ 0 ] = 3.2406 * X - 1.5372 * Y - 0.4986 * Z;"),
    ("source/Tube.cpp", "rgb[ 1 ] = -0.9689 * X + 1.8758 * Y + 0.0415 * Z;"),
    ("source/Tube.cpp", "rgb[ 2 ] = 0.0557 * X - 0.2040 * Y + 1.0570 * Z;"),
    # Intensifier: the seed, the clock, the frame length, the buffers, the passes.
    ("source/Intensifier.cpp", "seed = ++g_nextSeed;"),
    ("source/Intensifier.cpp", "constexpr int kClockVotes = 4;"),
    ("source/Intensifier.cpp", "constexpr int kClockPatience = 30;"),
    ("source/Intensifier.cpp", "constexpr double kMaxFrameDelta = 0.1;"),
    ("source/Intensifier.cpp", "constexpr int kBlock = 16;"),
    ("source/Intensifier.h", "static constexpr int kMaxLevels = 8;"),
    ("source/Intensifier.cpp", "if( ratio > 0.1 && ratio < 10.0 )"),
    ("source/Intensifier.cpp", "else if( ratio > 100.0 && ratio < 10000.0 )"),
    ("source/Intensifier.cpp", "if( hostDelta > 0.0 && clockScale == 0.0 && ++clockSamples >= kClockPatience )"),
    ("source/Intensifier.cpp", "clockScale  = hostDelta < 1.0 ? 1.0 : 0.001;"),
    ("source/Intensifier.cpp", "now = ( raw >= 0.0 && clockScale != 0.0 ) ? raw * clockScale : wallNow - wallStart;"),
    ("source/Intensifier.cpp", "double dt = lastNow >= 0.0 ? std::clamp( now - lastNow, 0.0, kMaxFrameDelta ) : 0.0;"),
    ("source/Intensifier.cpp", "dt = std::round( dt * 1e6 ) * 1e-6;"),
    ("source/Intensifier.cpp", "if( kernel.factor != frame.haloFactor || kernel.radiusPx != frame.haloRadiusPx )"),
    ("source/Intensifier.cpp", "bool ok = rate.Ensure( width, height, GL_R32F, S::Nearest );"),
    ("source/Intensifier.cpp", "ok = ok && reduce[ levels ].Ensure( w, h, GL_R32F, S::Nearest );"),
    ("source/Intensifier.cpp",
     "ok = ok && control[ 0 ].Ensure( 1, 1, GL_RGBA32F, S::Nearest ) && control[ 1 ].Ensure( 1, 1, GL_RGBA32F, S::Nearest );"),
    ("source/Intensifier.cpp", "ok = ok && counts.Ensure( width, height, GL_RGBA32F, S::Nearest );"),
    ("source/Intensifier.cpp", "const int spare = ( storeCurrent + 1 ) % 3;"),
    ("source/Intensifier.cpp", "const int next = ( storeCurrent + 1 ) % 3;"),
    ("source/Intensifier.cpp", 'glUniform1i( where( reduceShader, "Block" ), kBlock );'),
    ("source/Intensifier.cpp",
     'glUniform1f( where( reduceShader, "Scale" ), static_cast< float >( 1.0 / ( static_cast< double >( f ) * f ) ) );'),
    ("source/Intensifier.cpp",
     'glUniform1f( where( controlShader, "SensitivityArea" ), static_cast< float >( frame.sensitivity * tube::kTubeArea ) );'),
    ("source/Intensifier.cpp",
     'glUniform1f( where( countShader, "HaloFraction" ), hooks.haloOn ? static_cast< float >( tube::kHaloFraction ) : 0.0f );'),
    ("source/Intensifier.cpp", 'glUniform1ui( where( countShader, "FrameIndex" ), frameIndex );'),
    ("source/Intensifier.cpp", 'glUniform1ui( where( countShader, "Seed" ), seed );'),
    ("source/Intensifier.cpp",
     'glUniform1f( where( compositeShader, "MixAmount" ), std::clamp( params[ PT_MIX ], 0.0f, 1.0f ) );'),
    ("source/Intensifier.cpp",
     'glUniform1f( where( compositeShader, "PatternDepth" ), static_cast< float >( hooks.boundariesOn ? fixedDepth * tube::kBoundaryDepth : 0.0 ) );'),
    ("source/Intensifier.cpp",
     'glUniform1f( where( compositeShader, "PatternSpread" ), static_cast< float >( fixedDepth * tube::kCellSpread ) );'),
    ("source/Intensifier.cpp", "++frameIndex;"),
]

HEADER = """// GENERATED from source/Shaders.cpp by demo/tools/check_shaders.py --write.
// Do not edit: tools/verify.sh fails if a character of this differs from the
// plugin's. The one escape allowed is \\` for a backtick inside a comment.
"""


def read(*parts):
    with open(os.path.join(REPO, *parts)) as handle:
        return handle.read()


def cpp_version(source):
    match = re.search(r'const char\* const kVersion = "(.*?)";', source)
    return None if match is None else match.group(1)


def from_cpp(source, symbol):
    """The piece's text: its raw strings, joined as the compiler joins them.

    Returns (text, count of raw strings), or (None, 0) if the symbol is not
    there or what follows its `=` is not a run of adjacent raw strings ending
    in `;`.
    """
    match = re.search(r'const char\* const ' + symbol + r' = ((?:R"\(.*?\)"\s*)+);', source, re.S)
    if match is None:
        return None, 0
    parts = re.findall(r'R"\((.*?)\)"', match.group(1), re.S)
    rebuilt = re.sub(r'R"\(.*?\)"', "", match.group(1), flags=re.S)
    if rebuilt.strip() != "":
        return None, 0
    return "".join(parts), len(parts)


def cpp_pieces(source):
    """Every raw-string piece Shaders.cpp defines, by symbol."""
    return re.findall(r'const char\* const (k\w+) = R"\(', source)


def from_js(source, name):
    match = re.search(r'^export const ' + name + r' = `(.*?)`;$', source, re.S | re.M)
    if match is None:
        return None, None
    body = match.group(1)
    stray = re.search(r"\\(?!`)", body)
    if stray is not None:
        line = body[: stray.start()].count("\n") + 1
        return None, f"backslash that is not an escaped backtick, at line {line}"
    if "${" in body:
        return None, "template substitution"
    return body.replace("\\`", "`"), None


def uniforms(glsl):
    """Every uniform a piece declares, as (type, name), comments stripped
    first. An array (`uniform float Weights[ 169 ];`) counts by its name."""
    code = re.sub(r"//[^\n]*", "", glsl)
    return re.findall(r"^\s*uniform\s+(\w+)\s+(\w+)\s*(?:\[\s*\d+\s*\])?\s*;", code, re.M)


INTEGER_TYPES = re.compile(r"^(int|uint|ivec[234]|uvec[234]|bool|bvec[234]|[iu]?sampler\w+)$")


def write(cpp):
    out = [HEADER]
    version = cpp_version(cpp)
    if version is None:
        print("FAIL  kVersion not found in source/Shaders.cpp")
        return 1
    # kVersion is "#version 410 core\n" in the C++, the \n an escape there and
    # in the JS string literal alike.
    out.append(f"\n// kVersion, source/Shaders.cpp\nexport const VERSION = '{version}';\n")
    for name, symbol in SHADERS:
        body, count = from_cpp(cpp, symbol)
        if body is None:
            print(f"FAIL  {symbol} not found in source/Shaders.cpp (or not a run of adjacent raw strings)")
            return 1
        if "\\" in body or "${" in body:
            print(f"FAIL  {symbol} holds a backslash or ${{; the escape scheme cannot carry it")
            return 1
        joined = f", {count} adjacent raw strings joined" if count > 1 else ""
        out.append(f"\n// {symbol}, source/Shaders.cpp{joined}\nexport const {name} = `{body.replace('`', chr(92) + '`')}`;\n")
    with open(os.path.join(REPO, "demo", "shaders.js"), "w") as handle:
        handle.write("".join(out))
    print("wrote demo/shaders.js")
    return 0


def first_difference(a, b):
    left, right = a.splitlines(), b.splitlines()
    for i in range(max(len(left), len(right))):
        x = left[i] if i < len(left) else "<missing>"
        y = right[i] if i < len(right) else "<missing>"
        if x != y:
            return i + 1, x, y
    return None


def verbatim(entries, what):
    problems = 0
    for path, text in entries:
        try:
            body = read(*path.split("/"))
        except FileNotFoundError:
            print(f"FAIL  {path} is missing")
            problems += 1
            continue
        if text in body:
            print(f"ok    {what}: {path} still says {text.splitlines()[0][:64]}")
        else:
            print(f"FAIL  {what}: {path} no longer says: {text.splitlines()[0]}")
            problems += 1
    return problems


def check(cpp, js):
    problems = 0
    version_cpp = cpp_version(cpp)
    version_js = re.search(r"^export const VERSION = '(.*?)';$", js, re.M)
    if version_cpp is None or version_js is None or version_cpp != version_js.group(1):
        print("FAIL  VERSION does not match kVersion")
        problems += 1
    else:
        print(f"ok    {'VERSION':<20} matches kVersion")

    listed = {symbol for _, symbol in SHADERS}
    for symbol in cpp_pieces(cpp):
        if symbol not in listed:
            print(f"FAIL  source/Shaders.cpp defines {symbol}, which this check does not list -- "
                  "add it to SHADERS (and to the page, or say on the page why not)")
            problems += 1

    for name, symbol in SHADERS:
        cpp_text, count = from_cpp(cpp, symbol)
        js_text, complaint = from_js(js, name)
        if cpp_text is None:
            print(f"FAIL  {symbol} not found in source/Shaders.cpp (or not a run of adjacent raw strings)")
            problems += 1
        elif complaint is not None:
            print(f"FAIL  {name} in demo/shaders.js has a {complaint}")
            problems += 1
        elif js_text is None:
            print(f"FAIL  {name} not found in demo/shaders.js")
            problems += 1
        elif cpp_text == js_text:
            joined = f", {count} raw strings joined" if count > 1 else ""
            print(f"ok    {name:<20} matches {symbol} ({len(cpp_text)} chars{joined})")
        else:
            problems += 1
            print(f"FAIL  {name} has drifted from {symbol}")
            where = first_difference(cpp_text, js_text)
            if where:
                print(f"        first difference at line {where[0]}")
                print(f"          C++: {where[1]}")
                print(f"          js : {where[2]}")

    problems += verbatim(ASSEMBLY, "assembly")
    problems += verbatim(ORCHESTRATION, "orchestration")

    try:
        plugin = read("demo", "plugin.js")
    except FileNotFoundError:
        plugin = ""

    for symbol in UNIFORM_PIECES:
        text, _ = from_cpp(cpp, symbol)
        if text is None:
            continue
        declared = uniforms(text)
        if not declared:
            continue
        # Named as 'Name' or, for an array, element by element as 'Name[0]'.
        unset = [n for _, n in declared if f"'{n}'" not in plugin and f"'{n}[" not in plugin]
        floated = [n for t, n in declared
                   if INTEGER_TYPES.match(t) and re.search(r"\.set\(\s*'" + n + r"[\[']", plugin)]
        if unset:
            print(f"FAIL  {symbol} declares {', '.join(unset)}, which demo/plugin.js never names -- "
                  "the page would leave it at 0")
            problems += 1
        elif floated:
            print(f"FAIL  {symbol}'s integer uniform(s) {', '.join(floated)} go through .set(), which uploads a "
                  "float that GL refuses -- use setInt, setUint, setSampler or gl.uniform2i")
            problems += 1
        else:
            print(f"ok    uniforms: all {len(declared)} of {symbol}'s are named in demo/plugin.js, integers as integers")
    return problems


def main(argv):
    cpp = read("source", "Shaders.cpp")
    if "--write" in argv:
        return write(cpp)
    try:
        js = read("demo", "shaders.js")
    except FileNotFoundError:
        print("FAIL  demo/shaders.js is missing -- run demo/tools/check_shaders.py --write")
        return 1
    problems = check(cpp, js)
    print()
    if problems:
        print(f"{problems} problem(s) -- rerun demo/tools/check_shaders.py --write for a drifted copy "
              "(never edit shaders.js by hand); an assembly, orchestration, uniform or unlisted-piece "
              "failure needs a reader")
        return 1
    print(f"kVersion and all {len(SHADERS)} shader pieces, their assembly, the port's C++ and the uniforms "
          "are the plugin's")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
