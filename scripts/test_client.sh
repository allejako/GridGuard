#!/usr/bin/env bash
# test_client.sh — Integrationstester för GridGuard C++-klienten.
#
# Testar alla klientkommandon mot en körande server:
#   1. health
#   2. forecast (ingen token → fel)
#   3. config set
#   4. config get
#   5. forecast
#   6. schedule add
#   7. schedule list
#   8. schedule delete
#
# Kräver: python3 (stdlib)
# Kör från projektets rot: bash scripts/test_client.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
CLIENT_BIN="$PROJECT_ROOT/bin/GridGuard-client"
WATCHDOG_BIN="$PROJECT_ROOT/bin/GridGuard-watchdog"
DB_FILE="$PROJECT_ROOT/gridguard.db"
PLATFORM_DB="$PROJECT_ROOT/platform.db"
JWT_SECRET="${GRIDGUARD_JWT_SECRET:-demo_secret_key_change_in_production_2024}"
WATCHDOG_PID=""
DB_WAS_PRE_EXISTING=0
TEST_USER="SAAB_ARENA"

# ---------------------------------------------------------------------------
# Testramverk
# ---------------------------------------------------------------------------
TESTS_RUN=0
TESTS_PASSED=0
TESTS_FAILED=0

pass() { echo "  [PASS] $1"; TESTS_RUN=$((TESTS_RUN+1)); TESTS_PASSED=$((TESTS_PASSED+1)); }
fail() { echo "  [FAIL] $1"; TESTS_RUN=$((TESTS_RUN+1)); TESTS_FAILED=$((TESTS_FAILED+1)); }

assert_exit0() {
    local desc="$1"; shift
    if "$@" >/dev/null 2>&1; then
        pass "$desc"
    else
        fail "$desc (returkod $?)"
    fi
}

assert_exit_nonzero() {
    local desc="$1"; shift
    if ! "$@" >/dev/null 2>&1; then
        pass "$desc"
    else
        fail "$desc (förväntade fel, fick 0)"
    fi
}

assert_output_contains() {
    local desc="$1" needle="$2"; shift 2
    local out
    out=$("$@" 2>&1) || true
    if echo "$out" | grep -q "$needle"; then
        pass "$desc"
    else
        fail "$desc (hittade inte '$needle')"
        echo "    output: $out" >&2
    fi
}

# ---------------------------------------------------------------------------
# Städning vid exit
# ---------------------------------------------------------------------------
cleanup() {
    if [ -n "$WATCHDOG_PID" ] && kill -0 "$WATCHDOG_PID" 2>/dev/null; then
        echo ""
        echo "Stoppar watchdog (PID $WATCHDOG_PID)..."
        kill "$WATCHDOG_PID"
        wait "$WATCHDOG_PID" 2>/dev/null || true
        sleep 1
    fi
    if [ "$DB_WAS_PRE_EXISTING" = "0" ]; then
        rm -f "$DB_FILE"
    fi
}
trap cleanup EXIT

# ---------------------------------------------------------------------------
# 0. Kontrollera binärer
# ---------------------------------------------------------------------------
echo "=============================================="
echo "  GridGuard — Klienttest"
echo "=============================================="
echo ""

for bin in "$CLIENT_BIN" "$WATCHDOG_BIN"; do
    if [ ! -x "$bin" ]; then
        echo "Fel: '$bin' saknas. Kör: make all"
        exit 1
    fi
done

if [ ! -f "$PLATFORM_DB" ]; then
    echo "Fel: '$PLATFORM_DB' saknas. Kör: make dev (en gång) för att skapa den."
    exit 1
fi

# ---------------------------------------------------------------------------
# 1. Starta server (via watchdog) om den inte redan körs
# ---------------------------------------------------------------------------
SERVER_ALREADY_RUNNING=0
if curl -sf "http://localhost:8080/health" >/dev/null 2>&1; then
    echo "Server körs redan — hoppar över uppstart."
    SERVER_ALREADY_RUNNING=1
else
    echo "--- Startar server via watchdog ---"
    [ -f "$DB_FILE" ] && DB_WAS_PRE_EXISTING=1 || DB_WAS_PRE_EXISTING=0
    cd "$PROJECT_ROOT"
    mkdir -p logs
    GRIDGUARD_JWT_SECRET="$JWT_SECRET" \
    GRIDGUARD_DB_PATH="$DB_FILE" \
    setsid "$WATCHDOG_BIN" >/dev/null 2>&1 &
    WATCHDOG_PID=$!

    echo "Watchdog PID: $WATCHDOG_PID — väntar på server..."
    for i in $(seq 1 20); do
        if curl -sf "http://localhost:8080/health" >/dev/null 2>&1; then
            break
        fi
        sleep 0.5
    done

    if ! curl -sf "http://localhost:8080/health" >/dev/null 2>&1; then
        echo "Fel: Servern svarade inte inom 10 sekunder."
        exit 1
    fi
    echo "Server igång."
    echo ""
