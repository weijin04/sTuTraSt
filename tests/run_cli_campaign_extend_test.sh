#!/usr/bin/env bash

set -euo pipefail

BIN="$1"
FIXTURE_DIR="$2"
BIN="$(python3 -c 'import pathlib,sys; print(pathlib.Path(sys.argv[1]).resolve())' "$BIN")"
FIXTURE_DIR="$(python3 -c 'import pathlib,sys; print(pathlib.Path(sys.argv[1]).resolve())' "$FIXTURE_DIR")"
WORKDIR="$(mktemp -d)"
trap 'rm -rf "$WORKDIR"' EXIT

mkdir -p "$WORKDIR/baseline" "$WORKDIR/campaign" "$WORKDIR/multi" "$WORKDIR/multi_baseline"
for dir in baseline campaign multi multi_baseline; do
  cp "$FIXTURE_DIR/input.param" "$WORKDIR/$dir/input.param"
  cp "$FIXTURE_DIR/grid.cube" "$WORKDIR/$dir/grid.cube"
done

awk 'NR==6{$0="200"} NR==8{$0="1"}1' "$WORKDIR/baseline/input.param" > "$WORKDIR/baseline/input.param.tmp"
mv "$WORKDIR/baseline/input.param.tmp" "$WORKDIR/baseline/input.param"
awk 'NR==6{$0="100"} NR==8{$0="1"}1' "$WORKDIR/campaign/input.param" > "$WORKDIR/campaign/input.param.tmp"
mv "$WORKDIR/campaign/input.param.tmp" "$WORKDIR/campaign/input.param"
awk 'NR==6{$0="100"} NR==8{$0="2"}1' "$WORKDIR/multi/input.param" > "$WORKDIR/multi/input.param.tmp"
mv "$WORKDIR/multi/input.param.tmp" "$WORKDIR/multi/input.param"
awk 'NR==6{$0="200"} NR==8{$0="2"}1' "$WORKDIR/multi_baseline/input.param" > "$WORKDIR/multi_baseline/input.param.tmp"
mv "$WORKDIR/multi_baseline/input.param.tmp" "$WORKDIR/multi_baseline/input.param"

(
  cd "$WORKDIR/baseline"
  TUTRAST_KMC_SEED=42 "$BIN" > baseline.log 2>&1
)

(
  cd "$WORKDIR/multi_baseline"
  TUTRAST_KMC_SEED=42 "$BIN" > baseline.log 2>&1
)

(
  cd "$WORKDIR/campaign"
  TUTRAST_KMC_SEED=42 "$BIN" --campaign-dir ./campaign_state --campaign-plan-steps 200 > campaign100.log 2>&1
  test -f campaign_state/checkpoints/T300_run1.chk
  awk 'NR==6{$0="200"}1' input.param > input.param.tmp
  mv input.param.tmp input.param
  TUTRAST_KMC_SEED=42 "$BIN" --campaign-dir ./campaign_state > campaign200.log 2>&1
  awk 'NR==6{$0="150"}1' input.param > input.param.tmp
  mv input.param.tmp input.param
  if TUTRAST_KMC_SEED=42 "$BIN" --campaign-dir ./campaign_state > campaign_shrink.log 2>&1; then
    echo "Expected campaign shrink to be rejected"
    exit 1
  fi
  grep -q "Shrinking target n_steps" campaign_shrink.log
)

diff -u "$WORKDIR/baseline/D_ave_300.dat" "$WORKDIR/campaign/D_ave_300.dat"
diff -u "$WORKDIR/baseline/T300_msd1.dat" "$WORKDIR/campaign/T300_msd1.dat"

(
  cd "$WORKDIR/multi"
  TUTRAST_KMC_SEED=42 "$BIN" --campaign-dir ./campaign_state --campaign-plan-steps 200 --checkpoint-every 10 > multi100.log 2>&1
  test -f campaign_state/checkpoints/T300_run1.chk
  test -f campaign_state/checkpoints/T300_run2.chk
  awk 'NR==6{$0="200"}1' input.param > input.param.tmp
  mv input.param.tmp input.param
  TUTRAST_KMC_SEED=42 "$BIN" --campaign-dir ./campaign_state > multi_extend.log 2>&1
)

diff -u "$WORKDIR/multi_baseline/D_ave_300.dat" "$WORKDIR/multi/D_ave_300.dat"
diff -u "$WORKDIR/multi_baseline/T300_msd1.dat" "$WORKDIR/multi/T300_msd1.dat"
test -f "$WORKDIR/multi/T300_msd2.dat"
