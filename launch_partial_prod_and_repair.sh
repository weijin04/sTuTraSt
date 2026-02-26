#!/usr/bin/env bash
set -euo pipefail

ROOT="/home/sun07ao/xekr"
ST="$ROOT/sTuTraSt"
SELF_SCRIPT="$(realpath -m "${BASH_SOURCE[0]}")"
MAKEGRID_DIR="$ROOT/makegrid"
GRID_SCRIPT="$MAKEGRID_DIR/generate_pes_grid_v9.py"
CPP_BIN="$ST/build/tutrast"
MATLAB_DIR="$ROOT/TuTraSt"
MATLAB_MAIN="$MATLAB_DIR/TuTraSt_main.m"
COMPARE_PY="$ST/compare_outputs.py"
CIF_DIR="/home/sun07ao/xekr/CIF/clean/总/optimal"

MODE="${1:-launch}"
RUN_DIR="${2:-}"
if [[ -n "${RUN_DIR}" ]]; then
  RUN_DIR="$(realpath -m "$RUN_DIR")"
fi

PROBE="${PROBE:-Xe}"
CASE_LIMIT="${CASE_LIMIT:-6}"
CASE_OFFSET="${CASE_OFFSET:-0}"
MAX_JOBS="${MAX_JOBS:-2}"
GRID_NJOBS="${GRID_NJOBS:-1}"
GRID_CHUNK_SIZE="${GRID_CHUNK_SIZE:-20000}"
GRID_SPACING="${GRID_SPACING:-0.15}"
GRID_CUTOFF="${GRID_CUTOFF:-12.8}"
CPP_TIMEOUT="${CPP_TIMEOUT:-1800}"
MAT_TIMEOUT="${MAT_TIMEOUT:-2400}"
REPAIR_MAX_VOXELS="${REPAIR_MAX_VOXELS:-300000}"
REPAIR_SAMPLE_LIMIT="${REPAIR_SAMPLE_LIMIT:-4}"

SIM_TEMP="${SIM_TEMP:-1000}"
INPUT_ENERGY_STEP="${INPUT_ENERGY_STEP:-2}"
INPUT_ENERGY_CUTOFF="${INPUT_ENERGY_CUTOFF:-20}"
PROD_RUN_KMC="${PROD_RUN_KMC:-1}"
REPAIR_RUN_KMC="${REPAIR_RUN_KMC:-1}"

SWAP_THRESHOLD_KB="${SWAP_THRESHOLD_KB:-131072}"
MEM_FLOOR_KB="${MEM_FLOOR_KB:-8388608}"

get_mem_avail_kb() { awk '/MemAvailable/ {print $2}' /proc/meminfo; }
get_swap_used_kb() { awk '/SwapTotal/ {t=$2} /SwapFree/ {f=$2} END {print t-f}'; }
running_jobs() { (jobs -pr 2>/dev/null || true) | wc -l; }

probe_mass() {
  case "$1" in
    Xe|xe) echo "131.293e-3" ;;
    Kr|kr) echo "83.798e-3" ;;
    *) echo "131.293e-3" ;;
  esac
}

make_input_param() {
  local out="$1"
  local run_kmc="${2:-$PROD_RUN_KMC}"
  local mass
  mass="$(probe_mass "$PROBE")"
  cat > "$out" <<EOF
6
1
$SIM_TEMP
${run_kmc}
0
3000
100
5
1
1
$mass
$INPUT_ENERGY_STEP
$INPUT_ENERGY_CUTOFF
EOF
}

