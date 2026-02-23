#!/usr/bin/env bash
# test_e2e.sh — End-to-end integration test for GridGuard server.
#
# Testar hela systemet:
#   1. Bygger servern
#   2. Seedar databasen med mockdata via sqlite3
#   3. Startar servern
#   4. GET  /health
#   5. GET  /user/config  (förväntas 404 för okänd användare)
#   6. PUT  /user/config  (sparar konfiguration)
#   7. GET  /user/config  (verifierar att data sparades)
#   8. GET  /forecast     (testar hela pipeline: fetch → parse → compute)
#   9. Stoppar servern och rapporterar resultat
#
# Kräver: sqlite3, curl, python3 (stdlib)
# Kör från projektets rot: bash scripts/test_e2e.sh

set -euo pipefail

# ---------------------------------------------------------------------------
# Konfiguration
# ---------------------------------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
SERVER_BIN="$PROJECT_ROOT/bin/GridGuard-server"
# Servern öppnar alltid "gridguard.db" relativt sin CWD (PROJECT_ROOT).
DB_FILE="$PROJECT_ROOT/gridguard.db"
SERVER_PORT="8080"
SERVER_HOST="localhost"
JWT_SECRET="gridguard-test-secret"
# TOKEN_VALID från test_jwt_validator.c — sub=test_user, exp=2030-01-01
TOKEN="eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJzdWIiOiJ0ZXN0X3VzZXIiLCJleHAiOjE4OTM0NTYwMDB9.d33GazykNsOCuOyy545_484DACV1vEd3owJr-dvL-1c"
BASE_URL="http://$SERVER_HOST:$SERVER_PORT"
SERVER_PID=""

# ---------------------------------------------------------------------------
# Testramverk
# ---------------------------------------------------------------------------
TESTS_RUN=0
TESTS_PASSED=0
TESTS_FAILED=0

pass() { echo "  [PASS] $1"; TESTS_RUN=$((TESTS_RUN+1)); TESTS_PASSED=$((TESTS_PASSED+1)); }
fail() { echo "  [FAIL] $1"; TESTS_RUN=$((TESTS_RUN+1)); TESTS_FAILED=$((TESTS_FAILED+1)); }

assert_status() {
    local desc="$1" expected="$2" actual="$3"
    if [ "$actual" -eq "$expected" ]; then
        pass "$desc (HTTP $actual)"
    else
        fail "$desc (förväntade HTTP $expected, fick HTTP $actual)"
    fi
}

assert_contains() {
    local desc="$1" needle="$2" haystack="$3"
    if echo "$haystack" | grep -q "$needle"; then
        pass "$desc"
    else
        fail "$desc (hittade inte '$needle' i svaret)"
    fi
}

# ---------------------------------------------------------------------------
# Städning vid exit
# ---------------------------------------------------------------------------
cleanup() {
    if [ -n "$SERVER_PID" ] && kill -0 "$SERVER_PID" 2>/dev/null; then
        echo ""
        echo "Stoppar server (PID $SERVER_PID)..."
        kill "$SERVER_PID"
        wait "$SERVER_PID" 2>/dev/null || true
    fi
    # Ta bort testdatabasen men behåll eventuell riktig gridguard.db om den fanns före testet
    if [ "${DB_WAS_PRE_EXISTING:-0}" = "0" ]; then
        rm -f "$DB_FILE"
    fi
}
trap cleanup EXIT

# ---------------------------------------------------------------------------
# 0. Kontrollera beroenden
# ---------------------------------------------------------------------------
echo "=============================================="
echo "  GridGuard — End-to-End Integration Test"
echo "=============================================="
echo ""

for dep in sqlite3 curl python3; do
    if ! command -v "$dep" &>/dev/null; then
        echo "Fel: '$dep' saknas. Installera det och försök igen."
        exit 1
    fi
done

# ---------------------------------------------------------------------------
# 1. Bygg servern
# ---------------------------------------------------------------------------
echo "--- Bygger servern ---"
cd "$PROJECT_ROOT"
make server 2>&1 | tail -3
echo ""

