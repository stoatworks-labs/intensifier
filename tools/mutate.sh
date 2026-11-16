#!/usr/bin/env bash
#
# Mutation testing: change ONE character of the shipped code and require that a
# check fails. A check that still passes against a mutant was not looking at
# the code it claims to cover -- and a harness that compiled its own copy of
# the shaders, or its own copy of the physics, would pass every mutant here.
#
# One copy of the tree in a temporary directory (the FFGL SDK is symlinked,
# not copied), built arm64-only once. Each mutant edits its file in the copy,
# deletes that file's object (make compares mtimes to the second, so an edit
# within the same second as the last build would otherwise be missed), builds
# ittest, runs the named check at 320x180 under a time limit (a mutant can
# hang: honeydew's lesson; a timeout counts as caught), then puts the file
# back and deletes the object again. --resize always runs 1280x720 -> 320x180
# and the offline --tube ignores --size. (ferrofluid's script by way of
# fountain and filings; intensifier's mutants.)
#
#     tools/mutate.sh          every mutant
#     ONLY=3 tools/mutate.sh   the third alone
#
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# file | the exact text | its one-character mutant | the check that must fail | what it is
# The fields are split on '|', so neither a target nor a mutant may contain one
# (conway's trap): no GLSL || here.
MUTANTS=(
	"source/Shaders.cpp|vec3( 0.2126, 0.7152, 0.0722 ) );|vec3( 0.3126, 0.7152, 0.0722 ) );|--poisson|GLSL: the clip's luminance weights summing to 1.1, 2 -> 3"
	"source/Shaders.cpp|lum * clamp( src.a, 0.0, 1.0 ) : lum );|lum / clamp( src.a, 0.0, 1.0 ) : lum );|--alpha|GLSL: the clip's light divided by its alpha, * -> /"
	"source/Shaders.cpp|				total += texelFetch( Source, q, 0 ).r;|				total -= texelFetch( Source, q, 0 ).r;|--abc|GLSL: the frame's total subtracted, + -> -"
	"source/Shaders.cpp|max( current, 1e-30 ), MinDuty, 1.0 ) : 1.0;|max( current, 1e-30 ), MinDuty, 2.0 ) : 1.0;|--bsp|GLSL: the gate's duty allowed to 2, 1 -> 2"
	"source/Shaders.cpp|ivec2( 0 ), 0 ).a - target ) * AbcDecay;|ivec2( 0 ), 0 ).a + target ) * AbcDecay;|--abc|GLSL: the ABC lags towards minus its target, - -> +"
	"source/Shaders.cpp|	int side    = 2 * Radius + 1;|	int side    = 3 * Radius + 1;|--halo|GLSL: the halo's weights read with the wrong stride, 2 -> 3"
	"source/Shaders.cpp|( ( 1.0 - HaloFraction ) * y|( ( 1.0 + HaloFraction ) * y|--poisson|GLSL: the direct share 1 + eta, - -> +"
	"source/Shaders.cpp|float pAny   = lam * ( 1.0 - lam|float pAny   = lam * ( 2.0 - lam|--poisson|GLSL: the tiny rates' P( N >= 1 ) doubled, 1 -> 2"
	"source/Shaders.cpp|	float pk  = exp( -lam );|	float pk  = exp( +lam );|--poisson|GLSL: inversion starting from e^+lambda, - -> +"
	"source/Shaders.cpp|float b        = 0.931 + 2.53 * slam;|float b        = 0.931 + 3.53 * slam;|--poisson|GLSL: PTRS's b with 3.53 for 2.53, 2 -> 3"
	"source/Shaders.cpp|		h = ( 1.0 + t ) * log( 1.0 + t ) - t;|		h = ( 1.0 + t ) * log( 1.0 + t ) + t;|--poisson|GLSL: the log-pmf's h( t ) far from the mode, - -> +"
	"source/Shaders.cpp|		float q = -log( r.x );|		float q = +log( r.x );|--phd|GLSL: one photoelectron's gain negative, - -> +"
	"source/Shaders.cpp|float c = inversesqrt( 9.0 * d );|float c = inversesqrt( 8.0 * d );|--charge|GLSL: Marsaglia-Tsang's c with 8 for 9, 9 -> 8"
	"source/Shaders.cpp|uvec4( uint( p.x ), uint( p.y ), RngModel|uvec4( uint( p.x ), uint( p.x ), RngModel|--rng|GLSL: the hash keyed on x twice, y -> x"
	"source/Shaders.cpp|return ( vec4( h >> 8u ) + 0.5 )|return ( vec4( h >> 9u ) + 0.5 )|--poisson|GLSL: uniforms on [ 0, 1/2 ), 8 -> 9"
	"source/Shaders.cpp|s * Decay.x + q * Decay.z|s * Decay.x - q * Decay.z|--phosphor|GLSL: the frame's charge taken out of the store, + -> -"
	"source/Shaders.cpp|float ox = min( hi.x, float( x + 1 ) )|float ox = min( hi.x, float( x + 2 ) )|--resize|GLSL: the resample's overlap a pixel too wide, 1 -> 2"
	"source/Shaders.cpp|float j     = q.y / 0.866025404;|float j     = q.y / 0.966025404;|--fixed|GLSL: the honeycomb's rows 0.966 apart, 8 -> 9"
	"source/Shaders.cpp|AlphaModel == 1 ? src.a : 1.0;|AlphaModel == 1 ? src.a : 0.0;|--alpha|GLSL: the screen transparent, 1 -> 0"
	"source/Shaders.cpp|vec3( 1.0 / 2.4 ) ) - 0.055;|vec3( 1.0 / 2.4 ) ) - 0.065;|--abc|GLSL: the sRGB encode's offset 0.065, 5 -> 6"
	"source/Shaders.cpp|texelFetch( Store, q, 0 ).g;|texelFetch( Store, q, 0 ).r;|--phosphor|GLSL: the screen shows the store, not what it emitted, g -> r"
	"source/Tube.cpp|f.sensitivity * f.pitch * f.pitch * dt|f.sensitivity * f.pitch / f.pitch * dt|--poisson|C++: photoelectrons per lux over a pixel's pitch, not its area, * -> /"
	"source/Tube.cpp|kObjectiveTransmission / ( 4.0 * kFNumber|kObjectiveTransmission / ( 3.0 * kFNumber|--poisson|C++: the objective's 1 / 3N^2, 4 -> 3"
	"source/Tube.cpp|f.ebiRate         = f.electronsPerLux * f.ebiLux;|f.ebiRate         = f.electronsPerLux / f.ebiLux;|--alpha|C++: the EBI's rate divided, * -> /"
	"source/Tube.cpp|( f.electronsPerLux * 4.0 * kMaxLuminance )|( f.electronsPerLux * 5.0 * kMaxLuminance )|--abc|C++: L_max at 0.2 of the display, 4 -> 5"
	"source/Tube.cpp|f.decayB         = f.oneMinusA / x;|f.decayB         = f.oneMinusA * x;|--phosphor|C++: the frame's stored share ( 1 - a ) x, / -> *"
	"source/Tube.cpp|f.abcDecay       = std::exp( -dtA / kAbcTime );|f.abcDecay       = std::exp( +dtA / kAbcTime );|--abc|C++: the ABC's lag growing, - -> +"
	"source/Tube.cpp|f.haloRadiusPx = 2.0 * s.gap|f.haloRadiusPx = 3.0 * s.gap|--halo|C++: the halo's radius three gaps, 2 -> 3"
	"source/Tube.cpp|return 1.0 - std::sqrt( 1.0 - u * u );|return 1.0 - std::sqrt( 2.0 - u * u );|--tube|C++: the landing CDF's shape, 1 -> 2"
	"source/Tube.cpp|std::erf( ( i + 0.5 ) / s )|std::erf( ( i + 0.6 ) / s )|--phd|C++: the spot's pixels 1.1 wide on one side, 5 -> 6"
	"source/Tube.cpp|-0.9689 * X + 1.8758 * Y|-0.9689 * X + 1.9758 * Y|--phosphor|C++: the XYZ-to-sRGB matrix's green, 8 -> 9"
	"source/Tube.cpp|kMultifibre * hooks.cellScale / f.pitch;|kMultifibre * hooks.cellScale * f.pitch;|--fixed|C++: the honeycomb's cell in m x px, / -> *"
	"source/Controls.cpp|return low * std::pow( high / low, clamp01( v ) );|return low * std::pow( high * low, clamp01( v ) );|--tube|C++: a geometric control's range high x low, / -> *"
	"source/Intensifier.cpp|clockScale  = hostDelta < 1.0 ? 1.0 : 0.001;|clockScale  = hostDelta > 1.0 ? 1.0 : 0.001;|--clock|C++: the clock-unit fallback upside down, < -> >"
	"source/Intensifier.cpp|dt = std::round( dt * 1e6 ) * 1e-6;|dt = std::round( dt * 1e5 ) * 1e-6;|--poisson|C++: the frame counted a tenth as long, 6 -> 5"
	"source/Intensifier.cpp|static_cast< double >( storeWidth ) / width )|static_cast< double >( storeWidth ) * width )|--resize|C++: the resample's ratio old x new, / -> *"
)

