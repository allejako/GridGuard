#!/usr/bin/env bash
# profile_run.sh — Kör gprof-profilering av GridGuard
#
# Flöde:
#   1. Bygg med -pg (gprof instrumentering)
#   2. Kör bench_compute (genererar gmon.out)
#   3. Analysera med gprof → profile_report.txt
#   4. (Valfritt) Generera flamegraph om perf/stackcollapse finns
#
# Krav: make, gprof, gcc
# Användning: bash scripts/profile_run.sh [--flamegraph]

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
REPORT_DIR="$ROOT/docs/profiling"
TIMESTAMP=$(date +"%Y%m%d_%H%M%S")

FLAMEGRAPH=0
for arg in "$@"; do
    [[ "$arg" == "--flamegraph" ]] && FLAMEGRAPH=1
done

mkdir -p "$REPORT_DIR"

echo "============================================================"
echo "GridGuard Profiling Run — $TIMESTAMP"
echo "============================================================"

# ── 1. Bygg med -pg ───────────────────────────────────────────────────
echo ""
echo "[1/4] Bygger med gprof-instrumentering (-pg -O2)..."
cd "$ROOT"
make clean -s
make CFLAGS="-Wall -Wextra -Werror -std=c11 -pthread -g -pg -O2 -I src" \
     LDFLAGS="-pthread -lmbedtls -lmbedx509 -lmbedcrypto -lsqlite3 -lssl -lcrypto -lrt -pg" \
     bench-compute bench-queue bench-cache -s 2>&1

echo "    Bygget klart."

# ── 2. Kör bench_compute (genererar gmon.out) ─────────────────────────
echo ""
echo "[2/4] Kör bench_compute (genererar gmon.out)..."
cd "$ROOT"
bin/bench_compute > "$REPORT_DIR/bench_compute_${TIMESTAMP}.txt" 2>&1
cat "$REPORT_DIR/bench_compute_${TIMESTAMP}.txt"

if [ ! -f gmon.out ]; then
    echo "VARNING: gmon.out skapades inte (behöver -pg i hela länksteget)"
    echo "         Försöker köra server med -pg istället..."
else
    echo "    gmon.out skapad ($(du -h gmon.out | cut -f1))"
fi

# ── 3. gprof-analys ───────────────────────────────────────────────────
echo ""
echo "[3/4] Analyserar med gprof..."
GPROF_REPORT="$REPORT_DIR/gprof_${TIMESTAMP}.txt"

if [ -f gmon.out ]; then
    gprof bin/bench_compute gmon.out > "$GPROF_REPORT"
    echo "    Rapport: $GPROF_REPORT"
    echo ""
    echo "--- Topp 10 funktioner (platt profil) ---"
    gprof bin/bench_compute gmon.out --brief -p 2>/dev/null | head -20
else
    echo "    Hoppar över gprof (ingen gmon.out)"
fi

# Kör bench_queue och bench_cache också
for bench in bench_queue bench_cache; do
    echo ""
    echo "    Kör $bench..."
    cd "$ROOT"
    bin/$bench > "$REPORT_DIR/${bench}_${TIMESTAMP}.txt" 2>&1
    [ -f gmon.out ] && gprof bin/$bench gmon.out >> "$GPROF_REPORT" 2>/dev/null
done

# ── 4. perf stat (om tillgängligt) ───────────────────────────────────
echo ""
echo "[4/4] perf stat (om tillgängligt)..."
PERF_REPORT="$REPORT_DIR/perf_stat_${TIMESTAMP}.txt"

if command -v perf &>/dev/null; then
    perf stat -e cycles,instructions,cache-misses,cache-references,branch-misses \
        bin/bench_compute > /dev/null 2>> "$PERF_REPORT" || true
    echo "    perf stat sparat: $PERF_REPORT"
    cat "$PERF_REPORT"
else
    echo "    perf inte tillgängligt (installera linux-tools-$(uname -r) eller kör på Linux utan WSL-begränsningar)"
    echo ""
    echo "    Alternativ: kör Valgrind/callgrind:"
    echo "      make cachegrind"
fi

# ── Flamegraph (om --flamegraph och perf finns) ───────────────────────
if [[ $FLAMEGRAPH -eq 1 ]]; then
    echo ""
    echo "[Stretch] Genererar flamegraph..."
    FLAMEGRAPH_DIR="$ROOT/docs/profiling/flamegraph"
    mkdir -p "$FLAMEGRAPH_DIR"

    if command -v perf &>/dev/null && command -v stackcollapse-perf.pl &>/dev/null; then
        perf record -g --call-graph dwarf -o "$FLAMEGRAPH_DIR/perf.data" \
            bin/bench_compute > /dev/null 2>&1
        perf script -i "$FLAMEGRAPH_DIR/perf.data" | \
            stackcollapse-perf.pl | \
            flamegraph.pl > "$FLAMEGRAPH_DIR/flamegraph_${TIMESTAMP}.svg"
        echo "    Flamegraph: $FLAMEGRAPH_DIR/flamegraph_${TIMESTAMP}.svg"
    else
        echo "    Kräver: perf + FlameGraph-verktyg (https://github.com/brendangregg/FlameGraph)"
        echo "    Installera: git clone https://github.com/brendangregg/FlameGraph ~/FlameGraph"
        echo "                export PATH=\$PATH:~/FlameGraph"
    fi
fi

# ── Sammanfattning ────────────────────────────────────────────────────
echo ""
echo "============================================================"
echo "Profileringsresultat sparade i: $REPORT_DIR/"
ls -lh "$REPORT_DIR/"
echo "============================================================"
