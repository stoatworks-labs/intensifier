# AGENTS.md: Intensifier

Read this before changing the sampler (the count pass), the halo kernel, the control texel
(the ABC and the gate), the phosphor's integration, the clock, the resize or the composite.
`CLAUDE.md` has the commands; the spec is `~/Projects/resolume/specs/SPEC-intensifier.md`,
written before any code and kept in step with this repo (no As-built section: where the build
departed from the first draft, the spec was changed, and the changes are listed below).

## The idea

**The tube counts.** Light at the photocathode becomes a sparse stream of photoelectrons, each
multiplied by the microchannel plate by a gain that is different every time, and each burst
lights a spot of phosphor. The picture is the sum of those bursts. Scintillation, fireflies,
halos, the frame dimming when a torch comes in and the grain coming back in a bright scene all
fall out of the counting and the supply's two loops; none of them is drawn. Nothing in the
fleet counts photons: `plumbicon` (a camera tube's stored charge) and `readout` (a rolling
shutter) are the neighbours not duplicated.

## Decisions taken without asking (the brief says decide and write it down)

- **Identity.** `SW Intensifier`, `IT01` (checked free against every `"IT01"` in
  `~/Projects/*/*/source` and `~/dev/*/source`), an effect, `com.stoatworks.ffgl.intensifier`,
  harness `ittest`. One class, `IntensifierPlugin`; host indices are the ParamIds, the About
  block last.
- **The clip is the scene's reflectance**, Y × α, lit by Light Level, through an f/1.2 objective
  of transmission 0.9 (**assumed**, no source checked). Not the photocathode's illuminance
  directly: Light Level then reads in the units of the night sky (starlight, moonlight).
- **The frame's height spans the 18 mm photocathode.** A pixel's photoelectrons scale with
  its area, so a lower raster counts more a pixel and the noise per unit of photocathode is
  the same at every raster, as a counting device's must be.
- **The count is drawn exactly**, never approximated: a 32-bit integer comparison below
  λ = 1e-3 (a float's 24 bits would quantise the faintest rates), CDF inversion below 10,
  PTRS above (numpy's code, numpy's boundary). A normal approximation is one of the negative
  controls.
- **The plate's gain is a negative exponential** (Wiza §2.1: a single straight-channel plate far
  below saturation), so a pixel's charge is Gamma(N, 1): summed −log u for N ≤ 4,
  Marsaglia–Tsang above. Pólya (a saturated plate) is a negative control.