# ---------------------------------------------------------------------------
# 2. Seed databasen med mockdata
# ---------------------------------------------------------------------------
echo "--- Seedar databas: $DB_FILE ---"
[ -f "$DB_FILE" ] && DB_WAS_PRE_EXISTING=1 || DB_WAS_PRE_EXISTING=0
rm -f "$DB_FILE"
sqlite3 "$DB_FILE" <<'SQL'
CREATE TABLE IF NOT EXISTS user_configs (
    user_id          TEXT PRIMARY KEY,
    latitude         REAL NOT NULL,
    longitude        REAL NOT NULL,
    region           TEXT NOT NULL,
    solar_area_m2    REAL NOT NULL,
    solar_efficiency REAL NOT NULL,
    consumption_kwh  REAL NOT NULL DEFAULT 0.5,
    updated_at       INTEGER NOT NULL
);

-- Göteborg, SE3, 20m² solpaneler @ 18%, förbrukning 0.5 kWh/h
INSERT INTO user_configs VALUES (
    'test_user',
    57.7089,
    11.9746,
    'SE3',
    20.0,
    0.18,
    0.5,
    strftime('%s', 'now')
);

-- Extra användare: Stockholm, SE3
INSERT INTO user_configs VALUES (
    'test_user_sthlm',
    59.3293,
    18.0686,
    'SE3',
    15.0,
    0.20,
    0.6,
    strftime('%s', 'now')
);
SQL
echo "Seedade 2 användare."
echo ""

# ---------------------------------------------------------------------------
# 3. Starta servern
# ---------------------------------------------------------------------------
echo "--- Startar servern ---"
# Servern måste starta från PROJECT_ROOT så att den hittar gridguard.db och logs/
cd "$PROJECT_ROOT"
mkdir -p logs
GRIDGUARD_JWT_SECRET="$JWT_SECRET" "$SERVER_BIN" 2>/dev/null &
SERVER_PID=$!

# Vänta tills servern svarar (max 5 sek)
for i in $(seq 1 10); do
    if curl -sf "$BASE_URL/health" &>/dev/null; then
        break
    fi
    sleep 0.5
done

if ! curl -sf "$BASE_URL/health" &>/dev/null; then
    echo "Fel: Servern svarade inte inom 5 sekunder."
    exit 1
fi
echo "Server igång (PID $SERVER_PID)"
echo ""

# ---------------------------------------------------------------------------
# 4. GET /health — public endpoint
# ---------------------------------------------------------------------------
echo "--- GET /health ---"
RESP=$(curl -s -w "\n%{http_code}" "$BASE_URL/health")
BODY=$(echo "$RESP" | head -n -1)
CODE=$(echo "$RESP" | tail -n 1)
assert_status "GET /health returnerar 200" 200 "$CODE"
assert_contains "Svar innehåller \"status\":\"ok\"" '"status":"ok"' "$BODY"

# ---------------------------------------------------------------------------
# 5. GET /forecast — utan token (förväntas 401)
# ---------------------------------------------------------------------------
echo ""
echo "--- GET /forecast utan token ---"
CODE=$(curl -s -o /dev/null -w "%{http_code}" "$BASE_URL/forecast")
assert_status "GET /forecast utan token returnerar 401" 401 "$CODE"

# ---------------------------------------------------------------------------
# 6. GET /user/config för okänd användare (förväntas 404)
# ---------------------------------------------------------------------------
echo ""
echo "--- GET /user/config (okänd användare) ---"
# Generera token för en användare som INTE finns i DB
UNKNOWN_TOKEN=$(python3 - <<'PYEOF'
import hmac, hashlib, base64, json, time

def b64url(data):
    return base64.urlsafe_b64encode(data).rstrip(b'=').decode()

header  = b64url(json.dumps({"alg":"HS256","typ":"JWT"}, separators=(',',':')).encode())
payload = b64url(json.dumps({"sub":"unknown_user","exp":int(time.time())+3600}, separators=(',',':')).encode())
msg     = f"{header}.{payload}".encode()
sig     = b64url(hmac.new(b"gridguard-test-secret", msg, hashlib.sha256).digest())
print(f"{header}.{payload}.{sig}")
PYEOF
)
CODE=$(curl -s -o /dev/null -w "%{http_code}" \
    -H "Authorization: Bearer $UNKNOWN_TOKEN" \
    "$BASE_URL/user/config")
