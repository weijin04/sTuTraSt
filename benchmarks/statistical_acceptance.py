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


def parse_diffusion(workdir: Path) -> list[float]:
    values = (workdir / "D_ave_300.dat").read_text(encoding="ascii").strip().split()
    return [float(values[0]), float(values[2]), float(values[4])]


def run_binary(binary: Path, case_name: str, seed: int) -> list[float]:
    workdir = Path(tempfile.mkdtemp(prefix=f"tutrast-stat-{case_name}-"))
    try:
        subprocess.run(
            ["python3", str(GENERATOR), "--case", case_name, "--output-dir", str(workdir)],
            check=True,
        )
        env = os.environ.copy()
        env["TUTRAST_KMC_SEED"] = str(seed)
        subprocess.run(
            [str(binary)],
            cwd=workdir,
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            check=True,
        )
        return parse_diffusion(workdir)
    finally:
        shutil.rmtree(workdir, ignore_errors=True)


def ks_statistic(xs: list[float], ys: list[float]) -> float:
    xs_sorted = sorted(xs)
    ys_sorted = sorted(ys)
    combined = sorted(set(xs_sorted + ys_sorted))
    d = 0.0
    for value in combined:
        fx = sum(x <= value for x in xs_sorted) / len(xs_sorted)
        fy = sum(y <= value for y in ys_sorted) / len(ys_sorted)
        d = max(d, abs(fx - fy))
    return d


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--baseline-bin", required=True)
    parser.add_argument("--candidate-bin", required=True)
    parser.add_argument("--cases", nargs="*", default=["medium", "heavy"])
    parser.add_argument("--seeds", nargs="*", type=int, default=[11, 23, 37, 41, 53, 67, 79, 83])
    parser.add_argument("--output-json")
    args = parser.parse_args()

    baseline_bin = Path(args.baseline_bin).resolve()
    candidate_bin = Path(args.candidate_bin).resolve()
    summary = {
        "baseline_bin": str(baseline_bin),
        "candidate_bin": str(candidate_bin),
        "cases": {},
    }

    for case_name in args.cases:
        baseline_runs = [run_binary(baseline_bin, case_name, seed) for seed in args.seeds]
        candidate_runs = [run_binary(candidate_bin, case_name, seed) for seed in args.seeds]

        case_summary = {}
        for axis_index, axis_name in enumerate(["Dx", "Dy", "Dz"]):
            baseline_axis = [values[axis_index] for values in baseline_runs]
            candidate_axis = [values[axis_index] for values in candidate_runs]
            mean_baseline = sum(baseline_axis) / len(baseline_axis)
            mean_candidate = sum(candidate_axis) / len(candidate_axis)
            case_summary[axis_name] = {
                "mean_baseline": mean_baseline,
                "mean_candidate": mean_candidate,
                "mean_abs_delta": abs(mean_candidate - mean_baseline),
                "ks_d": ks_statistic(baseline_axis, candidate_axis),
            }
        summary["cases"][case_name] = case_summary

    if args.output_json:
        Path(args.output_json).write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")

    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    main()
