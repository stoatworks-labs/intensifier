"""Resolume's own demo clips through `ittest --pipe`: the alpha check, and
the measurements the defaults were set from.

    tools/clips.py alpha [--build DIR]      the output alpha on the DXV alpha clips
    tools/clips.py defaults [--build DIR]   where the demo clips put the ABC's knee,
                                            and the honeycomb against the photon noise

The clips are Resolume Arena's (`/Applications/Resolume Arena/media`), DXV
with alpha, decoded by ffmpeg (`-nostdin`: ffmpeg reads the terminal
otherwise, and a script running it in a loop loses its input). Nothing here
is a second copy of the plugin's formulas: every number is read off the
pipe's frames or its `--stats` lines.

**alpha.** The plugin's decision: output alpha = mix( a, 1, Mix ) -- the
screen fills the frame, so at Mix 1 the output is opaque, and the clip's
light reaches the photocathode weighted by its alpha (a transparent pixel
sends the tube nothing). Checked on BattleWeapon_Tank_09 and Ethnik2, the two
clips with real transparency: at Mix 1 every output alpha is 255; at Mix 0.5
every output alpha is within half a code of a / 2 + 127.5; and white painted
under every fully transparent pixel changes no output byte (the clips are
straight alpha over black, so without the paint a plugin that ignored alpha
would pass). The negative control paints ~200 opaque pixels instead, which
must show.

**defaults.** Light Level: for each demo clip, the scene illuminance at which
the ABC first engages (the frames' median ABC gain falls below 1), found by
bisection in log lux on the pipe's own --stats. The default is the median
clip's. Fixed Pattern: on that median clip at 1280x720, the honeycomb's rms
effect on the picture (Fixed Pattern 1 against 0, one seed, so the photons
are the same draws) against one frame's photon noise (two seeds at Fixed
Pattern 0, the difference over sqrt 2), at the default light and at 100x.
"""

import argparse
import math
import pathlib
import statistics
import subprocess
import sys

import numpy as np

REPO = pathlib.Path(__file__).resolve().parent.parent
MEDIA = pathlib.Path("/Applications/Resolume Arena/media")
ALPHA_CLIPS = ["Shop74/BattleWeapon_Tank_09.mov", "Shop74/Ethnik2.mov"]


def light_param(lux):
    """Light Level is 1e-5 .. 100 lux, geometric (the spec)."""
    return math.log(lux / 1e-5) / math.log(1e7)


def decode(path, width, height, frames, start=1.0):
    args = ["ffmpeg", "-nostdin", "-v", "error", "-ss", str(start), "-i", str(path), "-frames:v", str(frames),
            "-vf", f"scale={width}:{height}", "-f", "rawvideo", "-pix_fmt", "rgba", "-"]
    raw = subprocess.run(args, capture_output=True, check=True).stdout
    size = width * height * 4
    got = len(raw) // size
    if got == 0:
        raise RuntimeError(f"ffmpeg decoded nothing from {path}")
    return raw[:got * size], got


def pipe(ittest, raw, width, height, settings, seed=None, stats=False, fps=30):
    args = [str(ittest), "--pipe", "--size", f"{width}x{height}", "--fps", str(fps)]
    for s in settings:
        args += ["--set", s]
    if seed is not None:
        args += ["--seed", str(seed)]
    if stats:
        args += ["--stats"]
    result = subprocess.run(args, input=raw, capture_output=True)
    if result.returncode != 0:
        raise RuntimeError(f"ittest --pipe failed ({result.returncode}): {result.stderr.decode()[-400:]}")
    frames = np.frombuffer(result.stdout, dtype=np.uint8).reshape(-1, height, width, 4)
    lines = [ln.split() for ln in result.stderr.decode().splitlines() if ln.startswith("frame ")]
    gains = [float(ln[3]) for ln in lines]
    return frames, gains


def linear(codes):
    c = codes.astype(np.float64) / 255.0
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def luminance(frames):
    lin = linear(frames[..., :3])
    return 0.2126 * lin[..., 0] + 0.7152 * lin[..., 1] + 0.0722 * lin[..., 2]


