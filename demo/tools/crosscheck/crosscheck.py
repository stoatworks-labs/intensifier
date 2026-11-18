"""The browser demo against the plugin: the same frames, the same seed, two renderers.

    python3 demo/tools/crosscheck/crosscheck.py [--ittest build/ittest] [--size 480x270] [--frames 12] [--keep DIR]

Each run below is rendered twice:

  - by the PLUGIN: `ittest --pipe --seed N --stats` (the real IntensifierPlugin
    class in a GL 4.1 context, on the harness's seconds clock at --fps), fed
    the frames on stdin.
  - by the PAGE: demo/ served from here and loaded in headless Chrome,
    through window.intensifierDemo.renderRun, which makes a fresh instance,
    puts it on the same clock (SetClockScaleForTest( 1.0 ), SetTime( f / fps )),
    gives it the same seed, uploads each frame as ittest does (value / 255
    into RGBA32F) and reads the composite back from an RGBA32F target,
    quantised as ittest quantises it.

The frames are generated here: straight alpha, a dim graded ground, three
bright discs that move, fine stripes, and a card whose alpha ramps from 0 to 1.

The effect is stochastic, but not random: every draw is pcg4d of the pixel,
the frame counter and the seed, so with the same seed and the same clock the
plugin and the page draw the same photoelectrons wherever the GPU computes
the same rates. That makes a BIT comparison possible, and that is what this
does: it counts the bytes that are identical, frame by frame, over the run.
Where a rate rounds differently (a pow, log or exp a last bit apart between
GL 4.1 and WebGL2 through ANGLE on Metal) a draw can come out one count
different, and that pixel then differs by a whole photoelectron's light, so
the max difference is not the measure; the share of identical bytes is, with
the frame means as the statistics. The supply's control texel (the ABC's
gain, the gate's duty, the mean photocathode lux) is compared with ittest's
--stats lines float for float.

The CPU half is compared without a GPU too: tubedump.cpp, built here from
source/Tube.cpp and source/Controls.cpp with the plugin's own Release flags,
prints Compute's frame factors, the halo kernel, the spot and the phosphor's
colour, and the page's port (window.intensifierDemo.tube) works out the same.
Before any picture, the page's table of controls (names, kinds, defaults) is
compared with `ittest --list`; the option elements are compared by passing
them to ittest by name in the runs below (it refuses a name it does not have).

A run passes when at least --exact % of its bytes are identical and every
frame's mean is within --mean levels of 255. Negative controls -- the page
with one control one slider step (0.001) off, the seed one off, the frame
counter held still -- are compared with the plugin's run as it should be, and
each must fail that test, so the comparison is shown to see a slip of that
size.

Not run by tools/verify.sh: it needs a GPU and Chrome. Exit 1 if a table
differs, a uniform is missing or unset, a run or a CPU number fails, or a
negative control passes.
"""
import argparse
import functools
import http.server
import json
import math
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import threading

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
DEMO = os.path.abspath(os.path.join(HERE, "..", ".."))
REPO = os.path.abspath(os.path.join(DEMO, ".."))
sys.dont_write_bytecode = True

