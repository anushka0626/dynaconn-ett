#!/usr/bin/env python3
"""Builds the traces in traces/specs.tsv into data/traces/ and records them in traces/MANIFEST.tsv.

    python3 scripts/build_traces.py                  # build missing traces, update the manifest
    python3 scripts/build_traces.py --group rq3_p3   # only one group
    python3 scripts/build_traces.py --verify         # regenerate into a temp dir and compare hashes

Specs whose input file (data/...) is missing are skipped with a note (run scripts/fetch_data.sh).
The manifest is committed; the traces themselves are not (data/ is gitignored).
"""
import argparse
import concurrent.futures as cf
import hashlib
import os
import shlex
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SPECS = ROOT / "traces" / "specs.tsv"
MANIFEST = ROOT / "traces" / "MANIFEST.tsv"
TRACE_DIR = ROOT / "data" / "traces"
TRACEGEN = ROOT / "build-release" / "tracegen"


def read_specs():
    specs = []
    for line in SPECS.read_text().splitlines():
        if line.startswith("#") or not line.strip():
            continue
        name, group, args = line.split("\t")
        specs.append((name, group, args))
    return specs


def read_manifest():
    entries = {}
    if MANIFEST.exists():
        for line in MANIFEST.read_text().splitlines():
            if line.startswith("#") or not line.strip():
                continue
            name, group, sha, size, args = line.split("\t")
            entries[name] = (group, sha, int(size), args)
    return entries


def write_manifest(entries):
    with MANIFEST.open("w") as f:
        f.write("# name\tgroup\tsha256\tbytes\ttracegen arguments (written by scripts/build_traces.py)\n")
        for name in sorted(entries):
            group, sha, size, args = entries[name]
            f.write(f"{name}\t{group}\t{sha}\t{size}\t{args}\n")


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def missing_input(args):
    toks = shlex.split(args)
    if "--input" in toks:
        p = ROOT / toks[toks.index("--input") + 1]
        if not p.exists():
            return str(p.relative_to(ROOT))
    return None


def generate(tracegen, args, out_path):
    cmd = [str(tracegen)] + shlex.split(args) + ["--out", str(out_path)]
    r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError(f"tracegen failed: {' '.join(cmd)}\n{r.stderr}")
    return sha256_file(out_path), out_path.stat().st_size


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--group", help="only specs in this group")
    ap.add_argument("--verify", action="store_true", help="regenerate and compare with the manifest")
    ap.add_argument("--tracegen", default=str(TRACEGEN),
                    help="generator binary (e.g. a Clang build, to check cross-compiler identity with --verify)")
    ap.add_argument("--exclude-group", action="append", default=[], help="skip this group (repeatable)")
    ap.add_argument("-j", type=int, default=max(1, (os.cpu_count() or 2) // 2))
    opts = ap.parse_args()
    tracegen = Path(opts.tracegen)
    if not tracegen.exists():
        sys.exit(f"missing {tracegen}: build the Release config first (see CLAUDE.md)")

    specs = [s for s in read_specs() if (not opts.group or s[1] == opts.group) and s[1] not in opts.exclude_group]
    manifest = read_manifest()
    TRACE_DIR.mkdir(parents=True, exist_ok=True)

    todo, skipped = [], []
    for name, group, args in specs:
        miss = missing_input(args)
        if miss:
            skipped.append((name, miss))
        elif opts.verify:
            if name in manifest:
                todo.append((name, group, args))
        elif name not in manifest or manifest[name][3] != args or not (TRACE_DIR / f"{name}.dct").exists():
            todo.append((name, group, args))

    failures = 0
    with tempfile.TemporaryDirectory() as tmp, cf.ThreadPoolExecutor(max_workers=opts.j) as pool:
        futures = {}
        for name, group, args in todo:
            out = (Path(tmp) if opts.verify else TRACE_DIR) / f"{name}.dct"
            futures[pool.submit(generate, tracegen, args, out)] = (name, group, args)
        for i, fut in enumerate(cf.as_completed(futures), 1):
            name, group, args = futures[fut]
            sha, size = fut.result()
            if opts.verify:
                ok = sha == manifest[name][1]
                failures += not ok
                print(f"[{i}/{len(todo)}] {'OK  ' if ok else 'DIFF'} {name}")
                if opts.verify and ok:
                    Path(tmp, f"{name}.dct").unlink()
            else:
                manifest[name] = (group, sha, size, args)
                print(f"[{i}/{len(todo)}] {name} {sha[:12]} {size / 1e6:.1f} MB")

    if not opts.verify:
        write_manifest(manifest)
    for name, miss in skipped:
        print(f"skipped {name}: missing {miss} (scripts/fetch_data.sh)")
    if opts.verify:
        print(f"verified {len(todo) - failures}/{len(todo)} traces bit-for-bit")
        sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
