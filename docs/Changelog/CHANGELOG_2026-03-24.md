# Changelog 2026-03-24

**Branch:** `master`

---

## DNI/DHI-stöd och Hay & Davies POA-transpositionsmodell

Systemet beräknade tidigare solproduktion direkt från GHI (Global Horizontal Irradiance), vilket är den solstrålning som träffar en horisontell yta. För lutande solpaneler underskattar detta produktionen avsevärt. Denna uppdatering lägger till stöd för DNI (Direct Normal Irradiance) och DHI (Diffuse Horizontal Irradiance) och implementerar Hay & Davies-modellen för att beräkna POA (Plane of Array) — den faktiska strålningen mot panelens lutade yta.

### Ny funktion: `CalculatePOA()` i `src/compute/Compute.c`

Implementerar den fullständiga POA-beräkningskedjan:

1. **Solposition** (Spencer 1971-formel): Beräknar solens deklination, ekvation för tid, timvinkel, zenitvinkel och azimutvinkel för varje 15-minuterskvartal baserat på UTC-tidsstämpel och användarens koordinater.
2. **Infallsvinkel (AOI)**: Vinkeln mellan solens strålar och panelens normal, beräknad från zenitvinkel, azimutskillnad, panellutning och panelazimut.
3. **Hay & Davies-modell**: Delar upp POA-bestrålningen i fyra komponenter:
   - Direktstrålning på panelen (beam)
   - Circumsolär diffus strålning (anisotropisk, från solens riktning)
   - Isotrop himmeldiffus strålning
   - Markreflektion (albedo = 0,2)

Anisotropiindexet `f = DNI / GHI` styr fördelningen mellan circumsolär och isotrop diffus. Modellen degraderar korrekt till GHI vid `tiltDeg = 0°` (horisontell yta), vilket gör den bakåtkompatibel.

Paneltemperaturkorrigeringen (IEC 61215 NOCT-modellen) använder nu POA istället för GHI som indata, vilket är fysikaliskt korrekt.

### Nya datafält: panellutning och panelazimut

Följande fält lades till i hela pipeline:

| Fält | Typ | Default | Beskrivning |
|------|-----|---------|-------------|
| `panelTiltDeg` | `double` | 30.0° | Panelns lutning: 0° = horisontell, 90° = vertikal |
| `panelAzimuthDeg` | `double` | 180.0° | Panelns riktning (kompass): 0°=N, 90°=Ö, 180°=S, 270°=V |

### Ändrade filer

**Datastrukturer**
- `src/domain/UserConfig.h` — Lade till `panelTiltDeg` och `panelAzimuthDeg`
- `src/api/OpenMeteoResponse.h` — Lade till `directNormalIrradiance` och `diffuseRadiation` i `OpenMeteoEntry`
- `src/domain/Forecast.h` — Lade till DNI/DHI-fält i `ForecastEntry`
- `src/ipc/WorkRequest.h` — Lade till `panelTiltDeg`, `panelAzimuthDeg`, `latitudeDbl`, `longitudeDbl`
- `src/ipc/FetchResult.h` — Samma fyra fält för propagering genom IPC-pipeline
- `src/ipc/ParseResult.h` — Samma fyra fält

**Databas**
- `src/db/ClientDB.c` — Lade till `panel_tilt_deg` och `panel_azimuth_deg` i `CREATE_TABLE_SQL` samt två migreringsposter i `MIGRATE_SQL[]`
- `src/db/UserConfigDB.c` — Utökade `GET_SQL` och `UPSERT_SQL` med de nya kolumnerna; justerade kolumn- och bindningsindex

**API-lager**
- `src/api/APIEndpoints.c` — Lade till `direct_normal_irradiance` och `diffuse_radiation` i Open-Meteo-URL:en
- `src/api/APIParser.c` — Parsning av de nya JSON-arrayerna med `ValidateSolarRadiation()`-gränsvärden; faller tillbaka till 0,0 om arrayerna saknas i svaret

