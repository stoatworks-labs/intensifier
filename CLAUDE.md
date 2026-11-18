# intensifier

A night-vision image intensifier tube (Gen 2 or Gen 3) looking at the clip, for Resolume
Arena/Avenue as one FFGL effect. Light at the photocathode becomes a Poisson count of
photoelectrons a pixel a frame; the microchannel plate multiplies each one by an exponential
gain; 7.4% back-scatter across the gap into a halo of radius twice the gap; the phosphor
integrates them exactly over the frame; the supply's ABC holds the screen's mean and the
gated photocathode holds its current. The clip is the scene's reflectance. C++/GLSL, CMake
MODULE → universal `.bundle` (macOS). MIT.

| id | display name | bundle | type |
| --- | --- | --- | --- |
| `IT01` | `SW Intensifier` | `Intensifier.bundle`, `com.stoatworks.ffgl.intensifier` | effect |

Read `AGENTS.md` before touching the sampler (`count` in `Shaders.cpp`), the halo kernel
(`Tube.cpp`, `HaloCdf`), the control texel (ABC and gate), the phosphor's integration, the
clock, the resize, or the composite. The spec is `~/Projects/resolume/specs/SPEC-intensifier.md`
and is kept in step with this repo.

## Commands (CMake)
- Configure: `cmake -B build -DCMAKE_BUILD_TYPE=Release`
- Fast dev build: add `-DCMAKE_OSX_ARCHITECTURES=arm64`
- Build: `cmake --build build -j8`
- Install to Resolume: `cmake --install build` (into Arena's Extra Effects).
  Never from a shared session on this Mac: nothing here has been loaded into Arena.
- Render offline: `./build/ittest --out /tmp/i.png` (the defaults over the harness's card,
  120 frames in at 60 fps)
- A clip of your own: `--clip clip.rgba` (raw RGBA at `--size`, top row first)
- List parameters: `./build/ittest --list`
- Set anything by name: `./build/ittest --set "Light Level=0.6" --set "Photocathode=Gen 2"`
  (a standard control takes the host's 0..1; an option takes its name; anything unparseable
  is refused with exit 2)
- Change something later: `./build/ittest --out /tmp/i.png --at 60 "Light Level=0.9"`
- Film: `./build/ittest --film 600 --size 1280x720 --script cues | ffmpeg -f rawvideo -pix_fmt rgba -s 1280x720 -r 60 -i - out.mp4`
- A clip through it: `ffmpeg -nostdin -i in.mov -f rawvideo -pix_fmt rgba - | ./build/ittest --pipe --fps 30 --size WxH | ffmpeg -f rawvideo -pix_fmt rgba -s WxH -r 30 -i - out.mp4`
- `--seed N` (1..4294967295) fixes the instance's seed; `--stats` prints each frame's ABC gain,
  gate duty and mean photocathode lux on stderr.
- The defaults' evidence: `python3 tools/clips.py defaults` (all 33 of Arena's demo clips;
  slow). The alpha check on two of them: `python3 tools/clips.py alpha`.

## Verify
- Everything: `OXBOW=/path/to/oxbow tools/verify.sh` (GLSL reserved words, `glslc` on all 9
  shaders, the submodule pin, a fresh universal build with no warnings outside external/ffgl,
  lipo, plist, ad-hoc signature, `oxbow probe` and `selftest` (required: no oxbow is a
  failure); every check at 1280x720 and 320x180; every check again on Apple's software
  renderer at 320x180; `--offline`; the alpha check on Arena's clips; `--pipe`; the negative
  controls; the mutants; the sweep; the bench). `KEEP_BUILD=1` reuses `build-verify/`.
  It needs Resolume Arena's demo clips in `/Applications/Resolume Arena/media`.
