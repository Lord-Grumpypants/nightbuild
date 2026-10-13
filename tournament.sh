#!/bin/bash
set -uo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
ROUNDS="${1:-15}"
NIGHTBUILD="$ROOT/nightbuild"
NIGHTBUILD_BUILD="$ROOT/build"
NINJA_BUILD="$ROOT/ninja-build"
SAMU_BUILD="$ROOT/samu-build"

NINJA_BIN="$NINJA_BUILD/nightbuild"
SAMU_BIN="$SAMU_BUILD/nightbuild"

declare -a NIGHTBUILD_TIMES=()
declare -a NINJA_TIMES=()
declare -a SAMU_TIMES=()

FAILURES=0
RUN_TIME=""

if ! [[ "$ROUNDS" =~ ^[1-9][0-9]*$ ]]; then
    printf 'Usage: %s [positive-round-count]\n' "$0" >&2
    exit 2
fi

for tool in "$NIGHTBUILD" ninja samu python3; do
    if [[ "$tool" == */* ]]; then
        [[ -x "$tool" ]] || {
            printf 'Required executable not found: %s\n' "$tool" >&2
            exit 1
        }
    elif ! command -v "$tool" >/dev/null 2>&1; then
        printf 'Required tool not found: %s\n' "$tool" >&2
        exit 1
    fi
done

printf 'Requesting sudo access for cache purging...\n'
sudo -v || exit 1

cold_start() {
    if sudo purge >/dev/null 2>&1; then
        return 0
    fi

    printf 'WARNING: macOS cache purge failed; this run may not be fully cold.\n' >&2
    return 1
}

run_timed() {
    local tmp status real

    tmp="$(mktemp)" || {
        RUN_TIME="FAILED"
        return 1
    }

    printf '    '
    /usr/bin/time -p "$@" 2>"$tmp"
    status=$?

    if (( status != 0 )); then
        cat "$tmp" >&2
        rm -f "$tmp"
        RUN_TIME="FAILED"
        return 1
    fi

    real="$(awk '$1 == "real" { print $2; exit }' "$tmp")"
    rm -f "$tmp"

    if [[ -z "$real" ]]; then
        printf 'Could not read elapsed time.\n' >&2
        RUN_TIME="FAILED"
        return 1
    fi

    RUN_TIME="$real"
    printf 'real %ss\n' "$real"
    return 0
}

verify_new_output() {
    local path="$1"
    local started="$2"

    if [[ ! -f "$path" || ! -x "$path" ]]; then
        printf 'ERROR: expected executable missing or not executable: %s\n' "$path" >&2
        return 1
    fi

    local mtime
    mtime="$(stat -f '%m' "$path" 2>/dev/null)" || {
        printf 'ERROR: cannot inspect output modification time: %s\n' "$path" >&2
        return 1
    }

    if (( mtime < started )); then
        printf 'ERROR: output predates this build: %s\n' "$path" >&2
        return 1
    fi

    return 0
}
run_nightbuild() {
    local started before_mtime after_mtime

    printf '  NightBuild: '

    if [[ ! -x "$NIGHTBUILD" ]]; then
        printf 'FAILED: NightBuild executable missing: %s\n' "$NIGHTBUILD"
        NIGHTBUILD_TIMES+=(FAILED)
        ((FAILURES += 1))
        return
    fi

    rm -rf "$NIGHTBUILD_BUILD" || {
        printf 'FAILED to clean build directory\n'
        NIGHTBUILD_TIMES+=(FAILED)
        ((FAILURES += 1))
        return
    }

    if ! "$NIGHTBUILD" gen -C "$NIGHTBUILD_BUILD" >/dev/null; then
        printf 'FAILED during generation\n'
        NIGHTBUILD_TIMES+=(FAILED)
        ((FAILURES += 1))
        return
    fi

    before_mtime="$(stat -f '%m' "$NIGHTBUILD")" || {
        printf 'FAILED to inspect NightBuild executable\n'
        NIGHTBUILD_TIMES+=(FAILED)
        ((FAILURES += 1))
        return
    }

    cold_start || true
    started="$(date +%s)"

    if ! run_timed "$NIGHTBUILD" build -C "$NIGHTBUILD_BUILD" nightbuild; then
        printf '    NightBuild build command failed\n' >&2
        NIGHTBUILD_TIMES+=(FAILED)
        ((FAILURES += 1))
        return
    fi

    if [[ ! -x "$NIGHTBUILD" ]]; then
        printf 'ERROR: NightBuild executable missing after build\n' >&2
        NIGHTBUILD_TIMES+=(FAILED)
        ((FAILURES += 1))
        return
    fi

    after_mtime="$(stat -f '%m' "$NIGHTBUILD")" || {
        printf 'ERROR: cannot inspect NightBuild executable after build\n' >&2
        NIGHTBUILD_TIMES+=(FAILED)
        ((FAILURES += 1))
        return
    }

    if [[ "$after_mtime" -le "$before_mtime" ]]; then
        printf 'ERROR: NightBuild executable modification time did not advance\n' >&2
        NIGHTBUILD_TIMES+=(FAILED)
        ((FAILURES += 1))
        return
    fi

    NIGHTBUILD_TIMES+=("$RUN_TIME")
}

run_ninja() {
    local started
    printf '  Ninja: '

    rm -rf "$NINJA_BUILD/obj" \
           "$NINJA_BIN" \
           "$NINJA_BUILD/.ninja_log" \
           "$NINJA_BUILD/.ninja_deps" || {
        printf 'FAILED to clean outputs\n'
        NINJA_TIMES+=(FAILED)
        ((FAILURES += 1))
        return
    }

    cold_start || true
    started="$(date +%s)"

    if ! run_timed ninja -C "$NINJA_BUILD"; then
        NINJA_TIMES+=(FAILED)
        ((FAILURES += 1))
        return
    fi

    if ! verify_new_output "$NINJA_BIN" "$started"; then
        NINJA_TIMES+=(FAILED)
        ((FAILURES += 1))
        return
    fi

    NINJA_TIMES+=("$RUN_TIME")
}

run_samu() {
    local started
    printf '  samu: '

    rm -rf "$SAMU_BUILD/obj" \
           "$SAMU_BIN" \
           "$SAMU_BUILD/.ninja_log" \
           "$SAMU_BUILD/.ninja_deps" || {
        printf 'FAILED to clean outputs\n'
        SAMU_TIMES+=(FAILED)
        ((FAILURES += 1))
        return
    }

    cold_start || true
    started="$(date +%s)"

    if ! run_timed samu -C "$SAMU_BUILD" -j "$(sysctl -n hw.logicalcpu)"; then
        SAMU_TIMES+=(FAILED)
        ((FAILURES += 1))
        return
    fi

    if ! verify_new_output "$SAMU_BIN" "$started"; then
        SAMU_TIMES+=(FAILED)
        ((FAILURES += 1))
        return
    fi

    SAMU_TIMES+=("$RUN_TIME")
}

shuffle_competitors() {
    local i j tmp
    SHUFFLED=("NightBuild" "Ninja" "samu")

    for ((i=${#SHUFFLED[@]}-1; i>0; i--)); do
        j=$((RANDOM % (i + 1)))
        tmp="${SHUFFLED[i]}"
        SHUFFLED[i]="${SHUFFLED[j]}"
        SHUFFLED[j]="$tmp"
    done
}

print_stats() {
    local name="$1"
    shift
    local -a values=("$@")
    local valid=()
    local value

    for value in "${values[@]}"; do
        [[ "$value" == "FAILED" ]] || valid+=("$value")
    done

    if (( ${#valid[@]} == 0 )); then
        printf '%-10s no successful runs (%d failed)\n' "$name" "${#values[@]}"
        return
    fi

    printf '%s\n' "${valid[@]}" | python3 -c '
import statistics
import sys

v = sorted(float(line) for line in sys.stdin if line.strip())
mean = statistics.mean(v)
median = statistics.median(v)
sd = statistics.stdev(v) if len(v) > 1 else 0.0
cv = 100.0 * sd / mean if mean else 0.0

print(f"  avg {mean:.3f}s  median {median:.3f}s  σ {sd:.3f}s  CV {cv:.2f}%  n={len(v)}")
'
    local failed=$(( ${#values[@]} - ${#valid[@]} ))
    if (( failed > 0 )); then
        printf '  %d failed run(s)\n' "$failed"
    fi
}

declare -a SHUFFLED

printf '\nCold-build tournament · %d rounds\n' "$ROUNDS"
printf 'NightBuild build execution is timed after graph generation.\n'
printf 'Ninja and samu use their existing generated build graphs.\n\n'

for ((round=1; round<=ROUNDS; round++)); do
    printf 'Round %d/%d\n' "$round" "$ROUNDS"
    shuffle_competitors

    for competitor in "${SHUFFLED[@]}"; do
        case "$competitor" in
            NightBuild) run_nightbuild ;;
            Ninja) run_ninja ;;
            samu) run_samu ;;
        esac
    done

    printf '\n'
done

printf '\nResults\n'
printf 'NightBuild'
print_stats "NightBuild" "${NIGHTBUILD_TIMES[@]}"
printf 'Ninja'
print_stats "Ninja" "${NINJA_TIMES[@]}"
printf 'samu'
print_stats "samu" "${SAMU_TIMES[@]}"

printf '\n'
if (( FAILURES > 0 )); then
    printf 'Tournament finished with %d failed run(s). No winner declared.\n' "$FAILURES"
    exit 1
fi

python3 - "${NIGHTBUILD_TIMES[@]}" -- "${NINJA_TIMES[@]}" -- "${SAMU_TIMES[@]}" <<'PY'
import statistics
import sys

groups = {"NightBuild": [], "Ninja": [], "samu": []}
current = "NightBuild"

for item in sys.argv[1:]:
    if item == "--":
        current = {"NightBuild": "Ninja", "Ninja": "samu"}.get(current, "samu")
    else:
        groups[current].append(float(item))

means = {name: statistics.mean(times) for name, times in groups.items() if times}
if len(means) != 3:
    print("Not all tools have successful results; no winner declared.")
    raise SystemExit(1)

ranked = sorted(means.items(), key=lambda item: item[1])
print("Mean-time ranking:")
for rank, (name, mean) in enumerate(ranked, 1):
    print(f"  {rank}. {name}: {mean:.3f}s")

gap = ranked[1][1] - ranked[0][1]
print(f"\nFastest mean: {ranked[0][0]} ({ranked[0][1]:.3f}s)")
print(f"Gap to second place: {gap:.3f}s")
print("Treat small gaps cautiously; report medians and variability too.")
PY