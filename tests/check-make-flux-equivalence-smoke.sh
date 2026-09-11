#!/usr/bin/env bash
set -euo pipefail

make_flux_bin="$1"
compare_bin="$2"
parent_deck="$3"
parent_root="$4"
mode="$5"
mapping_text="$6"
mapping_text="${mapping_text#\'}"
mapping_text="${mapping_text%\'}"

if [[ ! -f "${parent_root}.FLUX" ]]; then
    echo "Expected ${parent_root}.FLUX in test result directory" >&2
    exit 1
fi

cp "$parent_deck" "${parent_root}.DATA"
printf '%s\n' "$mapping_text" > "${parent_root}.MAP"

"$make_flux_bin" \
    --parent="$PWD/${parent_root}" \
    --mapping="$PWD/${parent_root}.MAP" \
    --output="$PWD/${parent_root}.MAKE.FLUX" \
    --mode="$mode" \
    > "make_flux_${mode}_equivalence.log" 2>&1

"$compare_bin" "${parent_root}.FLUX" "${parent_root}.MAKE.FLUX"