assert_status "GET /user/config för okänd användare returnerar 404" 404 "$CODE"

# ---------------------------------------------------------------------------
# 7. GET /user/config — hämtar seedat data
# ---------------------------------------------------------------------------
echo ""
echo "--- GET /user/config (seedat test_user) ---"
RESP=$(curl -s -w "\n%{http_code}" \
    -H "Authorization: Bearer $TOKEN" \
    "$BASE_URL/user/config")
BODY=$(echo "$RESP" | head -n -1)
CODE=$(echo "$RESP" | tail -n 1)
assert_status "GET /user/config returnerar 200" 200 "$CODE"
assert_contains "Svar innehåller latitude"         '"latitude"'         "$BODY"
assert_contains "Svar innehåller region SE3"       '"region":"SE3"'     "$BODY"
assert_contains "Svar innehåller solar_area_m2"    '"solar_area_m2"'    "$BODY"
assert_contains "Svar innehåller consumption_kwh"  '"consumption_kwh"'  "$BODY"
echo "  Svar: $BODY"

# ---------------------------------------------------------------------------
# 8. PUT /user/config — ändra konfiguration via HTTP
# ---------------------------------------------------------------------------
echo ""
echo "--- PUT /user/config ---"
PUT_BODY='{"latitude":57.7089,"longitude":11.9746,"region":"SE3","solar_area_m2":25.0,"solar_efficiency":0.20,"consumption_kwh":0.6}'
RESP=$(curl -s -w "\n%{http_code}" \
    -X PUT \
    -H "Authorization: Bearer $TOKEN" \
    -H "Content-Type: application/json" \
    -d "$PUT_BODY" \
    "$BASE_URL/user/config")
BODY=$(echo "$RESP" | head -n -1)
CODE=$(echo "$RESP" | tail -n 1)
assert_status "PUT /user/config returnerar 200" 200 "$CODE"
assert_contains "Svar innehåller \"status\":\"ok\"" '"status":"ok"' "$BODY"

# Verifiera att ändringen sparades
RESP=$(curl -s \
    -H "Authorization: Bearer $TOKEN" \
    "$BASE_URL/user/config")
assert_contains "solar_area_m2 uppdaterades till 25" '"solar_area_m2":25' "$RESP"
assert_contains "solar_efficiency uppdaterades till 0.20" '"solar_efficiency":0.200' "$RESP"

# ---------------------------------------------------------------------------
# 9. GET /forecast — testar hela pipeline
# ---------------------------------------------------------------------------
echo ""
echo "--- GET /forecast (testar Fetch → Parse → Compute) ---"
echo "  (Gör riktiga API-anrop, kan ta upp till 30 sek...)"
RESP=$(curl -s -w "\n%{http_code}" \
    --max-time 60 \
    -H "Authorization: Bearer $TOKEN" \
    "$BASE_URL/forecast")
BODY=$(echo "$RESP" | head -n -1)
CODE=$(echo "$RESP" | tail -n 1)
assert_status "GET /forecast returnerar 200" 200 "$CODE"
assert_contains "Svar innehåller location"      '"location"'      "$BODY"
assert_contains "Svar innehåller region"        '"region"'        "$BODY"
assert_contains "Svar innehåller summary"       '"summary"'       "$BODY"
assert_contains "Svar innehåller forecast-array" '"forecast":'    "$BODY"
assert_contains "Svar innehåller signal-fält"   '"signal"'        "$BODY"

# Kontrollera att signalen är ett av BUY/SELL/IDLE
if echo "$BODY" | grep -qE '"signal":"(BUY|SELL|IDLE)"'; then
    pass "Forecast-poster har giltig signal (BUY/SELL/IDLE)"
else
    fail "Forecast-poster saknar giltig signal"
fi

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
