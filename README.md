# intensifier

> **AI-assisted project.** This codebase was created with [Claude](https://claude.com/claude-code)
> (Anthropic), directed and reviewed by a human author. It has been loaded into Resolume
> Arena on macOS and ran there (see [Status](#status)). Everything else below
> is established by an offline harness, `ittest`, that drives
> the real plugin class through the same `ProcessOpenGL` a host calls, in a headless GL
> context on a synthetic clock, on this Mac's GPU at two rasters and on Apple's software
> renderer. Read out of the plugin's own count buffer, the photoelectron counts are **Poisson
> in the photon rate** from λ = 4.4e-5 to 5000 a pixel a frame (every mean within **1.8σ** of
> the harness's own photon chain, every chi-square p ≥ 0.059). Read off the picture, the halo's
> rim sits at **twice the gap** to within **1.9 px** at gaps from 100 to 500 µm, and the
> automatic brightness control holds the screen's mean within **0.13%** of its limit before
> and after a **100×** step in the light, at 30, 60 and 120 fps. **27** deliberately wrong
> models and **36** one-character mutants of the shipped code are all caught. The objective,
> the brightness limit, its response time, the current limit and the honeycomb's depth are
> **chosen**, not measured: see [Status](#status).

A night-vision image intensifier tube looking at your clip, for
[Resolume](https://resolume.com) Arena and Avenue: an FFGL effect, **SW Intensifier**.

The clip is the scene, lit by starlight or moonlight. **The tube counts**: light at its
photocathode becomes a sparse stream of photoelectrons, each multiplied by the microchannel
plate by a gain that is different every time, and each burst lights a spot of green (P43) or
white (P45) phosphor. Nothing else is drawn. At starlight the picture is a shimmer of
discrete sparks that thickens into an image as the light comes up; a few events are several
times brighter than the rest (the fireflies on a dark field); round a lamp a halo forms,
brightest at its rim, its size set by the gap between the photocathode and the plate. When
a torch comes into frame the whole picture dims, because the power supply holds the screen's
mean brightness; in a very bright scene the photocathode is gated off for part of each frame
and the grain comes back. At high light a faint honeycomb, the fibre optics' multifibres,
shows through.

**Photosensitivity.** The tube turns a dark clip up, so a dark clip that pulses pulses
bright: measured with the WCAG general-flash rule (more than three flashes a second over the
whole frame or any third-of-frame window), **4** of Resolume's 33 demo clips cross the line
through it at the defaults where the bare clips did not, and **10** at 10 lux. Scintillation
on its own never does (none at 0.001 lux). A big step up in Light Level flares the whole
picture once before the tube pulls it back, so stepping it up and down three or more times a
second flashes. The [user guide](docs/USER-GUIDE.md#flashing) has the numbers.

<!-- downloads:start -->

## Download

**[v0.1.0](https://github.com/stoatworks-labs/intensifier/releases/tag/v0.1.0)** — prebuilt for macOS and Windows. Pick your platform:

<details>
<summary><b>macOS</b> — Universal (Apple Silicon + Intel)</summary>

| Build | Download | Size |
| --- | --- | --- |
| Universal (Apple Silicon + Intel) · .dmg disk image | [`intensifier-0.1.0-macos-universal.dmg`](https://github.com/stoatworks-labs/intensifier/releases/download/v0.1.0/intensifier-0.1.0-macos-universal.dmg) | 227 KB |
| Universal (Apple Silicon + Intel) · .zip archive | [`intensifier-macos-universal.zip`](https://github.com/stoatworks-labs/intensifier/releases/latest/download/intensifier-macos-universal.zip) | 187 KB |

</details>

<details>
<summary><b>Windows</b> — x64</summary>

| Build | Download | Size |
| --- | --- | --- |
| x64 · .exe installer | [`intensifier-0.1.0-windows-x86_64-setup.exe`](https://github.com/stoatworks-labs/intensifier/releases/download/v0.1.0/intensifier-0.1.0-windows-x86_64-setup.exe) | 250 KB |
| x64 · .zip archive | [`intensifier-windows-x86_64.zip`](https://github.com/stoatworks-labs/intensifier/releases/latest/download/intensifier-windows-x86_64.zip) | 120 KB |

</details>

All builds, checksums and release notes: [github.com/stoatworks-labs/intensifier/releases](https://github.com/stoatworks-labs/intensifier/releases).

macOS builds are signed and notarised and open normally. The Windows builds are unsigned, so SmartScreen warns once.

<!-- downloads:end -->

## Video

> **Photosensitivity warning:** this effect turns dark clips up, so a clip that pulses can
> flash brightly through it. Large-area flashes in the video are kept to three a second or
> fewer.

[![Intensifier: night vision that counts photons, for Resolume](docs/video-thumb.png)](https://www.youtube.com/watch?v=n6-1X9scw5g)

## Controls

| Group | |
| --- | --- |
| **Scene** | Light Level (the scene's illuminance, 0.00001–100 lux: 0.0001 overcast starlight, 0.002 a clear moonless sky, 0.05–0.3 full moon, 3.4 the end of civil twilight). |
| **Tube** | Photocathode (Gen 2 multialkali, Gen 3 GaAs), Gain (the tube's luminous gain, 1,000–100,000 fL/fc), Phosphor (P43 green, P45 white), Gap (photocathode to plate, 50–500 µm: the halo's radius is twice it), Fixed Pattern (the honeycomb, 0–1). |
| **Look** | Mix. |

The defaults are 0.1 lux, a Gen 3 tube at a gain of 40,000 with a P43 screen, a 175 µm gap and
Fixed Pattern 0.75. **Both the light and the honeycomb were set from Resolume's own demo
clips**: 0.1 lux is where the median clip (of all 33) brings the brightness control in, so a
typical clip sits at the point where the screen is at full brightness and the scintillation
is plain; at 0.75 the honeycomb equals one frame's photon noise at 100× that light, and is a
sixth of it at the default.

**Gain only matters below that point.** Above it the brightness control holds the screen's
mean, so Gain cancels out of the picture (at 10 lux on the harness's card, Gain 0.2 against 0.8 changes one 8-bit
code in one pixel). Below it, Gain is the screen's brightness. That is how the tube works.

Over a transparent part of a clip the screen still glows (the tube's own dark emission
sparkles faintly everywhere); a transparent pixel sends the tube no light. The output is
opaque at Mix 1.

## Status

**v0.1.0, 18 November 2026, and early days.** It has been loaded into Resolume Arena on
macOS (below), over a still of one of Resolume's demo clips: never a moving clip, never in a
show. On Windows only a test loader has run it so far. The macOS build is Developer ID-signed and notarised. There's a
[user guide](docs/USER-GUIDE.md) and a [browser demo](#browser-demo), but no OpenFX port and
no presets.

### In Resolume

![Three glowing rings in a triangle, green and yellow-green on black, each wrapped in a wide
soft green halo, as Resolume Arena's own composition snapshot shows them](docs/arena.jpg)

<sub>SW Intensifier inside Resolume Arena 7.29.0 on an Apple M4 Max, from Arena's
composition snapshot over REST, 18 November 2026: a still of Resolume's demo clip Trinity_09
at 10 lux (Light Level 0.857) with Gap at 500 µm, the rest at the defaults. The halos are the
photoelectrons that bounced back off the plate.</sub>

- **Windows:** the release's x64 DLL loads in `tools/wincheck.ps1` as a host loads it
  (`IT01`, effect, 12 parameters), in CI. The fleet's Arena gate on win-lab had not run on it
  when v0.1.0 was tagged: win-lab was busy with another release.
- **macOS: Arena 7.29.0 on an Apple M4 Max**, a local universal build of this source (ad-hoc
  signed, not the notarised download), mounted on a clip over Arena's REST API and read off
  Arena's composition snapshot (the middle three fifths of a 1920×1080 JPEG), over a still of
  Trinity_09:
  - all **12** parameters shown (with Arena's own Opacity) at the declared defaults, and the
    About line `Intensifier v0.1.0 - MIT - Stoatworks Labs, stoatworks-labs.com`. Its log:
    the M4 Max, `initialised`, and Arena's clock arriving in milliseconds, which it detected
    (scale 0.001);
  - two snapshots a second apart at the defaults differ by **4.9** levels a pixel: the tube
    redraws every frame. Mix 0 gives the bare still exactly (**0.0**);
  - P45 takes the green ratio G / ((R + B) / 2) from **1.82** to **1.04**: white;
  - Light Level 0 (0.00001 lux) drops the mean from **34.6** levels to **0.75** (sparks);
    Light Level 1 (100 lux) leaves it at **34.9**: the brightness control holding;
  - a step from 0.001 to 10 lux: the first snapshot after it had a mean of **69.9**, the one
    2 s later **34.9**. That is the flare, and the tube pulling it back;
  - at 0.001 lux, Gain 0 → 1 took the mean from **0.5** to **10.2**: Gain acts below the
    limit;
  - Gap 500 µm wraps each ring in a wide halo (above), 50 µm in a tight one;
  - over a white still at 10 and 100 lux, a spectral line at the honeycomb's row spacing
    (23 px in 1080) is **0.45** levels deep at Fixed Pattern 1 and **0.006** at 0: there,
    and faint, as meant;
  - Arena was quit without saving.

Not measured in Resolume: its cost, a moving clip, whether Arena hands an effect straight or
premultiplied colour (the plugin assumes straight), and any GPU but the M4 Max, and Windows at all.

### Measured offline, on macOS

`tools/verify.sh` passes on this machine (Apple Silicon, macOS 26) against a fresh universal
Release build (16 November 2026). Every picture check runs at **two rasters**, 1280×720 and
320×180, and again on **Apple's software renderer** at 320×180. The numbers below are from
`ittest --all` on the same commit (an arm64 build, the same code). Statistical checks reject
at p < 1e-6, about 5σ.

| check | result |
| --- | --- |
| `--poisson` | flat fields at λ = 0.02 (0.31 at 320×180, the dark emission's floor there), 0.5, 3, 9.5, 10.5, 40, 400 and 5000, and the gated dark emission's **4.4e-5** and **7e-4**, read from the plugin's count buffer: every mean within **1.81σ** of the harness's own λ (from the light, the sensitivity, the optics, the pixel's area, the frame and e), every variance within **2.1σ** of its standard error, every chi-square on the pmf p ≥ **0.059** |
| `--sampler` | on the test card, halo and all, 120,000 counts against the plugin's own λ up to 2e5: a randomised PIT uniform, p **0.80** (0.057 at 320×180) |
| `--charge` | the plate's charge for N photoelectrons is Gamma(N, 1), N from 1 to 2e5: PIT p **0.91** (0.21) |
| `--rng` | the counts' residuals between frames, across, up, both diagonals and two instances: worst \|r\| **0.0017** against 0.0052 (0.011 against 0.021) |
| `--phd` | 20,310 isolated dark-emission events read off the picture: the pulse heights' mean **1.0069** and variance **1.0272** (+0.99σ, +1.37σ), chi-square against Exp(1) p **0.62**; the spot keeps **0.921561** of an event's light in its centre pixel, as the 64 lp/mm spot predicts, to **3.6e-7** |
| `--halo` | one lit pixel on black: the rim at **+0.74**, **+0.88** and **+1.91 px** from 2 × gap at 100, 200 and 400 µm (tolerance 2.5, 3.5, 6.5 px), **+0.23**, **+0.79**, **+0.78 px** at 200, 300, 500 µm at 320×180 |
| `--abc` | the screen's mean **0.24969–0.24975** of the display before and after a 100× step (the limit is 0.25; ±1%); settled within 10% in **0.800**, **0.783** and **0.775 s** at 30, 60 and 120 fps; ln g at common times within **5.2e-5** (tolerance 7.4e-4); a torch on 1% of the frame dims the background **10.9857×** against the frame means' **10.9858×** |
| `--bsp` | the gated photocathode's current from the counts: **8e-8 A** at 10 and 100 lux, within **6.2e-6** of the limit (tolerance 6.4e-5); the ungated current at 0.1 lux to **8.7e-6** |
| `--phosphor` | the decay time from one flash's spill into the next frame: P43 **0.5211–0.5212 ms**, P45 **0.6514–0.6515 ms**, at 30, 60, 120 and 240 fps, within **5.4e-5** of the source (tolerance 1e-3); the screen's colour on the patent's xy to **3.7e-6** |
| `--fixed` | the honeycomb's spectral line at **33.00** DFT bins against the lattice's 32.84, in all three directions; its depth in each third of the frame **0.0086–0.0110** of the light (0.0079–0.0100 at 320×180) against the strips' area share 0.0106 (tolerance 0.0038 and 0.0047) |
| `--alpha` | alpha = mix(α, 1, Mix) to **3e-8**; a transparent pixel sends only the dark emission (λ to **2e-9**); Mix 0 the clip bit for bit. On two of Arena's own alpha clips, white painted under every transparent pixel changes **0** output bytes |
| `--resize` | 1280×720 → 320×180 mid-transition: the phosphor's spill **0.130436** against a run never resized **0.130435**; the brightness control's gain identical for 60 frames |
| `--clock` | a host clock at 4.99e8 ms against one from 0: **0** of 14,745,600 values differ; seconds and milliseconds clocks settle on the right unit |
| `--glstate` | the host's GL state comes back, **0** things changed |
| `--tube` | the halo kernel against a Monte Carlo of the mechanics (4 million landings each at six radii): chi-square p from **0.019** to 0.998 |
| `--pipe` | 2.5 frames in, exactly 2 out; bad cues and settings refused; a closed stdout (`\| head -c 1`) exits 1, not 141 |
| `--negative` | **27** deliberately wrong models, **27** caught |
| mutation | **36** one-character mutants of the shipped GLSL and C++, **36** caught |
| `tools/sweep.py` | all **7** controls measurably change the picture |
| shaders | all 9 compile through `glslc`, not merely through Apple's driver |
| the bundle | universal (`x86_64 arm64`), exports `plugMain`, ad-hoc signs; `oxbow` reads `SW Intensifier` / `IT01` / `effect` and renders through it |

Render cost, from verify.sh's final run on 16 November 2026 with other builds sharing the
machine, at the defaults over the harness's card: GPU time **0.85 ms** a frame at 720p
(worst 0.87), **1.31 ms** at 1080p (worst 1.41) and **3.38 ms** at 4K (one run; worst 3.71).
What part of that each pass takes, and the CPU's time a frame, were not measured. macOS only.

### Not established

- **Chosen, not measured:** the objective (f/1.2, transmission 0.9), the brightness control's
  limit (3 cd/m²) and response time (0.2 s), the gated photocathode's current limit (80 nA),
  the size of a multifibre (0.45 mm, derived) and both depths of the honeycomb. No source for
  any of them was reached.
- **The halo's share** (7.4% of photoelectrons) rests on one reading of a patent's sentence;
  the other reading (η) makes it **2.7×** stronger. Nothing measures it off the picture: the
  harness measures the rim and that the ring is there.
- **Gain is dead above the brightness limit**, over the top three quarters of its range at
  the defaults. Faithful to a tube, but it reads as a dead slider; a later version may
  relabel it or let it move the limit.
- **The default light, 0.1 lux,** is where the median demo clip brings the brightness control
  in. It was measured, not judged in a show, and a darker default would be noisier and more
  dramatic.
- **Some clips never reach the limit:** Resolume's nearly black Bass 001 doesn't engage the
  brightness control even at 100 lux, the top of Light Level.
- **No round eyepiece field.** A real scope shows a circle; here the whole frame is
  photocathode. An option for it may come later.
- **Back-scatter is assumed Lambertian**; the photocathode sees luminance only (no spectral
  response); both phosphors are single exponentials.
- **Resolume's alpha.** The plugin treats a clip's colour as straight alpha, which is what
  Arena's DXV demo clips decode to; whether Resolume hands an effect premultiplied colour has
  not been checked, in Arena or anywhere else.
- The defaults were measured on the demo clips through `--pipe`, not looked at in a show.
- No presets and no OpenFX port.

## Browser demo

**<https://intensifier-demo.stoatworks-labs.com/>** is SW Intensifier in a web page: every
control with its own name, group and default, over the kit's generated clips or a picture or
video of yours (decoded in the page, never uploaded). The eight passes are the plugin's own
GLSL, spliced unedited (`demo/tools/check_shaders.py`, run by `tools/verify.sh`). What runs
them (the tube's frame factors, the halo's kernel, the clock, the buffers and every uniform) is
a JavaScript port, and `demo/tools/crosscheck/` runs it and `ittest --pipe` on the same frames
with the same seed: on this Mac, 16 runs of 12 frames at 960 × 540 came out identical to the
byte, the control texel float for float. It is a demo, not the plugin, and says what it leaves
out: the kit's clips are premultiplied where the plugin assumes straight alpha, a frame is the
display's refresh, and the page has never been compared on another GPU.

## Build

macOS (universal: Apple Silicon and Intel):

```bash
git clone --recursive <this repo>
```

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
```

```bash
cmake --build build
```

```bash
cmake --install build
```

The last one copies `Intensifier.bundle` into `~/Documents/Resolume Arena/Extra Effects`.
Restart Arena and it's under Effects as **SW Intensifier**.

## Building and testing

```bash
OXBOW=/path/to/oxbow tools/verify.sh
```

That's the whole lot: shader hygiene and `glslc`, a fresh universal build with no warnings,
the bundle's architecture, plist and ad-hoc signature, a host's view of it through `oxbow`
(required), every check at two rasters and again on the software renderer, the alpha check on
Arena's demo clips (required), the pipe, the negative controls, the mutants, the sweep for
dead controls and the render cost. CI (`ci.yml`) builds on macOS and Windows and runs the
checks that need no GL; the pixel checks need a GPU, so this script is where they run.
`CLAUDE.md` lists the checks one by one; `./build/ittest --all` runs them all.

Render a frame of your own:

```bash
./build/ittest --out /tmp/intensifier.png --set "Light Level=0.4" --set "Photocathode=Gen 2"
```

Run a clip through it:

```bash
ffmpeg -nostdin -i in.mov -f rawvideo -pix_fmt rgba - | ./build/ittest --pipe --fps 30 --size 1280x720 | ffmpeg -f rawvideo -pix_fmt rgba -s 1280x720 -r 30 -i - out.mov
```

## License

MIT. See [LICENSE](LICENSE) and [ATTRIBUTIONS.md](ATTRIBUTIONS.md).

<!-- attributions:start -->
This project is built on other people's work — see [ATTRIBUTIONS.md](ATTRIBUTIONS.md).
<!-- attributions:end -->
