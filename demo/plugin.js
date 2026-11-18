/**
 * SW Intensifier (IT01, an FFGL effect), in a browser.
 *
 * A night-vision image intensifier tube looking at the clip. Light at the
 * photocathode becomes a Poisson count of photoelectrons a pixel a frame, the
 * microchannel plate multiplies each one by a gain that is different every
 * time, 7.4% of them back-scatter across the gap into a halo, the phosphor
 * integrates them exactly over the frame, and the supply's automatic
 * brightness control and gated photocathode hold the screen and the
 * photocathode's current.
 *
 * What runs here, and what is a port:
 *
 * - **The shaders are the plugin's.** `shaders.js` is spliced from
 *   source/Shaders.cpp by demo/tools/check_shaders.py --write, unedited, and
 *   every pass is assembled as Shaders.cpp's Assemble() assembles it (below,
 *   compilePrograms). The kit's port() changes the version line and adds
 *   precision qualifiers (highp int, so pcg4d's 32-bit unsigned products wrap
 *   as GLSL 4.10 says they do); nothing else. tools/verify.sh fails if a
 *   character drifts.
 * - **The CPU half is a port**, between the PORT and FRAME markers below:
 *   Controls.cpp (every conversion from the host's 0..1, the defaults),
 *   Tube.cpp (Compute's frame factors, BuildHaloKernel's cell weights by its
 *   polar quadrature, SpotWeights, PhosphorColour, DecayTime), and the CPU
 *   half of Intensifier.cpp: the constructor's defaults and its process-wide
 *   seed counter, UpdateClock (the vote on seconds or milliseconds and the
 *   offline fallback), ProcessOpenGL's frame length (clamped, rounded to the
 *   microsecond, a stopped clock running on the last one), and render(): the
 *   eight passes, their buffers and formats, the reduction to one texel, the
 *   control texel's ping-pong, the halo's coarse grid, the phosphor's three
 *   stores and the resample on a change of raster, and every uniform.
 *   demo/tools/crosscheck/ compares this page with ittest, the plugin's own
 *   harness, frame for frame and with the same seed; past that, only a reader
 *   checks it.
 * - `erf` is the page's own: JavaScript has none, and SpotWeights calls
 *   std::erf. The crosscheck compares the weights it gives with the plugin's.
 * - Not ported: the log (Diag.cpp), the About block, GLState.h's save and
 *   restore of a host's state (the page owns its context), and the harness's
 *   negative-control hooks, which are all off as shipped.
 */
import { mountDemo } from './vendor/demo.js';
import { PassBuffer, Program, bindTexture } from './vendor/gl.js';
import { SOURCES, SourceRenderer } from './vendor/sources.js';
import * as S from './shaders.js';

const f32 = Math.fround;

// ==== PORT: Controls.h and Controls.cpp =====================================

const PT = {
  LIGHT: 0,
  PHOTOCATHODE: 1,
  GAIN: 2,
  PHOSPHOR: 3,
  GAP: 4,
  FIXED: 5,
  MIX: 6,
  ABOUT_TEXT: 7, // the About block follows in the plugin and is not here
};

const kGen2 = 0;
const kGen3 = 1;
const kP43 = 0;
const kP45 = 1;

const kLightLow = 1e-5;
const kLightHigh = 100.0;
const kGainLow = 1000.0;
const kGainHigh = 100000.0;
const kGapLow = 50e-6;
const kGapHigh = 500e-6;

const clamp = (value, lo, hi) => Math.min(hi, Math.max(lo, value));
/** Controls.cpp's clamp01: the host's float, as a double, clamped. */
const clamp01 = (v) => clamp(v, 0.0, 1.0);
/** std::lround: half away from zero, which Math.round is not. */
const lround = (v) => (v < 0 ? -Math.floor(-v + 0.5) : Math.floor(v + 0.5));

function geometric(v, low, high) {
  return low * Math.pow(high / low, clamp01(v));
}
function inverseGeometric(value, low, high) {
  const lo = Math.min(low, high);
  const hi = Math.max(low, high);
  return f32(Math.log(clamp(value, lo, hi) / low) / Math.log(high / low));
}

function OptionIndex(value, count) {
  return clamp(lround(value), 0, count - 1);
}

const LightFromParam = (v) => geometric(v, kLightLow, kLightHigh);
const ParamFromLight = (lux) => inverseGeometric(lux, kLightLow, kLightHigh);
const GainFromParam = (v) => geometric(v, kGainLow, kGainHigh);
const ParamFromGain = (fLfc) => inverseGeometric(fLfc, kGainLow, kGainHigh);
const GapFromParam = (v) => geometric(v, kGapLow, kGapHigh);
const ParamFromGap = (metres) => inverseGeometric(metres, kGapLow, kGapHigh);
const FixedFromParam = (v) => clamp01(v);

const kPhotocathodeNames = ['Gen 2', 'Gen 3'];
const kPhosphorNames = ['P43', 'P45'];

/** Controls.cpp's kTable: id, name, group, kind, default (the host's 0..1, or the element index), elements. */
const kTable = [
  { id: PT.LIGHT, name: 'Light Level', group: 'Scene', kind: 'standard', value: ParamFromLight(0.1), elements: null },
  { id: PT.PHOTOCATHODE, name: 'Photocathode', group: 'Tube', kind: 'option', value: f32(kGen3), elements: kPhotocathodeNames },
  { id: PT.GAIN, name: 'Gain', group: 'Tube', kind: 'standard', value: ParamFromGain(40000.0), elements: null },
  { id: PT.PHOSPHOR, name: 'Phosphor', group: 'Tube', kind: 'option', value: f32(kP43), elements: kPhosphorNames },
  { id: PT.GAP, name: 'Gap', group: 'Tube', kind: 'standard', value: ParamFromGap(175e-6), elements: null },
  { id: PT.FIXED, name: 'Fixed Pattern', group: 'Tube', kind: 'standard', value: f32(0.75), elements: null },
  { id: PT.MIX, name: 'Mix', group: 'Look', kind: 'standard', value: f32(1.0), elements: null },
];
if (kTable.length !== PT.ABOUT_TEXT || kTable.some((row, i) => row.id !== i)) {
  throw new Error('the parameter table is not in index order');
}

// ==== PORT: Tube.h and Tube.cpp ===============================================

const kPi = 3.14159265358979323846;

const tube = {
  kElementaryCharge: 1.602176634e-19,
  kPhotocathodeDiameter: 18e-3,
  kTubeArea: 2.54e-4,
  kFNumber: 1.2,
  kObjectiveTransmission: 0.9,
  kSensitivity: [280e-6, 1500e-6],
  kEbi: [1.0e-7, 2.0e-7],
  kBackscatter: 0.20,
  kOpenArea: 0.63,
  kDecayPerDecade: [1.2e-3, 1.5e-3],
  kLimitingResolution: 64e3,
  kContrastAtLimit: 0.05,
  kMaxLuminance: 3.0,
  kAbcTime: 0.2,
  kCathodeCurrentLimit: 80e-9,
  kMinDuty: 1e-7,
  kMultifibre: 0.45e-3,
  kBoundaryWidth: 8e-6,
  kBoundaryDepth: 0.30,
  kCellSpread: 0.04,
  kPhosphorXY: [[0.334, 0.561], [0.269, 0.311]],
  kMaxHaloCells: 6,
  kDefaultFrame: 1.0 / 60.0,
};
tube.kHaloFraction = tube.kBackscatter * (1.0 - tube.kOpenArea);