ensure_repair_patch() {
  local patch_dir="$1"
  mkdir -p "$patch_dir"
  cat > "$patch_dir/cube2xsfdat.m" <<'EOF'
function [grid,grid_size,pot_data]=cube2xsfdat(E_unit)
disp('writing pot data...')
if E_unit==1
    conv2kJmol=1;
elseif E_unit==2
    conv2kJmol=4.184;
elseif E_unit==3
    conv2kJmol=1312.7497;
elseif E_unit==4
    conv2kJmol=96.4853;
elseif E_unit==5
    conv2kJmol=627.5096;
elseif E_unit==6
    conv2kJmol=0.0083144621;
else
    error('Unsupported E_unit: %d', E_unit);
end
fid = fopen('grid.cube','r');
if fid < 0
    error('Cannot open grid.cube');
end
fgetl(fid); fgetl(fid);
line3 = sscanf(fgetl(fid), '%f');
natoms = abs(round(line3(1)));
line4 = sscanf(fgetl(fid), '%f');
line5 = sscanf(fgetl(fid), '%f');
line6 = sscanf(fgetl(fid), '%f');
grid = [round(line4(1)) round(line5(1)) round(line6(1))];
a_grid = sqrt(line4(2)^2 + line4(3)^2 + line4(4)^2) * 0.529177249;
b_grid = sqrt(line5(2)^2 + line5(3)^2 + line5(4)^2) * 0.529177249;
c_grid = sqrt(line6(2)^2 + line6(3)^2 + line6(4)^2) * 0.529177249;
grid_size = [a_grid b_grid c_grid];
for i = 1:natoms
    fgetl(fid);
end
vals = fscanf(fid, '%f');
fclose(fid);
n_expected = grid(1) * grid(2) * grid(3);
if numel(vals) < n_expected
    error('Insufficient grid values: got %d, expected %d', numel(vals), n_expected);
end
vals = vals(1:n_expected);
shift_cube_data = (vals - min(vals)) * conv2kJmol;
data_mat = permute(reshape(shift_cube_data, [grid(3), grid(2), grid(1)]), [3, 2, 1]);
flat = data_mat(:);
n_rows = ceil(numel(flat) / 6);
n_pad = n_rows * 6 - numel(flat);
if n_pad > 0
    flat = [flat; zeros(n_pad, 1)];
end
pot_data = reshape(flat, 6, n_rows).';
fid_xsf = fopen('xsf_data.dat','w');
fprintf(fid_xsf, '%f %30f %30f %30f %30f %30f\n', pot_data.');
fclose(fid_xsf);
disp('writing pot data done')
end
EOF
}

select_cases() {
  local run_dir="$1"
  python3 - "$CIF_DIR" "$CASE_LIMIT" "$CASE_OFFSET" "$run_dir/selected_cases.csv" "$GRID_SPACING" "$GRID_CUTOFF" <<'PY'
import math, re, sys
from pathlib import Path

cif_dir = Path(sys.argv[1])
limit = int(sys.argv[2])
offset = int(sys.argv[3])
out = Path(sys.argv[4])
grid_spacing = float(sys.argv[5])
grid_cutoff = float(sys.argv[6])

items = []
for p in cif_dir.glob("*.cif"):
    txt = p.read_text(errors="ignore")
    def get(key):
        m = re.search(r'^' + re.escape(key) + r'\s+([0-9.]+)', txt, flags=re.M)
        return float(m.group(1)) if m else None
    a, b, c = get("_cell_length_a"), get("_cell_length_b"), get("_cell_length_c")
    if not (a and b and c):
        continue
    expand_target = 2.0 * grid_cutoff
    ea = math.ceil(expand_target / a) * a
    eb = math.ceil(expand_target / b) * b
    ec = math.ceil(expand_target / c) * c
    nx = math.ceil(ea / grid_spacing)
    ny = math.ceil(eb / grid_spacing)
    nz = math.ceil(ec / grid_spacing)
    est_vox = nx * ny * nz
    items.append((est_vox, p.stat().st_size, p.stem, str(p), nx, ny, nz))

items.sort(key=lambda x: (x[0], x[1], x[2]))
sel = items[offset:offset + limit]

out.write_text("case_id,cif,est_vox,nx,ny,nz\n", encoding="utf-8")
with out.open("a", encoding="utf-8") as f:
    for est_vox, _, case_id, cif, nx, ny, nz in sel:
        f.write(f"{case_id},{cif},{est_vox},{nx},{ny},{nz}\n")
print(f"selected={len(sel)} offset={offset} -> {out}")
PY
}

run_case() {
  local run_dir="$1"
  local case_id="$2"
  local cif="$3"
  local est_vox="$4"
  local nx="$5"
  local ny="$6"
  local nz="$7"

  local cdir="$run_dir/cases/$case_id"
  local grid_dir="$run_dir/grids"
  local run_cpp="$cdir/cpp"
  local grid_out="$grid_dir/${case_id}_${PROBE}.cube"
  mkdir -p "$cdir" "$run_cpp" "$grid_dir"

  local grid_status="OK"
  local cpp_status="OK"
  local basis_lines=0
  local proc_lines=0
  local cpp_time="0.000"

  if [[ ! -f "$grid_out" ]]; then
    set +e
    (
      source /home/sun07ao/.local/share/mamba/etc/profile.d/mamba.sh
      mamba activate mkgrid
      export JOBLIB_TEMP_FOLDER=/tmp
      export TMPDIR=/tmp
      cd "$MAKEGRID_DIR"
      python "$GRID_SCRIPT" "$cif" "$grid_out" \
        --probe "$PROBE" \
        --spacing "$GRID_SPACING" \
        --cutoff "$GRID_CUTOFF" \
        --n_jobs "$GRID_NJOBS" \
        --chunk_size "$GRID_CHUNK_SIZE"
    ) > "$cdir/grid.log" 2>&1
    local g_exit=$?
    set -e
    if [[ $g_exit -ne 0 || ! -f "$grid_out" ]]; then
      grid_status="FAIL"
      cpp_status="SKIP"
      printf '%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n' \
        "$case_id" "$cif" "$est_vox" "$nx" "$ny" "$nz" \
        "$grid_status" "$cpp_status" "$cpp_time" "$basis_lines" "$proc_lines" "$cdir" \
        >> "$run_dir/summary.csv"
      return 0
    fi
  fi

  cp "$grid_out" "$run_cpp/grid.cube"
  make_input_param "$run_cpp/input.param" "$PROD_RUN_KMC"

  # Use monotonic uptime instead of wall clock to avoid negative durations when NTP adjusts system time.
  local cs ce _
  read -r cs _ < /proc/uptime
  set +e
  (cd "$run_cpp" && timeout "$CPP_TIMEOUT" "$CPP_BIN" > stdout.log 2>&1)
  local c_exit=$?
  set -e
  read -r ce _ < /proc/uptime
  cpp_time="$(awk -v a="$cs" -v b="$ce" 'BEGIN{printf "%.3f", (b-a)}')"

  if [[ $c_exit -ne 0 ]]; then
    cpp_status="FAIL"
  fi
  if [[ -f "$run_cpp/basis.dat" ]]; then basis_lines=$(wc -l < "$run_cpp/basis.dat"); fi
  if [[ -f "$run_cpp/processes_${SIM_TEMP}.dat" ]]; then
    proc_lines=$(wc -l < "$run_cpp/processes_${SIM_TEMP}.dat")
  elif [[ -f "$run_cpp/processes_1000.dat" ]]; then
    # Fallback for older runs/scripts.
    proc_lines=$(wc -l < "$run_cpp/processes_1000.dat")
  fi

  printf '%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n' \
    "$case_id" "$cif" "$est_vox" "$nx" "$ny" "$nz" \
    "$grid_status" "$cpp_status" "$cpp_time" "$basis_lines" "$proc_lines" "$cdir" \
    >> "$run_dir/summary.csv"
}

compare_one_file() {
  local matdir="$1"
  local cppdir="$2"
  local fname="$3"
  local line
  line="$(python3 "$COMPARE_PY" "$matdir" "$cppdir" "$fname" 2>&1 | head -1 || true)"
  if echo "$line" | grep -q ':MATCH:'; then
    echo "MATCH"
  elif echo "$line" | grep -q ':SKIP:'; then
    echo "SKIP"
  elif echo "$line" | grep -q ':DIFF:'; then
    echo "DIFF"
  else
    echo "ERROR"
  fi
}

compare_dave_stat() {
  local matdir="$1"
  local cppdir="$2"
  local fname="$3"
  python3 - "$matdir/$fname" "$cppdir/$fname" <<'PY' 2>/dev/null || { echo "ERROR"; return; }
import math, sys, os
f1, f2 = sys.argv[1], sys.argv[2]
if (not os.path.exists(f1)) and (not os.path.exists(f2)):
    print("SKIP"); raise SystemExit(0)
if not os.path.exists(f1) or not os.path.exists(f2):
    print("DIFF"); raise SystemExit(0)
try:
    a = [float(x) for x in open(f1).read().split()]
    b = [float(x) for x in open(f2).read().split()]
except Exception:
    print("ERROR"); raise SystemExit(0)
if len(a) != 6 or len(b) != 6:
    print("DIFF"); raise SystemExit(0)

# D_ave format: Dx errx Dy erry Dz errz
ok = True
for i in (0, 2, 4):
    d1, e1 = a[i], abs(a[i+1])
    d2, e2 = b[i], abs(b[i+1])
    if max(abs(d1), abs(d2), e1, e2) < 1e-20:
        continue
    sigma = math.sqrt(e1*e1 + e2*e2)
    diff = abs(d1 - d2)
    # Tightened criterion for screening use:
    # 1) primary: statistical consistency within 3 sigma
    # 2) fallback: <=10% relative difference, but only for non-negligible (non-near-zero) axes
    stat_ok = (sigma > 0 and diff <= 3.0 * sigma)
    informative = max(abs(d1), abs(d2)) > 10.0 * max(e1, e2, 1e-30)
    rel_ok = informative and (diff / max(abs(d1), abs(d2), 1e-30) <= 0.10)
    if not (stat_ok or rel_ok):
        ok = False
        break
print("MATCH" if ok else "DIFF")
PY
}

run_repair_case() {
  local run_dir="$1"
  local case_id="$2"
  local case_dir="$3"
  local patch_dir="$4"

  local rdir="$run_dir/repair/$case_id"
  local matdir="$rdir/matlab"
  local cppdir="$case_dir/cpp"
  mkdir -p "$matdir" "$rdir"

  cp "$cppdir/grid.cube" "$matdir/grid.cube"
  cp "$cppdir/input.param" "$matdir/input.param"
  if [[ "${REPAIR_RUN_KMC}" != "${PROD_RUN_KMC}" ]]; then
    awk -v kmc="$REPAIR_RUN_KMC" 'NR==4{$0=kmc} {print}' "$matdir/input.param" > "$matdir/input.param.tmp"
    mv "$matdir/input.param.tmp" "$matdir/input.param"
  fi

  set +e
  (cd "$matdir" && env -u LD_LIBRARY_PATH timeout "$MAT_TIMEOUT" octave -qf --path "$patch_dir:$MATLAB_DIR" "$MATLAB_MAIN" > stdout.log 2>&1)
  local m_exit=$?
  set -e

  local proc_file evol_file dave_file
  proc_file="processes_${SIM_TEMP}.dat"
  evol_file="Evol_${SIM_TEMP}.dat"
  dave_file="D_ave_${SIM_TEMP}.dat"
  local s_basis s_proc s_bt s_ts s_evol s_dave s_tunnel all_ok note
  if [[ $m_exit -eq 0 ]]; then
    s_basis="$(compare_one_file "$matdir" "$cppdir" "basis.dat")"
    s_proc="$(compare_one_file "$matdir" "$cppdir" "$proc_file")"
    s_bt="$(compare_one_file "$matdir" "$cppdir" "BT.dat")"
    s_ts="$(compare_one_file "$matdir" "$cppdir" "TS_data.out")"
    s_evol="$(compare_one_file "$matdir" "$cppdir" "$evol_file")"
    s_dave="$(compare_dave_stat "$matdir" "$cppdir" "$dave_file")"
    s_tunnel="$(compare_one_file "$matdir" "$cppdir" "tunnel_info.out")"

    all_ok=1
    for s in "$s_basis" "$s_proc" "$s_bt" "$s_ts" "$s_evol" "$s_dave" "$s_tunnel"; do
      if [[ "$s" == "DIFF" || "$s" == "ERROR" ]]; then
        all_ok=0
      fi
    done
    note="ok"
  else
    # Avoid recording timeout/nonzero-exit runs as fake DIFF rows.
    if [[ $m_exit -eq 124 ]]; then
      s_basis="TIMEOUT"; s_proc="TIMEOUT"; s_bt="TIMEOUT"; s_ts="TIMEOUT"; s_evol="TIMEOUT"; s_dave="TIMEOUT"; s_tunnel="TIMEOUT"
      note="mat_timeout"
    else
      s_basis="MAT_FAIL"; s_proc="MAT_FAIL"; s_bt="MAT_FAIL"; s_ts="MAT_FAIL"; s_evol="MAT_FAIL"; s_dave="MAT_FAIL"; s_tunnel="MAT_FAIL"
      note="mat_nonzero_exit"
    fi
    all_ok=0
  fi

  printf '%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n' \
    "$case_id" "$m_exit" "$s_basis" "$s_proc" "$s_bt" "$s_ts" "$s_evol" "$s_dave" "$s_tunnel" "$all_ok" "$note" \
    >> "$run_dir/repair_summary.csv"
}

run_producer() {
  local run_dir="$1"
  select_cases "$run_dir"
  local -a active_pids=()

  while IFS=, read -r case_id cif est_vox nx ny nz; do
    [[ -z "${case_id:-}" ]] && continue
    while true; do
      local rj mem swap
      # `jobs -pr` is unreliable in non-interactive shells. Track launched child PIDs explicitly.
      local -a still_active=()
      local pid
      for pid in "${active_pids[@]:-}"; do
        if kill -0 "$pid" 2>/dev/null; then
          still_active+=("$pid")
        fi
      done
      active_pids=("${still_active[@]}")
      rj="${#active_pids[@]}"
      mem="$(get_mem_avail_kb)"
      swap="$(get_swap_used_kb < /proc/meminfo)"
      printf '%s,%s,%s,%s\n' "$(date +%s)" "$rj" "$mem" "$swap" >> "$run_dir/scheduler.csv"
      if (( rj < MAX_JOBS )) && (( mem > MEM_FLOOR_KB )) && (( swap < SWAP_THRESHOLD_KB )); then
        break
      fi
      sleep 5
    done
    run_case "$run_dir" "$case_id" "$cif" "$est_vox" "$nx" "$ny" "$nz" &
    active_pids+=("$!")
    sleep 1
  done < <(tail -n +2 "$run_dir/selected_cases.csv")
  wait
  touch "$run_dir/PRODUCTION_DONE"
}

run_repair_monitor() {
  local run_dir="$1"
  local patch_dir="$run_dir/repair_patch"
  ensure_repair_patch "$patch_dir"
  local repair_header="case_id,mat_exit,basis,processes,BT,TS,Evol,Dave,tunnel,all_ok,note"
  if [[ ! -f "$run_dir/repair_summary.csv" ]]; then
    echo "$repair_header" > "$run_dir/repair_summary.csv"
  else
    local first_line
    first_line="$(head -n 1 "$run_dir/repair_summary.csv" || true)"
    if [[ "$first_line" != "$repair_header" ]]; then
      local tmp_rs="$run_dir/repair_summary.csv.tmp.$$"
      {
        echo "$repair_header"
        cat "$run_dir/repair_summary.csv"
      } > "$tmp_rs"
      mv "$tmp_rs" "$run_dir/repair_summary.csv"
    fi
  fi

  while true; do
    if [[ -f "$run_dir/summary.csv" ]]; then
      # case_id,case_dir for OK cpp runs and small voxel sizes
      local pending
      pending="$(python3 - "$run_dir/summary.csv" "$run_dir/repair_summary.csv" "$REPAIR_MAX_VOXELS" "$REPAIR_SAMPLE_LIMIT" <<'PY'
import csv, sys
summary, repair, max_vox, limit = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4])
done=set()
try:
    with open(repair) as f:
        for r in csv.DictReader(f):
            done.add(r["case_id"])
