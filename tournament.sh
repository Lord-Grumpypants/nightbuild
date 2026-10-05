#!/bin/bash

set -u

ROOT="$(cd "$(dirname "$0")" && pwd)"

ROUNDS="${1:-15}"

NIGHTBUILD="$ROOT/nightbuild"
NIGHTBUILD_BUILD="$ROOT/build"

NINJA_BUILD="$ROOT/ninja-build"

SAMU_BUILD="$ROOT/samu-build"

declare -a NIGHTBUILD_TIMES=()
declare -a NINJA_TIMES=()
declare -a SAMU_TIMES=()

echo "Requesting sudo access for benchmark cache purging..."

if ! sudo -v; then
echo "ERROR: sudo authentication failed"
exit 1
fi

run_timed() {
local label="$1"
shift


local tmp
tmp="$(mktemp)"

echo
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "  $label"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

/usr/bin/time -p "$@" 2>"$tmp"
local status=$?

if [[ $status -ne 0 ]]; then
    cat "$tmp" >&2

    local result
    result="$(awk '/^real / { print $2 }' "$tmp")"

    rm -f "$tmp"

    echo
    echo "  → FAILED (exit $status)"

    RUN_TIME="FAILED"
    return "$status"
fi

local result
result="$(awk '/^real / { print $2 }' "$tmp")"

rm -f "$tmp"

echo
echo "  → ${result}s"

RUN_TIME="$result"
return 0


}

cold_start() {
sudo purge >/dev/null 2>&1 || true
}

shuffle_competitors() {
COMPETITORS=(
"NightBuild"
"Ninja"
"samu"
)


local i
local j
local tmp

for ((i=${#COMPETITORS[@]}-1; i>0; i--)); do
    j=$((RANDOM % (i + 1)))

    tmp="${COMPETITORS[i]}"
    COMPETITORS[i]="${COMPETITORS[j]}"
    COMPETITORS[j]="$tmp"
done


}

run_nightbuild() {
rm -rf "$NIGHTBUILD_BUILD"


"$NIGHTBUILD" gen -C "$NIGHTBUILD_BUILD" >/dev/null

cold_start

if run_timed "NightBuild · clean build" \
    "$NIGHTBUILD" build -C "$NIGHTBUILD_BUILD" nightbuild; then

    # if [[ ! -x "$NIGHTBUILD_BUILD/nightbuild" ]]; then
    #     echo "ERROR: NightBuild did not produce nightbuild"
    #     return 1
    # fi

    NIGHTBUILD_TIMES+=("$RUN_TIME")
else
    return 1
fi


}

run_ninja() {
rm -rf \
"$NINJA_BUILD/obj" \
"$NINJA_BUILD/nightbuild" \
"$NINJA_BUILD/.ninja_log" \
"$NINJA_BUILD/.ninja_deps"


cold_start

if run_timed "Ninja · clean build" \
    ninja -C "$NINJA_BUILD"; then

    if [[ ! -x "$NINJA_BUILD/nightbuild" ]]; then
        echo "ERROR: Ninja did not produce nightbuild"
        return 1
    fi

    NINJA_TIMES+=("$RUN_TIME")
else
    return 1
fi


}

run_samu() {
rm -rf \
"$SAMU_BUILD/obj" \
"$SAMU_BUILD/nightbuild" \
"$SAMU_BUILD/.ninja_log" \
"$SAMU_BUILD/.ninja_deps"


cold_start

if run_timed "samu · clean build" \
    samu -C "$SAMU_BUILD"; then

    if [[ ! -x "$SAMU_BUILD/nightbuild" ]]; then
        echo "ERROR: samu did not produce nightbuild"
        return 1
    fi

    SAMU_TIMES+=("$RUN_TIME")
else
    return 1
fi


}

run_competitor() {
local competitor="$1"


case "$competitor" in
    NightBuild)
        run_nightbuild
        ;;
    Ninja)
        run_ninja
        ;;
    samu)
        run_samu
        ;;
    *)
        echo "ERROR: unknown competitor: $competitor"
        return 1
        ;;
esac


}

mean() {
printf '%s\n' "$@" |
awk '
{
sum += $1
count++
}
END {
if (count)
printf "%.3f", sum / count
else
printf "0.000"
}
'
}

median() {
printf '%s\n' "$@" |
sort -n |
awk '
{
values[NR] = $1
}
END {
if (NR == 0) {
printf "0.000"
} else if (NR % 2 == 1) {
printf "%.3f", values[(NR + 1) / 2]
} else {
printf "%.3f", 
(values[NR / 2] + values[NR / 2 + 1]) / 2
}
}
'
}