# name, settings as `ittest --set` takes them (host values; options by element
# name), fps, seed.
RUNS = [
    ("defaults", {}, 60, 1),
    ("gen2", {"Photocathode": "Gen 2"}, 60, 1),
    ("starlight", {"Light Level": "0.15"}, 60, 1),
    ("no-light", {"Light Level": "0"}, 24, 7),
    ("twilight", {"Light Level": "0.8"}, 60, 1),
    ("gated-100-lux", {"Light Level": "1"}, 60, 1),
    ("low-gain-starlight", {"Light Level": "0.2", "Gain": "0.1"}, 60, 1),
    ("p45", {"Phosphor": "P45"}, 60, 1),
    ("gap-500um", {"Gap": "1"}, 60, 1),
    ("gap-50um", {"Gap": "0"}, 60, 1),
    ("fixed-0", {"Fixed Pattern": "0"}, 60, 1),
    ("fixed-1", {"Fixed Pattern": "1", "Light Level": "0.9"}, 60, 1),
    ("mix-half", {"Mix": "0.5"}, 60, 1),
    ("mix-0", {"Mix": "0"}, 60, 1),
    ("30fps", {}, 30, 1),
    ("seed-4e9", {}, 60, 4000000000),
]
# The negative controls: a run rendered by the page with one thing slightly
# off, compared with the plugin's run as it should be.
NEGATIVES = [
    ("defaults", "light-1-step", {"Light Level": "0.5724"}, None, None, "Light Level one slider step (0.001) off"),
    ("defaults", "gap-1-step", {"Gap": "0.5451"}, None, None, "Gap one step off"),
    ("defaults", "fixed-1-step", {"Fixed Pattern": "0.751"}, None, None, "Fixed Pattern one step off"),
    # Gain acts only below the ABC's knee (README): at the defaults the ABC
    # takes a Gain step straight back out, so this one is at starlight.
    ("low-gain-starlight", "gain-1-step", {"Gain": "0.101"}, None, None, "Gain one step off (starlight)"),
    ("defaults", "seed-plus-1", {}, 2, None, "the seed one off"),
    ("defaults", "frozen-counter", {}, None, "frozen-counter", "the frame counter never advancing"),
]
# The CPU numbers: host values (light, photocathode, gain, phosphor, gap,
# fixed), raster, counting interval.
TUBES = [
    ({}, 960, 540, 1 / 60),
    ({}, 1920, 1080, 1 / 60),
    ({}, 320, 180, 1 / 30),
    ({"Light Level": "0", "Photocathode": "0", "Gain": "0", "Phosphor": "1", "Gap": "0", "Fixed Pattern": "0"},
     1280, 720, 0.1),
    ({"Light Level": "1", "Gain": "1", "Gap": "1", "Fixed Pattern": "1"}, 640, 360, 1e-6),
    ({"Gap": "1"}, 3840, 2160, 1 / 120),
    ({"Gap": "0.3", "Light Level": "0.33"}, 1024, 768, 0.016667),
    ({"Gap": "0.77"}, 720, 576, 0.04),
    ({}, 1, 1, 1 / 60),
]
ORDER = ["Light Level", "Photocathode", "Gain", "Phosphor", "Gap", "Fixed Pattern"]
OPTIONS = {"Photocathode": ["Gen 2", "Gen 3"], "Phosphor": ["P43", "P45"]}


def parse_size(text):
    w, h = text.lower().split("x")
    return int(w), int(h)


def scene(width, height, count):
    """The frames, RGBA8 straight alpha, top row first."""
    y, x = np.mgrid[0:height, 0:width].astype(np.float64)
    u, v = (x + 0.5) / width, (y + 0.5) / height
    out = []
    for f in range(count):
        t = f / 60.0
        grey = 0.06 + 0.30 * u + 0.08 * np.sin(2 * math.pi * (u * 9 + v * 3))
        grey += 0.05 * ((np.floor(x / 2) + np.floor(y / 2)) % 2) * (v > 0.8)
        rgb = np.stack([grey * 0.9, grey, grey * 1.1], axis=-1)
        alpha = np.ones((height, width))
        for cx, cy, r, s in ((0.2, 0.3, 0.05, 0.6), (0.55, 0.6, 0.03, -0.4), (0.8, 0.25, 0.015, 0.2)):
            px, py = cx + s * t, cy + 0.1 * math.sin(3 * t + cx * 7)
            d = np.hypot((u - px) * width / height, v - py)
            rgb[d < r] = 1.0
        card = (u > 0.62) & (u < 0.95) & (v > 0.55) & (v < 0.9)
        rgb[card] = [0.85, 0.7, 0.5]
        alpha[card] = ((u[card] - 0.62) / 0.33)
        rgba = np.concatenate([np.clip(rgb, 0, 1), alpha[..., None]], axis=-1)
        out.append(np.round(rgba * 255).astype(np.uint8).tobytes())
    return out


def run_ittest(ittest, args, env, stdin=b""):
    done = subprocess.run([ittest, *args], input=stdin, capture_output=True, check=False, env=env)
    if done.returncode != 0:
        sys.exit(f"ittest {' '.join(args)} failed ({done.returncode}): {done.stderr.decode(errors='replace').strip()}")
    return done.stdout, done.stderr.decode(errors="replace")


