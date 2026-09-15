#!/usr/bin/env bash
set -euo pipefail

flow_bin="$1"
make_flux_bin="$2"
compare_bin="$3"
sector_deck="$4"
mapping_file="$5"

parent_root="BASE_DUMPFLUX_FLORES"
sector_root="BASE_SECTOR_PRES"
parent_dir="$(cd "$(dirname "$sector_deck")" && pwd)"
parent_case="${parent_dir}/${parent_root}"
sector_case="${parent_dir}/${sector_root}"

if [[ ! -f "${parent_root}.UNRST" ]]; then
    echo "Expected ${parent_root}.UNRST in test result directory" >&2
    exit 1
fi

if [[ ! -f "${parent_case}.DATA" ]]; then
    echo "Expected parent deck at ${parent_case}.DATA" >&2
    exit 1
fi

cleanup_links=()
for ext in UNRST SMSPEC UNSMRY ESMRY INIT; do
    src="$PWD/${parent_root}.${ext}"
    dst="${parent_case}.${ext}"
    if [[ -f "$src" ]]; then
        ln -sfn "$src" "$dst"
        cleanup_links+=("$dst")
    fi
done

cleanup() {
    for link in "${cleanup_links[@]:-}"; do
        if [[ -L "$link" ]]; then
            rm -f "$link"
        fi
    done
}
trap cleanup EXIT

"$make_flux_bin" \
    --parent="$parent_case" \
    --mapping="$mapping_file" \
    --output="$PWD/${sector_root}.FLUX" \
    --mode=pressure \
    > "make_flux_sector_parent.log" 2>&1

ln -sfn "$PWD/${sector_root}.FLUX" "${sector_case}.FLUX"
cleanup_links+=("${sector_case}.FLUX")

"$flow_bin" \
    --output-dir="$PWD" \
    --enable-tuning=true \
    "$sector_deck" \
    > "sector_run.log" 2>&1

"$compare_bin" \
    "$PWD/${parent_root}.UNRST" \
    "$PWD/${sector_root}.UNRST" \
    "$mapping_file" \
    8e-2 1e-2 1.5e-1 \
    > "comparator.log" 2>&1
