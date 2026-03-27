#!/usr/bin/env python3

import argparse
import json
import os
import shutil
import subprocess
import tempfile
from pathlib import Path


SCRIPT_DIR = Path(__file__).resolve().parent
GENERATOR = SCRIPT_DIR / "generate_synthetic_fixture.py"


def run_case(binary: Path, case_name: str, seed: int) -> dict:
    workdir = Path(tempfile.mkdtemp(prefix=f"tutrast-bench-{case_name}-"))
    try:
        subprocess.run(
            ["python3", str(GENERATOR), "--case", case_name, "--output-dir", str(workdir)],
            check=True,
        )
        time_cmd = ["/usr/bin/time", "-f", "%e %M", str(binary)]
        env = os.environ.copy()
        env["TUTRAST_KMC_SEED"] = str(seed)
        proc = subprocess.run(
            time_cmd,
            cwd=workdir,
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            check=True,
        )
        stderr_lines = [line for line in proc.stderr.strip().splitlines() if line.strip()]
        elapsed, max_rss_kib = stderr_lines[-1].split()
        return {
            "case": case_name,
            "elapsed_seconds": float(elapsed),
            "max_rss_kib": int(max_rss_kib),
        }
    finally:
        shutil.rmtree(workdir, ignore_errors=True)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--bin", required=True)
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--cases", nargs="*", default=["small", "medium", "heavy"])
    parser.add_argument("--output-json")
    args = parser.parse_args()

    binary = Path(args.bin).resolve()
    results = [run_case(binary, case_name, args.seed) for case_name in args.cases]
    payload = {"binary": str(binary), "seed": args.seed, "results": results}

    if args.output_json:
        Path(args.output_json).write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")

    print(json.dumps(payload, indent=2))


if __name__ == "__main__":
    main()