- **η = 0.20 × (1 − 0.63) = 0.074**: US 9,177,764's 20% back-scatter read as 20% of the 37%
  of photoelectrons that strike the web between channels (US 8,135,253's open area). The
  sentence could also mean 20% of all; the spec says so.
- **The halo is gathered, not scattered.** A displaced Poisson process is Poisson with the
  convolved intensity, so λ_total = D[(1 − η)λ + η(K ⊛ λ) + λ_EBI] and the count stays exactly
  Poisson. K ⊛ λ is computed on a grid f = max(1, ⌈R/6⌉) times coarser than the frame and
  upsampled bilinearly: a halo ring is resolved to about f pixels.
- **Back-scatter is Lambertian** (**assumed**): the landing radius has CDF 1 − √(1 − u²), a
  disc brightest at its rim at 2d. The derivation from mechanics is in the spec; the rim at
  twice the gap is US 7,023,126's.
- **The phosphor is integrated exactly over each frame** (a 360° shutter): emitted =
  S(1 − a) + Q(1 − b), store = Sa + Qb. Frame-rate independent and energy-conserving by
  construction; P43 carries 3.1% into the next frame at 60 fps and no long smear (the IDEAS
  bullet's comet trails belong to P20 screens, which the model does not invent).
- **The ABC is a first-order lag in log gain** (a plate's gain is exponential in its voltage,
  Wiza eq. 4) towards min(1, L_max / (Γ D Ē)). **L_max = 3 cd/m² and τ_ABC = 0.2 s are
  chosen**: no checked figure was reached. The first frame starts settled.
- **The BSP is the gated photocathode**, D = clamp(I_lim / (S Ē A), 1e-7, 1), within the frame.
  **I_lim = 80 nA is chosen** (−800 V across 10 GΩ, the middle of US 5,949,063's resistor
  range), which puts the knee near the dark limit of civil twilight for an average scene.
- **The display shows ℓ / (4 L_max)**, so a frame held by the ABC averages 0.25 linear and a
  lamp clips. The spot is the 64 lp/mm limit read as a Gaussian MTF at 5% (σ 6.1 µm), applied
  as an exact pixel-integrated 5 × 5 kernel.
- **The honeycomb is the fibre-optic inverter's**, after the phosphor (the AN/PVS-14 manual's
  diagram): multifibres 0.45 mm flat to flat (**derived**: several thousand fibres at 6–8 µm),
  boundaries 8 µm wide passing 30% less and cells ±4% (**both chosen**: "faint" is the only
  word a source gives), box-filtered across the pixel. It multiplies the screen, so it sits
  under the photon noise at low light and shows at high light, as the manual says.
- **Output alpha = mix(α, 1, Mix)**: the screen fills the frame, EBI sparks included where the
  clip is transparent. A transparent pixel reflects nothing: the clip's light is weighted by
  α, right for straight alpha. Resolume's DXV alpha demo clips are straight alpha over black:
  2802 of 7549 partly transparent pixels in BattleWeapon_Tank_09 and 123 of 4217 in Ethnik2
  have a channel above their alpha, which premultiplied colour cannot.
- **Randomness is `pcg4d`** of (x, y, frame counter, seed × 256 + draw index). The frame
  counter is a uint32 incremented per rendered frame, not the clock, so the picture keeps
  scintillating when the host's clock stops, as a live tube does; it repeats after 2³²
  frames (2.3 years at 60 fps). The seed is a process-wide counter, so two instances never
  show the same sparks. `--rng` measures the result.
- **The clock** is kept in double (ferrofluid's seconds/milliseconds vote with fountain's
  offline fallback); Δt is clamped to [0, 0.1] s and rounded to the microsecond, so a clock at
  4.99e8 ms and one from 0 give the same factors to the bit. A paused host (Δt = 0) runs the
  whole tube on the last non-zero frame length.
- **A resize resamples the phosphor's store by area** (charge conserved) and keeps the ABC's
  state.
- **Defaults from Resolume's demo clips** (`tools/clips.py defaults`, all 33 DXV clips in
  `media/AV` and `media/Shop74`, through `--pipe`): Light Level **0.1 lux**, the median clip's
  ABC knee (Ethnik2, 0.1021 lux; the knees run from 0.0076 to 4.6 lux and Bass 001 never
  engages below 100 lux); Fixed Pattern **0.75**, which puts the honeycomb at one frame's photon
  noise at 100× the knee on Ethnik2 at 720p (measured 0.995×) and at 0.160× at the knee.
- **No OpenFX port and no presets.** (At the build there was also no browser demo, no user
  guide, no Windows build and no workflows; the release added them once the repo went public
  as `stoatworks-labs/intensifier`, where Actions are allowed: `ci.yml` and `release.yml`
  are xenon's, `tools/wincheck.ps1` penumbra's.)

## Changes from the spec's first draft

Each was made in the spec as well.

- **The halo kernel's cell weights** were "the exact integral of K"; the code integrates F
  along 1024 rays a cell (a polar midpoint rule, which converges because F is continuous).
  `--tube` checks the result against a Monte Carlo of the mechanics.
- **The clock** gained the microsecond rounding: the difference of two large doubles is not the
  same Δt as the difference of two small ones, and the rounding makes them agree to the bit. A
  paused host first held the phosphor and the ABC while the count went on at the last
  interval; that charges the store with nothing emitted, so now the whole tube steps by the
  last interval.
- **Straight alpha** was asserted; it is now measured on Arena's clips (above).
- **`--poisson`** went from 0.02–400 to 0.02–5000 plus the gated EBI's 4e-5 and 7e-4, to cover
  the sampler's 1e-3 boundary as well as 10. Its dispersion test became the sample variance
  against its own standard error from Poisson's fourth central moment.
