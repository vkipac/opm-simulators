#!/usr/bin/env bash
set -euo pipefail

# With --flux-fpr-as-fprflux=true, a sector run reads FPR as FPRFLUX in its UDQ
# definitions and ACTIONX conditions: the field pressure averaged over the
# whole parent model at every time step of the run, rather than the parent's
# FPR at the parent's own report steps.
#
# The cut-out sector defines FU_FPR as FPR. With the switch, FU_FPR has to
# follow FPRFLUX exactly, while the FPR summary vector stays the parent's. The
# two differ early on, so a substitution that did not happen shows.

flow_bin="$1"
make_flux_bin="$2"
summary_bin="$3"
parent_deck="$4"
sector_deck="$5"

parent_base="$(basename "$parent_deck" .DATA)"
sector_base="$(basename "$sector_deck" .DATA)"

rm -rf sector
mkdir -p sector

cp "$parent_deck" "${parent_base}.DATA"

"$make_flux_bin" --parent="$PWD/${parent_base}" \
                 --mapping-inline='box 3 1 1 5 1 1' \
                 --output="$PWD/sector/${parent_base}.FLUX" > fpr_make_flux.log 2>&1 || {
    echo "check-flux-fpr-as-fprflux: make_flux failed" >&2
    cat fpr_make_flux.log >&2
    exit 1
}

cp "$sector_deck" "sector/${sector_base}.DATA"
(cd sector && "$flow_bin" "${sector_base}.DATA" --output-dir=. \
                          --flux-fpr-as-fprflux=true > sector.log 2>&1) || {
    echo "check-flux-fpr-as-fprflux: the sector run failed" >&2
    grep -iE "error|problem" -A4 sector/sector.log >&2 || true
    exit 1
}

if ! grep -q "FPR is evaluated as FPRFLUX" "sector/${sector_base}.PRT" ||
   ! grep -q "UDQ FU_FPR" "sector/${sector_base}.PRT"; then
    echo "check-flux-fpr-as-fprflux: the sector run did not report FU_FPR as changed" >&2
    exit 1
fi

compare='
    function rel(a, b) { d = a - b; if (d < 0) d = -d; s = (a < 0 ? -a : a); if (s < 1) s = 1; return d / s }
    NF == 0 { next }
    { ++rows }'

# Every time step of the sector: FU_FPR is FPRFLUX, and not FPR.
"$summary_bin" -n "sector/${sector_base}.SMSPEC" FPR FPRFLUX FU_FPR \
    | awk "$compare"'
        {
            if (rel($3, $2) > 1.0e-6) {
                printf "check-flux-fpr-as-fprflux: FU_FPR is %s and FPRFLUX %s at time step %d\n",
                       $3, $2, rows > "/dev/stderr"; bad = 1
            }
            if (rel($3, $1) > 1.0e-4) differs = 1
        }
        END {
            if (rows == 0) { print "check-flux-fpr-as-fprflux: no summary data" > "/dev/stderr"; exit 1 }
            if (!differs) { print "check-flux-fpr-as-fprflux: FU_FPR never differs from FPR" > "/dev/stderr"; exit 1 }
            exit bad
        }'

# Report steps: the FPR summary vector is still the parent's.
paste <("$summary_bin" -r -n "${parent_base}.SMSPEC" FPR) \
      <("$summary_bin" -r -n "sector/${sector_base}.SMSPEC" FPR) \
    | awk "$compare"'
        rel($2, $1) > 1.0e-4 {
            printf "check-flux-fpr-as-fprflux: FPR is %s in the sector and %s in the parent at report step %d\n",
                   $2, $1, rows > "/dev/stderr"; bad = 1
        }
        END { exit (rows == 0 || bad) }'
