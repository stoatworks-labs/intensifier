// The page's half of the crosscheck: fetch the job and its frames from
// crosscheck.py's server, render each run through window.intensifierDemo.renderRun
// (a fresh instance, the harness's clock, the given seed), work out the port's
// CPU numbers through window.intensifierDemo.tube, and post it all back, with
// the page's table of controls and any uniform the page set that a program
// lacks or a program has that nothing set. crosscheck.py compares; nothing is
// judged here.
const post = (path, body) => fetch(path, { method: 'POST', body });

async function main() {
  for (let i = 0; i < 400 && !window.intensifierDemo; i += 1) await new Promise((r) => setTimeout(r, 50));
  if (!window.intensifierDemo) throw new Error('window.intensifierDemo never appeared: the page did not start');
  const job = await (await fetch('/xc/job.json')).json();
  const frames = [];
  for (let f = 0; f < job.frames; f += 1) {
    frames.push(new Uint8Array(await (await fetch(`/xc/frame/${f}`)).arrayBuffer()));
  }
  await post('/xc/table', JSON.stringify(window.intensifierDemo.table));
  const missing = new Set();
  const unset = new Map();
  const errors = [];
  const refused = [];
  for (const run of job.runs) {
    const out = window.intensifierDemo.renderRun({
      settings: run.settings,
      width: job.width,
      height: job.height,
      fps: run.fps,
      seed: run.seed,
      frames: frames.slice(0, run.frames),
      negative: run.negative ?? null,
    });
    out.missing.forEach((name) => missing.add(name));
    // A uniform left unset in every run is a fault; the resample pass's run
    // only on a change of raster, which no run here makes.
    out.unset.forEach((name) => unset.set(name, (unset.get(name) ?? 0) + 1));
    if (out.error) errors.push(`${run.name}: GL error ${out.error}`);
    if (!out.ok) refused.push(run.name);
    const all = new Uint8Array(out.pictures.reduce((n, p) => n + p.length, 0));
    let at = 0;
    for (const p of out.pictures) { all.set(p, at); at += p.length; }
    await post(`/xc/out/${run.name}`, all);
    await post(`/xc/control/${run.name}`, JSON.stringify({ controls: out.controls, seed: out.seed, levels: out.levels,
      haloFactor: out.haloFactor, radius: out.radius }));
  }
  const tubes = job.tubes.map((t) => window.intensifierDemo.tube(t));
  await post('/xc/tubes', JSON.stringify(tubes));
  const never = [...unset].filter(([, n]) => n === job.runs.length).map(([name]) => name);
  await post('/xc/missing', JSON.stringify({ missing: [...missing], unset: never, errors, refused }));
  await post('/xc/done', 'ok');
}

main().catch((error) => post('/xc/error', String(error?.stack ?? error)));