- **`--charge`** is new (the Gamma), and `--sampler` runs on 20,000 pixels a frame.
- **`--phd`** reads isolated EBI events (Gen 2, 240 fps, a black clip) over three frames, and
  gained the spot's share and two negative controls (a constant gain, a spot twice as wide).
- **`--halo`** dropped "η within 5%": with f > 1 the coarse grid puts part of the halo inside
  the source's own neighbourhood, where it cannot be told from the direct light, and no clean
  bound was found. The ring must instead stand 10σ clear of the background. Its negative
  control went from a radius of 1.5d (inside the tolerance at small gaps) to d.
- **`--abc`** dropped the overshoot on the step frame: the gain moves about 8% of the way in
  that frame, so the display clips and a mean on it says nothing. The settling time is read
  off the picture instead.
- **`--bsp`** reads the current from the counts against I_lim, and the ungated current below
  the knee.
- **`--fixed`** gained the depth in each third of the frame (mutant 18 below).
- **`--resize`** compares with a run never resized, same seed, instead of "stays in the band".
- **`--clock`** runs on the software renderer too (the draft excluded it): it compares two
  instances in one context, and measured there it is bit-exact (0 of 921,600 values differ).
- **`--tube`** checks the kernel against a Monte Carlo of the mechanics, not against its own
  closed-form CDF.

## The traps actually hit