/** The decay time, s, of a phosphor: per decade -> e-folding. */
function DecayTime(phosphor) {
  return tube.kDecayPerDecade[phosphor === kP45 ? 1 : 0] / Math.log(10.0);
}

/**
 * The frame's factors, in double as in the C++. `dt` is the counting
 * interval, s (never 0). The harness's TubeHooks are all at their shipped
 * values, so the hook factors (tauScale, haloScale, cellScale, spotScale 1;
 * phosphorPerFrame and abcPerFrame false) are folded in.
 */
function Compute(s, width, height, dt) {
  const f = {};
  const gen = s.photocathode === kGen2 ? 0 : 1;
  const h = Math.max(height, 1);
  f.pitch = tube.kPhotocathodeDiameter / h;
  f.sensitivity = tube.kSensitivity[gen];
  f.electronsPerLux = (f.sensitivity * f.pitch * f.pitch * dt) / tube.kElementaryCharge;
  f.luxPerY = (s.sceneLux * tube.kObjectiveTransmission) / (4.0 * tube.kFNumber * tube.kFNumber);
  f.rate = f.electronsPerLux * f.luxPerY;
  f.ebiLux = tube.kEbi[gen];
  f.ebiRate = f.electronsPerLux * f.ebiLux;
  // 1 fL/fc = (1/pi) (cd/m^2)/lx.
  f.gain = s.gainFLfc / kPi;
  f.displayScale = f.gain / (f.electronsPerLux * 4.0 * tube.kMaxLuminance);

  f.tau = DecayTime(s.phosphor) * 1.0;
  const dtP = dt;
  const x = dtP / f.tau;
  f.decayA = Math.exp(-x);
  f.oneMinusA = -Math.expm1(-x);
  f.decayB = f.oneMinusA / x;
  f.oneMinusB = 1.0 - f.decayB;

  const dtA = dt;
  f.abcDecay = Math.exp(-dtA / tube.kAbcTime);

  f.haloRadiusPx = (2.0 * s.gap * 1.0) / f.pitch;
  f.haloFactor = Math.max(1, Math.trunc(Math.ceil(f.haloRadiusPx / tube.kMaxHaloCells - 1e-9)));

  f.cellPx = (tube.kMultifibre * 1.0) / f.pitch;
  f.boundaryHalfPx = (0.5 * tube.kBoundaryWidth) / f.pitch;
  // A Gaussian MTF exp( -2 pi^2 sigma^2 f^2 ) at 5% at the limiting resolution.
  const sigma = Math.sqrt(Math.log(1.0 / tube.kContrastAtLimit) / (2.0 * kPi * kPi)) / tube.kLimitingResolution;
  f.sigmaPx = (sigma * 1.0) / f.pitch;
  return f;
}

/** The landing radius's CDF in u = r / R: 1 - sqrt( 1 - u^2 ). */
function HaloCdf(u) {
  u = clamp(u, 0.0, 1.0);
  return 1.0 - Math.sqrt(1.0 - u * u);
}

/** IEEE remainder, as std::remainder: x - n y, n the nearest integer to x / y, ties to even. */
function remainder(x, y) {
  const q = x / y;
  let n = Math.round(q);
  if (Math.abs(q - Math.trunc(q)) === 0.5) n = 2 * Math.round(q / 2);
  return x - n * y;
}

/** Tube.cpp's raySquare: where the ray at theta is inside the unit square at ( cx, cy ). */
function raySquare(cosT, sinT, cx, cy) {
  let lo = 0.0;
  let hi = 1e300;
  const d = [cosT, sinT];
  const c = [cx, cy];
  for (let k = 0; k < 2; k += 1) {
    const a = c[k] - 0.5;
    const b = c[k] + 0.5;
    if (Math.abs(d[k]) < 1e-300) {
      if (a > 0.0 || b < 0.0) return null;
      continue;
    }
    let t0 = a / d[k];
    let t1 = b / d[k];
    if (t0 > t1) [t0, t1] = [t1, t0];
    lo = Math.max(lo, t0);
    hi = Math.min(hi, t1);
  }
  return hi > lo ? { enter: lo, exit: hi } : null;
}

/**
 * The halo's cell weights: the exact integral of the landing density over
 * each cell of a ( 2 radius + 1 )^2 window, by Tube.cpp's polar midpoint
 * rule (1024 angles a cell), normalised; floats, as the uniform takes them.
 */
function BuildHaloKernel(radiusPx, factor) {
  const k = { radiusPx, factor: Math.max(factor, 1), radius: 0, rawSum: 0, weights: null };
  const R = radiusPx / k.factor;
  k.radius = Math.max(0, Math.trunc(Math.ceil(R + 0.5)) - 1);
  const side = 2 * k.radius + 1;
  const w = new Float64Array(side * side);
  if (R <= 1e-9) {
    w[k.radius * side + k.radius] = 1.0;
  } else {
    const kSamples = 1024;
    for (let j = -k.radius; j <= k.radius; j += 1) {
      for (let i = -k.radius; i <= k.radius; i += 1) {
        let lo = 0.0;
        let span = 2.0 * kPi;
        if (i !== 0 || j !== 0) {
          const mid = Math.atan2(j, i);
          let a0 = 1e9;
          let a1 = -1e9;
          for (let c = 0; c < 4; c += 1) {
            const x = i + (c & 1 ? 0.5 : -0.5);
            const y = j + (c & 2 ? 0.5 : -0.5);
            let a = Math.atan2(y, x) - mid;
            a = remainder(a, 2.0 * kPi);
            a0 = Math.min(a0, a);
            a1 = Math.max(a1, a);
          }
          lo = mid + a0;
          span = a1 - a0;
        }
        let sum = 0.0;
        for (let s = 0; s < kSamples; s += 1) {
          const theta = lo + (span * (s + 0.5)) / kSamples;
          const hit = raySquare(Math.cos(theta), Math.sin(theta), i, j);
          if (!hit) continue;
          sum += HaloCdf(hit.exit / R) - HaloCdf(hit.enter / R);
        }
        w[(j + k.radius) * side + (i + k.radius)] = (sum * span) / kSamples / (2.0 * kPi);
      }
    }
  }
  let total = 0.0;
  for (const v of w) total += v;
  k.rawSum = total;
  k.weights = new Float32Array(w.length);
  for (let n = 0; n < w.length; n += 1) k.weights[n] = w[n] / total;
  return k;
}

/**
 * erf, the page's own (std::erf in the plugin): the Maclaurin series below
 * 2.5, 1 - erfc by its continued fraction (modified Lentz) above. Good to
 * about 1e-15, far inside the float the weights are rounded to.
 */
