#!/usr/bin/env python3
"""Semantic comparison of TuTraSt output files.

Goals:
- Compare MATLAB/Octave TuTraSt vs C++ sTuTraSt outputs robustly
- Also support C++ vs C++ comparisons (e.g. unit conversion cross-checks)

Handles:
- whitespace normalization
- 1-based vs 0-based indexing (auto-inferred with sensible fallbacks)
- periodic-boundary equivalent coordinates in TS outputs (wrap by grid size)
- floating-point tolerance
- row ordering differences
- TS-group numbering differences (ignored where appropriate)
"""

import sys
import os
import re

def parse_numbers(line):
    """Parse a line into a list of floats."""
    return [float(x) for x in line.split()]

def compare_numeric(val1, val2, rtol=1e-6, atol=1e-10):
    """Compare two numbers with relative and absolute tolerance."""
    if val1 == val2:
        return True
    if abs(val1) < atol and abs(val2) < atol:
        return True
    if val1 == 0 or val2 == 0:
        return abs(val1 - val2) < atol
    return abs(val1 - val2) / max(abs(val1), abs(val2)) < rtol

def read_cube_dims(dir_path):
    """Read grid dimensions (nx, ny, nz) from a Gaussian cube file if present."""
    cube_path = os.path.join(dir_path, "grid.cube")
    if not os.path.exists(cube_path):
        return None
    try:
        with open(cube_path) as f:
            # Cube: 2 comment lines, then atom-count+origin, then 3 grid lines.
            f.readline()
            f.readline()
            f.readline()
            nx = int(f.readline().split()[0])
            ny = int(f.readline().split()[0])
            nz = int(f.readline().split()[0])
        return (abs(nx), abs(ny), abs(nz))
    except Exception:
        return None

def infer_indexing(dir_path, file_type, dims, rows):
    """Infer whether a directory's outputs are 1-based ('matlab') or 0-based ('cpp')."""
    lower = dir_path.lower()
    if "matlab" in lower or "octave" in lower:
        return "matlab"
    if "cpp" in lower:
        return "cpp"

    if not rows:
        return "cpp"

    if file_type == "basis":
        # basis.dat coords are integer grid indices.
        coords = [r[:3] for r in rows if len(r) >= 3]
        if any(any(int(round(c)) == 0 for c in xyz) for xyz in coords):
            return "cpp"
        if dims is not None:
            for xyz in coords:
                for c, n in zip(xyz, dims):
                    if int(round(c)) == n:
                        return "matlab"
        # Ambiguous (no 0 and no n). Default to cpp to avoid shifting C++ vs C++.
        return "cpp"

    if file_type == "ts_data":
        # TS coords are typically half-integers; C++ representations often include -0.5 at PBC.
        coords = [r[3:6] for r in rows if len(r) >= 6]
        if any(any(c < 0 for c in xyz) for xyz in coords):
            return "cpp"
        if dims is not None:
            for xyz in coords:
                for c, n in zip(xyz, dims):
                    if c > (n - 0.5 + 1e-9):
                        return "matlab"
        return "cpp"

    return "cpp"

def wrap_half_coordinate(coord, n):
    """Wrap a half-integer coordinate into [-0.5, n-0.5] under PBC."""
    if n is None:
        return coord
    # Shift so the canonical range becomes [0, n), then shift back.
    return ((coord + 0.5) % n) - 0.5

def normalize_row(nums, file_type, indexing, dims):
    """Normalize a row based on file type and indexing convention."""
    out = list(nums)
    if file_type == "basis":
        # Columns 1-3 (0-indexed: 0-2) are coordinates: MATLAB 1-based -> 0-based
        if indexing == "matlab":
            for j in range(min(3, len(out))):
                out[j] -= 1
        if dims is not None:
            for j, n in enumerate(dims):
                if j < len(out):
                    out[j] = float(int(round(out[j])) % n)
    elif file_type == "ts_data":
        # Columns 4-6 (0-indexed: 3-5) are coordinates: MATLAB 1-based -> 0-based
        if indexing == "matlab":
            for j in range(3, min(6, len(out))):
                out[j] -= 1
        if dims is not None and len(out) >= 6:
            for j, n in enumerate(dims, start=3):
                if j < 6:
                    out[j] = wrap_half_coordinate(out[j], n)
        # TS group IDs can differ while remaining physically equivalent.
        if len(out) > 0:
            out[0] = 0
    elif file_type == "processes":
        # Column 11 (0-indexed: 10) is tsgroup_id - can differ in numbering
        # Zero it out for comparison
        if len(out) > 10:
            out[10] = 0
    return out

