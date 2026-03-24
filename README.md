# GridGuard

[![CI](https://github.com/allejako/GridGuard/actions/workflows/ci.yml/badge.svg?branch=development)](https://github.com/allejako/GridGuard/actions/workflows/ci.yml)

Smart energioptimering för svenska hushåll. Systemet hämtar väderdata och spotpriser i realtid, beräknar förväntad solcellsproduktion och genererar en tidsbaserad energiplan som visar när det är billigast att köpa, sälja eller undvika el.

## Arkitektur

```
bin/GridGuard  (launcher)
└── GridGuard-watchdog  (supervisor — hanterar krasch och omstart)
    ├── GridGuard-fetcher  (hämtar väder + spotpris från externa API:er)
    ├── GridGuard-parser   (validerar och strukturerar rådata)
    └── GridGuard-server   (HTTP API + energiberäkningar)
```

Processer kommunicerar via namngivna pipes och Unix domain sockets. Cachen delas via POSIX shared memory med `pthread_rwlock` för trådsäker åtkomst.

## Beroenden

```bash
# Ubuntu/Debian
sudo apt-get install libmbedtls-dev libsqlite3-dev libssl-dev python3

# Fedora/RHEL
sudo dnf install mbedtls-devel sqlite-devel openssl-devel python3
```

## Snabbstart (Development)

```bash
make dev
```

Kommandot bygger allt, seedar databaser, startar systemet och skriver ut ett JWT-token redo att använda.

## Production Installation

### Snabbinstallation

```bash
# 1. Bygg release-version
make release

# 2. Installera till system (default: /opt/gridguard)
sudo make install-systemd

# 3. Konfigurera JWT-hemlighet
echo 'GRIDGUARD_JWT_SECRET="your-secret-key-here"' | sudo tee /etc/default/gridguard

# 4. Starta service
sudo systemctl start gridguard
sudo systemctl enable gridguard  # Autostart vid boot

# 5. Verifiera
curl http://localhost:8080/health
```

### Installation utan systemd

Om du vill installera manuellt utan systemd-integration:

```bash
sudo make install PREFIX=/opt/gridguard
```

Detta kopierar binaries, scripts och config-exempel till `/opt/gridguard`. Du kan sedan köra watchdog manuellt eller med ditt eget init-system.

### Systemd-hantering

```bash
# Status
sudo systemctl status gridguard

# Loggar
sudo journalctl -u gridguard -f

# Omstart
sudo systemctl restart gridguard

# Stoppa
sudo systemctl stop gridguard

# Avinstallera
sudo systemctl stop gridguard
sudo systemctl disable gridguard
sudo make uninstall
```

### Stabilitetstestning

Innan produktionsdeploy, kör soak test för att verifiera stabilitet och minnesanvändning:

```bash
# 24-timmars test (rekommenderat före kundleverans)
make soak-test

# Snabb 1-timmars validering
make soak-test-short
```

Rapport genereras till: `logs/soak_test/soak_test_report.txt`

**Pass-kriterier:**
- 0 krascher
- <3 omstarter
- <10% minnestillväxt
- Inga heartbeat-failures

## Development - Köra lokalt

```bash
# 1. Bygg och starta allt
make dev

# 2. Öppna dashboard i separat terminal
make client

# 3. Stoppa systemet
make stop
```

`make dev` seedar databaser, genererar JWT-token och startar watchdog med alla processer.

## Konfiguration

GridGuard löser konfigurationsvärden i tre steg:

1. **Config-fil** (`config/gridguard.conf`) — INI-format, ignorerad av git
2. **Miljövariabler** — överskrider config-filen
3. **Compile-time defaults** — fallback om inget annat anges

### Kom igång

```bash
cp config/gridguard.conf.example config/gridguard.conf
```

Filen är förkommenterad — de flesta defaults fungerar direkt. Justera `db_path` om du vill lagra databasen utanför projektkatalogen.

Custom config-sökväg:
```bash
bin/GridGuard-watchdog --config /path/to/gridguard.conf
```

Hot-reload utan omstart:
```bash
kill -SIGHUP $(cat /var/run/gridguard.pid)
```

### Konfigurerbara nycklar

| Nyckel | Miljövariabel | Default | Beskrivning |
|---|---|---|---|
| `server.port` | — | `8080` | HTTP-port |
| `database.db_path` | `GRIDGUARD_DB_PATH` | auto | Sökväg till gridguard.db |
| `cache.weather_ttl` | — | `900` | Väder-cache i sekunder (15 min) |
| `cache.price_ttl` | — | `43200` | Priscache i sekunder (12 h) |
| `cache.forecast_ttl` | — | `1800` | Forecast-cache i sekunder (30 min) |
| `network.timeout` | — | `30` | HTTP-timeout i sekunder |
| `network.max_retries` | — | `3` | Antal återförsök vid HTTP-fel |

### JWT

JWT-hemligheten hanteras **enbart via miljövariabel** — den lagras aldrig i config-filen.

```bash
export GRIDGUARD_JWT_SECRET="your-secret"
```

Hemligheten delas med plattformen som utfärdar tokens. Validering sker i `JWTValidator` via mbedTLS HS256.

Se `docs/CONFIG_DESIGN.md` för fullständig dokumentation.

## API

Servern lyssnar på port `8080`.

### Publika endpoints

```
GET  /        Välkomstsida med API-dokumentation
GET  /health  Hälsokontroll
GET  /metrics Processtatistik (watchdog, heartbeats, uptime)
```

### Autentiserade endpoints (JWT krävs)

```
GET    /forecast          96-timmars energiprognos med BUY/SELL/AVOID/IDLE-signaler
GET    /user/config       Hämta konfiguration
PUT    /user/config       Spara konfiguration (koordinater, solpaneler, elavgifter)
GET    /schedule          Lista schemalagda laster
POST   /schedule          Schemalägg en last till billigaste tidsfönster
DELETE /schedule/:id      Avboka ett schema
```

**Exempel — sätt konfiguration:**
```bash
curl -X PUT http://localhost:8080/user/config \
  -H "Authorization: Bearer $TOKEN" \
  -H "Content-Type: application/json" \
  -d '{
    "latitude": 59.3293,
    "longitude": 18.0686,
    "region": "SE3",
    "location": "Stockholm",
    "solar_area_m2": 20.0,
    "solar_efficiency": 0.18,
    "consumption_kwh": 1.5,
    "grid_fee_low": 0.25,
    "grid_fee_normal": 0.35,
    "grid_fee_high": 0.45,
    "panel_tilt_deg": 30.0,
    "panel_azimuth_deg": 180.0
  }'
```

**Exempel — schemalägg elbilsladdning:**
```bash
curl -X POST http://localhost:8080/schedule \
  -H "Authorization: Bearer $TOKEN" \
  -H "Content-Type: application/json" \
  -d '{"load_id": "ev_charger", "duration_minutes": 480, "power_kw": 3.5}'
```

## CLI-klient

```bash
# Hälsokontroll
bin/GridGuard-client --token $TOKEN health

# Energiprognos
bin/GridGuard-client --token $TOKEN forecast

# Konfiguration
bin/GridGuard-client --token $TOKEN config set \
  --lat 59.33 --lon 18.07 --region SE3 --location Stockholm \
  --solar-area 20 --solar-eff 0.18 --consumption 1.5 \
  --panel-tilt 30 --panel-azimuth 180

# Schemalägg last
bin/GridGuard-client --token $TOKEN schedule add \
  --load ev_charger --duration 480 --power 3.5

# Lista scheman
bin/GridGuard-client --token $TOKEN schedule list
```

## Bygga och testa

```bash
make              # Bygg allt
make debug        # Debug-build med -O0
make release      # Release-build med -O2
make profile      # Profileringsbuild med gprof

make test         # Kör alla legacy C-tester
make test-gtest   # Kör Google Tests med ASAN/UBSAN
make test-all     # Kör alla tester

make bench        # Kör alla benchmarks
make bench-compute
make bench-queue
make bench-cache

make valgrind-server   # Minnesanalys
make helgrind          # Trådanalys
```

## Projektstruktur

```
src/
├── server/     HTTP-server, request-routing, applikationskärna
├── fetcher/    Hämtar data från Open-Meteo och Elprisetjustnu
├── parser/     Validerar och strukturerar rådata till domänmodeller
├── compute/    Energiplanberäkningar (BUY/SELL/AVOID/IDLE, solproduktion)
├── watchdog/   Processövervakning, heartbeats, omstartslogik
├── domain/     Domänmodeller och lastschemaläggning
├── cache/      Process-delad cache (POSIX shm + rwlock)
├── api/        URL-byggare för externa API:er (Open-Meteo, Elprisetjustnu)
├── auth/       JWT-validering (mbedTLS)
├── db/         SQLite-databas (användarkonfiguration, scheman)
├── ipc/        IPC-structs (WorkRequest, FetchResult, ParseResult)
├── net/        TCP-server, HTTP-parser
├── sys/        Logger, trådpool, kö, signalhantering
├── libs/       Tredjepartsbibliotek (cJSON)
├── client/     C++-klient med RAII och STL
└── tests/
    ├── unit/         Google Test — scheduler, restart policy, HTTP, kö, logger
    ├── integration/  C-integrationstester — pipeline, API, parser
    └── benchmarks/   Prestandamätningar — compute, cache, kö

docs/
├── ARCHITECTURE.md
├── API.md
├── Old/        Arkiverade dokument
└── Changelog/
```

## Dokumentation

- `docs/ARCHITECTURE.md` — Arkitektur, IPC, watchdog och designbeslut
- `docs/API.md` — Komplett API-referens med request/response-exempel
- `docs/Old/PERFORMANCE_REPORT.md` — Profileringsresultat och optimeringar
- `docs/Changelog/` — Detaljerade ändringsloggar per vecka
