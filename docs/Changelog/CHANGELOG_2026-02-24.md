# Ändringslogg - 2026-02-24

## SQLite user config, omskriven BUY/SELL/IDLE-logik och chunked HTTP-fix

Fas 3 är implementerad. Servern lagrar nu per-användarkonfiguration i SQLite och beräknar BUY/SELL/IDLE baserat på varje användares faktiska solpaneler, förbrukning och spotpris. Batterisystemet är borttaget. En kritisk bugg i HTTPClient-lagret som gjorde att alla tre externa API-svar tolkades fel är åtgärdad. E2E-testskript: 20/20 tester godkända.

---

## Fas 3 — SQLite user config + GET/PUT /user/config

Servern behöver veta var användaren bor och hur stora solpaneler hen har. Det finns inget konfigurationssystem utanför själva enheten — konfigurationen sätts av användaren via HTTP och lagras lokalt i SQLite. Plattformen (Next.js) rör aldrig den här datan.

`Database` i `src/infrastructure/database/` öppnar SQLite med `SQLITE_OPEN_FULLMUTEX` — det betyder att SQLite sköter trådsäkerheten internt och att flera worker-trådar kan anropa `sqlite3_prepare_v2` och `sqlite3_step` utan extern synkronisering.

`UserConfigDB` implementerar två operationer mot tabellen `user_configs`: `Get` returnerar 0 (hittad), 1 (finns inte) eller -1 (fel). `Upsert` använder `INSERT OR REPLACE` och sätter `updated_at = time(NULL)` automatiskt.

`GET /user/config` kräver giltig JWT, slår upp `sub` mot `user_configs` och returnerar konfigurationen som JSON. `PUT /user/config` parsar request-body med cJSON, validerar att obligatoriska fält finns och anropar `UserConfigDB_Upsert`. `consumption_kwh` är valfritt och defaultar till 0.5 kWh/h.

### user_configs-tabellen

```sql
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
```

`user_id` är direkt hämtat från JWT:ens `sub`-fält — samma ID som plattformen utfärdat token för. Ingen separat användartabell behövs.

---

## Borttaget: batterisystemet

`Battery.h`, `Consumption.h` och `Solar.h` var modeller som aldrig refererades av någon produktionskod. De är borttagna. `Energy.h` och `Energy.c` är omskrivna: bara tre möjliga åtgärder — `ACTION_BUY_FROM_GRID`, `ACTION_SELL_TO_GRID`, `ACTION_IDLE`. Inga batterifält, inga laddningscykler.

---

## Omskriven beslutslogik i Compute

Den gamla `Compute` hade hårdkodade `SolarConfig`, `BatteryConfig` och `ConsumptionProfile`-structs. Nu skickas per-användardata direkt in som parametrar i `Compute_GenerateEnergyPlan`.

Algoritmen arbetar i två pass. Första passet räknar ut genomsnittligt spotpris för dagen och sätter köptröskel till 80 % av snittet. Andra passet beräknar per timme:

```
solarKwh = (irradiance / 1000) × area × efficiency × 0.75
netKwh   = solarKwh − consumptionKwh
```

Om `netKwh > 0.05`: sälj till nätet. Om pris under köptröskel: köp (t.ex. ladda varmvattenberedare, starta diskmaskin). Annars: IDLE. Prestandafaktorn 0.75 täcker kabelförluster, temperaturpåverkan och inverterverkningsgrad.

Per-användarvärdena (`solarAreaM2`, `solarEfficiency`, `consumptionKwh`) flödar nu hela vägen genom pipeline: ClientHandler läser från SQLite → WorkRequest → FetchResult → ParseResult → Compute.

---

## Kritisk buggfix: HTTP chunked transfer-encoding

Alla tre externa API:er (SMHI, Open-Meteo, Elpriset) svarar med `Transfer-Encoding: chunked`. Utan avkodning injicerades chunk-storlekarna (hexadecimala tal som `800`, `c03`) rakt in i JSON-kroppen. Det resulterade i att SMHI-parsern inte hittade `timeSeries`, Open-Meteo-parsern kraschade med felmeddelandet "c03" och Elpriset-parsern fick "Spot price JSON is not an array".

`decode_chunked` i `HTTPClient.c` itererar chunk-size-rader, kopierar chunk-data och hoppar över CRLF-avdelare. `str_istr` är en portabel skiftlägesokänslig sökning via `strncasecmp` (från `<strings.h>`) som hittar `transfer-encoding: chunked` i headerblocket utan att kräva `_GNU_SOURCE`.