function erf(x) {
  const ax = Math.abs(x);
  if (ax === 0) return 0;
  let out;
  if (ax < 2.5) {
    // erf x = 2/sqrt(pi) sum_n (-1)^n x^(2n+1) / ( n! (2n+1) )
    let term = ax;
    let sum = ax;
    const x2 = ax * ax;
    for (let n = 1; n < 200; n += 1) {
      term *= -x2 / n;
      const add = term / (2 * n + 1);
      sum += add;
      if (Math.abs(add) < 1e-17 * Math.abs(sum)) break;
    }
    out = (2.0 / Math.sqrt(kPi)) * sum;
  } else {
    // erfc x = exp( -x^2 ) / sqrt(pi) * 1 / ( x + (1/2) / ( x + 1 / ( x + (3/2) / ( x + 2 / ( x + ... ))))).
    const tiny = 1e-300;
    let f = ax;
    let C = ax;
    let D = 0.0;
    for (let n = 1; n < 500; n += 1) {
      const an = n / 2;
      D = ax + an * D;
      if (D === 0) D = tiny;
      C = ax + an / C;
      if (C === 0) C = tiny;
      D = 1 / D;
      const delta = C * D;
      f *= delta;
      if (Math.abs(delta - 1) < 1e-16) break;
    }
    const erfc = Math.exp(-ax * ax) / Math.sqrt(kPi) / f;
    out = 1.0 - erfc;
  }
  return x < 0 ? -out : out;
}

/** Five pixel-integrated taps of the spot's Gaussian, normalised, as floats. */
function SpotWeights(sigmaPx) {
  const w = new Float64Array(5);
  let total = 0.0;
  const s = Math.max(sigmaPx, 1e-6) * Math.sqrt(2.0);
  for (let i = -2; i <= 2; i += 1) {
    w[i + 2] = 0.5 * (erf((i + 0.5) / s) - erf((i - 0.5) / s));
    total += w[i + 2];
  }
  const out = new Float32Array(5);
  for (let i = 0; i < 5; i += 1) out[i] = w[i] / total;
  return out;
}

/** The phosphor's colour in linear sRGB, normalised to Rec. 709 luminance 1. */
function PhosphorColour(phosphor) {
  const x = tube.kPhosphorXY[phosphor === kP45 ? 1 : 0][0];
  const y = tube.kPhosphorXY[phosphor === kP45 ? 1 : 0][1];
  const X = x / y;
  const Y = 1.0;
  const Z = (1.0 - x - y) / y;
  // IEC 61966-2-1, XYZ (D65) to linear sRGB.
  const rgb = [
    3.2406 * X - 1.5372 * Y - 0.4986 * Z,
    -0.9689 * X + 1.8758 * Y + 0.0415 * Z,
    0.0557 * X - 0.2040 * Y + 1.0570 * Z,
  ];
  const luminance = 0.2126 * rgb[0] + 0.7152 * rgb[1] + 0.0722 * rgb[2];
  return rgb.map((c) => c / luminance);
}

// ==== END PORT ================================================================

// ==== FRAME: Intensifier.cpp, the CPU half ====================================

/** Frames that must agree before the host's clock unit is settled. */
const kClockVotes = 4;
/** Frames of advancing host time after which an unsettled vote is decided by the step's size. */
const kClockPatience = 30;
/** Host seconds a single frame may advance by. */
const kMaxFrameDelta = 0.1;
/** The reduction's block, each axis. */
const kBlock = 16;
const kMaxLevels = 8;

/** Every instance its own sparks: a process-wide counter (here, page-wide). */
let gNextSeed = 0;

const wallStartMs = performance.now();
const wallSeconds = () => (performance.now() - wallStartMs) / 1000;

const ceilDiv = (a, b) => Math.trunc((a + b - 1) / b);

/** glUniform2i by name, counted as missing the way the kit's setters count. */
function setIvec2(program, name, a, b) {
  const location = program.location(name);
  if (location === null) {
    program.missing.add(name);
    return;
  }
  program.gl.uniform2i(location, a | 0, b | 0);
}

/** GLState.h's unbindTextureUnits. */
function unbindTextureUnits(gl, count) {
  for (let u = count - 1; u >= 0; u -= 1) {
    gl.activeTexture(gl.TEXTURE0 + u);
    gl.bindTexture(gl.TEXTURE_2D, null);
  }
}

/**
 * InitGL: AssembleVertex() is kVersion + kQuadVertex for every pass, and each
 * fragment stage is Assemble( pass ), as Shaders.cpp joins it.
 */
function compilePrograms(gl) {
  const vertex = S.VERSION + S.QUAD_VERTEX;
  const assemble = {
    rate: S.VERSION + S.COMMON + S.RATE_FRAGMENT,
    reduce: S.VERSION + S.REDUCE_FRAGMENT,
    control: S.VERSION + S.CONTROL_FRAGMENT,
    halo: S.VERSION + S.HALO_FRAGMENT,
    count: S.VERSION + S.RANDOM + S.SAMPLERS + S.COUNT_FRAGMENT,
    phosphor: S.VERSION + S.PHOSPHOR_FRAGMENT,
    resample: S.VERSION + S.RESAMPLE_FRAGMENT,
    composite: S.VERSION + S.COMMON + S.RANDOM + S.COMPOSITE_FRAGMENT,
  };
  const programs = {};
  for (const [name, fragment] of Object.entries(assemble)) programs[name] = new Program(gl, vertex, fragment, name);
  // A core profile draws nothing without a vertex array bound; the triangle
  // needs no attributes.
  programs.vao = gl.createVertexArray();
  return programs;
}

/** One SW Intensifier instance. */
class IntensifierInstance {
  constructor(gl, programs) {
    this.gl = gl;
    this.programs = programs;
    this.vao = programs.vao;
    // The plugin's float params[]: a Float32Array rounds as the host's float does.
    this.params = new Float32Array(PT.ABOUT_TEXT);
    for (let id = 0; id < PT.ABOUT_TEXT; id += 1) this.params[id] = kTable[id].value;
    this.seed = (gNextSeed = (gNextSeed + 1) >>> 0);
    this.frameIndex = 0;

    this.frame = null;
    this.kernel = { radiusPx: -1, factor: 0, radius: 0, weights: new Float32Array(1) };

    const nearest = () => new PassBuffer(gl, { filter: 'nearest' });
    this.rate = nearest();
    this.reduce = Array.from({ length: kMaxLevels }, nearest);
    this.levels = 0;
    this.coarse = nearest();
    this.halo = nearest();
    this.control = [nearest(), nearest()];
    this.controlCurrent = 0;
    this.haveControl = false;
    this.counts = nearest();
    this.store = [nearest(), nearest(), nearest()];
    this.storeCurrent = 0;
    this.storeWidth = 0;
    this.storeHeight = 0;

    // The clock.
    this.hostTime = -1.0;
    this.clockScale = 0.0;
    this.lastRawTime = -1.0;
    this.lastWallTime = -1.0;
    this.wallStart = -1.0;
    this.secondsVotes = 0;
    this.millisVotes = 0;
    this.clockSamples = 0;
    this.settledJump = false;
    this.now = 0.0;
    this.lastNow = -1.0;
    this.countingInterval = tube.kDefaultFrame;
  }

  /** The About block's indices are not here: a web page has links of its own. */
  SetFloatParameter(index, value) {
    if (index < 0 || index >= PT.ABOUT_TEXT) return false;
    this.params[index] = value;
    return true;
  }

  SetTime(time) {
    this.hostTime = time;
  }

  /** The harness's SetClockScaleForTest and SetSeedForTest, for the crosscheck. */
  SetClockScaleForTest(scale) {
    this.clockScale = scale;
  }
  SetSeedForTest(seed) {
    this.seed = seed >>> 0;
  }

  CurrentSettings() {
    const p = this.params;
    return {
      sceneLux: LightFromParam(p[PT.LIGHT]),
      photocathode: OptionIndex(p[PT.PHOTOCATHODE], 2),
      gainFLfc: GainFromParam(p[PT.GAIN]),
      phosphor: OptionIndex(p[PT.PHOSPHOR], 2),
      gap: GapFromParam(p[PT.GAP]),
      fixedPattern: FixedFromParam(p[PT.FIXED]),
    };
  }

