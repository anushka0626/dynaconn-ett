#!/usr/bin/env python3
"""Runs benchmark plans: (structure x trace x treap seed) jobs, each in its own `bench` process.

    python3 scripts/run_sweep.py --plan pilot              # run (or resume) a plan
    python3 scripts/run_sweep.py --plan pilot --dry-run    # list the jobs and an estimate
    python3 scripts/run_sweep.py --list                    # list plans

Rules (docs/PREREGISTRATION.md §2):
  * jobs run one at a time, pinned to P-core CPU 2 (sibling CPU 3 must stay idle); one structure
    per process;
  * pre-flight: governor `performance`, turbo off, Release bench, and a clean git tree (results
    must map to a commit; --allow-dirty only for development, and such rows are flagged);
  * the job order is shuffled with a fixed seed (bias control against drift), and recorded;
  * results go to results/raw/<plan>.csv (gitignored); finished jobs are skipped on resume.
"""
import argparse
import glob
import csv
import random
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BENCH = ROOT / "build-release" / "bench"
BENCH_STATS = ROOT / "build-release" / "bench_stats"
TRACES = ROOT / "data" / "traces"
MANIFEST = ROOT / "traces" / "MANIFEST.tsv"
RESULTS = ROOT / "results" / "raw"
CPU = 2

ALL = ["bfs", "dsu_eager", "dsu_lazy", "cached_bfs", "ett_p0", "ett_f0", "ett_f1", "ett_f2",
       "ett_f2_k1", "ett_f2_k2", "ett_f2_k3", "dtree_p0", "dtree_f2", "dnd", "hks_p0", "hks_f2",
       "hdt_p0", "hdt_f2"]
TREAP = {"ett_p0", "ett_f0", "ett_f1", "ett_f2", "ett_f2_k1", "ett_f2_k2", "ett_f2_k3",
         "hks_p0", "hks_f2", "hdt_p0", "hdt_f2"}
RQ1_RQ2 = ["ett_p0", "ett_f0", "ett_f1", "ett_f2", "hdt_p0", "hdt_f2", "dtree_p0", "dtree_f2"]
RQ3 = ["ett_f2", "hks_f2", "hdt_f2", "dtree_f2", "dnd", "bfs", "dsu_eager", "dsu_lazy", "cached_bfs"]
TREAP_SEEDS = (1, 2, 3)  # §2.1: 3 treap seeds per trace seed

# Repetitions per job for the confirmatory plans: set from the pilot (§2.5) and recorded as an
# amendment before any confirmatory run. None until then; confirmatory plans refuse to run.
CONFIRMATORY_REPS = None


def plan(name):
    """Returns (jobs, reps, warmup, mode, binary). A job is (structure, trace_name, treap_seed)."""
    groups = traces_by_group()

    def jobs(structures, trace_names, seeds_for=lambda s: TREAP_SEEDS if s in TREAP else (0,)):
        return [(s, t, seed) for s in structures for t in trace_names for seed in seeds_for(s)]

    if name == "pilot":  # every structure, pilot traces, one treap seed, many reps
        return jobs(ALL, groups["pilot"], lambda s: (1,) if s in TREAP else (0,)), 10, 1, "throughput", BENCH
    if CONFIRMATORY_REPS is None:
        sys.exit(f"plan {name!r}: repetitions not set yet. Run the pilot, then record r as an amendment "
                 "and set CONFIRMATORY_REPS.")
    r = CONFIRMATORY_REPS
    if name == "rq1_rq2":
        return jobs(RQ1_RQ2, groups["rq1_rq2_rq3c"]), r, 1, "throughput", BENCH
    if name == "rq3c":
        small = [t for t in groups["rq1_rq2_rq3c"] if "_v1000_" in t]
        return jobs(RQ3, small), r, 1, "throughput", BENCH
    if name == "rq3_p1":
        return jobs(RQ3, groups["rq3_p1"]), r, 1, "throughput", BENCH
    if name == "rq3_p3":
        return jobs(RQ3, groups["rq3_p3"]), r, 1, "throughput", BENCH
    if name == "rq3b":  # mean delete time per op type: latency mode
        b1 = [t for t in groups["rq3b"] if "_b1_" in t]
        b2 = [t for t in groups["rq3b"] if "_b2_" in t]
        return jobs(["hks_f2", "hdt_f2"], b1) + jobs(["dtree_f2", "hks_f2"], b2), r, 1, "latency", BENCH
    if name == "rq5_work":  # H5a/H5b are operation counts: deterministic, one run
        return jobs(["ett_f2", "ett_f2_k1", "ett_f2_k2", "ett_f2_k3"], groups["rq5"]), 1, 0, "throughput", BENCH_STATS
    sys.exit(f"unknown plan {name!r}")


