#!/usr/bin/env bash
set -euo pipefail

# Two-well sector case: one producer inside the FLUX region and one outside,
# sharing a group target that is driven by a UDQ. The sector run has to drop
# the well outside the region and reduce the group target by what that well
# produced in the parent run.

flow_bin="$1"
compare_bin="$2"
producer_deck="$3"
consumer_deck="$4"

producer_base="$(basename "$producer_deck" .DATA)"
consumer_base="$(basename "$consumer_deck" .DATA)"

"$flow_bin" "$producer_deck" --output-dir="$(pwd)" > twowell_dump.log 2>&1
cp "${producer_base}.FLUX" "${consumer_base}.FLUX"
cp "$consumer_deck" "${consumer_base}.DATA"
"$flow_bin" "${consumer_base}.DATA" --output-dir="$(pwd)" > twowell_use.log 2>&1

grep -q "End of simulation" twowell_use.log

# Compare on report steps only. The FLUX payload carries the parent's summary
# values once per report step, while the UDQ in this deck advances on every
# time step, so the sector can only reproduce the parent at report step
# boundaries - which is where the exchanged boundary data is defined.
for keyword in WOPR:P1 WBHP:P1; do
    "$compare_bin" -d -t SMRY -k "$keyword" "$producer_base" "$consumer_base" 2e-2 2e-2
done