  UpdateClock() {
    const wallNow = wallSeconds();
    if (this.wallStart < 0.0) this.wallStart = wallNow;
    const raw = this.hostTime;

    if (this.clockScale === 0.0 && raw >= 0.0 && this.lastRawTime >= 0.0 && this.lastWallTime >= 0.0) {
      const hostDelta = raw - this.lastRawTime;
      const wallDelta = wallNow - this.lastWallTime;
      if (hostDelta > 0.0 && wallDelta >= 0.0005) {
        const ratio = hostDelta / wallDelta;
        if (ratio > 0.1 && ratio < 10.0) this.secondsVotes += 1;
        else if (ratio > 100.0 && ratio < 10000.0) this.millisVotes += 1;
        if (this.secondsVotes >= kClockVotes || this.millisVotes >= kClockVotes) {
          this.clockScale = this.millisVotes > this.secondsVotes ? 0.001 : 1.0;
          this.settledJump = true;
        }
      }
      if (hostDelta > 0.0 && this.clockScale === 0.0 && (this.clockSamples += 1) >= kClockPatience) {
        this.clockScale = hostDelta < 1.0 ? 1.0 : 0.001;
        this.settledJump = true;
      }
    }
    if (raw >= 0.0) this.lastRawTime = raw;
    this.lastWallTime = wallNow;

    this.now = raw >= 0.0 && this.clockScale !== 0.0 ? raw * this.clockScale : wallNow - this.wallStart;
  }

  ensureBuffers(width, height) {
    const gl = this.gl;
    this.rate.ensure(width, height, gl.R32F);
    this.levels = 0;
    let w = width;
    let h = height;
    while ((w > 1 || h > 1) && this.levels < kMaxLevels) {
      w = ceilDiv(w, kBlock);
      h = ceilDiv(h, kBlock);
      this.reduce[this.levels].ensure(w, h, gl.R32F);
      this.levels += 1;
    }
    const f = this.frame.haloFactor;
    if (f > 1) this.coarse.ensure(ceilDiv(width, f), ceilDiv(height, f), gl.R32F);
    this.halo.ensure(ceilDiv(width, f), ceilDiv(height, f), gl.R32F);
    this.control[0].ensure(1, 1, gl.RGBA32F);
    this.control[1].ensure(1, 1, gl.RGBA32F);
    this.counts.ensure(width, height, gl.RGBA32F);
  }

  draw(target) {
    const gl = this.gl;
    gl.bindFramebuffer(gl.FRAMEBUFFER, target.fbo);
    gl.viewport(0, 0, target.width, target.height);
    gl.drawArrays(gl.TRIANGLES, 0, 3);
  }

  /**
   * ProcessOpenGL. `input` is { texture, width, height }; the composite goes
   * into `hostFBO` (null: the canvas) at a viewport of the input's size from
   * the origin. Returns false where the plugin returns FF_FAIL.
   */
  ProcessOpenGL(input, hostFBO = null) {
    const gl = this.gl;
    if (!input || !input.texture) return false;
    gl.disable(gl.BLEND);
    const width = input.width | 0;
    const height = input.height | 0;
    if (width <= 0 || height <= 0 || !this.vao) return false;

    this.UpdateClock();
    if (this.settledJump) {
      this.lastNow = -1.0;
      this.settledJump = false;
    }
    let dt = this.lastNow >= 0.0 ? clamp(this.now - this.lastNow, 0.0, kMaxFrameDelta) : 0.0;
    this.lastNow = this.now;
    // To the microsecond (std::round: dt is never negative here).
    dt = Math.round(dt * 1e6) * 1e-6;
    if (dt > 0.0) this.countingInterval = dt;

    this.frame = Compute(this.CurrentSettings(), width, height, this.countingInterval);
    if (this.kernel.factor !== this.frame.haloFactor || this.kernel.radiusPx !== this.frame.haloRadiusPx) {
      this.kernel = BuildHaloKernel(this.frame.haloRadiusPx, this.frame.haloFactor);
    }

    this.render(input, [0, 0, width, height], hostFBO, width, height);
    this.frameIndex = (this.frameIndex + 1) >>> 0;
    return true;
  }

