#!/bin/bash

set -euo pipefail

flux_file="FLUX_DUMP_SMOKE.FLUX"

test -s "${flux_file}"

# Ensure expected core arrays exist in the emitted file.
strings "${flux_file}" | grep -q "FLUXHEAD"
strings "${flux_file}" | grep -q "LOCGLOB"
strings "${flux_file}" | grep -q "FLXSTEP"