- **std::max swallows NaN.** Mutant 2 (the clip's light divided by its alpha) made λ NaN over
  transparent pixels, and `--alpha` passed: `std::max( worst, nan )` returns `worst`. Every
  worst-case accumulation now goes through `worse()`, which propagates NaN.
- **A stale relink ran the last mutant's binary.** The first mutation run let mutants 1–3
  survive that a hand reproduction caught. `mutate.sh` now deletes the binary as well as the
  object before each build, and requires a GLSL mutant's text to be in the built binary.
- **A mutant hung the harness**: mutant 22 (photoelectrons per lux over the pitch, not the
  area) made counts of ~1e14 and `fitPoisson` tried to bin them. It refuses a range over 4e6
  now. A timeout would have counted as caught; a hang that never ends would not have.
- **A period check passed a honeycomb broken over most of the frame.** Mutant 18 (the
  lattice's row spacing wrong in the forward transform only) leaves the pattern right near
  the frame's centre, where the DFT window sat, and drops it elsewhere. The depth is now
  measured in each third of the frame.
- **The spot is invisible at 320×180** (σ 0.061 px: the centre pixel keeps 1.000000), so mutant
  30 survived there. A mutant can name its raster (`--phd@1280x720`).
- **The first alpha criterion was vacuous**: Arena's clips are black under α = 0, so "no light
  from transparent pixels" passed for a plugin that ignored alpha. White is now painted under
  every transparent pixel and must change no byte.
- **A faint event's spot share was contaminated** by earlier events' phosphor spill (worst
  per-event share 0.004 at 720p). The share is now light-weighted over the events.
- **ffmpeg reads the terminal** unless `-nostdin`: a script looping over clips loses its input.
- **zsh has no PIPESTATUS** and **macOS has no timeout**: the pipe checks run under bash, the
  mutants under perl's alarm.
- **The FFGL SDK warns** (FFGLUtilities.cpp); verify.sh ignores warnings under external/ffgl
  only.

## Verified, and assumed

Verified, by `OXBOW=... tools/verify.sh` on 16 November 2026 (numbers in README's Status):
every check at 1280×720 and 320×180 on this Mac's GPU and again on Apple's software renderer
at 320×180; the alpha decision on two of Arena's clips; the pipe; the negative controls and
mutants; the sweep; the bundle universal, signed ad hoc, read and rendered by oxbow.

The three IDEAS claims and their tolerances:

- **Counts Poisson in the photon rate** (`--poisson`, `--sampler`): the mean within
  5√(λ/n) of the harness's own λ (5 standard errors of a Poisson mean); the variance within 5
  of its standard errors (Poisson's fourth central moment); a chi-square on the pmf, bins
  merged to expected ≥ 5, p ≥ 1e-6 (the regularised incomplete gamma). 5σ and p 1e-6 are the
  same two-sided rate: a correct model fails one comparison in about a million.
- **Halo radius ∝ gap** (`--halo`): the rim, as the profile's outer half-maximum, within
  f + 0.5 px of 2d: f/2 for the source's position in its coarse cell, f/2 for the bilinear
  upsample's blur, 0.5 px for the profile's pixel grid. Three gaps a raster, so the
  proportionality is measured at 100–400 µm (720p) and 200–500 µm (180p).
- **ABC holds the mean within a stated band over a 100× step** (`--abc`): the band is ±1% of
  L_max, chosen in the spec as the claim's band (measured within 0.13% at both rasters and
  three frame rates). Frame-rate independence: ln g at the times all three rates share within
  three float roundings a frame at |ln g| ≤ ln 200 over the 120 fps run's 720 frames, plus the
  clock's microsecond rounding propagated through the lag (7.4e-4 in all); the settling times
  within one 30 fps frame.

The rest, briefly: `--charge` and `--sampler` PITs chi-square p ≥ 1e-6; `--rng` |r| ≤ 5/√n;
`--phd` 5 standard errors, the spot's share to 1e-4 (float); `--bsp` 5σ of the count plus three
levels of 256 float additions; `--phosphor` τ to 1e-3 relative and xy to 1e-3 (the matrices'
four digits); `--fixed` one DFT bin, and the depth within 5σ of the cells' spread plus 0.5/s
for the box filter; `--alpha` 1e-6 and bit-exact; `--resize` 5σ of the new EBI events plus an
edge allowance, ln g to 1e-4; `--clock` and `--glstate` exact.

Assumed or chosen, not measured: the objective (f/1.2, 0.9); Lambertian back-scatter; the η
reading; L_max, τ_ABC and I_lim; the multifibre's size and both honeycomb depths; that the
photocathode sees luminance only (no spectral response); single-exponential phosphors.

**Not measured off the picture: η itself.** In a flat field the halo term reproduces the
input, so `--poisson` cannot see η's value (only a sign error, mutant 7). `--halo` measures the
rim and the ring's presence, not its share.

Not done: presets, an OpenFX port; looking at the demo clips in a show rather than as
numbers; a moving clip in Resolume.

## In Resolume (v0.1.0, 18 November 2026)

- **Windows, win-lab, Arena 7.27.1, llvmpipe**, run 15 minutes after the tag (the lock was
  held through the tag window): the release zip's DLL through
  `plugin-bench/arena/arenaprobe.py --expect arena/expect/intensifier.json` (plugin-bench
  5fdd7d3): 9 passed, 0 failed, 0 skipped. 13 controls (with Opacity) as declared; render
  48.15 levels from the bare input against a 0.54 floor; every control live: Light Level
  54.7, Mix and Opacity 48.1, Phosphor 38.7, Gain 19.2, Gap 5.9, Photocathode 2.9, Fixed
  Pattern 2.3 (Gain at Light 0.2857, Gap and Fixed Pattern at 0.857, the expect file's
  `needs`). The thumbnail served the bare input to 3 grabs (one outlasted 5.5 s). Plugin log:
  Mesa 26.2 llvmpipe, GL 4.5 core, clock raw 50007.55 → scale 0.001.
- **macOS, Arena 7.29.0, Apple M4 Max**, a local universal build of 3914e59 in Extra Effects
  (removed after), probed over REST on 8080 by the release's `macprobe.py` (filings'
  pattern): one layer added over the show, a 1920×1080 still of Trinity_09 (Alpha, opacity
  1), SW Intensifier added by `effect:///video/SW%20Intensifier`, and every reading taken off
  `GET /composition/monitors/1/snapshot.jpg` (middle 3/5). The tube redraws every frame, so
  no reading is "identical": each is set against a same-settings pair (4.9 levels a pixel at
  the defaults, 0.67 at 10 lux). 12 of 12 parameters shown, defaults as declared; the log
  showed the clock arriving in ms (raw 7.67e8, scale 0.001). Mix 0 equalled the bare still
  (0.0). P45: green ratio 1.82 → 1.04. Light 0 → mean 0.75 from 34.6; Light 1 → 34.9 (held).
  A step 1e-3 → 10 lux read 69.9 at once and 34.9 after 2 s (the flare). Gain 0 → 1 at 1e-3
  lux: 0.5 → 10.2. Gap 500 µm: wide halos. **The honeycomb is invisible to a mean-difference
  reading** (Fixed Pattern 0 vs 1 at 10 lux: 0.70 and 0.74 against a 0.67 floor) but plain
  in the spectrum: over a white still at 10 and 100 lux a line at (ky 28 of a 648-row crop,
  kx 0), the lattice's 23 px row spacing, is 0.45 levels deep at FP 1 and 0.006 at FP 0
  (JPEG's 8 px block line dominates at FP 0). Quit came up as the "Quit!" dialog; Quit, not
  Save & Quit; every .avc's mtime unchanged.
- A trap when reading snapshots: Gap 500 µm raised the crop's mean from 34.9 to 48.4 levels
  (linear 0.099 to 0.116; 50 µm gave 0.087) although the ABC holds the screen's mean. The
  rings clip at white (7.9% of the crop's pixels saturated), so halo light moved off them
  into dark pixels shows where it was clipped before, and the gamma encoding exaggerates it.
  Compare like with like, never a mean against the ABC's limit. Gen 2 against Gen 3 at 1e-3
  lux: linear means 0.0057 and 0.0063, as bright to within 10% through a JPEG that smooths
  Gen 2's sparser, brighter sparks.

## Would this hold on another rasteriser, at another raster?

One line per check. Apple's software renderer at 320×180 passes every one of them.

- `--tube`: no GL; double.
- `--poisson`, `--sampler`, `--charge`: the samplers are float and integer arithmetic a GL 4.1
  driver must do the same way; the tolerances are statistical at the raster's own n, so they
  tighten or loosen with it, never fixed in pixels.
- `--rng`: pcg4d is integer arithmetic, identical on any driver; the limit 5/√n is the
  raster's.
- `--phd`: statistical; the spot's share is computed for the raster checked (0.9216 at 720p,
  1.0000 at 180p, where the spot is under a pixel and the share says little).
- `--halo`: the tolerance is in coarse cells, and f grows with the halo's size in pixels; at
  320×180 a 100 µm gap's rim is 2 px out, so the gaps checked there start at 200 µm.
- `--abc`: the band is relative; the frame-rate tolerance counts float roundings, not
  pixels.
- `--bsp`: the reduce is exact 16 × 16 block sums in a fixed order, so the float roundoff bound
  holds on any IEEE float GPU.
- `--phosphor`: per-frame factors computed in double on the CPU; holds anywhere.
- `--fixed`: the period to a DFT bin at both rasters; the box filter's allowance is 0.5/s of
  the loss, 2.8% at 720p and 11% at 180p, where a cell is 4.5 px.
- `--alpha`: 1e-6 is a few float roundings; Mix 0 is a texelFetch and a copy.
- `--resize`: only 1280×720 → 320×180; the area resample is float.
- `--clock`: compares the plugin with itself under two clocks in one context, so it holds
  wherever one driver is deterministic run to run.
- `--glstate`: whatever the driver reports.

## The negative controls and mutants

`ittest --negative`, each wrong model required to fail (checks failed of those run), all 27
caught:

| check | wrong model | failed |
| --- | --- | --- |
| tube | the halo kernel built at half the radius (a rim at the gap) | 5 |
| poisson | a rounded normal for the count | 16 |
| poisson | at most one event a pixel a frame | 16 |
| sampler | the rate jittered 30% per pixel (over-dispersed) | 2 |
| rng | the frame left out of the seed | 4 |
| rng | a seed of x + y | 4 |
| rng | every instance seeded alike | 4 |
| phd | a saturated plate: Pólya, shape 4 | 2 |
| phd | a constant gain | 2 |
| phd | the display's spot twice as wide | 1 |
| charge | a saturated plate: Pólya, shape 4 | 2 |
| charge | a constant gain | 2 |
| halo | the halo's radius the gap, not twice it | 5 |
| halo | no back-scatter | 6 |
| abc | no ABC | 8 |
| abc | the ABC's lag stepped per frame | 2 |
| bsp | no gate | 4 |
| phosphor | the phosphor stepped per frame (60 fps factors at every rate) | 4 |
| phosphor | a decay ten times too long | 4 |
| phosphor | the screen drawn in sRGB white | 4 |
| fixed | the honeycomb drawn 1.25× too large | 2 |
| fixed | the boundaries not drawn, the cells' spread kept | 4 |
| alpha | the clip's alpha passed through | 2 |
| alpha | the clip's light not weighted by its alpha | 2 |
| resize | a new raster clears the phosphor and the ABC | 1 |
| clock | the host's clock read in single precision | 4 |
| glstate | the host's GL state not put back | 2 |

The 1.25× honeycomb passes the depth half of `--fixed` (its loss falls by 1/1.25, inside the
tolerance at 320×180) and fails the period half; the boundaries-off control fails both.

`tools/mutate.sh`, 36 one-character mutants, each built arm64 and run at 320×180 (the spot's
at 1280×720) under a 600 s alarm, all caught. GLSL: the luminance weights summing to 1.1, the
direct share 1 + η, the tiny rates' P(N ≥ 1) doubled, inversion from e^+λ, PTRS's b, the
log-pmf's h(t), uniforms on [0, ½) (`--poisson`); the clip's light divided by α, the screen
transparent (`--alpha`); the frame's total subtracted, the lag towards minus its target, the
sRGB offset (`--abc`); the duty allowed to 2 (`--bsp`); the halo weights' stride (`--halo`);
one photoelectron's gain negative (`--phd`); Marsaglia–Tsang's c (`--charge`); the hash keyed
on x twice (`--rng`); the charge taken out of the store, the screen showing the store
(`--phosphor`); the resample's overlap a pixel wide (`--resize`); the honeycomb's rows
(`--fixed`). C++: photoelectrons per lux over the pitch, the objective's 1/3N², the frame a
tenth as long (`--poisson`); the EBI's rate divided (`--alpha`); L_max at 0.2 of the display,
the lag growing (`--abc`); the stored share, the XYZ-to-sRGB green (`--phosphor`); the halo
three gaps out (`--halo`); the landing CDF's shape, a geometric range high × low (`--tube`);
the spot's pixel wide on one side (`--phd`); the honeycomb's cell in m × px (`--fixed`); the
clock-unit fallback upside down (`--clock`); the resample's ratio old × new (`--resize`).

## The sweep's honest list

`python3 tools/sweep.py`: all 7 controls change the picture, each in a context where it acts:
Gain in low light (Light Level 0.143), Gap and Fixed Pattern in bright light (0.857).
`--bare` (the defaults, no context) also finds nothing dead, but only because the sweep
renders each control's two ends. **Gain is dead over most of its travel at the defaults, by
design.** On the harness's card at 0.1 lux the ABC's gain is 0.086 (`--stats`), so the tube
sits under its knee only below about 3,400 fL/fc (Gain 0.27): over the top three quarters of
the slider the ABC holds the screen's mean and Gain cancels (Gain 0.2 against 0.8 at 10 lux:
one 8-bit code in one pixel differs). The sweep's bare pass sees Gain only through its bottom
end.

## The browser demo (2026-11-18)

`demo/` is <https://intensifier-demo.stoatworks-labs.com>, built to the fleet's
`resolume-demo` kit rules (`~/Projects/resolume/specs/DEMO-BRIEF.md`), from penumbra's. What a
reader of it must know:

- **The shaders are the plugin's**: kVersion and all twelve raw-string pieces of
  `source/Shaders.cpp` (kQuadVertex, kCommon, kRandom, kSamplers and the eight passes), spliced
  by `demo/tools/check_shaders.py --write` into `demo/shaders.js` and compared character for
  character, with the assembly on both sides (AssembleVertex, Assemble's switch, InitGL's
  stages, plugin.js's copy) and every uniform any piece declares named in plugin.js. The
  integers go through `setInt` (Premultiply, Block, Radius, Fresh, AbcOn, BspOn, the three
  models, AlphaModel), `setUint` (FrameIndex, Seed), `setSampler`, or `gl.uniform2i` (the
  ivec2s SourceSize, CoarseSize, HaloSize, OldSize, ViewOrigin, Size); a `.set()` on one fails
  the check. The vertex stage is the plugin's gl_VertexID triangle, drawn from an empty VAO,
  not the kit's quad. `tools/verify.sh` runs the check.
- **The CPU half is a port**, in double, between the PORT and FRAME markers of
  `demo/plugin.js`: Controls.cpp (the table, geometric and its inverse, OptionIndex with
  `lround`), Tube.cpp (Compute with the shipped TubeHooks folded in, HaloCdf, BuildHaloKernel
  with `std::remainder` ported ties-to-even, SpotWeights, PhosphorColour, DecayTime), and the
  CPU half of Intensifier.cpp: the process-wide seed counter (the page's instance is seed 1),
  UpdateClock's vote and patience, the frame length (clamped to 0.1 s, rounded to the
  microsecond, a stopped clock on the last length), the kernel rebuild test, ensureBuffers,
  render()'s eight passes, the control ping-pong, the coarse grid, the three phosphor stores
  and the resample, and every uniform in order. `erf` is the page's own (JavaScript has
  none). Not ported: the log, the About block, ScopedGLState, and the harness's hooks (all off
  as shipped). The check holds the port to the C++ lines it came from (ORCHESTRATION in
  check_shaders.py): change Controls, Tube or Intensifier.cpp and change the port, then the
  anchor.
- **The composite's viewport is the input's size**, from the origin. The kit draws at the
  Composition size; a visitor's picture keeps its own size, so changing Composition after
  loading one leaves it in the corner (the page says so).
- **Premultiplied clips.** The kit's clips and a visitor's picture are premultiplied; the
  plugin weights light by alpha as for straight alpha, so a soft edge is weighted twice. Only
  the "Shape on transparency" clip shows it. Stated on the page, with the README's open
  question.
- **A frame is the display's refresh**, not Resolume's: each frame counts photoelectrons over
  its own length. The page's clock is seconds (the vote settles on seconds within four frames);
  paused, a control change draws one frame at the last frame's length and the counter
  advances, so the sparks change, as in a host with a stopped clock.
- **The page's own `differences` list says all of this on the page.** Keep it true. The line
  under the picture (the pixel's size on the photocathode, the photoelectrons a white pixel
  expects, the halo, and every sixth frame the control texel read back) is the page's, and
  says so. The presets are the page's (the plugin declares none).

How it was verified, and how to again (it needs this Mac's GPU and Chrome, so verify.sh does
not run it):

- `python3 demo/tools/crosscheck/crosscheck.py --ittest build/ittest [--size 960x540]`. The
  table of controls first, against `ittest --list` (names, kinds, defaults; the option
  elements by name through `--set`). Then 16 seeded runs of 12 generated frames (straight
  alpha: a graded ground, moving discs, stripes, a card whose alpha ramps): the defaults,
  Gen 2, starlight, no light at 24 fps, twilight, 100 lux gated, low gain at starlight, P45,
  gaps of 500 and 50 µm, Fixed Pattern 0 and 1, Mix 0.5 and 0, 30 fps, seed 4e9. Each through
  `ittest --pipe --seed --stats` and through the page's `renderRun` (a fresh instance on the
  harness's clock, the frames uploaded as ittest uploads them, the composite read back from
  an RGBA32F target and quantised as ittest quantises). 18 November 2026, M4 Max, Chrome
  157.0.8103.79 through ANGLE on Metal, against ittest on GL 4.1 (an arm64 build of bf1bd17):
  **bit-exact**. At 960 × 540 none of 398 million bytes differs; at 480 × 270 one byte of
  99.5 million differs, by one level (the starlight run). The control texel (the ABC's gain,
  the gate's duty, the mean lux) equals `--stats` float for float in every frame of every run.
  Every uniform set, none missing, no GL error, no frame refused; the resample's three
  uniforms go unset because no run changes the raster (ittest's `--resize` covers that path
  in the plugin; the page's resample was exercised only by switching Composition, with no
  console error).
- The CPU half without a GPU: `tubedump.cpp`, built from Tube.cpp and Controls.cpp with the
  plugin's Release flags, against the page's port at nine rasters, intervals and settings
  (1×1 to 3840×2160, 1 µs to 0.1 s): every float the GPU receives (the kernel's weights, the
  spot) identical, every int identical, every double identical except the phosphor's colour
  (1–5 ulp) and, in two of the nine, the kernel's raw sum (1 ulp). The phosphor's gap is
  clang fusing `3.2406 * X - 1.5372 * Y - 0.4986 * Z` into multiply-adds
  (`-ffp-contract=on`, its default): built with `-ffp-contract=off`, tubedump gives the
  page's colour to the bit. Both round to the same floats, which is all the GPU sees.
- Negative controls, each caught (pass: 99.99% of bytes identical over the run, no frame's
  mean 0.05 levels apart), at 960 × 540 (480 × 270): Light Level one slider step (0.001) off
  89.6% of bytes identical (96.5%); Gap one step off 99.92% (99.90%); Fixed Pattern one step
  off 99.94% (99.94%); Gain one step off at starlight 99.92% (99.92%) (at the defaults the ABC
  takes a Gain step straight back out, so that control is run below the knee); the seed one
  off 36.5% (43.6%); the frame counter never advancing 41.7% (48.2%).
- Flash, by xenon's rule (`flashcheck.py`: a transition is an opposing change of 0.1 of white
  whose darker end is below 0.8; more than 6 in any second fails; the whole frame and 5 × 5
  windows a third of the frame each way), on the page's own instance read back at 960 × 540,
  60 fps, linear luminance per pixel: every clip at the defaults, the eleven presets on three
  clips, 8 s each: **at most 2 transitions in any second in any window, 0 over the whole
  frame**. The scintillation averages out in any window; a window's mean never moves more
  than 0.017 of white between frames. What does flash is a visitor's own step up in light:
  the ABC lags 0.2 s in log gain, so a big step flares the frame towards white once (0.0001
  to 100 lux: mean 0.99; 0.1 to 3.4 lux: 0.87) before it is pulled back. Stepping Light Level
  up and down at 1, 2, 3, 4 and 6 times a second gives 2–3, 4–5, 6–7, 8–9 and 12–13
  transitions a second, so only a visitor working the control three times a second or faster
  crosses the line. No gate: nothing on the page flashes without the visitor's hand, and one
  flare per step is the plugin's own behaviour. Light Level's hint says it flares.