  render(input, hostViewport, hostFBO, width, height) {
    const gl = this.gl;
    const P = this.programs;
    const frame = this.frame;
    this.ensureBuffers(width, height);
    gl.bindVertexArray(this.vao);
    const f = frame.haloFactor;
    const coarseW = ceilDiv(width, f);
    const coarseH = ceilDiv(height, f);

    // The phosphor's store, carried across a change of raster: resampled into
    // a fresh buffer before the old one is released.
    if (this.storeWidth > 0 && (this.storeWidth !== width || this.storeHeight !== height)) {
      const spare = (this.storeCurrent + 1) % 3;
      this.store[spare].ensure(width, height, gl.RG32F);
      P.resample.use();
      bindTexture(gl, 0, this.store[this.storeCurrent].texture);
      P.resample.setSampler('OldStore', 0);
      setIvec2(P.resample, 'OldSize', this.storeWidth, this.storeHeight);
      P.resample.set('Ratio', f32(this.storeWidth / width), f32(this.storeHeight / height));
      this.draw(this.store[spare]);
      this.storeCurrent = spare;
    }
    const next = (this.storeCurrent + 1) % 3;
    this.store[this.storeCurrent].ensure(width, height, gl.RG32F);
    this.store[next].ensure(width, height, gl.RG32F);
    this.storeWidth = width;
    this.storeHeight = height;

    // 1. rate
    P.rate.use();
    bindTexture(gl, 0, input.texture);
    P.rate.setSampler('InputTexture', 0);
    P.rate.setInt('Premultiply', 1);
    this.draw(this.rate);

    // 2. reduce, to one texel
    P.reduce.use();
    P.reduce.setSampler('Source', 0);
    P.reduce.setInt('Block', kBlock);
    P.reduce.set('Scale', 1.0);
    {
      let source = this.rate.texture;
      let w = width;
      let h = height;
      for (let level = 0; level < this.levels; level += 1) {
        bindTexture(gl, 0, source);
        setIvec2(P.reduce, 'SourceSize', w, h);
        this.draw(this.reduce[level]);
        source = this.reduce[level].texture;
        w = this.reduce[level].width;
        h = this.reduce[level].height;
      }
    }
    const total = this.levels > 0 ? this.reduce[this.levels - 1].texture : this.rate.texture;

    // 3. control
    const previous = this.controlCurrent;
    this.controlCurrent = 1 - this.controlCurrent;
    P.control.use();
    bindTexture(gl, 0, total);
    bindTexture(gl, 1, this.control[previous].texture);
    P.control.setSampler('Total', 0);
    P.control.setSampler('Previous', 1);
    P.control.set('PixelCount', width * height);
    P.control.set('LuxPerY', frame.luxPerY);
    P.control.set('EbiLux', frame.ebiLux);
    P.control.set('SensitivityArea', frame.sensitivity * tube.kTubeArea);
    P.control.set('CurrentLimit', tube.kCathodeCurrentLimit);
    P.control.set('MinDuty', tube.kMinDuty);
    P.control.set('Gain', frame.gain);
    P.control.set('MaxLuminance', tube.kMaxLuminance);
    P.control.set('AbcDecay', frame.abcDecay);
    P.control.setInt('Fresh', this.haveControl ? 0 : 1);
    P.control.setInt('AbcOn', 1);
    P.control.setInt('BspOn', 1);
    this.draw(this.control[this.controlCurrent]);
    this.haveControl = true;

    // 4. halo: the coarse grid's block means, then the gather
    let coarseTexture = this.rate.texture;
    if (f > 1) {
      P.reduce.use();
      bindTexture(gl, 0, this.rate.texture);
      setIvec2(P.reduce, 'SourceSize', width, height);
      P.reduce.setInt('Block', f);
      P.reduce.set('Scale', 1.0 / (f * f));
      this.draw(this.coarse);
      coarseTexture = this.coarse.texture;
    }
    P.halo.use();
    bindTexture(gl, 0, coarseTexture);
    P.halo.setSampler('Coarse', 0);
    setIvec2(P.halo, 'CoarseSize', coarseW, coarseH);
    P.halo.setInt('Radius', this.kernel.radius);
    P.halo.setArray('Weights', this.kernel.weights, 1);
    this.draw(this.halo);

    // 5. count
    P.count.use();
    bindTexture(gl, 0, this.rate.texture);
    bindTexture(gl, 1, this.halo.texture);
    bindTexture(gl, 2, this.control[this.controlCurrent].texture);
    P.count.setSampler('Rate', 0);
    P.count.setSampler('Halo', 1);
    P.count.setSampler('Control', 2);
    setIvec2(P.count, 'HaloSize', coarseW, coarseH);
    P.count.set('HaloFactor', f);
    P.count.set('RateScale', frame.rate);
    P.count.set('EbiRate', frame.ebiRate);
    P.count.set('HaloFraction', tube.kHaloFraction);
    P.count.setInt('SamplerModel', 0);
    P.count.setInt('GainModel', 0);
    P.count.setInt('RngModel', 0);
    P.count.setUint('FrameIndex', this.frameIndex);
    P.count.setUint('Seed', this.seed);
    this.draw(this.counts);

    // 6. phosphor
    P.phosphor.use();
    bindTexture(gl, 0, this.counts.texture);
    bindTexture(gl, 1, this.store[this.storeCurrent].texture);
    P.phosphor.setSampler('Counts', 0);
    P.phosphor.setSampler('Store', 1);
    P.phosphor.set('Decay', frame.decayA, frame.oneMinusA, frame.decayB, frame.oneMinusB);
    this.draw(this.store[next]);
    this.storeCurrent = next;

    // 7. composite, into the host's framebuffer and viewport
    gl.bindFramebuffer(gl.FRAMEBUFFER, hostFBO);
    gl.viewport(hostViewport[0], hostViewport[1], hostViewport[2], hostViewport[3]);
    P.composite.use();
    bindTexture(gl, 0, input.texture);
    bindTexture(gl, 1, this.store[this.storeCurrent].texture);
    bindTexture(gl, 2, this.control[this.controlCurrent].texture);
    P.composite.setSampler('InputTexture', 0);
    P.composite.setSampler('Store', 1);
    P.composite.setSampler('Control', 2);
    setIvec2(P.composite, 'ViewOrigin', hostViewport[0], hostViewport[1]);
    setIvec2(P.composite, 'Size', width, height);
    P.composite.setArray('Spot', SpotWeights(frame.sigmaPx), 1);
    P.composite.set('DisplayScale', frame.displayScale);
    const rgb = PhosphorColour(this.CurrentSettings().phosphor);
    P.composite.set('PhosphorRGB', rgb[0], rgb[1], rgb[2]);
    P.composite.set('MixAmount', clamp(this.params[PT.MIX], 0.0, 1.0));
    P.composite.setInt('AlphaModel', 0);
    const fixedDepth = FixedFromParam(this.params[PT.FIXED]);
    P.composite.set('CellPx', frame.cellPx);
    P.composite.set('BoundaryHalf', frame.boundaryHalfPx);
    P.composite.set('PatternDepth', fixedDepth * tube.kBoundaryDepth);
    P.composite.set('PatternSpread', fixedDepth * tube.kCellSpread);
    P.composite.set('FrameCentre', f32(0.5) * f32(width), f32(0.5) * f32(height));
    gl.drawArrays(gl.TRIANGLES, 0, 3);

    unbindTextureUnits(gl, 3);
    gl.activeTexture(gl.TEXTURE0);
    gl.useProgram(null);
    gl.bindVertexArray(null);
  }

  /** One texel: r g, g D, b the mean photocathode lux, a ln g (the harness's Control()). */
  readControl() {
    const gl = this.gl;
    const target = this.control[this.controlCurrent];
    if (!target.fbo) return null;
    gl.bindFramebuffer(gl.FRAMEBUFFER, target.fbo);
    const out = new Float32Array(4);
    gl.readPixels(0, 0, 1, 1, gl.RGBA, gl.FLOAT, out);
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
    return [out[0], out[1], out[2], out[3]];
  }

  dispose() {
    this.rate.dispose();
    for (const b of this.reduce) b.dispose();
    this.coarse.dispose();
    this.halo.dispose();
    for (const b of this.control) b.dispose();
    this.counts.dispose();
    for (const b of this.store) b.dispose();
    this.storeWidth = 0;
    this.storeHeight = 0;
    this.haveControl = false;
  }
}

// ==== END FRAME ===============================================================

//===========================================================================
// For demo/tools/crosscheck/ and the flash measurement: a hook a headless
// Chrome drives. Nothing on the page calls it.
//===========================================================================

/** RGBA8 given top row first, uploaded as ittest uploads a frame: float, value / 255, RGBA32F, LINEAR. */
function uploadFloatFrame(gl, rgba, width, height, texture = null) {
  const t = texture ?? gl.createTexture();
  const row = width * 4;
  const pixels = new Float32Array(row * height);
  for (let y = 0; y < height; y += 1) {
    const from = y * row;
    const to = (height - 1 - y) * row;
    for (let x = 0; x < row; x += 1) pixels[to + x] = rgba[from + x] / 255;
  }
  gl.bindTexture(gl.TEXTURE_2D, t);
  if (!texture) {
    // Every pass reads it by texelFetch, so the filter never acts; but a float
    // texture set to LINEAR without OES_texture_float_linear is incomplete,
    // and an incomplete texture fetches ( 0, 0, 0, 1 ).
    const filter = gl.getExtension('OES_texture_float_linear') ? gl.LINEAR : gl.NEAREST;
    gl.texStorage2D(gl.TEXTURE_2D, 1, gl.RGBA32F, width, height);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, filter);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, filter);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
  }
  gl.pixelStorei(gl.UNPACK_ALIGNMENT, 1);
  gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, false);
  gl.pixelStorei(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL, false);
  gl.texSubImage2D(gl.TEXTURE_2D, 0, 0, 0, width, height, gl.RGBA, gl.FLOAT, pixels);
  gl.bindTexture(gl.TEXTURE_2D, null);
  return t;
}

