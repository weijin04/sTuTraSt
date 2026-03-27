#!/usr/bin/env bash

set -euo pipefail

BIN="$1"
FIXTURE_DIR="$2"
WORKDIR="$(mktemp -d)"
trap 'rm -rf "$WORKDIR"' EXIT

mkdir -p "$WORKDIR/baseline2" "$WORKDIR/baseline3" "$WORKDIR/campaign"
for dir in baseline2 baseline3 campaign; do
  cp "$FIXTURE_DIR/input.param" "$WORKDIR/$dir/input.param"
  cp "$FIXTURE_DIR/grid.cube" "$WORKDIR/$dir/grid.cube"
done

awk 'NR==8{$0="2"}1' "$WORKDIR/baseline2/input.param" > "$WORKDIR/baseline2/input.param.tmp"
mv "$WORKDIR/baseline2/input.param.tmp" "$WORKDIR/baseline2/input.param"
awk 'NR==8{$0="2"}1' "$WORKDIR/campaign/input.param" > "$WORKDIR/campaign/input.param.tmp"
mv "$WORKDIR/campaign/input.param.tmp" "$WORKDIR/campaign/input.param"

(
  cd "$WORKDIR/baseline2"
  TUTRAST_KMC_SEED=42 "$BIN" > baseline2.log 2>&1
)

(
  cd "$WORKDIR/campaign"
  TUTRAST_KMC_SEED=42 "$BIN" --campaign-dir ./campaign_state --checkpoint-every 10 --max-kmc-steps 20 > campaign_pause.log 2>&1
  test -f campaign_state/campaign.manifest
  grep -q "Campaign paused during run 1" campaign_pause.log
  TUTRAST_KMC_SEED=42 "$BIN" --campaign-dir ./campaign_state --checkpoint-every 10 > campaign_finish.log 2>&1
)

diff -u "$WORKDIR/baseline2/D_ave_300.dat" "$WORKDIR/campaign/D_ave_300.dat"
diff -u "$WORKDIR/baseline2/T300_msd1.dat" "$WORKDIR/campaign/T300_msd1.dat"
diff -u "$WORKDIR/baseline2/T300_msd2.dat" "$WORKDIR/campaign/T300_msd2.dat"

awk 'NR==8{$0="3"}1' "$FIXTURE_DIR/input.param" > "$WORKDIR/baseline3/input.param"
awk 'NR==8{$0="3"}1' "$WORKDIR/campaign/input.param" > "$WORKDIR/campaign/input.param.tmp"
mv "$WORKDIR/campaign/input.param.tmp" "$WORKDIR/campaign/input.param"

(
  cd "$WORKDIR/baseline3"
  TUTRAST_KMC_SEED=42 "$BIN" > baseline3.log 2>&1
)

(
  cd "$WORKDIR/campaign"
  TUTRAST_KMC_SEED=42 "$BIN" --campaign-dir ./campaign_state --checkpoint-every 10 > campaign_append.log 2>&1
)

diff -u "$WORKDIR/baseline3/D_ave_300.dat" "$WORKDIR/campaign/D_ave_300.dat"
diff -u "$WORKDIR/baseline3/T300_msd3.dat" "$WORKDIR/campaign/T300_msd3.dat"