except FileNotFoundError:
    pass
if limit > 0 and len(done) >= limit:
    raise SystemExit(0)
todo=[]
with open(summary) as f:
    for r in csv.DictReader(f):
        if r["cpp_status"] != "OK":
            continue
        if int(r["est_vox"]) > max_vox:
            continue
        todo.append((int(r["est_vox"]), r["case_id"], r["case_dir"]))
todo.sort()
if limit > 0:
    todo = todo[:limit]
for _,cid,cdir in todo:
    if cid in done:
        continue
    print(f"{cid},{cdir}")
PY
)"
      if [[ -n "$pending" ]]; then
        while IFS=, read -r cid cdir; do
          [[ -z "${cid:-}" ]] && continue
          run_repair_case "$run_dir" "$cid" "$cdir" "$patch_dir"
        done <<< "$pending"
      fi
    fi

    if [[ -f "$run_dir/PRODUCTION_DONE" ]]; then
      # Exit when no more pending repair tasks
      local remain
      remain="$(python3 - "$run_dir/summary.csv" "$run_dir/repair_summary.csv" "$REPAIR_MAX_VOXELS" "$REPAIR_SAMPLE_LIMIT" <<'PY'
import csv, sys
summary, repair, max_vox, limit = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4])
done=set()
try:
    with open(repair) as f:
        for r in csv.DictReader(f):
            done.add(r["case_id"])
