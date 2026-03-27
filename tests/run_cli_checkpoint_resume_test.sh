#!/usr/bin/env bash

set -euo pipefail

BIN="$1"
FIXTURE_DIR="$2"
WORKDIR="$(mktemp -d)"
trap 'rm -rf "$WORKDIR"' EXIT

mkdir -p "$WORKDIR/fresh" "$WORKDIR/pause" "$WORKDIR/invalid"
cp "$FIXTURE_DIR/input.param" "$WORKDIR/fresh/input.param"
cp "$FIXTURE_DIR/grid.cube" "$WORKDIR/fresh/grid.cube"
cp "$FIXTURE_DIR/input.param" "$WORKDIR/pause/input.param"
cp "$FIXTURE_DIR/grid.cube" "$WORKDIR/pause/grid.cube"
cp "$FIXTURE_DIR/input.param" "$WORKDIR/invalid/input.param"
cp "$FIXTURE_DIR/grid.cube" "$WORKDIR/invalid/grid.cube"

(
  cd "$WORKDIR/fresh"
  TUTRAST_KMC_SEED=42 "$BIN" > fresh.log 2>&1
)

(
  cd "$WORKDIR/pause"
  TUTRAST_KMC_SEED=42 "$BIN" --checkpoint-every 10 --max-kmc-steps 20 > pause.log 2>&1
  test -f T300_run1.chk
  test ! -f D_ave_300.dat
  test ! -f T300_msd1.dat
  TUTRAST_KMC_SEED=42 "$BIN" --resume ./T300_run1.chk > resume.log 2>&1
)

diff -u "$WORKDIR/fresh/D_ave_300.dat" "$WORKDIR/pause/D_ave_300.dat"
diff -u "$WORKDIR/fresh/T300_msd1.dat" "$WORKDIR/pause/T300_msd1.dat"

if (
  cd "$WORKDIR/invalid"
  "$BIN" --checkpoint-every 0 > invalid.log 2>&1
); then
  echo "Expected invalid CLI invocation to fail"
  exit 1
fi

grep -q "checkpoint-every must be > 0" "$WORKDIR/invalid/invalid.log"