limit() {
	# macOS has no timeout(1): perl's alarm is enough.
	perl -e 'alarm shift; exec @ARGV' "$@"
}

tree="$WORK/tree"
mkdir -p "$tree/external"
cp -R "$REPO/source" "$REPO/tools" "$REPO/cmake" "$REPO/CMakeLists.txt" "$tree/"
ln -s "$REPO/external/ffgl" "$tree/external/ffgl"
cmake -S "$tree" -B "$tree/build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 -DINTENSIFIER_BUILD_TOOLS=ON >/dev/null
cmake --build "$tree/build" --target ittest -j8 >/dev/null 2>&1 || { echo "the unmutated copy does not build"; exit 1; }

caught=0
ran=0
index=0
for entry in "${MUTANTS[@]}"; do
	index=$(( index + 1 ))
	[[ -n "${ONLY:-}" && "$ONLY" != "$index" ]] && continue
	ran=$(( ran + 1 ))
	IFS='|' read -r file original mutant check what <<<"$entry"
	cp "$REPO/$file" "$tree/$file"

	python3 - "$tree/$file" "$original" "$mutant" <<'PY'
import sys, pathlib
path, original, mutant = pathlib.Path(sys.argv[1]), sys.argv[2], sys.argv[3]
text = path.read_text()
if text.count(original) != 1:
    sys.exit(f"mutation target found {text.count(original)} times in {path}: '{original}'")
