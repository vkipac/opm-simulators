#!/usr/bin/env bash
set -euo pipefail

flow_bin="$1"
consumer_deck="$2"

if [[ ! -f FLUX_DUMP_USEFLUX_FLUX_SMOKE.FLUX ]]; then
    echo "Expected FLUX_DUMP_USEFLUX_FLUX_SMOKE.FLUX in test result directory" >&2
    exit 1
fi

cp FLUX_DUMP_USEFLUX_FLUX_SMOKE.FLUX FLUX_USE_FLUX_SMOKE.FLUX
cp "$consumer_deck" FLUX_USE_FLUX_SMOKE.DATA
"$flow_bin" FLUX_USE_FLUX_SMOKE.DATA > useflux_flux_smoke.log 2>&1

grep -q "Total number of active cells: 100" useflux_flux_smoke.log