def alpha_check(ittest):
    width, height, count = 320, 180, 20
    failures, negative_caught = 0, 0
    for name in ALPHA_CLIPS:
        path = MEDIA / name
        if not path.exists():
            print(f"  FAIL  {path} is missing: the alpha check needs Resolume Arena's demo clips")
            return 1
        raw, frames = decode(path, width, height, count)
        clip = np.frombuffer(raw, dtype=np.uint8).reshape(frames, height, width, 4)
        a = clip[..., 3].astype(np.float64)
        full, _ = pipe(ittest, raw, width, height, ["Mix=1"], seed=1)
        half, _ = pipe(ittest, raw, width, height, ["Mix=0.5"], seed=1)
        opaque_alpha = int(full[..., 3].min())
        off_half = float(np.abs(half[..., 3].astype(np.float64) - (a / 2.0 + 127.5)).max())
        # The clips are straight alpha over black: where a = 0 the colour is
        # (almost always) black already, so a plugin that ignored alpha would
        # look the same. Paint every fully transparent pixel white: the output
        # must not change by a single byte (one seed, so the same photons).
        white = clip.copy()
        hidden = white[..., 3] == 0
        white[..., :3][hidden] = 255
        painted, _ = pipe(ittest, white.tobytes(), width, height, ["Mix=1"], seed=1)
        changed = int(np.count_nonzero(painted != full))
        # The negative control: the same paint on a few opaque pixels must show.
        shown = clip.copy()
        opaque = np.argwhere(shown[..., 3] == 255)
        pick = opaque[:: max(1, len(opaque) // 200)]
        shown[pick[:, 0], pick[:, 1], pick[:, 2], :3] = 255
        control, _ = pipe(ittest, shown.tobytes(), width, height, ["Mix=1"], seed=1)
        negative_caught += int(np.count_nonzero(control != full)) > 0
        ok = opaque_alpha == 255 and off_half <= 0.5 + 1e-9 and changed == 0
        failures += not ok
        print(f"  {'ok  ' if ok else 'FAIL'}  {path.name}: {frames} frames at {width}x{height}, {hidden.mean():.0%} fully "
              f"transparent; Mix 1: alpha >= {opaque_alpha} (want 255); Mix 0.5: alpha within {off_half:.2f} codes of "
              f"a/2 + 127.5 (tolerance 0.5, the byte's rounding); white painted under every transparent pixel changes "
              f"{changed} output bytes (want 0)")
    print(f"  {'ok  ' if negative_caught == len(ALPHA_CLIPS) else 'FAIL'}  negative control: white painted on ~200 "
          f"opaque pixels shows in the output on {negative_caught} of {len(ALPHA_CLIPS)} clips")
    failures += negative_caught != len(ALPHA_CLIPS)
    print(f"\n  {'PASS' if failures == 0 else 'FAIL'}")
    return 0 if failures == 0 else 1


def knee(ittest, raw, width, height):
    """The lowest scene lux at which the frames' median ABC gain is below 1."""
    lo, hi = math.log(1e-5), math.log(100.0)

    def engaged(log_lux):
        _, gains = pipe(ittest, raw, width, height, [f"Light Level={light_param(math.exp(log_lux)):.7f}"], stats=True)
        return statistics.median(gains[5:]) < 1.0

    if not engaged(hi):
        return None
    for _ in range(14):
        mid = 0.5 * (lo + hi)
        if engaged(mid):
            hi = mid
        else:
            lo = mid
    return math.exp(hi)


def defaults(ittest):
    clips = sorted(p for p in (MEDIA / "AV").glob("*.mov")) + sorted(p for p in (MEDIA / "Shop74").glob("*.mov"))
    if not clips:
        print(f"no demo clips under {MEDIA}")
        return 1
    width, height = 640, 360
    knees = []
    for path in clips:
        raw, frames = decode(path, width, height, 30)
        k = knee(ittest, raw, width, height)
        knees.append((k if k is not None else math.inf, path))
        print(f"  {path.parent.name}/{path.name:28s} ABC engages at {k:.4g} lux" if k else
              f"  {path.parent.name}/{path.name:28s} the ABC never engages below 100 lux")
    knees.sort(key=lambda kp: kp[0])
    median_lux, median_clip = knees[len(knees) // 2]
    finite = [k for k, _ in knees if math.isfinite(k)]
    print(f"\n{len(clips)} clips: the ABC engages from {finite[0]:.3g} to {finite[-1]:.3g} lux; the median clip, "
          f"{median_clip.name}, at {median_lux:.4g} lux (Light Level {light_param(median_lux):.4f})")

    # The honeycomb against the photon noise on the median clip, 1280x720.
    width, height = 1280, 720
    raw, frames = decode(median_clip, width, height, 12)

    def honeycomb(scale, fixed):
        light = f"Light Level={light_param(min(100.0, median_lux * scale)):.7f}"
        plain, _ = pipe(ittest, raw, width, height, [light, "Fixed Pattern=0"], seed=1)
        other, _ = pipe(ittest, raw, width, height, [light, "Fixed Pattern=0"], seed=2)
        comb, _ = pipe(ittest, raw, width, height, [light, f"Fixed Pattern={fixed}"], seed=1)
        y0, y1, yc = luminance(plain[4:]), luminance(other[4:]), luminance(comb[4:])
        noise = float(np.sqrt(np.mean((y0 - y1) ** 2) / 2.0))
        pattern = float(np.sqrt(np.mean((yc - y0) ** 2)))
        print(f"  {median_clip.name} at {scale:g}x the knee, 1280x720: the honeycomb at Fixed Pattern {fixed} moves a "
              f"pixel by {pattern:.4g} rms; one frame's photon noise is {noise:.4g} rms (ratio {pattern / noise:.3f})")
        return pattern / noise

    honeycomb(1.0, 1)
    ratio = honeycomb(100.0, 1)
    # The criterion: in a bright scene (100x the knee) the honeycomb is as
    # strong as one frame's photon noise, so it shows there (the eye averages
    # the noise over frames, not the pattern) and hides under the
    # scintillation at the knee. Fixed Pattern to the nearest 0.05.
    fixed = round(min(1.0, 1.0 / ratio) * 20) / 20
    print(f"\nFixed Pattern {fixed:g} puts the honeycomb at one frame's photon noise at 100x the knee; measured there:")
    honeycomb(1.0, fixed)
    honeycomb(100.0, fixed)
    return 0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("what", choices=["alpha", "defaults"])
    parser.add_argument("--build", type=pathlib.Path, default=REPO / "build")
    args = parser.parse_args()
    build = args.build if args.build.is_absolute() else REPO / args.build
    ittest = build / "ittest"
    if not ittest.exists():
        print(f"{ittest} not found", file=sys.stderr)
        return 1
    if args.what == "alpha":
        print("\n=== alpha on a real clip: Resolume's DXV alpha clips through --pipe")
        return alpha_check(ittest)
    return defaults(ittest)


if __name__ == "__main__":
    sys.exit(main())