except FileNotFoundError:
    pass
if limit > 0 and len(done) >= limit:
    print(0)
    raise SystemExit(0)
todo=[]
with open(summary) as f:
    for r in csv.DictReader(f):
        if r["cpp_status"]!="OK":
            continue
        if int(r["est_vox"])>max_vox:
            continue
        todo.append((int(r["est_vox"]), r["case_id"]))
todo.sort()
if limit > 0:
    todo = todo[:limit]
left=0
for _, cid in todo:
    if cid not in done:
        left += 1
print(left)
PY
)"
      if [[ "$remain" == "0" ]]; then
        break
      fi
    fi
    sleep 15
  done
}

launch() {
  local ts uniq run_dir
  ts="$(date +%Y%m%d_%H%M%S)"
  uniq="$(date +%N)"
  run_dir="$ST/production_partial_optimal_${PROBE}_${ts}_off$(printf '%03d' "$CASE_OFFSET")_${uniq}"
  mkdir -p "$run_dir/cases" "$run_dir/grids" "$run_dir/repair"
  echo "RUN_DIR=$run_dir"
  cat > "$run_dir/config.env" <<EOF
PROBE=$PROBE
CASE_LIMIT=$CASE_LIMIT
CASE_OFFSET=$CASE_OFFSET
MAX_JOBS=$MAX_JOBS
GRID_NJOBS=$GRID_NJOBS
GRID_CHUNK_SIZE=$GRID_CHUNK_SIZE
GRID_SPACING=$GRID_SPACING
GRID_CUTOFF=$GRID_CUTOFF
CPP_TIMEOUT=$CPP_TIMEOUT
MAT_TIMEOUT=$MAT_TIMEOUT
REPAIR_MAX_VOXELS=$REPAIR_MAX_VOXELS
REPAIR_SAMPLE_LIMIT=$REPAIR_SAMPLE_LIMIT
PROD_RUN_KMC=$PROD_RUN_KMC
REPAIR_RUN_KMC=$REPAIR_RUN_KMC
SIM_TEMP=$SIM_TEMP
INPUT_ENERGY_STEP=$INPUT_ENERGY_STEP
INPUT_ENERGY_CUTOFF=$INPUT_ENERGY_CUTOFF
SWAP_THRESHOLD_KB=$SWAP_THRESHOLD_KB
MEM_FLOOR_KB=$MEM_FLOOR_KB
EOF

  echo "timestamp,running_jobs,mem_avail_kb,swap_used_kb" > "$run_dir/scheduler.csv"
  echo "case_id,cif,est_vox,nx,ny,nz,grid_status,cpp_status,cpp_time_s,basis_lines,proc_lines,case_dir" > "$run_dir/summary.csv"
  echo "case_id,mat_exit,basis,processes,BT,TS,Evol,Dave,tunnel,all_ok,note" > "$run_dir/repair_summary.csv"

  "$SELF_SCRIPT" produce "$run_dir" &
  local prod_pid=$!
  "$SELF_SCRIPT" repair "$run_dir" &
  local repair_pid=$!

  wait "$prod_pid"
  wait "$repair_pid"

  python3 - "$run_dir/summary.csv" "$run_dir/repair_summary.csv" <<'PY'
import csv,sys
s=list(csv.DictReader(open(sys.argv[1])))
r=list(csv.DictReader(open(sys.argv[2])))
ok=sum(1 for x in s if x["cpp_status"]=="OK")
fail=sum(1 for x in s if x["cpp_status"]=="FAIL" or x["grid_status"]=="FAIL")
print(f"production_ok={ok} production_fail={fail} total={len(s)} repair_checked={len(r)}")
PY

}

case "$MODE" in
  launch)
    launch
    ;;
  produce)
    if [[ -z "$RUN_DIR" ]]; then
      echo "Usage: $0 produce <RUN_DIR>" >&2
      exit 1
    fi
    run_producer "$RUN_DIR"
    ;;
  repair)
    if [[ -z "$RUN_DIR" ]]; then
      echo "Usage: $0 repair <RUN_DIR>" >&2
      exit 1
    fi
    run_repair_monitor "$RUN_DIR"
    ;;
  *)
    echo "Usage: $0 [launch|produce|repair] [RUN_DIR]" >&2
    exit 1
    ;;
esac