`WorkCompletion`-bufferten ökades från 8 192 till 32 768 byte för att rymma en fullständig 76-posters prognosrespons (~8 700 byte).

---

## E2E-testskript

`scripts/test_e2e.sh` testar hela systemet i ett körning:

1. Bygger servern
2. Seedar `gridguard.db` med två testanvändare via `sqlite3`
3. Startar servern med `GRIDGUARD_JWT_SECRET=gridguard-test-secret`
4. Kör 20 tester: `/health`, auth-gate (401), okänd användare (404), GET/PUT `/user/config`, och ett fullständigt `/forecast`-anrop mot riktiga API:er
5. Stoppar servern och tar bort testdatabasen

Kör från projektroten: `bash scripts/test_e2e.sh`

Testresultat från körning 2026-02-24: **20/20 godkända**. Pipeline producerade 76 poster (SMHI), 96 poster (Open-Meteo), 96 priser (Elpriset), avg pris 0.086 SEK/kWh, BUY/SELL/IDLE per timme.

---

## Ändrade filer

| Fil | Ändring |
|-----|---------|
| `src/application/models/config/UserConfig.h` | Ny — per-användarkonfigurationsstruktur |
| `src/infrastructure/database/Database.h/c` | Ny — SQLite-wrapper med fullmutex |
| `src/infrastructure/database/UserConfigDB.h/c` | Ny — Get/Upsert mot user_configs |
| `src/server/ClientHandler.c` | HandleGetUserConfig, HandlePutUserConfig, HandleForecast läser från SQLite |
| `src/application/core/GridGuard.h/c` | lat/lon/solar-fält i WorkRequest, Database i GridGuard-struct |
| `src/application/workers/FetchWorker.h/c` | Använder request->lat/lon istf hårdkodade konstanter |
| `src/application/workers/ParseWorker.h/c` | Vidarebefordrar per-användardata i ParseResult |
| `src/application/workers/ComputeWorker.c` | Anropar Compute med per-användarparametrar |
| `src/application/services/Compute.h/c` | Omskriven — inga config-structs, bara per-request-parametrar |
| `src/application/models/domain/Energy.h/c` | Omskriven — bara BUY/SELL/IDLE, inga batterifält |
| `src/network/client/HTTPClient.c` | decode_chunked + str_istr |
| `src/concurrency/sync/WorkCompletion.h` | Buffer 8 KiB → 32 KiB |
| `src/application/configs/Config.h` | DB_PATH tillagt, batteri/solar-konstanter borttagna |
| `Makefile` | DATABASE_DIR, -lsqlite3, mkdir för database build-dir |
| `scripts/test_e2e.sh` | Ny — komplett E2E-testskript |
| `src/application/models/config/Battery.h` | Borttagen |
| `src/application/models/config/Consumption.h` | Borttagen |
| `src/application/models/config/Solar.h` | Borttagen |

---

## Nästa steg — C++-klienten

C++-klienten är den enda C++-delen i projektet och täcker kursmål 9 (RAII, STL, klasser). Den kommunicerar mot servern via HTTP och JWT — exakt samma gränssnitt som nu är fullt implementerat och testat.

Tre klasser:

**`TokenManager`** — RAII-klass som läser JWT från `~/.gridguard/token`. Konstruktorn öppnar filen, destruktorn stänger den. `std::string getToken()` returnerar token-strängen. Kastar `std::runtime_error` om filen saknas.

**`ForecastClient`** — Skickar `GET /forecast` med `Authorization: Bearer <token>`. Använder `std::string` för att bygga headers. Returnerar JSON-strängen som `std::string`.

**`ResponseFormatter`** — Parsar JSON-svaret och formaterar BUY/SELL/IDLE-planen för terminalen. Använder `std::vector<ForecastEntry>` och `std::map<std::string, int>` för att räkna signaler. Skriver ut en tabell med tidpunkt, signal, pris och solproduktion.

Kommandon som CLI:n behöver stödja:

```bash
gridguard login <token>       # Sparar JWT till ~/.gridguard/token
gridguard forecast            # Hämtar och formaterar prognos
gridguard config              # Öppnar webbläsaren till http://localhost:8080/config#token=...
```