**IPC-pipeline**
- `src/fetcher/Fetcher.c` — Kopierar de fyra nya fälten från `WorkRequest` till `FetchResult`
- `src/parser/Parser.c` — Kopierar DNI/DHI från `OpenMeteoEntry` till `ForecastEntry`; kopierar panelorienteringsfält från `FetchResult` till `ParseResult`

**Beräkningsmotor**
- `src/compute/Compute.h` — Utökade `Compute_GenerateEnergyPlan`-signaturen med `panelTiltDeg`, `panelAzimuthDeg`, `latitude`, `longitude`
- `src/compute/Compute.c` — Lade till `#include <math.h>`; ny `CalculatePOA()`-funktion; ersatte `irradiance`-baserad produktion med `poa`-baserad
- `src/compute/ComputeWorker.c` — Uppdaterade anropsstället för `Compute_GenerateEnergyPlan` med de fyra nya argumenten

**HTTP-server**
- `src/server/ClientHandler.c` — `HandleGetUserConfig`: panelorienteringsfälten ingår nu i JSON-svaret. `HandlePutUserConfig`: parsning och validering av `panel_tilt_deg` (0–90°) och `panel_azimuth_deg` (0–359°) med standardvärden 30,0/180,0; fyller i WorkRequest med de nya fälten

**C++-klient**
- `src/client/GridGuardClient.hpp` — `setUserConfig` fick valfria parametrar `panelTiltDeg = 30.0` och `panelAzimuthDeg = 180.0`
- `src/client/GridGuardClient.cpp` — Lade till `panel_tilt_deg` och `panel_azimuth_deg` i JSON-kroppen
- `src/client/UserConfigWrapper.cpp` — Validering av lutning [0, 90] och azimut [0, 360); fälten ingår nu i `getAsMap()`
- `src/client/main.cpp` — CLI-flaggorna `--panel-tilt` och `--panel-azimuth` med defaultvärden 30,0/180,0; `printUsage()` uppdaterad

**Seeding och bygge**
- `scripts/seed_user_config.py` — `panel_tilt_deg` och `panel_azimuth_deg` i CREATE och INSERT; SAAB Arena seedas med `tilt=5.0°` (flatt arentak) och `azimuth=180.0°` (söder)
- `Makefile` — Lade till `-lm` i `LDFLAGS` för att länka libm (`acos`, `sin`, `cos`, `sqrt`, `fmax`, `fmin`)

### Buggfix: GHI-fallback när DNI/DHI saknas

`CalculatePOA()` returnerade nästan 0 W/m² om Open-Meteo svarar med `null` för DNI/DHI-fälten (parsern sätter dem då till 0.0) men GHI är giltig. Utan fallback reducerades solproduktionen till enbart markreflektion (~1 % av GHI) — en allvarlig regression mot det gamla beteendet.

**Fix:** Om `DNI ≤ 0` och `DHI ≤ 0` men `GHI > 0` behandlas all strålning som isotrop himmeldiffus (`DHI_eff = GHI`). Vid `tilt = 0°` ger detta exakt GHI — identiskt med gamla beteendet. Vid `tilt = 5°` (SAAB Arena) ger det 499 W/m² mot inkommande GHI = 500 W/m².

- `src/compute/Compute.c` — GHI-fallback inlagd direkt efter natt-checken

### Tester uppdaterade

- `tests/unit/test_compute_gtest.cpp` — Alla 9 anrop till `Compute_GenerateEnergyPlan` uppdaterade med de fyra nya parametrarna (`panelTiltDeg=0.0, panelAzimuthDeg=180.0, lat=0.0, lon=0.0`); tilt=0° gör POA = GHI och håller testerna deterministiska
- `tests/unit/test_user_config_db_gtest.cpp` — `SolarParamsPersist` utökad med round-trip-verifiering av `panelTiltDeg=35°` och `panelAzimuthDeg=195°`

Resultat: 9/9 compute-tester och 7/7 user_config_db-tester gröna.