STATS = re.compile(r"^frame (\d+) gain (\S+) duty (\S+) photocathode (\S+)$")
f32 = lambda value: struct.unpack("f", struct.pack("f", value))[0]


# ---------------------------------------------------------------- the server
class Handler(http.server.SimpleHTTPRequestHandler):
    """demo/ as it is, plus /xc/ for the job, the frames and the page's answers."""

    def __init__(self, *args, state, **kwargs):
        self.state = state
        super().__init__(*args, directory=DEMO, **kwargs)

    def log_message(self, *args):
        pass

    def do_GET(self):
        if self.path == "/xc/job.json":
            return self.reply(json.dumps(self.state["job"]).encode(), "application/json")
        if self.path.startswith("/xc/frame/"):
            return self.reply(self.state["frames"][int(self.path[len("/xc/frame/"):])], "application/octet-stream")
        return super().do_GET()

    def do_POST(self):
        body = self.rfile.read(int(self.headers.get("Content-Length", 0)))
        if self.path.startswith("/xc/out/"):
            self.state["page"][self.path[len("/xc/out/"):]] = body
        elif self.path.startswith("/xc/control/"):
            self.state["controls"][self.path[len("/xc/control/"):]] = json.loads(body)
        elif self.path == "/xc/table":
            self.state["table"] = json.loads(body)
        elif self.path == "/xc/tubes":
            self.state["tubes"] = json.loads(body)
        elif self.path == "/xc/missing":
            self.state["missing"] = json.loads(body)
        elif self.path == "/xc/error":
            self.state["error"] = body.decode(errors="replace")
            self.state["finished"].set()
        elif self.path == "/xc/done":
            self.state["finished"].set()
        self.reply(b"ok", "text/plain")

    def reply(self, body, kind):
        self.send_response(200)
        self.send_header("Content-Type", kind)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


# ---------------------------------------------------------------- the judging
def compare(a, b, width, height, frames):
    x = np.frombuffer(a, np.uint8).reshape(frames, height, width, 4).astype(np.int16)
    y = np.frombuffer(b, np.uint8).reshape(frames, height, width, 4).astype(np.int16)
    diff = np.abs(x - y)
    worst = diff.max(axis=3)
    means = np.abs(x[..., :3].mean(axis=(1, 2, 3)) - y[..., :3].mean(axis=(1, 2, 3)))
    return {
        "bytes": float((diff == 0).mean() * 100.0),
        "differ": int((diff != 0).sum()),
        "pixels": float((worst == 0).mean() * 100.0),
        "within4": float((worst <= 4).mean() * 100.0),
        "max": int(worst.max()),
        "mean": float(means.max()),
        "first": float((worst[0] == 0).mean() * 100.0),
    }


def passes(s, args):
    return s["bytes"] >= args.exact and s["mean"] <= args.mean


LIST_ROW = re.compile(r"^(\d+)\s+(.+?)\s{2,}(standard|option|text|event)\s+(\S+)\s*$")


def compare_table(ittest, page_table, env):
    """`ittest --list` against the page's table: name, kind, default (4 dp), in order."""
    rows = []
    out, _ = run_ittest(ittest, ["--list"], env)
    for line in out.decode().splitlines()[1:]:
        m = LIST_ROW.match(line.rstrip())
        if m is None:
            continue
        index, name, kind, value = m.groups()
        if kind in ("text", "event"):
            continue
        rows.append((int(index), name, kind, float(value)))
    problems = []
    if len(rows) != len(page_table):
        problems.append(f"ittest lists {len(rows)} controls before the About block, the page {len(page_table)}")
    for (index, name, kind, value), mine in zip(rows, page_table):
        if (name, kind) != (mine["name"], mine["kind"]) or abs(value - mine["value"]) > 0.00005:
            problems.append(f"#{index}: ittest says {name} {kind} {value:.4f}, the page "
                            f"{mine['name']} {mine['kind']} {mine['value']:.4f}")
    return len(rows), problems


def host_values(settings):
    """The six host values tubedump takes, as strings: options by index, and
    "-" for a control left at the plugin's constructor default."""
    return [str(OPTIONS[n].index(settings[n])) if settings.get(n) in OPTIONS.get(n, []) else settings.get(n, "-")
            for n in ORDER]


