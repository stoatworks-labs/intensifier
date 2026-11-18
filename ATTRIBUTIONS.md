# Attributions

Intensifier is built on other people's work. This file lists what that work is, who did
it, and what it is doing here.

It is generated — the master lists live in the `stoatworks-backend` repo and are
pushed out by `scripts/sync-attributions.py`. Edit it there, not here.

## Code we derived from other people's work

Someone else solved this first, and this project would not exist in its current form without their work.

### The harness and the tooling — Stoatworks filings, fountain, and ferrofluid before them

<https://github.com/stoatworks-labs/filings>  
Licence: MIT  
Copyright: Stoatworks Labs

The harness's rig, PNG writer, cue sheets, --set by display name, --pipe/--film (SIGPIPE ignored, a closed stdout an exit of 1), --negative with its Perturb, and --offline (tools/ittest is filings' fgtest, which is fountain's fttest and ferrofluid's fetest, under new checks); tools/verify.sh, tools/mutate.sh (with fountain's perl alarm), tools/glslc.sh and tools/sweep.py; the clock's vote on seconds or milliseconds (ferrofluid's, by way of rosette and millpond) with fountain's offline fallback (source/Intensifier.cpp). All adapted.

### The pass buffers, the GL state and the log — Stoatworks millpond and radar

<https://github.com/stoatworks-labs/millpond>  
Licence: MIT  
Copyright: Stoatworks Labs

source/PassBuffer.{h,cpp} and source/Diag.{h,cpp} are millpond's, by way of filings. source/GLState.h is radar's, which is vectrix's, from resolume-scopes; intensifier lets the harness's negative control skip the restore.

### The release workflow and the DLL check — Stoatworks xenon and penumbra

<https://github.com/stoatworks-labs/xenon>  
Licence: MIT  
Copyright: Stoatworks Labs

.github/workflows/ci.yml and release.yml are xenon's (slitscan's before it): a universal bundle and a Windows DLL, ATTRIBUTIONS.md in each zip, the Windows installer per user into Resolume's Extra Effects. tools/wincheck.ps1, which loads the DLL as a host loads it and reads back its name, id, type and parameters, is penumbra's (pepper's). Adapted.

### The look card — Stoatworks schlieren

<https://github.com/stoatworks-labs/schlieren>  
Licence: MIT  
Copyright: Stoatworks Labs

The harness's test card (a sunset sky, a sun, a skyline, colour bars), the default clip in ittest.

### The Poisson and Gamma samplers — NumPy

<https://github.com/numpy/numpy>  
Licence: BSD-3-Clause  
Copyright: NumPy Developers

The count pass's Poisson sampler for λ ≥ 10 (Hörmann's transformed rejection with squeeze) and its Gamma sampler for N > 4 (Marsaglia and Tsang) are GLSL ports of random_poisson_ptrs and random_standard_gamma in numpy/random/src/distributions/distributions.c (v2.1.0), including the λ = 10 boundary. Hörmann's paper (Insurance: Mathematics and Economics 12, 1993) and Marsaglia and Tsang's (ACM TOMS 26(3), 2000) were not read; the method is cited as numpy implements it.

## Third-party code this project uses

Libraries, SDKs and frameworks the project is built on or bundles.

### Resolume FFGL SDK

<https://github.com/resolume/ffgl>  
Licence: BSD-3-Clause  
Copyright: FreeFrame

Vendored as a git submodule at external/ffgl (third_party/ffgl in oxbow).

The plugin ABI itself. An FFGL effect or source is defined by this SDK's headers — there is no other way to be loadable by Resolume Arena and Avenue.

### GLEW — the OpenGL Extension Wrangler Library

<https://github.com/nigels-com/glew>  
Licence: BSD-3-Clause (with Mesa 3-D and Khronos components)  
Copyright: Milan Ikits, Marcelo E. Magallon and Lev Povalahev

Arrives inside the FFGL submodule at external/ffgl/deps/glew-2.1.0. Not fetched separately.

Resolves OpenGL entry points on Windows, where the system headers stop at OpenGL 1.1.

### libpng

<http://www.libpng.org/pub/png/libpng.html>  
Licence: PNG Reference Library License (libpng)  
Copyright: the PNG Reference Library authors

Arrives inside the FFGL submodule, under the SDK's CustomThumbnail sample.

Part of the upstream SDK tree rather than something these plugins call directly — listed because it is present in the checkout.

## Work we checked ourselves against

No code was taken from these — but they were how we knew we had it right, and that is worth saying out loud.

### Image intensifiers, catalogue TII0007E — Hamamatsu Photonics

The photocathodes' luminous sensitivity (GaAs 1500 µA/lm, multialkali 280 µA/lm) and EBI (2.0e-11 and 1.0e-11 lm/cm²), the one-plate tube's 64 lp/mm limiting resolution and luminous gain, the 18 mm format, and the glossary's definitions (luminous sensitivity against a 2856 K source; EBI; luminous emittance) (source/Tube.h). Read on 2026-11-16.

### Image Intensifiers, datasheet DS001 rev03 — Photek

P43's decay of 1.2 ms a decade, the 18 mm tube's active area of 2.54 cm², the fibre pitch (with US 6,066,020, the 6–8 µm the multifibre's size is derived from), and limiting resolution as the frequency of 5% contrast (source/Tube.h). Read on 2026-11-16.

### The phosphor table — Applied Scintillation Technologies (a SLAC-hosted copy)

P45's decay, 1.5 ms to 10%, and P43's, 1.5 ms. With Exosens's product page ("P43 (green phosphor) or P45 (white phosphor)"), which says P45 decays much as P43 does. Read on 2026-11-16.

### Microchannel plate detectors — J. L. Wiza, Nucl. Instrum. Methods 162 (1979) 587–601

A straight-channel plate's single-electron pulse heights approximate a negative exponential below saturation (§2.1, fig. 4), and a plate's gain is exponential in its voltage (eq. 4): the plate's gain model and the ABC as a lag in log gain. Read from the reprint at psec.uchicago.edu on 2026-11-16.

### US patents on image intensifier tubes — Exelis, ITT, Litton, Ni-Tec, Sperry and Hitachi, read as text on Google Patents

US 9,177,764 (Exelis): ~20% of photoelectrons back-scatter off the plate's input face. US 8,135,253 (Exelis): 63% open area; a multifibre of several thousand fibres. US 7,023,126 (ITT): the halo's radius twice the cathode-to-anode distance, brightest at its rim. US 6,624,406 (Litton): the gap, 75–125 µm reduced-halo and 250 µm conventional. US 6,066,020 (ITT): hexagonal multifibres. US 4,385,092 (Ni-Tec): dimmer multifibre boundaries, fixed pattern noise. US 5,949,063 (Litton): the ABC and the resistor protection. US 4,882,481 (Sperry): the gated photocathode's duty. US 4,694,217 (Hitachi): P43's and P45's CIE xy. Read on 2026-11-16.

### AN/PVS-14 Night Vision Monocular Device Operator Manual — January 2012 copy

The honeycomb "can be seen in every Image Intensifier if the light level is high enough", and a fibre-optic inverter after the phosphor: the Fixed Pattern control's placement. Read on 2026-11-16.

### Wikipedia, "Image intensifier" and "Lux"

Gen 2's and Gen 3's sensitivities in kind, autogating, a 4G tube's halo; the Light Level references (starlight, moonlight, the dark limit of civil twilight). Read on 2026-11-16.

### Hash functions for GPU rendering — M. Jarzynski and M. Olano

<https://jcgt.org/published/0009/03/02/>

Journal of Computer Graphics Techniques 9(3) (2020): pcg4d, the source of every random draw (source/Shaders.cpp), as the paper's code gives it.

## Standards and published specifications

What the implementation is measured against.

- **The 2019 SI** — The elementary charge, 1.602176634e−19 C exactly, in source/Tube.h.
- **IEC 61966-2-1 (sRGB)** — The clip's decoding and the output's encoding, and the XYZ-to-linear-sRGB matrix the phosphors' colours go through (source/Shaders.cpp, source/Tube.cpp).
- **ITU-R BT.709** — The luminance weights 0.2126, 0.7152, 0.0722 the photocathode sees the clip through.
- **CIE 1931** — The phosphors' chromaticities, as xy.

## Getting this wrong

If your work is here and the description is inaccurate, the licence is wrong, or you would rather not be listed — open an issue and it will be fixed.