The thumb (`stoatworks-labs.com/thumbs/intensifier.png`) and the project page did not exist
when the demo went live; since v0.1.0's site push `index.html` carries the `project-page`
meta, and the no-script block links the project page and the guide as well as the repository.

## Open questions

- **Straight or premultiplied alpha in Resolume's pipeline?** The plugin weights the clip's
  light by α (right for straight alpha, and Arena's DXV clips decode straight through ffmpeg).
  If Resolume hands an effect premultiplied colour, transparent edges are darkened twice.
  Needs a look in Arena with an alpha clip (the v0.1.0 probe used an opaque still).
- **L_max and τ_ABC** (3 cd/m², 0.2 s) are chosen; a real tube's ABC response and output limit
  would replace them. Photonis's XR5 leaflet reportedly gives about 4 cd/m² and could not be
  fetched.
- **Gain does nothing above the knee.** Faithful, but a VJ who turns it up at 10 lux sees no
  change. Keep it, relabel it, or let Gain also scale L_max?
- **The default light** puts a typical clip at the knee (0.1 lux, full moon). A darker default
  (starlight) is more dramatic and noisier; the defaults were measured, not looked at in a
  show.
- **Bass 001 never engages the ABC** below 100 lux (it is nearly black); fine, or should
  Light Level reach further?
- **η's reading** (20% of the web's 37%, or 20% of everything) changes the halo's strength
  2.7×; nothing measured settles it.
- Should the round field of a real tube (the eyepiece circle) be an option? Left out: the frame
  is all photocathode.