/** An RGBA32F target's pixels, quantised as ittest --pipe quantises: lround( clamp( v ) x 255 ), top row first. */
function readQuantised(gl, target) {
  const { width, height } = target;
  gl.bindFramebuffer(gl.FRAMEBUFFER, target.fbo);
  const raw = new Float32Array(width * height * 4);
  gl.readPixels(0, 0, width, height, gl.RGBA, gl.FLOAT, raw);
  gl.bindFramebuffer(gl.FRAMEBUFFER, null);
  const row = width * 4;
  const out = new Uint8Array(row * height);
  for (let y = 0; y < height; y += 1) {
    const from = (height - 1 - y) * row;
    const to = y * row;
    // lround( clamp( v, 0.0f, 1.0f ) * 255.0f ): the product rounded to a float first.
    for (let x = 0; x < row; x += 1) out[to + x] = lround(f32(clamp(raw[from + x], 0, 1) * 255));
  }
  return out;
}

/** The uniforms a program declares that nothing has set: each would sit at 0. */
function unsetUniforms(gl, program) {
  const set = new Set([...program.locations].filter(([, loc]) => loc !== null).map(([name]) => name));
  const out = [];
  const count = gl.getProgramParameter(program.program, gl.ACTIVE_UNIFORMS);
  for (let i = 0; i < count; i += 1) {
    const name = gl.getActiveUniform(program.program, i).name;
    if (!set.has(name) && !set.has(name.replace(/\[0\]$/, ''))) out.push(name);
  }
  return out;
}

const LINEAR = new Float64Array(256).map((_, i) => {
  const s = i / 255;
  return s <= 0.04045 ? s / 12.92 : ((s + 0.055) / 1.055) ** 2.4;
});

/**
 * flashcheck.py's levels, for one frame of RGBA8 (top row first): the whole
 * frame's mean linear luminance, then each of the 5 x 5 windows a third of
 * the frame each way, stepped by a sixth, row-major.
 */
function flashLevels(pixels, width, height) {
  const ww = Math.floor(width / 3);
  const wh = Math.floor(height / 3);
  const sx = Math.floor(width / 6);
  const sy = Math.floor(height / 6);
  const y = new Float64Array(width * height);
  let total = 0;
  for (let p = 0; p < y.length; p += 1) {
    const v = 0.2126 * LINEAR[pixels[4 * p]] + 0.7152 * LINEAR[pixels[4 * p + 1]] + 0.0722 * LINEAR[pixels[4 * p + 2]];
    y[p] = v;
    total += v;
  }
  const row = [total / y.length];
  for (let j = 0; j < 5; j += 1) {
    for (let i = 0; i < 5; i += 1) {
      let sum = 0;
      for (let yy = j * sy; yy < j * sy + wh; yy += 1) for (let xx = i * sx; xx < i * sx + ww; xx += 1) sum += y[yy * width + xx];
      row.push(sum / (ww * wh));
    }
  }
  return row;
}

function applySettings(run, settings) {
  for (const [name, value] of Object.entries(settings)) {
    const info = kTable.find((row) => row.name === name);
    if (!info) throw new Error(`no parameter called '${name}'`);
    const index = info.elements ? info.elements.indexOf(value) : -1;
    const number = index >= 0 ? index : Number(value);
    if (!Number.isFinite(number)) throw new Error(`'${value}' is not a number or an option of ${name}`);
    // ittest parses with strtof: a float.
    run.SetFloatParameter(info.id, f32(number));
  }
}

function installHook(gl, programs) {
  window.intensifierDemo = {
    table: kTable.map((row) => ({ name: row.name, kind: row.kind, value: row.value, elements: row.elements ?? [] })),

    /** The port's CPU numbers for a raster and settings, for tubedump's. */
    tube({ settings = {}, width, height, dt }) {
      const run = new IntensifierInstance(gl, programs);
      applySettings(run, settings);
      const s = run.CurrentSettings();
      const fr = Compute(s, width, height, dt);
      const k = BuildHaloKernel(fr.haloRadiusPx, fr.haloFactor);
      const out = { settings: s, frame: fr, kernel: { radius: k.radius, rawSum: k.rawSum, weights: [...k.weights] },
        spot: [...SpotWeights(fr.sigmaPx)], phosphor: PhosphorColour(s.phosphor), decayTime: DecayTime(s.phosphor),
        haloFraction: tube.kHaloFraction };
      run.dispose();
      return out;
    },

    /**
     * ittest --pipe's run, in the page: a FRESH instance on a seconds clock
     * (the rig's SetClockScaleForTest( 1.0 )), its seed set, the settings by
     * display name, then one frame per picture (RGBA8, top row first) at
     * SetTime( f / fps ), each composited into an RGBA32F target and read
     * back quantised as ittest writes it. With each frame, the control texel.
     */
    renderRun({ settings = {}, width, height, fps = 60, seed = 0, frames, negative = null }) {
      const run = new IntensifierInstance(gl, programs);
      run.SetClockScaleForTest(1.0);
      if (seed) run.SetSeedForTest(seed);
      const host = new PassBuffer(gl, { filter: 'nearest' });
      let texture = null;
      try {
        applySettings(run, settings);
        host.ensure(width, height, gl.RGBA32F);
        const pictures = [];
        const controls = [];
        let ok = true;
        frames.forEach((rgba, f) => {
          texture = uploadFloatFrame(gl, rgba, width, height, texture);
          run.SetTime(f / fps);
          // A negative control for the crosscheck: the frame counter never advances.
          if (negative === 'frozen-counter') run.frameIndex = 0;
          host.clearTo(0, 0, 0, 0);
          ok = run.ProcessOpenGL({ texture, width, height }, host.fbo) && ok;
          pictures.push(readQuantised(gl, host));
          controls.push(run.readControl());
        });
        const unset = Object.entries(programs).filter(([k]) => k !== 'vao').flatMap(([k, p]) => unsetUniforms(gl, p).map((u) => `${k}: ${u}`));
        const missing = Object.entries(programs).filter(([k]) => k !== 'vao').flatMap(([k, p]) => [...p.missing].map((u) => `${k}: ${u}`));
        return { ok, pictures, controls, missing, unset, error: gl.getError(), seed: run.seed, levels: run.levels,
          haloFactor: run.frame?.haloFactor, radius: run.kernel.radius };
      } finally {
        if (texture) gl.deleteTexture(texture);
        host.dispose();
        run.dispose();
      }
    },

    /**
     * The flash measurement: the page's own clip and a fresh instance on the
     * page's clock arithmetic (t = f / fps), `seconds` long, read back from an
     * RGBA8 target as the canvas would show it, reduced to flashcheck.py's
     * levels. `changes` is [ [ frame, { settings } ], ... ] applied as a
     * visitor would apply them.
     */
    flashRun({ clip = 'scene', settings = {}, changes = [], width = 960, height = 540, fps = 60, seconds = 10 }) {
      const run = new IntensifierInstance(gl, programs);
      const sources = new SourceRenderer(gl);
      const host = new PassBuffer(gl, { filter: 'nearest' });
      const source = SOURCES.find((s) => s.id === clip);
      if (!source) throw new Error(`no clip ${clip}`);
      try {
        applySettings(run, settings);
        host.ensure(width, height, gl.RGBA8);
        const levels = [];
        const gains = [];
        const n = Math.round(seconds * fps);
        const row = width * 4;
        const read = new Uint8Array(row * height);
        const top = new Uint8Array(row * height);
        for (let f = 0; f < n; f += 1) {
          for (const [at, change] of changes) if (at === f) applySettings(run, change);
          const input = sources.render(source, width, height, f / fps);
          run.SetTime(f / fps);
          host.clearTo(0, 0, 0, 0);
          run.ProcessOpenGL(input, host.fbo);
          gl.bindFramebuffer(gl.FRAMEBUFFER, host.fbo);
          gl.readPixels(0, 0, width, height, gl.RGBA, gl.UNSIGNED_BYTE, read);
          for (let y = 0; y < height; y += 1) top.set(read.subarray((height - 1 - y) * row, (height - y) * row), y * row);
          levels.push(flashLevels(top, width, height));
          gains.push(run.readControl());
        }
        gl.bindFramebuffer(gl.FRAMEBUFFER, null);
        return { levels, gains, error: gl.getError() };
      } finally {
        sources.dispose();
        host.dispose();
        run.dispose();
      }
    },
  };
}