- Every check at once: `./build/ittest --all`. One at a time (`--size WxH`: that raster alone):
  - **The counting**: `--poisson` (counts Poisson in λ from 0.02 to 5000 and at the gated EBI's
    4e-5), `--sampler` (the card's counts Poisson in the plugin's own λ: a randomised PIT),
    `--charge` (the plate's charge Gamma(N, 1): a PIT), `--rng` (no correlation between frames,
    neighbours or instances), `--phd` (isolated events' light off the picture: Exp(1); the
    spot's share).
  - **The tube**: `--halo` (the rim at 2 × gap off the picture), `--abc` (mean held ±1% over a
    100× step; frame-rate independent; the torch), `--bsp` (the photocathode current at
    I_lim), `--phosphor` (τ from the spill at 30–240 fps; the colour), `--fixed` (the
    honeycomb's period and depth), `--alpha`.
  - **The host**: `--resize` (mid-run: the ABC and the phosphor carry over), `--clock` (a clock
    at 4.99e8 ms gives the same bits as one from 0; the unit settles), `--glstate`.
  - **No GL**: `--tube` (the halo kernel against a Monte Carlo of the mechanics; the colours;
    the conversions).
- The checks can fail: `./build/ittest --negative` (27 wrong models), `tools/mutate.sh`
  (36 mutants, arm64, 320x180 unless a mutant names its raster, perl alarm at 600 s; `ONLY=n`
  runs one).
- No dead controls: `python3 tools/sweep.py` (each control in a context where it acts;
  `--bare` drops the context).
- Render cost: `./build/ittest --bench` (720p, 1080p, one 4K run).
- `ITTEST_RENDERER=software` runs any of it on Apple's software renderer.
- Shared Mac: arm64 builds while iterating, no 4K loops, never kill a process you didn't start.

## Notes
- **Eight fragment passes and one shared full-raster triangle** (`Shaders.h`, nine shaders
  for `glslc`): rate (the clip's Y × α), reduce (exact 16 × 16 block sums down to one texel;
  also the halo's coarse grid), control (one texel: g, D, the mean lux, ln g), halo (K ⊛ λ on
  a grid f times coarser), count (Poisson N and Gamma Q per pixel), phosphor (the store and
  what it emits), resample (the store to a new raster, on a resize only), composite (spot,
  gain, honeycomb, colour, sRGB, alpha). Nothing is blended.
- **No time reaches a shader.** The CPU keeps the clock in double, rounds Δt to the
  microsecond and sends per-frame factors as floats. A paused host runs the tube at the last
  frame length.
- **Every random draw is `pcg4d`** of (x, y, frame counter, seed × 256 + draw). The counter is
  a uint32 the CPU increments per rendered frame; the seed a process-wide counter.
- All host parameters are 0..1 and mapped in `Controls.cpp`; options hold the element index.
- GLSL 4.10 core. Reserved words (`common partition active … packed sample patch layout flat`,
  `noise1`–`noise4`, `any far near fixed`) must not be identifiers, and nothing writes `M_PI`;
  `verify.sh` greps for both.
- Override `SetTextParameter` to return `FF_SUCCESS` for the About block, or no host can
  instantiate the plugin.
- `intensifier_core` is an OBJECT library: the plugin registers itself from a file-scope
  constructor nothing references.
- The macOS build must be universal. Check with `lipo`, never the build log.
- **Provisional hand copies:** `source/StoatworksAbout*.h` (guide = "", no user guide) and
  `ATTRIBUTIONS.md`. Once the project is registered in stoatworks-backend, `sync-about.py`
  and `sync-attributions.py` generate them; edit them there then.
- **Workflows** (the repo is public, `stoatworks-labs/intensifier`, so Actions are allowed):
  `ci.yml` builds on macOS (arm64, `ittest --list` and `--offline`, `glslc`) and Windows on
  every push and pull request; `release.yml` builds the universal bundle and the x64 DLL
  (checked by `tools/wincheck.ps1`), with a .dmg, zips and an NSIS installer, and publishes on
  a `v*` tag. A `workflow_dispatch` of `release.yml` builds without publishing. The pixel
  checks still run only in `tools/verify.sh`, on a Mac.
- `~/Projects/resolume/intensifier`, public at `github.com/stoatworks-labs/intensifier`.

## Browser demo
- `demo/` is <https://intensifier-demo.stoatworks-labs.com>; `.github/workflows/deploy.yml`
  deploys it on a push to main (a route plus a proxied AAAA 100:: record, not a custom
  domain: `wrangler.toml`).
- After a shader edit: `python3 demo/tools/check_shaders.py --write` (verify.sh fails on
  drift). It also fails if a C++ line the page's CPU half was ported from changes (the
  control table, Tube.h's constants, Compute, BuildHaloKernel, the clock, the buffers, the
  uniforms): change the port, then the anchor.
- After a change to Controls, Tube or Intensifier.cpp's CPU half: change the port in
  `demo/plugin.js` (between the PORT and FRAME markers) too.
- Locally: `python3 -m http.server -d demo`, or `npx wrangler dev` for the real `_headers`.
- Against the plugin: `python3 demo/tools/crosscheck/crosscheck.py --ittest build/ittest`
  (Chrome and a GPU; builds its own `tubedump` from Tube.cpp and Controls.cpp). AGENTS.md has
  the gaps and the last figures.

## Diagnostics

`source/Diag.{h,cpp}`: log file only, no crash handler (this runs inside Resolume).

    ~/Library/Logs/intensifier/intensifier.YYYY-MM-DD.log
    %LOCALAPPDATA%\intensifier\logs\intensifier.YYYY-MM-DD.log

`INTENSIFIER_LOG_DIR` overrides the folder.