def ulps(a, b):
    if a == b:
        return 0
    ia = struct.unpack("<q", struct.pack("<d", a))[0]
    ib = struct.unpack("<q", struct.pack("<d", b))[0]
    return abs(ia - ib)


def compare_tube(plugin, page):
    """Every number: doubles counted by ulps apart, floats and ints exactly."""
    out = {"doubles": 0, "exact": 0, "worst": 0, "inexact": [], "floats": 0, "float_exact": 0, "ints_ok": True}

    def dbl(where, a, b):
        out["doubles"] += 1
        u = ulps(float(a), float(b))
        out["exact"] += u == 0
        out["worst"] = max(out["worst"], u)
        if u:
            out["inexact"].append(f"{where} {u}")

    for key in ("sceneLux", "gainFLfc", "gap", "fixedPattern"):
        dbl(f"settings.{key}", plugin["settings"][key], page["settings"][key])
    for key in ("photocathode", "phosphor"):
        out["ints_ok"] &= plugin["settings"][key] == page["settings"][key]
    for key, value in plugin["frame"].items():
        if key == "haloFactor":
            out["ints_ok"] &= value == page["frame"][key]
        else:
            dbl(f"frame.{key}", value, page["frame"][key])
    out["ints_ok"] &= plugin["kernel"]["radius"] == page["kernel"]["radius"]
    dbl("kernel.rawSum", plugin["kernel"]["rawSum"], page["kernel"]["rawSum"])
    pw, qw = plugin["kernel"]["weights"], page["kernel"]["weights"]
    out["ints_ok"] &= len(pw) == len(qw)
    for a, b in zip(pw, qw):
        out["floats"] += 1
        out["float_exact"] += f32(a) == f32(b)
    for a, b in zip(plugin["spot"], page["spot"]):
        out["floats"] += 1
        out["float_exact"] += f32(a) == f32(b)
    for i, (a, b) in enumerate(zip(plugin["phosphor"], page["phosphor"])):
        dbl(f"phosphor[{i}]", a, b)
    dbl("decayTime", plugin["decayTime"], page["decayTime"])
    dbl("haloFraction", plugin["haloFraction"], page["haloFraction"])
    return out