//===========================================================================
// The parameters: the plugin's own, in its declaration's order and groups,
// with its names, element lists and defaults. Hints from the README.
//===========================================================================

const V = (v) => f32(v);

/** A number of lux as the README writes the sky: 0.0001, 0.002, 0.1, 3.4. */
function luxText(lux) {
  if (lux >= 10) return `${lux.toFixed(0)} lux`;
  if (lux >= 1) return `${lux.toFixed(1)} lux`;
  const digits = Math.max(0, -Math.floor(Math.log10(lux)) + 1);
  return `${lux.toFixed(Math.min(digits, 6))} lux`;
}
function skyAt(lux) {
  // The README's references, with the bands between them named as such.
  if (lux < 0.0005) return 'overcast starlight';
  if (lux < 0.01) return 'a clear moonless sky';
  if (lux < 0.04) return 'between a moonless sky and full moon';
  if (lux <= 0.35) return 'full moon';
  if (lux < 2) return 'between full moon and twilight';
  if (lux < 10) return 'the end of civil twilight';
  return 'brighter than twilight';
}

const SPEC = [
  ['light', PT.LIGHT, (v) => luxText(LightFromParam(V(v))),
    'The scene’s illuminance, 0.00001 to 100 lux, geometric: 0.0001 overcast starlight, 0.002 a clear moonless sky, 0.05–0.3 full moon, 3.4 the end of civil twilight. The clip is what the light falls on, its reflectance. Dark scenes are a sparse shimmer of single photoelectrons; as the light comes up the sparks thicken into a picture. A big step up flares the whole picture towards white for a moment, until the brightness control pulls it back over a second or two, as the real tube does.'],
  ['photocathode', PT.PHOTOCATHODE, null,
    'Gen 2: a multialkali photocathode, 280 µA/lm. Gen 3: gallium arsenide, 1500 µA/lm (Hamamatsu’s typical figures), so about five times the photoelectrons from the same light, and less grain.'],
  ['gain', PT.GAIN, (v) => `${Math.round(GainFromParam(V(v))).toLocaleString('en-GB')} fL/fc`,
    'The tube’s luminous gain, 1,000 to 100,000 fL/fc. It only matters below the point where the automatic brightness control holds the screen: above it the picture is the same whatever Gain says. Below it, Gain is the screen’s brightness.'],
  ['phosphor', PT.PHOSPHOR, null,
    'The screen: P43, the green one, 1.2 ms a decade; P45, white, 1.5 ms to 10%. At any frame rate both decay inside a frame or two; nothing smears.'],
  ['gap', PT.GAP, (v) => { const gap = GapFromParam(V(v)); return `${(gap * 1e6).toFixed(0)} µm`; },
    'The photocathode-to-plate gap, 50 to 500 µm. About one photoelectron in fourteen bounces off the plate and lands up to twice the gap away, so bright points get a halo brightest at its rim, its radius twice the gap on the tube’s 18 mm photocathode (0.7 mm across at the default 175 µm).'],
  ['fixed', PT.FIXED, (v) => `${(FixedFromParam(V(v)) * 100).toFixed(0)}%`,
    'The fibre optics’ honeycomb: hexagonal multifibres 0.45 mm across whose boundaries pass less light. It multiplies the screen, so it hides under the photon noise at low light and shows at high light.'],
  ['mix', PT.MIX, (v) => `${(clamp01(V(v)) * 100).toFixed(0)}%`,
    'The tube against the clip, in code values. At 0 the clip comes through untouched.'],
];

const PARAMS = SPEC.map(([id, slot, display, hint]) => {
  const row = kTable[slot];
  const p = { id, slot, name: row.name, type: row.kind, default: row.value, group: row.group, hint };
  if (row.elements) p.elements = row.elements;
  if (display) p.display = display;
  return p;
});

//===========================================================================
// The page.
//===========================================================================

const page = { instance: null, stats: '', control: null };

function createRenderer(gl) {
  const programs = compilePrograms(gl);
  const instance = new IntensifierInstance(gl, programs);
  page.instance = instance;
  installHook(gl, programs);
  let frames = 0;

  return {
    render({ input, params, time }) {
      // The host restates every parameter each frame.
      for (const p of PARAMS) instance.SetFloatParameter(p.slot, params.get(p.id));
      instance.SetTime(time);
      instance.ProcessOpenGL(input, null);

      // The line under the picture: the port's numbers, and every sixth frame
      // the control texel read back (a readback stalls the GPU, so not every
      // frame).
      frames += 1;
      if (frames % 6 === 1) page.control = instance.readControl();
      const fr = instance.frame;
      if (!fr) return;
      const c = page.control;
      const parts = [
        `${luxText(LightFromParam(instance.params[PT.LIGHT]))} (${skyAt(LightFromParam(instance.params[PT.LIGHT]))})`,
        `${input.width} × ${input.height}: a pixel ${(fr.pitch * 1e6).toFixed(1)} µm of photocathode`,
        `${fr.rate.toPrecision(3)} photoelectrons a pixel a frame on white`,
        `frame ${(instance.countingInterval * 1e3).toFixed(1)} ms`,
        `halo ${fr.haloRadiusPx.toFixed(1)} px on a ${fr.haloFactor}× grid`,
      ];
      if (c) {
        parts.push(`brightness control ×${c[0].toPrecision(3)}${c[0] < 0.999 ? ' (holding the screen)' : ' (full gain)'}`);
        parts.push(c[1] < 0.9999 ? `photocathode gated to ${(c[1] * 100).toPrecision(3)}%` : 'photocathode not gated');
        parts.push(`mean ${c[2].toPrecision(3)} lux on it`);
      }
      page.stats = parts.join(' · ');
    },
  };
}

const COMMON_GAPS = {
  es: 'GLSL ES 3.00 in WebGL2, not desktop GL 4.1 core. The shader text is the plugin’s; only the version line and precision qualifiers are changed by the kit. The random draws are pcg4d in 32-bit unsigned integers, which WebGL2’s highp guarantees, so they are the same integers; the rates they are compared against are floats through the driver’s pow, log and exp, which on another GPU can round a draw the other way.',
  presets: 'The presets are the page’s. The plugin declares none; each is only a combination of the plugin’s own parameters.',
  about: 'The About block (a text line and link buttons for the host’s panel) is absent: a web page has links of its own. So are the plugin’s log file and its harness’s negative-control hooks, all off here as in a shipped instance. There is no audio path, and the plugin has none.',
};

