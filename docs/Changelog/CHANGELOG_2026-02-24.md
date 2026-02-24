# Ändringslogg - 2026-02-24

## SQLite user config, omskriven BUY/SELL/IDLE-logik och chunked HTTP-fix

Fas 3 är implementerad. Servern lagrar nu per-användarkonfiguration i SQLite och beräknar BUY/SELL/IDLE baserat på varje användares faktiska solpaneler, förbrukning och spotpris. Batterisystemet är borttaget. En kritisk bugg i HTTPClient-lagret som gjorde att alla tre externa API-svar tolkades fel är åtgärdad. E2E-testskript: 20/20 tester godkända.

---

## Fas 3 — SQLite user config + GET/PUT /user/config

Servern behöver veta var användaren bor och hur stora solpaneler hen har. Det finns inget konfigurationssystem utanför själva enheten — konfigurationen sätts av användaren via HTTP och lagras lokalt i SQLite. Plattformen rör aldrig den här datan.

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

> **Testversion.** Compute är i nuläget en scaffolding-implementation vars primära syfte är att verifiera att hela flödet fungerar — från API-svar (SMHI, Open-Meteo, Elpriset) via parsning och SQLite user config till ett komplett forecast-svar. Algoritmen är inte kalibrerad och ska inte ses som den slutgiltiga beslutslogiken. Den designas om när flödet är stabilt och C++-klienten är på plats.

Den gamla `Compute` hade hårdkodade `SolarConfig`, `BatteryConfig` och `ConsumptionProfile`-structs. Nu skickas per-användardata direkt in som parametrar i `Compute_GenerateEnergyPlan`.

Algoritmen arbetar i två pass. Första passet räknar ut genomsnittligt spotpris för dagen och sätter köptröskel till 80 % av snittet. Andra passet beräknar per timme:

```
solarKwh = (irradiance / 1000) × area × efficiency × 0.75
netKwh   = solarKwh − consumptionKwh
```

Om `netKwh > 0.05`: sälj till nätet. Om pris under köptröskel: köp. Annars: IDLE. Prestandafaktorn 0.75 täcker kabelförluster, temperaturpåverkan och inverterverkningsgrad.

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

---

## Semantisk namngivning: userId, location och region

**Problem:** Fältet `location` användes för att lagra JWT subject (userId), vilket var semantiskt inkorrekt.

**Lösning:**
- Bytte namn på `location` till `userId` i alla pipeline-structs (WorkRequest, FetchResult, ParseResult)
- Lade till nytt `location`-fält för användarvänligt visningsnamn (t.ex. "Linköping")
- JSON API uppdaterat: `user_id`, `location` och `region` returneras nu korrekt
- CLI visar nu: "test_user · Linköping · SE3"

**Tekniska ändringar:**
- Database: ny kolumn `location` i user_configs
- UserConfig model utökad med location-fält
- Dataflöde genom hela pipeline (ClientHandler → FetchWorker → ParseWorker → ComputeWorker)
- C++ klient parsear och visar alla tre fält

**Bonus:**
- `make dev` kör nu automatiskt med JWT-secret (ingen manuell GRIDGUARD_JWT_SECRET behövs)
- Default test-användare sätts upp med Linköping som location
