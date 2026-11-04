#!/usr/bin/env bash
set -euo pipefail

# A sector run replays the parent's ACTIONX triggers for actions whose
# conditions read quantities the sector does not compute itself.
#
# Both decks get an ACTIONX that shuts P1, inside the sector, once WOPT of P2,
# outside it, exceeds 700. make_flux records when the parent triggered it, and
# the sector run has to trigger it at the same time, say that it replayed it,
# and so give P1 the parent's rates throughout. With --flux-replay-actions=false
# the condition is evaluated in the sector run instead.

flow_bin="$1"
make_flux_bin="$2"
summary_bin="$3"
parent_deck="$4"
sector_deck="$5"

parent_base="$(basename "$parent_deck" .DATA)"
sector_base="$(basename "$sector_deck" .DATA)"

action="ACTIONX
  'A_P2' 1 /
  WOPT 'P2' > 700 /
/
WELOPEN
  'P1' 'SHUT' /
/
ENDACTIO
"

with_action() {
    awk -v a="$action" '/^TSTEP/ && !done { printf "%s\n", a; done = 1 } { print }' "$1"
}

rm -rf replay
mkdir -p replay/sector replay/evaluate
cd replay

with_action "$parent_deck" > "${parent_base}.DATA"
"$flow_bin" "${parent_base}.DATA" --output-dir=. > parent.log 2>&1 || {
    echo "check-flux-action-replay: the parent run failed" >&2
    exit 1
}

"$make_flux_bin" --parent="$PWD/${parent_base}" \
                 --mapping-inline='box 3 1 1 5 1 1' \
                 --output="$PWD/sector/${parent_base}.FLUX" > make_flux.log 2>&1 || {
    echo "check-flux-action-replay: make_flux failed" >&2
    cat make_flux.log >&2
    exit 1
}

if ! grep -q "Recorded 1 ACTIONX trigger(s) of the parent" make_flux.log; then
    echo "check-flux-action-replay: make_flux did not record the parent's trigger" >&2
    cat make_flux.log >&2
    exit 1
fi

run_sector() {
    local dir="$1"; shift
    [[ "$dir" == sector ]] || cp "sector/${parent_base}.FLUX" "$dir/"
    with_action "$sector_deck" > "$dir/${sector_base}.DATA"
    (cd "$dir" && "$flow_bin" "${sector_base}.DATA" --output-dir=. "$@" > sector.log 2>&1) || {
        echo "check-flux-action-replay: the sector run in ${dir} failed" >&2
        grep -iE "error|problem" -A4 "$dir/sector.log" >&2 || true
        exit 1
    }
}

run_sector sector
run_sector evaluate --flux-replay-actions=false

prt="sector/${sector_base}.PRT"
if ! grep -q "A_P2: reads WOPT P2 (reads well P2, outside the sector)" "$prt" ||
   ! grep -q "Action A_P2 triggered at .*replayed: the parent run triggered it at" "$prt"; then
    echo "check-flux-action-replay: the sector run did not replay A_P2" >&2
    grep -A8 "ACTIONX replay" "$prt" >&2 || true
    exit 1
fi

if ! grep -q "every ACTIONX condition is evaluated" "evaluate/${sector_base}.PRT" ||
   grep -q "replayed:" "evaluate/${sector_base}.PRT"; then
    echo "check-flux-action-replay: --flux-replay-actions=false still replayed A_P2" >&2
    exit 1
fi

paste <("$summary_bin" -r -n "${parent_base}.SMSPEC" WOPR:P1) \
      <("$summary_bin" -r -n "sector/${sector_base}.SMSPEC" WOPR:P1) \
    | awk '
        NF == 0 { next }
        {
            ++rows; p = $1; s = $2; last = p;
            scale = (p < 0 ? -p : p); if (scale < 1) scale = 1;
            d = p - s; if (d < 0) d = -d;
            if (d > 1.0e-4 * scale) {
                printf "check-flux-action-replay: WOPR:P1 is %s in the sector and %s in the parent at report step %d\n",
                       s, p, rows > "/dev/stderr"; bad = 1
            }
        }
        END {
            if (rows == 0) { print "check-flux-action-replay: no summary data" > "/dev/stderr"; exit 1 }
            if (last > 1.0e-6 || last < -1.0e-6) {
                print "check-flux-action-replay: the parent never shut P1" > "/dev/stderr"; exit 1
            }
            exit bad
        }'