const mounted = mountDemo({
  name: 'SW Intensifier',
  kind: 'effect',
  pluginId: 'IT01',
  tagline:
    'Night vision that counts photons: an image intensifier tube, Gen 2 or Gen 3, looking at your clip. Light at its photocathode becomes a sparse stream of photoelectrons, each multiplied by the microchannel plate by a gain that is different every time, and each burst lights a spot of green or white phosphor. So the picture scintillates at starlight and thickens as the light comes up, halos ring bright points, the whole frame dims when something bright comes in, and a faint honeycomb shows at high light. None of it is drawn.',
  repo: 'https://github.com/stoatworks-labs/intensifier',
  blurb:
    'It is SW Intensifier’s own GLSL, its eight passes ported from the repository to WebGL2 unedited, run by a JavaScript port of the plugin’s CPU half (the tube’s frame factors, the halo’s kernel, the clock, the pass orchestration) that only a reader and a crosscheck against its harness check, on generated clips in this page — no install.',
  needFloat: true,
  showBackdrop: true,
  // Lights on black first: points for the halos, a dark ground for the sparks.
  sources: ['spot', 'scene', 'grid', 'bars', 'ramp', 'detail', 'alpha'],
  params: PARAMS,
  // The plugin declares no presets. These are the page's: combinations of
  // the plugin's own parameters and nothing else.
  presets: {
    'Overcast starlight (0.0001 lux): single photoelectrons': { light: ParamFromLight(0.0001) },
    'A clear moonless sky (0.002 lux)': { light: ParamFromLight(0.002) },
    'Gen 2 under a moonless sky: five times the grain': { light: ParamFromLight(0.002), photocathode: kGen2 },
    'The end of civil twilight (3.4 lux): the honeycomb shows': { light: ParamFromLight(3.4) },
    'Daylight on a gated tube (100 lux): the grain comes back': { light: ParamFromLight(100) },
    'White phosphor (P45)': { phosphor: kP45 },
    'Wide gap (500 µm): big halos': { gap: ParamFromGap(500e-6) },
    'Close gap (50 µm): tight halos': { gap: ParamFromGap(50e-6) },
    'Low gain at starlight: a dim screen': { light: ParamFromLight(0.0003), gain: ParamFromGain(2000) },
    'No honeycomb (Fixed Pattern 0)': { fixed: 0 },
    'Half tube, half clip (Mix 0.5)': { mix: 0.5 },
  },
  differences: [
    'The CPU half is a port. The eight shaders are the plugin’s, unedited. What runs them is translated to JavaScript by hand in demo/plugin.js: Controls.cpp’s conversions and defaults; Tube.cpp’s frame factors (the photoelectrons a pixel expects, the dark emission, the display scale, the phosphor’s and the brightness control’s decay over the frame, the halo’s radius and grid, the honeycomb’s cell, the spot), its halo kernel (each cell’s share of the landings, by the same 1024-angle polar quadrature), the spot’s weights and the phosphor’s colour; and Intensifier.cpp’s clock, frame length and pass orchestration — the rate pass, the 16 × 16 reduction to one texel, the control texel’s ping-pong, the halo’s coarse grid and gather, the count, the phosphor’s three stores, the resample on a change of size, the composite, and every uniform. demo/tools/crosscheck/ compares this page with the plugin’s harness frame for frame (AGENTS.md has the figures); outside those cases, only a reader checks it. demo/tools/check_shaders.py fails the repository’s verify script if a character of the shaders drifts.',
    'The sparks are the plugin’s draws, not a recording: every random number is pcg4d of the pixel, a frame counter and the instance’s seed, as in the plugin, and this page’s instance is the first in its process, seed 1. Given the same frames, the same seed and the same clock, the plugin’s harness draws the same photoelectrons: on an Apple M4 Max, ittest and this page in Chrome agreed on all but one byte of 16 seeded runs at two sizes (one byte of 99.5 million different, by one level, at 480 × 270; none of 398 million at 960 × 540). On another GPU a rate rounded a last bit differently can flip a draw, and that pixel then differs by a photoelectron’s light.',
    'erf is the page’s. The spot’s five weights come from std::erf in the plugin; JavaScript has none, so the page computes it by its series and continued fraction, to about 1e-15, far inside the float the weights are rounded to. The crosscheck compares the weights.',
    'The frame’s height spans the tube’s 18 mm photocathode, in the plugin and here, so a pixel’s share of the light depends on the Composition size: at 960 × 540 each pixel collects four times what it would at 1920 × 1080, and the picture is that much less grainy. The plugin does the same at whatever size Resolume renders.',
    'A frame here is one of the display’s refreshes (60 or 120 a second, or whenever the browser gets round to it), not Resolume’s output frame. Each frame counts photoelectrons over its own length, clamped to a tenth of a second and rounded to the microsecond, and the phosphor and the brightness control decay over it, so a slower display shows the same tube at a slower rate. The clock is the page’s, in seconds; the plugin’s vote on whether its host sends seconds or milliseconds is ported and settles on seconds. Paused, the clock stands still and a frame is drawn only when a control moves: the plugin then runs the tube on the last frame’s length, as it does in a host that has stopped its clock, so the sparks change. Restart is the clock going backwards, which the plugin takes as no time passing.',
    'The clips are the kit’s, generated in the page, and premultiplied, as is a picture of your own here. The plugin weights the clip’s light by its alpha, which is right for straight alpha; on a premultiplied clip a soft edge is weighted twice. Whether Resolume hands an effect straight or premultiplied colour is an open question in the README, and only the “Shape on transparency” clip has an edge to show it. The plugin’s defaults were set on Resolume’s own demo clips, where at 0.1 lux the median clip just engages the brightness control; the kit’s clips are brighter, so at the defaults the control is already holding the screen on every one of them (at gains from 0.77 on the geometry card down to 0.035 on the 100% bars), and on the bright ones Light Level changes the grain but not the brightness until it is turned well down.',
    'A picture or video of your own runs at its own size, which is then the raster the photocathode spans; changing Composition afterwards does not resize it.',
    'The output is the page’s 8-bit canvas, as the plugin’s is a host’s buffer. At Mix 1 it is opaque; below 1, over the clip’s transparent parts, what is behind shows through as the browser composites it.',
    COMMON_GAPS.es,
    'The line under the picture is not the plugin. It reports what the port computed for the frame — the pixel’s size on the photocathode, the photoelectrons a white pixel expects, the frame’s length, the halo’s size — and, every sixth frame, the plugin’s control texel read back: the brightness control’s gain, the gate’s duty and the mean light on the photocathode. The plugin draws no such thing.',
    COMMON_GAPS.presets,
    COMMON_GAPS.about,
    'Nothing here is measured. The plugin’s numerical proof — the counts Poisson from 4.4e-5 to 5000 a pixel a frame, the halo’s rim at twice the gap, the brightness control holding the screen within 0.13% across a hundredfold step, 27 wrong models and 36 one-character mutants caught — is tools/ittest in the repository, and that harness, not this page, is the reason to believe the tube. What Resolume itself has shown is in the README’s Status.',
  ],
  createRenderer,
});

// The line under the picture.
const statLine = document.createElement('p');
statLine.className = 'stage__status';
statLine.setAttribute('aria-live', 'off');
statLine.dataset.role = 'intensifier-stats';
document.querySelector('.stage')?.append(statLine);
if (statLine.isConnected) {
  const tick = () => {
    if (statLine.textContent !== page.stats) statLine.textContent = page.stats;
    requestAnimationFrame(tick);
  };
  requestAnimationFrame(tick);
}

// For a headless check: the mounted page.
page.mounted = mounted ?? null;
window.intensifierPage = page;