if sum(a != b for a, b in zip(original, mutant)) != 1 or len(original) != len(mutant):
    sys.exit("a mutant must differ by exactly one character")
path.write_text(text.replace(original, mutant))
PY

	object="$( basename "$file" ).o"
	find "$tree/build" -name "$object" -delete
	printf '\n== mutant %d: %s\n' "$index" "$what"
	status=0
	if ! cmake --build "$tree/build" --target ittest -j8 >"$WORK/build.log" 2>&1; then
		# A mutant that does not compile proves nothing about the checks.
		printf '   FAIL  the mutant does not build -- choose another\n'
		tail -5 "$WORK/build.log" | sed 's/^/        /'
	else
		limit 600 "$tree/build/ittest" "$check" --size 320x180 >"$WORK/log" 2>&1 || status=$?
		if [[ "$status" -eq 0 ]]; then
			printf '   FAIL  %s still PASSES -- the check does not cover this code\n' "$check"
		else
			printf '   ok    %s fails against the mutant (exit %d%s):\n' "$check" "$status" "$( [[ "$status" -eq 142 ]] && echo ', the time limit' )"
			# A mutant that timed out printed no FAIL: grep finding none must not end the run.
			{ grep -E '^  FAIL' "$WORK/log" || true; } | head -2 | cut -c1-160 | sed 's/^/        /'
			caught=$(( caught + 1 ))
		fi
	fi
	cp "$REPO/$file" "$tree/$file"
	find "$tree/build" -name "$object" -delete
done

printf '\nmutants: %d, caught: %d\n' "$ran" "$caught"
[[ "$ran" -gt 0 && "$caught" -eq "$ran" ]]