PLANS = ["pilot", "rq1_rq2", "rq3c", "rq3_p1", "rq3_p3", "rq3b", "rq5_work"]


def traces_by_group():
    groups = {}
    for line in MANIFEST.read_text().splitlines():
        if line.startswith("#") or not line.strip():
            continue
        name, group = line.split("\t")[:2]
        groups.setdefault(group, []).append(name)
    return {g: sorted(v) for g, v in groups.items()}


def read_file(p):
    try:
        return Path(p).read_text().strip()
    except OSError:
        return "na"


def on_ac_power():
    """True if on mains power, or if the machine has no AC supply entry (desktop)."""
    supplies = glob.glob("/sys/class/power_supply/AC*/online") + glob.glob("/sys/class/power_supply/ADP*/online")
    return not supplies or any(read_file(f) == "1" for f in supplies)


def quarantine_jobs(csv_path, keys):
    """Moves every row of the given jobs to <plan>.quarantine.csv (kept for the record, never
    analysed): interrupted jobs, or jobs that ran while on battery."""
    if not keys or not csv_path.exists():
        return
    with csv_path.open() as f:
        reader = csv.DictReader(f)
        fields, rows = reader.fieldnames, list(reader)
    key = lambda r: (r["structure"], r["trace"], int(r["treap_seed"]))
    keep = [r for r in rows if key(r) not in keys]
    moved = [r for r in rows if key(r) in keys]
    qpath = csv_path.with_suffix(".quarantine.csv")
    new_q = not qpath.exists()
    with qpath.open("a", newline="") as f:
        w = csv.DictWriter(f, fieldnames=fields)
        if new_q:
            w.writeheader()
        w.writerows(moved)
    tmp = csv_path.with_suffix(".tmp")
    with tmp.open("w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=fields)
        w.writeheader()
        w.writerows(keep)
    tmp.replace(csv_path)
    print(f"quarantined {len(moved)} rows of {len(keys)} job(s) to {qpath.name}", flush=True)


def wait_for_ac():
    if on_ac_power():
        return
    print("on battery: paused until the laptop is plugged in", flush=True)
    while not on_ac_power():
        time.sleep(10)
    print("AC power back: resuming", flush=True)


def preflight(allow_dirty):
    problems = []
    if not on_ac_power():
        problems.append("on battery power (plug the laptop in)")
    gov = read_file(f"/sys/devices/system/cpu/cpu{CPU}/cpufreq/scaling_governor")
    if gov != "performance":
        problems.append(f"governor is {gov!r} (sudo cpupower frequency-set -g performance)")
    no_turbo = read_file("/sys/devices/system/cpu/intel_pstate/no_turbo")
    if no_turbo not in ("1", "na"):
        problems.append("turbo is on (echo 1 | sudo tee /sys/devices/system/cpu/intel_pstate/no_turbo)")
    if not BENCH.exists() or not BENCH_STATS.exists():
        problems.append("build-release/bench missing (build the Release config)")
    commit = subprocess.run(["git", "rev-parse", "HEAD"], cwd=ROOT, capture_output=True, text=True).stdout.strip()
    dirty = subprocess.run(["git", "status", "--porcelain"], cwd=ROOT,
                           capture_output=True, text=True).stdout.strip() != ""
    if dirty and not allow_dirty:
        problems.append("git tree has uncommitted changes (commit first, or --allow-dirty for development)")
    if problems:
        sys.exit("pre-flight failed:\n  " + "\n  ".join(problems))
    return commit, dirty


def job_row_counts(csv_path):
    counts = {}
    if csv_path.exists():
        with csv_path.open() as f:
            for row in csv.DictReader(f):
                key = (row["structure"], row["trace"], int(row["treap_seed"]))
                counts[key] = counts.get(key, 0) + 1
    return counts


def done_jobs(csv_path, reps, warmup):
    """Jobs with all their rows present (warm-up + reps) in the results file."""
    return {k for k, c in job_row_counts(csv_path).items() if c >= reps + warmup}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--plan")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--allow-dirty", action="store_true")
    ap.add_argument("--cap-seconds", type=float, default=600)
    ap.add_argument("--order-seed", type=int, default=20261018)
    opts = ap.parse_args()
    if opts.list or not opts.plan:
        print("\n".join(PLANS))
        return

    jobs, reps, warmup, mode, binary = plan(opts.plan)
    rng = random.Random(opts.order_seed)
    rng.shuffle(jobs)
    out = RESULTS / f"{opts.plan}.csv"
    if not opts.dry_run:  # an interrupted job left partial rows: set them aside, rerun cleanly
        partial = {k for k, c in job_row_counts(out).items() if c < reps + warmup}
        quarantine_jobs(out, partial)
    finished = done_jobs(out, reps, warmup)
    todo = [j for j in jobs if (j[0], j[1], j[2] if j[0] in TREAP else 0) not in finished]
    print(f"plan {opts.plan}: {len(jobs)} jobs, {len(jobs) - len(todo)} done, {len(todo)} to run "
          f"({reps} reps + {warmup} warm-up, mode {mode}, {binary.name})")
    if opts.dry_run:
        for j in todo[:20]:
            print("  ", *j)
        return

    commit, dirty = preflight(opts.allow_dirty)
    RESULTS.mkdir(parents=True, exist_ok=True)
    diff_sha = "none"
    if dirty:  # keep the exact uncommitted code that produced these rows
        diff = subprocess.run(["git", "diff", "HEAD"], cwd=ROOT, capture_output=True).stdout
        untracked = subprocess.run(["git", "ls-files", "--others", "--exclude-standard"], cwd=ROOT,
                                   capture_output=True, text=True).stdout.split()
        for f in untracked:  # new files are not in `git diff HEAD`
            diff += subprocess.run(["git", "diff", "--no-index", "/dev/null", f], cwd=ROOT,
                                   capture_output=True).stdout
        import hashlib
        diff_sha = hashlib.sha256(diff).hexdigest()
        (RESULTS / f"{opts.plan}.{commit[:12]}.{diff_sha[:12]}.diff").write_bytes(diff)
    meta = (f"commit={commit};dirty={int(dirty)};diff_sha256={diff_sha};plan={opts.plan};"
            f"order_seed={opts.order_seed}")
    t_start = time.time()
    for i, (structure, trace, seed) in enumerate(todo, 1):
        cmd = ["taskset", "-c", str(CPU), str(binary), "--structure", structure,
               "--trace", str(TRACES / f"{trace}.dct"), "--treap-seed", str(seed or 1),
               "--reps", str(reps), "--warmup", str(warmup), "--mode", mode,
               "--cap-seconds", str(opts.cap_seconds), "--out", str(out), "--meta", meta]
        key = (structure, trace, seed if structure in TREAP else 0)
        while True:  # a job that saw battery power is quarantined and rerun on AC
            wait_for_ac()
            t0 = time.time()
            r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
            if on_ac_power():
                break
            quarantine_jobs(out, {key})
        if r.returncode != 0 or "WRONG ANSWERS" in r.stderr:
            print(f"  FAILED {structure} {trace} seed {seed}: {r.stderr.strip()}", file=sys.stderr)
        elapsed = time.time() - t_start
        eta = elapsed / i * (len(todo) - i)
        print(f"[{i}/{len(todo)}] {structure:<10} {trace:<40} seed {seed}  {time.time() - t0:6.1f}s  "
              f"(eta {eta / 3600:.1f} h)", flush=True)


if __name__ == "__main__":
    main()
