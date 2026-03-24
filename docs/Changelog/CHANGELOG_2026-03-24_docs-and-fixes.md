# Changelog 2026-03-24 — Dokumentationsuppdateringar och fixes

**Branch:** `feature/dni-dhi-poa-model`

---

## Dokumentationsuppdateringar efter DNI/DHI-implementationen

Samtliga docs-filer uppdaterades för att spegla det aktuella systemets implementation.

### `docs/ARCHITECTURE.md`

**IPC-structs (End-to-end dataflödesdiagram):**
- `WorkRequest`, `FetchResult` och `ParseResult` saknade de fyra fält som lades till i DNI/DHI-implementationen: `panelTiltDeg`, `panelAzimuthDeg`, `latitudeDbl`, `longitudeDbl`. Dessa är nu tillagda i respektive structs mermaid-box.

**SQLite-schema:**
- `gridguard.db`-schemat i arkitekturdiagrammet saknade `panel_tilt_deg` och `panel_azimuth_deg`. Nu tillagt.

**Solcellsmodell:**
- Sektionen hette "Solcellsmodell — Temperaturkorrigering (IEC 61724 / IEC 61215)" och beskrev tre steg. Hay & Davies POA-transpositionsmodellen (steg 0) lades till som nytt första steg, inklusive formel, GHI-fallback-beskrivning och implementeringshänvisning till `CalculatePOA()`.
- NOCT-formeln i steg 1 uppdaterades: refererar nu till `POA` som indata istället för `irradiance`.
- Energiformeln i steg 3 refererar nu till `poa` istället för `irradiance`.
- Mermaid-diagrammet i steg 3 uppdaterades: "Solinstrålning W/m² (Open-Meteo: shortwave_radiation)" → "POA W/m² (Hay & Davies, steg 0)".
- Implementeringshänvisningen uppdaterades: lades till `CalculatePOA()` bredvid `CalculatePanelTemperature()` och `Compute_GenerateEnergyPlan()`.

### `docs/API.md`

**GET /forecast:**
- Beskrivningstexten och fälttabellen nämnde `BUY`, `SELL` och `AVOID` men inte `IDLE`. `IDLE` tillagt som fjärde rekommendationsvärde med förklaring att det filtreras bort i JSON-output.

**GET /user/config:**
- Exempelsvaret saknade `panel_tilt_deg` och `panel_azimuth_deg`. Båda tillagda med defaultvärden (30.0 / 180.0).

**PUT /user/config:**
- Request body-exemplet saknade `panel_tilt_deg` och `panel_azimuth_deg`. Båda tillagda.
- Fälttabellen utökades med de två nya fälten inkl. valideringsintervall (0–90° respektive 0–359°) och defaultvärden.
- Felfallstabellen fick två nya rader för `Invalid panel_tilt_deg` och `Invalid panel_azimuth_deg`.

### `docs/README.md`

- `GET /forecast`-raden i endpointlistan uppdaterades: "BUY/SELL/AVOID" → "BUY/SELL/AVOID/IDLE".
- curl-exemplet för `PUT /user/config` fick `panel_tilt_deg` och `panel_azimuth_deg`.
- CLI-konfigurationsexemplet fick flaggorna `--panel-tilt 30` och `--panel-azimuth 180`.
- Kommentaren i projektstrukturen uppdaterades: "BUY/SELL/AVOID" → "BUY/SELL/AVOID/IDLE".

### `docs/DEMOKUND.md`

- **Faktafel fixat:** Kundprofilens rubrikrad stod `500 m² solpaneler` trots att alla beräkningar i dokumentet redan använde 1 500 m². Ändrat till `1 500 m²`.
- **Konfigurationsblock kompletterat:** Lade till `5° lutning (flatt arentak), söderazimut 180°` — dessa fält seedas med just de värdena i `scripts/seed_user_config.py` och bör synas i kundprofilen.
- **Solcellsmodell:** Hay & Davies POA-transpositionsmodellen lades till som steg 0, med ett SAAB Arena-exempel (tilt 5°, GHI = 800 W/m² → POA ≈ 800 W/m²). Steg 1–3 är i övrigt oförändrade — vid 5° lutning är POA ≈ GHI och siffrorna påverkas inte.

### `.gitignore`

- `build_test/` lades till under Build artifacts — katalogen skapas av `cmake -B build_test` men saknades i ignoreringen.
- `docs/profiling/` lades till under Profiling and coverage — benchmark- och gprof-resultfiler (`bench_*.txt`, `gprof_*.txt`) dök upp som pending changes.
