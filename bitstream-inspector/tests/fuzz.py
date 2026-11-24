#!/usr/bin/env python3
"""Mutation smoke fuzzer for the bsinfo parser.

Seeds are the generated raw vectors. Every mutated file must either parse or
fail cleanly (exit 0/1); sanitizer diagnostics, signals and timeouts fail the
run. The seed is fixed by default so CI failures are reproducible.
"""
import argparse
import os
import random
import subprocess
import sys
import tempfile


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bsinfo", required=True)
    ap.add_argument("--vectors", required=True)
    ap.add_argument("--iterations", type=int, default=200)
    ap.add_argument("--seed", type=int, default=20261124)
    ap.add_argument("--timeout", type=float, default=10.0)
    args = ap.parse_args()

    seeds = []
    for name in sorted(os.listdir(args.vectors)):
        if name.startswith(("h264", "h265")) and name.endswith((".h264", ".h265")):
            seeds.append(os.path.join(args.vectors, name))
    if not seeds:
        sys.stderr.write("no seed streams found\n")
        return 1

    rng = random.Random(args.seed)
    env = dict(os.environ)
    env["ASAN_OPTIONS"] = "detect_leaks=0:abort_on_error=1"
    env["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1:exitcode=99"

    work = tempfile.mkdtemp(prefix="bs-fuzz-", dir=args.vectors)
    case = os.path.join(work, "case.bin")
    failures = []
    for iteration in range(args.iterations):
        seed = seeds[iteration % len(seeds)]
        data = bytearray(open(seed, "rb").read())
        for _ in range(rng.randint(1, 12)):
            limit = min(len(data), 8192)
            if not limit:
                break
            pos = rng.randrange(limit)
            data[pos] = rng.randrange(256)
        if rng.random() < 0.1:
            data = data[:rng.randrange(len(data) + 1)]
        with open(case, "wb") as f:
            f.write(data)
        try:
            proc = subprocess.run([args.bsinfo, case], stdout=subprocess.DEVNULL,
                                  stderr=subprocess.PIPE,
                                  timeout=args.timeout, env=env)
        except subprocess.TimeoutExpired:
            failures.append((iteration, seed, "timeout", ""))
            continue
        err = proc.stderr.decode("utf-8", "replace")
        bad = proc.returncode not in (0, 1) or "Sanitizer" in err or \
            "runtime error:" in err
        if bad:
            failures.append((iteration, seed, proc.returncode, err[-600:]))
            keep = os.path.join(work, f"fail-{iteration}.bin")
            with open(keep, "wb") as f:
                f.write(data)
        if len(failures) >= 5 or (iteration + 1) % 50 == 0:
            print(f"{iteration + 1} iterations, {len(failures)} failures")
        if len(failures) >= 5:
            break

    print(f"fuzz done: {iteration + 1} iterations, {len(failures)} failures")
    for iteration, seed, code, err in failures[:5]:
        print(f"FAIL iteration {iteration} seed {os.path.basename(seed)} "
              f"exit {code}")
        if err:
            print(err)
    if failures:
        print("cases kept in " + work)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