stddev() {
local avg="$1"
shift


printf '%s\n' "$@" |
    awk -v mean="$avg" '
        {
            sum += ($1 - mean) * ($1 - mean)
            count++
        }
        END {
            if (count > 1)
                printf "%.3f", sqrt(sum / (count - 1))
            else
                printf "0.000"
        }
    '


}

cv() {
local avg="$1"
local deviation="$2"


awk -v mean="$avg" -v sd="$deviation" '
    BEGIN {
        if (mean > 0)
            printf "%.2f%%", (sd / mean) * 100
        else
            printf "0.00%%"
    }
'


}

print_statistics() {
local name="$1"
shift


local avg
local med
local sd
local variation

avg="$(mean "$@")"
med="$(median "$@")"
sd="$(stddev "$avg" "$@")"
variation="$(cv "$avg" "$sd")"

printf "  %-10s " "$name"
printf "avg %ss  " "$avg"
printf "median %ss  " "$med"
printf "σ %ss  " "$sd"
printf "CV %s\n" "$variation"

printf "             rounds: "
printf '%ss  ' "$@"
printf "\n"


}

echo

echo "╔════════════════════════════════════════════════════╗"
echo "║              NIGHTBUILD TOURNAMENT                ║"
echo "╚════════════════════════════════════════════════════╝"

echo
echo "Rounds: $ROUNDS"
echo "Project: $ROOT"
echo
echo "Competitors:"
echo "  1. NightBuild"
echo "  2. Ninja"
echo "  3. samu"
echo
echo "Order: randomized every round"
echo "Cache: sudo purge before every build"
echo

for ((round=1; round<=ROUNDS; round++)); do


echo
echo "╔════════════════════════════════════════════════════╗"
printf "║                 ROUND %-2d                           ║\n" "$round"
echo "╚════════════════════════════════════════════════════╝"

shuffle_competitors

echo
printf "  Order: %s → %s → %s\n" \
    "${COMPETITORS[0]}" \
    "${COMPETITORS[1]}" \
    "${COMPETITORS[2]}"

for competitor in "${COMPETITORS[@]}"; do
    if ! run_competitor "$competitor"; then
        echo
        echo "Tournament aborted because $competitor failed."
        exit 1
    fi
done


done

NB_AVG="$(mean "${NIGHTBUILD_TIMES[@]}")"
NB_MEDIAN="$(median "${NIGHTBUILD_TIMES[@]}")"
NB_SD="$(stddev "$NB_AVG" "${NIGHTBUILD_TIMES[@]}")"
NB_CV="$(cv "$NB_AVG" "$NB_SD")"

NINJA_AVG="$(mean "${NINJA_TIMES[@]}")"
NINJA_MEDIAN="$(median "${NINJA_TIMES[@]}")"
NINJA_SD="$(stddev "$NINJA_AVG" "${NINJA_TIMES[@]}")"
NINJA_CV="$(cv "$NINJA_AVG" "$NINJA_SD")"

SAMU_AVG="$(mean "${SAMU_TIMES[@]}")"
SAMU_MEDIAN="$(median "${SAMU_TIMES[@]}")"
SAMU_SD="$(stddev "$SAMU_AVG" "${SAMU_TIMES[@]}")"
SAMU_CV="$(cv "$SAMU_AVG" "$SAMU_SD")"

echo
echo
echo "╔════════════════════════════════════════════════════╗"
echo "║                    RESULTS                         ║"
echo "╚════════════════════════════════════════════════════╝"

echo

print_statistics "NightBuild" "${NIGHTBUILD_TIMES[@]}"
echo
print_statistics "Ninja" "${NINJA_TIMES[@]}"
echo
print_statistics "samu" "${SAMU_TIMES[@]}"

echo
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

WINNER="$(
    printf '%s\n' \
        "NightBuild $NB_AVG" \
        "Ninja $NINJA_AVG" \
        "samu $SAMU_AVG" |
        sort -k2,2n |
        head -1 |
        cut -d' ' -f1
)"

BEST="$(
    printf '%s\n' \
        "$NB_AVG" \
        "$NINJA_AVG" \
        "$SAMU_AVG" |
        sort -n |
        head -1
)"

echo

echo "🏆 TOURNAMENT RESULT"

echo

echo "   Winner: $WINNER"

echo "   Average: ${BEST}s"


echo
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo
echo "Benchmark complete."
echo
