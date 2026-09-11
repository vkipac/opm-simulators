#!/usr/bin/env bash
set -euo pipefail

flow_bin="$1"
consumer_deck="$2"

if [[ ! -f FLUX_DUMP_USEFLUX_SUFFIX_SMOKE.FLUX ]]; then
    echo "Expected FLUX_DUMP_USEFLUX_SUFFIX_SMOKE.FLUX in test result directory" >&2
    exit 1
fi

# Stage only an invalid suffix file; it must not be accepted as FLUX input.
rm -f FLUX_USE_INVALID_SUFFIX_SMOKE.FLUX
rm -f FLUX_USE_INVALID_SUFFIX_SMOKE.FLUX????
cp FLUX_DUMP_USEFLUX_SUFFIX_SMOKE.FLUX FLUX_USE_INVALID_SUFFIX_SMOKE.FLUXABCD
cp "$consumer_deck" FLUX_USE_INVALID_SUFFIX_SMOKE.DATA

if "$flow_bin" FLUX_USE_INVALID_SUFFIX_SMOKE.DATA > useflux_invalid_suffix_smoke.log 2>&1; then
    echo "Expected USEFLUX consumer run to fail with invalid FLUX suffix-only input" >&2
    exit 1
fi

grep -q "USEFLUX requested but no FLUX file found" useflux_invalid_suffix_smoke.log