def rows_equal(row1, row2, rtol=1e-6, atol=1e-10):
    """Check if two numeric rows are equal within tolerance."""
    if len(row1) != len(row2):
        return False
    return all(compare_numeric(a, b, rtol, atol) for a, b in zip(row1, row2))

def sort_key(row):
    # Stable key for sorting float rows before comparison.
    return tuple(round(x, 12) for x in row)

def compare_files(file1, file2, file_type):
    """Compare two output files semantically. Returns (match, message)."""
    if not os.path.exists(file1) and not os.path.exists(file2):
        return True, "SKIP (both missing)"
    if not os.path.exists(file1):
        return False, f"MISSING: {file1}"
    if not os.path.exists(file2):
        return False, f"MISSING: {file2}"

    # tunnel_info.out has completely different formats (MATLAB=verbose text, C++=structured)
    if file_type == "tunnel_info":
        return True, "SKIP (format differs)"

    with open(file1) as f:
        lines1 = [l.strip() for l in f if l.strip()]
    with open(file2) as f:
        lines2 = [l.strip() for l in f if l.strip()]

    if len(lines1) != len(lines2):
        return False, f"LINE COUNT: {len(lines1)} vs {len(lines2)}"

    rows1 = [parse_numbers(l) for l in lines1]
    rows2 = [parse_numbers(l) for l in lines2]

    # Check column counts
    for i, (r1, r2) in enumerate(zip(rows1, rows2)):
        if len(r1) != len(r2):
            return False, f"LINE {i+1}: column count {len(r1)} vs {len(r2)}"

    # For robust normalization, infer indexing (MATLAB 1-based vs C++ 0-based) per-directory.
    dir1 = os.path.dirname(file1)
    dir2 = os.path.dirname(file2)
    dims = read_cube_dims(dir1) or read_cube_dims(dir2)
    idx1 = infer_indexing(dir1, file_type, dims, rows1)
    idx2 = infer_indexing(dir2, file_type, dims, rows2)

    norm1 = [normalize_row(r, file_type, idx1, dims) for r in rows1]
    norm2 = [normalize_row(r, file_type, idx2, dims) for r in rows2]

    if file_type in ("ts_data", "processes"):
        # Row order may differ, sort both before comparing.
        norm1.sort(key=sort_key)
        norm2.sort(key=sort_key)

    for i, (r1, r2) in enumerate(zip(norm1, norm2)):
        if not rows_equal(r1, r2):
            return False, f"LINE {i+1}: {r1} vs {r2}"

    return True, "MATCH"

def main():
    if len(sys.argv) < 4:
        print(f"Usage: {sys.argv[0]} <matlab_dir> <cpp_dir> <file1> [file2 ...]")
        sys.exit(1)

    dir1 = sys.argv[1]
    dir2 = sys.argv[2]
    files = sys.argv[3:]

    all_match = True
    for fname in files:
        if fname == "basis.dat":
            ftype = "basis"
        elif fname == "TS_data.out":
            ftype = "ts_data"
        elif fname == "tunnel_info.out":
            ftype = "tunnel_info"
        elif re.match(r"^processes_\d+\.dat$", fname):
            ftype = "processes"
        else:
            ftype = "generic"
        f1 = os.path.join(dir1, fname)
        f2 = os.path.join(dir2, fname)
        match, msg = compare_files(f1, f2, ftype)
        status = "MATCH" if match and "MATCH" in msg else ("SKIP" if "SKIP" in msg else "DIFF")
        print(f"{fname}:{status}:{msg}")
        if not match and "SKIP" not in msg:
            all_match = False

    sys.exit(0 if all_match else 1)

if __name__ == "__main__":
    main()