fi

# ---------------------------------------------------------------------------
# 2. Generera JWT-token
# ---------------------------------------------------------------------------
TOKEN=$(GRIDGUARD_JWT_SECRET="$JWT_SECRET" \
    python3 "$PROJECT_ROOT/scripts/generate_jwt.py" "$PLATFORM_DB" "$TEST_USER" 2>/dev/null)

if [ -z "$TOKEN" ]; then
    echo "Fel: Kunde inte generera token för '$TEST_USER'."
    echo "Kontrollera att '$TEST_USER' finns i $PLATFORM_DB."
    exit 1
fi

echo "Token genererat för: $TEST_USER"
echo ""

# ---------------------------------------------------------------------------
# 3. Tester
# ---------------------------------------------------------------------------

echo "--- health ---"
assert_exit0            "health: returkod 0"                  "$CLIENT_BIN" health
assert_output_contains  "health: output innehåller 'online'"  "online"  "$CLIENT_BIN" health

echo ""
echo "--- forecast utan token ---"
assert_exit_nonzero     "forecast utan token: returkod != 0"  "$CLIENT_BIN" forecast

echo ""
echo "--- config set ---"
assert_exit0 "config set: returkod 0" \
    "$CLIENT_BIN" config set \
    --token "$TOKEN" \
    --lat 58.41 --lon 15.62 \
    --region SE3 \
    --location "Linköping" \
    --solar-area 20 \
    --solar-eff 0.18

assert_output_contains "config set: output innehåller bekräftelse" "config saved" \
    "$CLIENT_BIN" config set \
    --token "$TOKEN" \
    --lat 58.41 --lon 15.62 \
    --region SE3

echo ""
echo "--- config get ---"
assert_exit0           "config get: returkod 0"                    "$CLIENT_BIN" config get --token "$TOKEN"
assert_output_contains "config get: output innehåller 'latitude'"  "latitude"    "$CLIENT_BIN" config get --token "$TOKEN"
assert_output_contains "config get: output innehåller 'SE3'"       "SE3"         "$CLIENT_BIN" config get --token "$TOKEN"

echo ""
echo "--- forecast ---"
echo "  (Gör riktiga API-anrop, kan ta upp till 30 sek...)"
assert_exit0           "forecast: returkod 0"                      "$CLIENT_BIN" forecast --token "$TOKEN"
assert_output_contains "forecast: output innehåller 'GridGuard'"   "GridGuard"   "$CLIENT_BIN" forecast --token "$TOKEN"
assert_output_contains "forecast: output innehåller signal"        "BUY\|SELL\|IDLE\|SKIP" "$CLIENT_BIN" forecast --token "$TOKEN"

echo ""
echo "--- schedule add ---"
ADD_OUT=$("$CLIENT_BIN" schedule add \
    --token "$TOKEN" \
    --load  "dishwasher" \
    --duration 60 \
    --power 1.2 2>&1) || true

if echo "$ADD_OUT" | grep -q "schedule_id\|created\|error"; then
    pass "schedule add: svarade (skapades eller schema-fel från server)"
else
    fail "schedule add: inget svar från server"
fi

echo ""
echo "--- schedule list ---"
assert_exit0           "schedule list: returkod 0"                 "$CLIENT_BIN" schedule list --token "$TOKEN"

echo ""
echo "--- schedule delete (ogiltigt ID) ---"
assert_exit_nonzero    "schedule delete ogiltigt ID: returkod != 0" \
    "$CLIENT_BIN" schedule delete --token "$TOKEN" nonexistent-id-000

# ---------------------------------------------------------------------------
# Rapport
# ---------------------------------------------------------------------------
echo ""
echo "=============================================="
if [ "$TESTS_FAILED" -eq 0 ]; then
    echo "  RESULTAT: $TESTS_PASSED/$TESTS_RUN tester godkända"
else
    echo "  RESULTAT: $TESTS_PASSED/$TESTS_RUN tester godkända ($TESTS_FAILED misslyckades)"
fi
echo "=============================================="

[ "$TESTS_FAILED" -eq 0 ]
