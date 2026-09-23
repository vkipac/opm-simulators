#!/usr/bin/env bash
set -euo pipefail

# A defaulted THPRES entry is not a number anyone wrote down. It is the largest
# initial potential difference found anywhere along that region boundary, so
# arriving at it means looking at the whole boundary. A sector holds part of
# one, and working the entry out from a sector therefore gives a number that is
# too small -- on a real field, 0.01 bar where the parent had 0.81, which let
# flow through faces the parent held shut and left the sector drifting away
# from its parent by a third of a bar.
#
# The producer records its table and the consumer adopts it. This checks both
# halves: that the arrays reach the file, and that the consumer's own report of
# its thresholds is the parent's table rather than the one it would have
# derived.
#
# The decks are built so the two genuinely differ: the region boundary runs the
# full depth of the grid with the pressure jump growing a bar per layer, while
# the sector holds only the top three layers. Derived locally the entry comes
# out around 3 bar; the parent's is around 10.

flow_bin="$1"
producer_deck="$2"
consumer_deck="$3"

producer_base="$(basename "$producer_deck" .DATA)"
consumer_base="$(basename "$consumer_deck" .DATA)"

cp "$producer_deck" "${producer_base}.DATA"
"$flow_bin" "${producer_base}.DATA" --output-dir="$(pwd)" > thpres_dump.log 2>&1
grep -q "End of simulation" thpres_dump.log

for array in FLXTHPR FLXEQLN; do
    if ! strings "${producer_base}.FLUX" | grep -qw "${array}"; then
        echo "check-flux-threshold-pressure: the producer wrote no ${array} array," \
             "so a reduced run cannot be told what the thresholds were" >&2
        exit 1
    fi
done

# The threshold table as the run itself reports it, one "from to value" triple
# per line. Comparing the report rather than the raw array keeps the check on
# the number the simulator will actually use.
thresholds() {
    grep -E '^ +[0-9]+ +[0-9]+ +[0-9.eE+-]+ +BARSA *$' "$1" \
        | awk '{print $1, $2, $3}'
}

parent_thresholds="$(thresholds "${producer_base}.PRT")"

if [[ -z "$parent_thresholds" ]]; then
    echo "check-flux-threshold-pressure: the producer reported no threshold pressures," \
         "so there is nothing for this test to compare" >&2
    exit 1
fi

rm -rf sector
mkdir -p sector
cp "$consumer_deck" "sector/${consumer_base}.DATA"
# The consumer's USEFLUX names the producer's base, so the file keeps its name.
cp "${producer_base}.FLUX" "sector/${producer_base}.FLUX"

(cd sector && "$flow_bin" "${consumer_base}.DATA" --output-dir=. > consumer.log 2>&1) || {
    echo "check-flux-threshold-pressure: consumer failed" >&2
    grep -iE "exception|error" sector/consumer.log >&2 || true
    exit 1
}

grep -q "End of simulation" sector/consumer.log

# The adopted table is logged after the locally derived one, so take the last
# report in the file.
sector_thresholds="$(thresholds "sector/${consumer_base}.PRT" | tail -n "$(wc -l <<< "$parent_thresholds")")"

if [[ "$sector_thresholds" != "$parent_thresholds" ]]; then
    echo "check-flux-threshold-pressure: the sector did not adopt the parent's" \
         "threshold pressures" >&2
    echo "parent:" >&2
    echo "$parent_thresholds" >&2
    echo "sector:" >&2
    echo "$sector_thresholds" >&2
    exit 1
fi

echo "check-flux-threshold-pressure: ok"