def main(argv):
    parser = argparse.ArgumentParser()
    parser.add_argument("--ittest", default=os.path.join(REPO, "build", "ittest"))
    parser.add_argument("--size", default="480x270")
    parser.add_argument("--frames", type=int, default=12)
    parser.add_argument("--keep", help="write each run's last frame, both renderers, here as PNGs")
    parser.add_argument("--exact", type=float, default=99.99,
                        help="share of bytes, %%, that must be identical over a run (default 99.99)")
    parser.add_argument("--ulps", type=int, default=8,
                        help="the furthest apart a CPU double may be, in units in the last place (default 8): "
                             "clang fuses multiply-adds (-ffp-contract=on), JavaScript never does")
    parser.add_argument("--mean", type=float, default=0.05,
                        help="the largest difference of a frame's mean, levels of 255 (default 0.05)")
    parser.add_argument("--chrome", default=os.environ.get(
        "CHROME", "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"))
    parser.add_argument("--cxx", default="/usr/bin/clang++")
    parser.add_argument("--port", type=int, default=0, help="the local server's port (default: any free one)")
    args = parser.parse_args(argv)
    width, height = parse_size(args.size)
    if not os.access(args.ittest, os.X_OK):
        sys.exit(f"no ittest at {args.ittest}: build it (cmake --build build --target ittest)")

    work = tempfile.mkdtemp(prefix="intensifier-xc-")
    # The harness writes the plugin's log; keep it out of ~/Library/Logs.
    env = {**os.environ, "INTENSIFIER_LOG_DIR": os.path.join(work, "log")}
    size = f"{width}x{height}"
    frame_bytes = width * height * 4

    # tubedump: the plugin's CPU half alone, with the plugin's Release flags.
    tubedump = os.path.join(work, "tubedump")
    built = subprocess.run([args.cxx, "-std=c++17", "-O3", "-DNDEBUG", "-I", os.path.join(REPO, "source"),
                            os.path.join(HERE, "tubedump.cpp"), os.path.join(REPO, "source", "Tube.cpp"),
                            os.path.join(REPO, "source", "Controls.cpp"), "-o", tubedump],
                           capture_output=True, check=False)
    if built.returncode != 0:
        sys.exit(f"tubedump did not build:\n{built.stderr.decode(errors='replace')}")
    plugin_tubes = []
    for settings, w, h, dt in TUBES:
        done = subprocess.run([tubedump, str(w), str(h), repr(dt), *host_values(settings)], capture_output=True,
                              check=True)
        plugin_tubes.append(json.loads(done.stdout))

    frames = scene(width, height, args.frames)
    stream = b"".join(frames)
    plugin, stats = {}, {}
    for name, settings, fps, seed in RUNS:
        sets = [x for k, v in settings.items() for x in ("--set", f"{k}={v}")]
        out, err = run_ittest(args.ittest, ["--pipe", "--size", size, "--fps", str(fps), "--seed", str(seed),
                                            "--stats", *sets], env, stream)
        if len(out) != frame_bytes * args.frames:
            sys.exit(f"ittest --pipe for {name} wrote {len(out)} bytes, not {frame_bytes * args.frames}")
        plugin[name] = out
        stats[name] = [tuple(float(g) for g in m.groups()[1:]) for m in map(STATS.match, err.splitlines()) if m]

    runs = [{"name": n, "settings": s, "fps": fps, "seed": seed, "frames": args.frames} for n, s, fps, seed in RUNS]
    by_name = {r["name"]: r for r in runs}
    for base, name, change, seed, negative, _ in NEGATIVES:
        r = {**by_name[base], "name": name, "settings": {**by_name[base]["settings"], **change}}
        if seed is not None:
            r["seed"] = seed
        if negative:
            r["negative"] = negative
        runs.append(r)
    # The page's applySettings takes an option's element name or its index;
    # a control it is not given stays at the port's default.
    tubes = [{"settings": s, "width": w, "height": h, "dt": dt} for s, w, h, dt in TUBES]
    job = {"width": width, "height": height, "frames": args.frames, "runs": runs, "tubes": tubes}

    state = {"job": job, "frames": frames, "page": {}, "controls": {}, "table": None, "tubes": None,
             "missing": None, "error": None, "finished": threading.Event()}
    server = http.server.ThreadingHTTPServer(("127.0.0.1", args.port), functools.partial(Handler, state=state))
    port = server.server_address[1]
    threading.Thread(target=server.serve_forever, daemon=True).start()
    profile = tempfile.mkdtemp(prefix="intensifier-chrome-")
    chrome = subprocess.Popen([
        args.chrome, "--headless=new", "--no-first-run", "--no-default-browser-check",
        f"--user-data-dir={profile}", "--use-angle=metal", "--enable-gpu", "--ignore-gpu-blocklist",
        "--window-size=1600,1200", f"http://127.0.0.1:{port}/tools/crosscheck/",
    ], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        if not state["finished"].wait(900):
            sys.exit("the page never finished (900 s)")
    finally:
        chrome.terminate()
        try:
            chrome.wait(10)
        except subprocess.TimeoutExpired:
            chrome.kill()
        server.shutdown()
        shutil.rmtree(profile, ignore_errors=True)
    if state["error"]:
        shutil.rmtree(work, ignore_errors=True)
        sys.exit(f"the page failed:\n{state['error']}")

    failures = 0
    count, table_problems = compare_table(args.ittest, state["table"] or [], env)
    if table_problems:
        failures += len(table_problems)
        print("the page's controls differ from ittest --list:")
        for line in table_problems:
            print(f"  {line}")
    else:
        print(f"controls: all {count} of ittest --list's before the About block, same names, kinds and defaults, "
              "in order; the option elements by name through ittest --set")
    gaps = state["missing"] or {}
    # The resample pass runs only on a change of raster, and no run changes it.
    expected_unset = {u for u in gaps.get("unset", []) if u.startswith("resample: ")}
    unexpected = sorted(set(gaps.get("unset", [])) - expected_unset)
    for label, values in (("uniforms the page set that a program lacks", gaps.get("missing")),
                          ("uniforms a program has that the page never set", unexpected),
                          ("GL errors", gaps.get("errors")), ("frames the instance refused", gaps.get("refused"))):
        if values:
            failures += 1
            print(f"{label}: {', '.join(sorted(set(values)))}")
    if not (gaps.get("missing") or unexpected or gaps.get("errors") or gaps.get("refused")):
        print("every uniform set, none missing, no GL error, no frame refused "
              f"(unset, as expected with no change of raster: {', '.join(sorted(expected_unset)) or 'none'})")

    print("\nthe CPU half: tubedump (source/Tube.cpp, Controls.cpp at -O3) against the page's port")
    for (settings, w, h, dt), mine, theirs in zip(TUBES, state["tubes"] or [], plugin_tubes):
        c = compare_tube(theirs, mine)
        ok = c["ints_ok"] and c["float_exact"] == c["floats"] and c["worst"] <= args.ulps
        failures += 0 if ok else 1
        label = f"{w}x{h} dt {dt:.6g} " + (",".join(f"{k}={v}" for k, v in settings.items()) or "defaults")
        print(f"  {label}\n      doubles {c['exact']}/{c['doubles']} exact"
              f"{' (ulps apart: ' + ', '.join(c['inexact']) + ')' if c['inexact'] else ''}; "
              f"floats {c['float_exact']}/{c['floats']} exact; ints {'same' if c['ints_ok'] else 'DIFFER'}  "
              f"{'ok' if ok else 'FAIL'}")

    if args.keep:
        from PIL import Image
        os.makedirs(args.keep, exist_ok=True)

    print(f"\n{size}, {args.frames} frames a run, seeded\n")
    print(f"{'run':<22} {'bytes==':>9} {'bytes!=':>8} {'pix==':>9} {'pix<=4':>9} {'1st==':>9} {'max':>4} {'|dmean|':>8}  control texel")
    for name, settings, fps, seed in RUNS:
        got = state["page"].get(name)
        if got is None or len(got) != frame_bytes * args.frames:
            print(f"{name:<22} no pictures from the page")
            failures += 1
            continue
        s = compare(plugin[name], got, width, height, args.frames)
        page_controls = state["controls"][name]["controls"]
        want = stats[name]
        same = sum(1 for a, b in zip(want, page_controls) for i in range(3) if f32(a[i]) == f32(b[i]))
        rel = max((abs(f32(a[i]) - f32(b[i])) / max(abs(f32(a[i])), 1e-30)
                   for a, b in zip(want, page_controls) for i in range(3)), default=0.0)
        ok = passes(s, args) and len(want) == args.frames
        failures += 0 if ok else 1
        print(f"{name:<22} {s['bytes']:8.3f}% {s['differ']:>8} {s['pixels']:8.3f}% {s['within4']:8.3f}% {s['first']:8.3f}% {s['max']:>4} "
              f"{s['mean']:8.4f}  {same}/{3 * len(want)} floats equal, worst {rel:.2g} rel  {'ok' if ok else 'FAIL'}")
        if args.keep:
            last = slice(frame_bytes * (args.frames - 1), frame_bytes * args.frames)
            for label, data in (("plugin", plugin[name][last]), ("page", got[last])):
                Image.frombytes("RGBA", (width, height), data).save(os.path.join(args.keep, f"{name}-{label}.png"))

    print("\nnegative controls: the page a little off, against the plugin as it should be")
    for base, name, _, _, _, label in NEGATIVES:
        late = state["page"].get(name)
        s = compare(plugin[base], late, width, height, args.frames) if late else None
        caught = s is not None and not passes(s, args)
        failures += 0 if caught else 1
        print(f"  {label:<40} " + (f"{s['bytes']:8.3f}% {s['pixels']:8.3f}% {s['max']:>4} {s['mean']:8.4f}  "
                                     f"{'caught' if caught else 'NOT CAUGHT'}" if s else "no pictures"))
    print(f"\n{'every run passes, every CPU number agrees, every negative control is caught' if not failures else f'{failures} problem(s)'}"
          f" (pass: {args.exact}% of bytes identical over the run and no frame's mean more than {args.mean} levels apart)")
    shutil.rmtree(work, ignore_errors=True)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
