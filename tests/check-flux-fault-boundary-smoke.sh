#!/usr/bin/env bash
set -euo pipefail

# A boundary face the parent could not flow through must not become an open
# pressure boundary in the reduced run, even though the FLUX file supplies an
# exterior pressure for it like any other face. The transmissibility recorded
# alongside that pressure is what decides, and for a face sealed by a fault or
# a multiplier it is zero.
#
# The sector is the i=1 column, whose only boundary faces are the ten I+ faces
# towards i=2, all sealed by MULTX in the parent. The parent's producer pulls
# the far side down to 50 bar while the sealed column stays at its initial 300
# bar. The sector deck carries no multiplier of its own, so the only thing that
# can hold it at 300 bar is the zero transmissibility in the FLUX file: without
# it the face falls back on the default outer-boundary transmissibility and the
# column drains to 50 bar, which is a mistake far too large to be mistaken for
# drift.

flow_bin="$1"
summary_bin="$2"
inspect_bin="$3"
producer_deck="$4"
consumer_deck="$5"

parent_base="$(basename "$producer_deck" .DATA)"
sector_base="$(basename "$consumer_deck" .DATA)"

cp "$consumer_deck" "${sector_base}.DATA"
"$flow_bin" "${sector_base}.DATA" --output-dir="$(pwd)" > fault_use.log 2>&1
grep -q "End of simulation" fault_use.log

initial_pressure=300.0
exterior_pressure=50.0

# Take the last value of a summary vector, skipping the blank line and heading
# the tool prints, and ignoring the bogus t=0 row.
last_value() {
    "$summary_bin" "$1.SMSPEC" "$2" 2>/dev/null | awk 'NF { value = $1 } END { print value }'
}

# 1. The parent has to actually be sealed, or there is nothing to reproduce.
parent_sector="$(last_value "$parent_base" "BPR:1,1,5")"
parent_exterior="$(last_value "$parent_base" "BPR:2,1,5")"

awk -v got="$parent_sector" -v want="$initial_pressure" 'BEGIN {
    if ((got - want) ^ 2 > 0.01) {
        printf "parent sector cell drifted to %s bar, expected to stay at %s\n", got, want > "/dev/stderr";
        exit 1;
    }
}'

awk -v got="$parent_exterior" -v want="$exterior_pressure" 'BEGIN {
    if ((got - want) ^ 2 > 1.0) {
        printf "parent exterior cell is at %s bar, expected it drawn down to about %s\n", got, want > "/dev/stderr";
        exit 1;
    }
}'

# 2. The face must be described, and described as sealed. A file that simply
#    omitted it would also keep the sector at 300 bar, for the wrong reason.
"$inspect_bin" "${parent_base}.FLUX" 1 1 5 2> inspect_fault.log > series_fault.txt

if ! grep -qE "^ +I\+ .* transmissibility 0 " inspect_fault.log; then
    echo "check-flux-fault-boundary-smoke: expected an I+ face with zero transmissibility" >&2
    cat inspect_fault.log >&2
    exit 1
fi

# The exterior pressure has to be far from the sector's, or a leak would not
# show up in the comparison below.
if ! awk 'NR > 2 { if ($2 < 1.0e7) { found = 1 } } END { exit found ? 0 : 1 }' series_fault.txt; then
    echo "check-flux-fault-boundary-smoke: the exterior pressure never drops far below the sector's," \
         "so this case could not detect a leak" >&2
    cat series_fault.txt >&2
    exit 1
fi

# 3. And the reduced run must hold, matching the parent rather than the
#    pressure on the far side of the sealed face.
sector_value="$(last_value "$sector_base" "BPR:1,1,5")"

awk -v got="$sector_value" -v want="$parent_sector" -v leak="$exterior_pressure" 'BEGIN {
    if ((got - want) ^ 2 > 0.01) {
        printf "sector cell is at %s bar but the parent holds it at %s.\n", got, want > "/dev/stderr";
        if ((got - leak) ^ 2 < 100.0) {
            printf "It has drained towards the %s bar on the far side of the sealed face,\n", leak > "/dev/stderr";
            print  "so the zero transmissibility in the FLUX file was not applied and the" > "/dev/stderr";
            print  "face is acting as an open pressure boundary." > "/dev/stderr";
        }
        exit 1;
    }
}'
