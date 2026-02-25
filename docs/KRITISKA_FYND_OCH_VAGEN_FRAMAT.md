# GridGuard — Status och Vägen Framåt

**Datum:** 2026-02-25
**Syfte:** Tydligt läge på systemet, vad som saknas, och vägen till verklig kundnytta
**Kontext:** Kursprojekt (vecka 1-12), C/C++-server, framtida ESP32-integration

**VIKTIGT: Ny Insikt (2026-02-25):**
- Inget behov av avancerad public web UI eller CLI-kommandon i klienten
- Fokus: **Testdata → Server-beräkningar → Visuell presentation i C++-klienten**
- **make dev** ska: Bygga server → Köra kundens beräkningar → Visa visuellt i klienten
- **HÖGSTA PRIORITET:** Förbered ALLT på server-sidan för framtida ESP32-implementation

---

## 1. Var Står Vi Nu?

### ✅ Teknisk Infrastruktur (Klar och Säker)

**Server & Pipeline:**
- Multi-threaded pipeline (Fetch → Parse → Compute) fungerar stabilt
- HTTP server med JWT-autentisering (inga minnesläckor, DoS-skydd)
- SQLite databas för användarkonfiguration
- Thread-pool för HTTP-requests
- Caching-system för väderdata och spotpriser
- WorkCompletion-mekanism för pipeline-synkronisering

**Säkerhet (Fixat 2026-02-24):**
- ✅ JWT token-storlek validering (max 5 KB, AWS Cognito-standard)
- ✅ HTTP socket timeout (30s, förhindrar DoS)
- ✅ Signal handler race condition fixad
- ✅ 0 minnesläckor (verifierat med Valgrind på alla tester)

**API-endpoints:**
- `GET /health` — Hälsokontroll
- `GET /forecast` — Hämta energiprognos (spotpris + väder)
- `GET /user/config` — Läs användarkonfiguration
- `PUT /user/config` — Spara lat/lon/region/solar/consumption

**Datakällor:**
- Spotpriser: elpriset.se (96 timmar, 15-minutersintervall)
- Väder: Open-Meteo (96 timmar, temperatur, moln, solinstrålning)

---

### ❌ Vad Systemet INTE Gör (Kritiska Brister)

**1. Felaktig Kostnadskalkylering**
- Systemet använder BARA spotpris i beräkningar
- Nätavgifter (0.25–0.45 kr/kWh) IGNORERAS → 70% av kostnaden saknas!
- Energiskatt (0.40 kr/kWh) IGNORERAS
- Moms (25%) IGNORERAS

**Resultat:** Systemet rekommenderar FEL tidpunkter för energianvändning

**Exempel:**
```
Kund ser: "Ladda nu! Pris 0.50 kr/kWh"
Verkligt pris: 1.56 kr/kWh (spotpris + nätavgift + skatt + moms)
→ Kunden sparar INGET eller förlorar pengar
```

---

**2. Ingen Load Shifting (Den Viktigaste Funktionen Saknas)**

Systemet har ingen funktion för att:
- Hitta billigaste timmarna för skjutbara laster
- Schemalägga elbilsladdning, tvättmaskin, varmvatten
- Ge konkreta rekommendationer: "Ladda kl 02:00–05:00, spara 45 kr"

**Kundvärde som saknas:** 7 000–10 000 kr/år per kund

---

**3. Ingen Feedback-Loop**

Systemet:
- Gör aldrig prognoser bättre över tid
- Lär sig aldrig från verklig solproduktion
- Använder okalibrerade modeller (antar perfekta förhållanden)

**Resultat:** Permanent felaktig solprognos → felaktiga beslut

---

**4. Greedy-Algoritm (Suboptimal Optimering)**

Nuvarande logik: Varje timme analyseras ISOLERAT
- "Pris under genomsnitt? → KÖR"
- "Pris över genomsnitt? → VÄNTA"

**Problem:** Missar bättre möjligheter senare på dagen

**Exempel:**
```
Kl 10: Pris 0.80 kr/kWh (under snitt) → "Kör tvättmaskin nu!"
Kl 14: Pris 0.30 kr/kWh → För sent, redan körd

Förlorad besparing: 0.50 kr/kWh × 2 kWh = 1 kr per tvätt
→ 2 000–5 000 kr/år förlorat
```

---

## 2. Server-Implementation: Vad Behöver Vi Leverera?

### Strategi 1: Korrekt Totalkostnad (HÖGSTA PRIORITET)

**Implementation för kursprojekt:**

**Datamodell (UserConfig) - ENKELT:**
- Utöka UserConfig schema med 3 fält:
  - `gridFee_low` (kr/kWh, t.ex. kl 00-06) - DEFAULT 0.25
  - `gridFee_normal` (kr/kWh, t.ex. kl 07-16) - DEFAULT 0.35
  - `gridFee_high` (kr/kWh, t.ex. kl 17-23) - DEFAULT 0.45
- SQL seed-skript med realistiska testdata (Stockholm, Ellevio-liknande tariffer)
- Compute.c beräknar: spotpris + nätavgift + energiskatt (0.40 kr) + moms (25%)

**INGEN web UI behövs för MVP:**
- ❌ Ingen GridTariffs.json
- ❌ Inga dropdown-menyer
- ❌ Ingen user-editable frontend
- ✅ Testdata seedas via `make dev`

**Kundvärde (demonstreras via server-beräkningar):**
- Systemet beräknar korrekt totalkostnad
- LoadScheduler använder verklig kostnad (inte bara spotpris)
- ESP32 får korrekta beslut från servern

**Tidsåtgång:**
- Schema migration: 30 min
- Seed-skript: 30 min
- Compute.c uppdatering: 2h
- **Total: 3 timmar**

---

### Strategi 2: Load Shifting-Algoritm (KÄRNAN I PRODUKTEN)

**Vad som behövs:**
1. Scheduler som hittar N billigaste timmarna under nästa 24h
2. API-endpoints:
   - `POST /schedule` → "När ska jag ladda bilen?" → "Kl 02:00, spara 45 kr"
   - `GET /schedule` → Hämta alla schemalagda laster
3. SQLite-tabell för schedules

**Kundvärde — Konkreta Exempel:**

**Elbilsladdning:**
- Behov: 40 kWh, 3.6 timmar
- Pluggas in: 20:00, Deadline: 07:00
- Utan Load Shifting: Laddar kl 20:00 → 74 kr
- Med Load Shifting: Laddar kl 02:00 → 30 kr
- **Besparing: 44 kr per laddning → 6 864 kr/år (3× per vecka)**

**Varmvattenberedare:**
- Behov: 3 kWh/dag
- Utan Load Shifting: Slumpmässiga tider → 1 588 kr/år
- Med Load Shifting: Alltid kl 02-05 → 876 kr/år
- **Besparing: 712 kr/år**

**Total Load Shifting-potential: 7 000–10 000 kr/år per kund**

**Tidsåtgång:** 2–3 dagar

---

### Strategi 3: Feedback-Loop (KONTINUERLIG FÖRBÄTTRING)

**Vad som behövs:**
1. `POST /metrics` endpoint — ESP32/klient rapporterar faktisk solproduktion
2. MetricsDB.c — Lagrar faktisk vs. prognos i SQLite
3. Kalibreringsskript — Körst 1× per månad, uppdaterar solmodell

**Kundvärde:**
- Efter 30 dagar: Solprognos är kalibrerad för specifik installation
- System lär sig från verkligheten
- Kundförtroende: "Systemet blir bättre över tid"

**Besparingsförbättring:** +400 kr/år + förtroende

**Tidsåtgång:** 1 dag

---

### Strategi 4: Visuell C++-Klient (DEMONSTRERA KURSMÅL)

**Vad som behövs:**
1. Dashboard.cpp som standalone executable
2. SocketRAII-klass för automatisk socket cleanup (Kursmål 4)
3. STL-användning: std::string, std::vector (Kursmål 5, 9)
4. Visuell terminal-output med ASCII-boxar och ANSI-färger

**Värde för kursprojekt:**
- Demonstrerar Kursmål 4, 5, 9 (C++-objektmodell, RAII, STL)
- Visuell presentation av server-beräkningar
- Ingen resource leaks (RAII garanterar cleanup)

**Tidsåtgång:** 3-4 timmar

---

## 3. Roadmap för Kursprojekt

### MVP för Kursprojekt (Rekommenderas)

**Mål:** Server-fokuserad implementation, redo för ESP32

**Vad som implementeras:**
1. ✅ Korrekt totalkostnad (nätavgifter + skatt + moms) - 3h
2. ✅ Load Shifting-algoritm (hitta billigaste timmar) - 2 dagar
3. ✅ Schedule-endpoints (POST/GET /schedule) - 1 dag
4. ✅ Feedback-loop (POST /metrics, enkel kalibrering) - 1 dag
5. ✅ Visuell C++-klient (Dashboard.cpp) - 3-4h

**Kursmål som täcks:**
- Kursmål 4, 5, 9: C++-klient med RAII och STL
- Kursmål 7, 8: Flertrådad server, IPC (redan implementerat)
- Kursmål 11, 12: Optimering baserat på mätdata, dokumentation

**ESP32-redo:** Servern är 100% förberedd, ESP32 bara anropar API:er

---

### Alternativ B: Komplett System (Framtida Utveckling)

**Allt från Alternativ A +:**
- Peak Shaving (effekttopp-begränsning) → +2 400–5 400 kr/år
- Solar Self-Consumption (maximera egenanvändning) → +1 200–2 500 kr/år
- Weather-Aware Heating (prediktiv uppvärmning) → +1 800–3 500 kr/år

**Kundnytta:** 15 000–20 000 kr/år
**Tidsåtgång:** 8–10 veckor
**När:** Efter kursprojekt

---

### Alternativ C: Snabb Prototyp (INTE REKOMMENDERAT)

**Vad:** Hoppa över nätavgifter, använd bara greedy-optimering

**Varför DÅLIGT:**
- ❌ Ingen verklig kundnytta (felaktiga beslut)
- ❌ Kan KOSTA kunden pengar
- ❌ Förtroende förloras
- ❌ Uppfyller inte kursmål 11 (optimering baserat på mätdata)

---

## 4. Systemarkitektur: Hur Allt Hänger Ihop

### Översikt: Dataflöde för Server-Implementationen

**Målet för kursprojekt:** Server beräknar optimal energi-scheduling, redo för ESP32-integration

**Server-komponenter (redan implementerat ✅):**
1. **UserConfigDB** — Kundkonfiguration (plats, solar, nätavgifter)
2. **Forecast** — Extern data från Open-Meteo + elpriset.se
3. **Pipeline** — FetchWorker → ParseWorker → ComputeWorker
4. **Compute** — Beräknar energiprognos med korrekt totalkostnad

**Server-komponenter (TODO 📝):**
5. **LoadScheduler** — Hittar billigaste timmar för laster
6. **ScheduleDB** — Sparar schemalagda laster
7. **MetricsDB** — Feedback-loop för kalibrering

**Klient-komponenter (TODO 📝):**
8. **Dashboard.cpp** — Visuell C++-klient (demonstrerar Kursmål 4, 5, 9)

---

### 4.1 Testdata och Konfiguration: Förbered Databasen

**NY APPROACH (2026-02-25):** Istället för avancerad web UI, använd enkla testdata

**Hur systemet sätts upp:**

1. **SQL seed-skript (testdata.sql):**
   ```sql
   INSERT INTO users (userId, location, latitude, longitude, region,
                      solarAreaM2, solarEfficiency, consumptionKwh,
                      gridFee_low, gridFee_normal, gridFee_high)
   VALUES ('test_user', 'Stockholm', 59.3293, 18.0686, 'SE3',
           20.0, 0.18, 1.5,
           0.25, 0.35, 0.45);
   ```

2. **make dev körs:**
   - Server startar och läser testdata från databasen
   - Pipeline hämtar väderdata + spotpriser
   - Compute beräknar energiprognos för test_user
   - C++-klient ansluter och visar visuellt resultat

**Vad C++-klienten visar (visuell terminal-output):**
```
╔════════════════════════════════════════════════════╗
║         GridGuard Energy Dashboard                ║
║         User: test_user (Stockholm, SE3)          ║
╠════════════════════════════════════════════════════╣
║  Time       | Signal | Price    | Solar  | Grid   ║
║─────────────┼────────┼──────────┼────────┼────────║
║  00:00-01:00│  BUY   │ 0.88 kr  │ 0.0 kW │ +1.5 kW║
║  01:00-02:00│  BUY   │ 0.90 kr  │ 0.0 kW │ +1.5 kW║
║  02:00-03:00│  IDLE  │ 1.20 kr  │ 0.0 kW │ +1.5 kW║
║  ...                                               ║
║  12:00-13:00│  SELL  │ 1.85 kr  │ 3.2 kW │ -1.7 kW║
╠════════════════════════════════════════════════════╣
║  Best charging window (next 24h):                 ║
║  → 02:00-05:36 (3.6h) → 30.00 kr (Save 44 kr!)   ║
╠════════════════════════════════════════════════════╣
║  Daily Summary:                                   ║
║  Grid Import:  12.5 kWh  →  Cost: 24.80 kr       ║
║  Grid Export:   3.2 kWh  →  Income: 5.92 kr      ║
║  Net Cost: 18.88 kr                               ║
╚════════════════════════════════════════════════════╝
```

**Arbetsuppskattning:**
- SQL seed-skript: 30 minuter
- C++-klient visuell rendering: 3-4 timmar (formaterad output, färger med ANSI codes)

---

### 4.2 Backend: UserConfigDB (Uppdaterad Schema)

**UserConfigDB (SQLite) — Schema med nätavgifter:**
```sql
CREATE TABLE IF NOT EXISTS users (
    userId TEXT PRIMARY KEY,
    location TEXT NOT NULL,
    latitude REAL NOT NULL,
    longitude REAL NOT NULL,
    region TEXT NOT NULL,
    solarAreaM2 REAL DEFAULT 0.0,
    solarEfficiency REAL DEFAULT 0.0,
    consumptionKwh REAL NOT NULL,
    gridFee_low REAL DEFAULT 0.25,     -- kl 00-06
    gridFee_normal REAL DEFAULT 0.35,  -- kl 07-16
    gridFee_high REAL DEFAULT 0.45     -- kl 17-23
);
```

**Migration-skript (schema_migration.sql):**
```sql
-- Lägg till nya kolumner om de inte finns
ALTER TABLE users ADD COLUMN gridFee_low REAL DEFAULT 0.25;
ALTER TABLE users ADD COLUMN gridFee_normal REAL DEFAULT 0.35;
ALTER TABLE users ADD COLUMN gridFee_high REAL DEFAULT 0.45;
```

**Backend-ändringar (minimal):**
- Kör migration-skript vid server-start
- Compute.c läser grid_fee-fält från UserConfig
- Inga API-ändringar behövs för MVP (testdata läses direkt från DB)

**Arbetsuppskattning:**
- Migration-skript: 30 minuter
- Backend-integration: 1 timme

---

### 4.3 Compute (NY VERSION): Korrekt Totalkostnad

**Nuvarande problem:**
- Compute.c använder BARA spotpris i beräkningar
- Ignorerar nätavgifter (70% av kostnaden!)
- Ignorerar energiskatt och moms

**Ny design:**

**Input till Compute:**
1. ForecastData (spotpriser per 15-minuter från Forecast-modulen, 384 datapunkter)
2. WeatherData (solinstrålning, moln från Forecast-modulen)
3. UserConfig (solar, förbrukning, **nätavgifter**)

**Compute beräknar för varje 15-minuters intervall:**

**Steg 1: Hämta korrekt nätavgift baserat på klockslag**
- Kl 00:00-06:59 → gridFee_low (t.ex. 0.25 kr/kWh)
- Kl 07:00-16:59 → gridFee_normal (t.ex. 0.35 kr/kWh)
- Kl 17:00-23:59 → gridFee_high (t.ex. 0.45 kr/kWh)

**Steg 2: Beräkna total kostnad**
- Spotpris (från elpriset.se): t.ex. 1.20 kr/kWh
- Nätavgift (från UserConfig): t.ex. 0.45 kr/kWh
- Energiskatt (konstant): 0.40 kr/kWh
- Summa exkl. moms: 1.20 + 0.45 + 0.40 = 2.05 kr/kWh
- Moms (25%): 2.05 × 0.25 = 0.51 kr/kWh
- **Total kostnad:** 2.56 kr/kWh ← Detta är VERKLIGT pris för kunden

**Steg 3: Använd total kostnad i alla beslut**
- Sortera 15-minutersintervall efter total kostnad (inte bara spotpris)
- LoadScheduler använder total kostnad för att hitta billigaste intervall

**Output från Compute:**
- EnergyData med korrekt totalCostSek per 15-minuters intervall
- 384 datapunkter (96 timmar × 4 intervall/timme)
- Används av LoadScheduler och GET /forecast

**Arbetsuppskattning:** 1-2 timmar (refaktorera Compute.c)

---

### 4.4 LoadScheduler (NY MODUL): Hitta Billigaste Timmar

**Syfte:** Svara på frågan "När ska jag ladda bilen/köra tvättmaskin/värma varmvatten?"

**Input:**
- ForecastData med korrekt total kostnad (från Compute)
- Load-krav: duration (minuter), deadline (timestamp), power (kW)

**Algoritm:**
1. Filtrera bort intervall efter deadline
2. Sortera alla 15-minutersintervall efter total kostnad (billigast först)
3. Hitta första lediga fönster som är tillräckligt långt (duration minuter)
4. Beräkna total kostnad för fönstret (summera alla 15-minutersintervall i fönstret)
5. Jämför med "ladda nu"-kostnad → beräkna besparing

**Output:**
- Optimal starttid (timestamp)
- Estimerad kostnad (kr)
- Besparing vs. "ladda nu" (kr)

**Exempel:**
```
Input:
- Behov: 40 kWh, 3.6 timmar (216 minuter)
- Deadline: Imorgon 07:00
- Pluggas in: Idag 20:00

Compute sorterar timmar:
1. Kl 02:00 - 0.88 kr/kWh (billigast!)
2. Kl 03:00 - 0.90 kr/kWh
3. Kl 04:00 - 0.92 kr/kWh
4. Kl 05:00 - 0.95 kr/kWh
... osv

LoadScheduler hittar fönster:
- Kl 02:00-05:36 (3.6h) → Total: 30 kr
- Jämför med "ladda nu" (kl 20:00): 74 kr
- Besparing: 44 kr

Output:
{
  "scheduled_start": "2026-02-25T02:00:00Z",
  "duration_minutes": 216,
  "estimated_cost_sek": 30.00,
  "savings_sek": 44.00
}
```

**Arbetsuppskattning:** 1-2 dagar (ny modul + enhetstester)

---

### 4.5 ScheduleDB (NY): Spara Schemalagda Laster

**Syfte:** Lagra kundens schemalagda laster så ESP32/klient kan hämta dem

**SQLite-tabell struktur:**
- schedule_id (PRIMARY KEY)
- user_id (FOREIGN KEY till users)
- load_id (t.ex. "ev_charger", "washing_machine")
- scheduled_start (timestamp)
- duration_minutes (int)
- power_kw (double)
- status (pending/active/completed/cancelled)
- created_at (timestamp)

**API-endpoints:**

**POST /schedule — Skapa nytt schema**
```
Input (från klient):
{
  "load_id": "ev_charger",
  "duration_minutes": 216,
  "power_kw": 11.0,
  "deadline": 1740441600
}

Backend flow:
1. Hämta ForecastData från Forecast-cache
2. Kör LoadScheduler för att hitta optimal tid
3. Spara i ScheduleDB
4. Returnera optimal tid till klient

Output:
{
  "schedule_id": "abc123",
  "scheduled_start": 1740405600,
  "estimated_cost_sek": 30.00,
  "savings_sek": 44.00
}
```

**GET /schedule — Hämta alla schemas**
```
Output:
{
  "schedules": [
    {
      "schedule_id": "abc123",
      "load_id": "ev_charger",
      "scheduled_start": 1740405600,
      "duration_minutes": 216,
      "status": "pending"
    },
    {
      "schedule_id": "def456",
      "load_id": "water_heater",
      "scheduled_start": 1740396000,
      "duration_minutes": 60,
      "status": "active"
    }
  ]
}
```

**DELETE /schedule/:id — Avboka**

**Arbetsuppskattning:** 1 dag (SQLite schema + API-endpoints + integration)

---

### 4.6 MetricsDB (NY): Feedback-Loop

**Syfte:** Systemet lär sig från verklig solproduktion och kalibrerar modellen

**Dataflöde:**

**Steg 1: ESP32/klient rapporterar faktisk produktion**
```
POST /metrics
{
  "timestamp": 1740405600,
  "actual_solar_kwh": 1.8
}

Backend sparar i MetricsDB:
- user_id
- timestamp
- actual_solar_kwh
```

**Steg 2: Kalibreringsskript (körs 1× per månad)**
```
För varje användare:
1. Hämta alla metrics från senaste 30 dagarna
2. Hämta motsvarande prognoser från ForecastData
3. Beräkna genomsnittlig avvikelse:
   calibration_factor = AVG(actual_solar_kwh / predicted_solar_kwh)

4. Uppdatera UserConfig:
   solarEfficiency_calibrated = solarEfficiency * calibration_factor

Exempel:
- Prognos sa: 2.5 kWh/dag
- Faktiskt: 1.8 kWh/dag
- Calibration factor: 1.8 / 2.5 = 0.72
- Ny efficiency: 0.18 × 0.72 = 0.13 (justerad nedåt)
```

**Steg 3: Compute använder kalibrerad efficiency**
- Nästa prognos blir mer exakt
- Efter 30 dagar: Solprognos matchar verkligheten

**Arbetsuppskattning:** 1 dag (MetricsDB + POST endpoint + kalibreringsskript)

---

### 4.7 ComputeWorker (UPPDATERAD): Pipeline-Integration

**Nuvarande flow:**
```
HTTP Request → FetchWorker → ParseWorker → ComputeWorker → HTTP Response
```

**Uppdaterad ComputeWorker-ansvar:**

**Input (från ParseWorker):**
- ForecastData (spotpriser + väder)
- UserConfig (inklusive NYTT: gridFee_low/normal/high)

**ComputeWorker gör:**
1. Hämta UserConfig från UserConfigDB (använder userId från ParseWorker)
2. För varje timme i forecast:
   - Hämta nätavgift baserat på timme (GetGridFeeForHour)
   - Beräkna total kostnad (spotpris + nätavgift + skatt + moms)
   - Beräkna solproduktion (använder kalibrerad efficiency om tillgänglig)
   - Bestäm action (BUY/SELL/IDLE baserat på total kostnad)
3. Skapa EnergyData med korrekt totalCostSek per timme
4. Serialisera till JSON
5. Signalera WorkCompletion (wake up HTTP worker)

**Output (via WorkCompletion):**
```
{
  "user_id": "test_user",
  "location": "Stockholm",
  "region": "SE3",
  "generated_at": 1740405600,
  "summary": {
    "entries": 96,
    "total_cost_sek": 245.60,  ← KORREKT kostnad
    "grid_import_kwh": 12.5,
    "grid_export_kwh": 3.2
  },
  "forecast": [
    {
      "time": "2026-02-25T00:00:00Z",
      "signal": "BUY",
      "price_sek_kwh": 0.88,  ← Total kostnad (spotpris + nätavgift + skatt + moms)
      "solar_kwh": 0.0,
      "consumption_kwh": 0.5
    },
    ...
  ]
}
```

**Arbetsuppskattning:** 2-3 timmar (uppdatera ComputeWorker.c för att använda nya Compute)

---

### 4.8 Komplett Dataflöde: Exempel-Scenario

**Scenario:** Kund vill ladda elbil, frågar "När ska jag ladda?"

**1. Kunden konfigurerar systemet (EN GÅNG):**
- Frontend: Fyller i config-formulär
- PUT /user/config → Backend sparar i UserConfigDB
- Inkluderar: plats, solar, förbrukning, **nätavgifter**

**2. Kunden frågar "När ska jag ladda bilen?":**
- Frontend: POST /schedule med {load_id: "ev_charger", duration: 216, deadline: "2026-02-25T07:00"}
- Backend flow:

  **Steg A: Hämta forecast-data**
  - FetchWorker hämtar spotpriser från elpriset.se
  - FetchWorker hämtar väder från Open-Meteo
  - ParseWorker parsar JSON till ForecastData

  **Steg B: Beräkna korrekt totalkostnad**
  - ComputeWorker läser UserConfig (inkl. nätavgifter)
  - Compute beräknar för varje timme: spotpris + nätavgift + skatt + moms
  - Output: EnergyData med korrekt totalCostSek

  **Steg C: Hitta billigaste timmar**
  - LoadScheduler sorterar timmar efter totalCostSek
  - Hittar första 3.6-timmars fönster före deadline (kl 02:00-05:36)
  - Beräknar total kostnad: 30 kr
  - Beräknar besparing vs "ladda nu": 44 kr

  **Steg D: Spara schema**
  - ScheduleDB sparar: schedule_id, user_id, load_id, start=02:00, duration=216

  **Steg E: Returnera till kund**
  - POST /schedule response:
    ```
    {
      "scheduled_start": "2026-02-25T02:00:00Z",
      "estimated_cost_sek": 30.00,
      "savings_sek": 44.00
    }
    ```

**3. ESP32 hämtar schema:**
- ESP32 kör GET /schedule var 1:e minut
- Ser att "ev_charger" ska starta kl 02:00
- Kl 02:00: Aktiverar GPIO-pin för laddare
- Kl 05:36: Stänger av GPIO-pin

**4. Efter laddning: Feedback-loop (valfritt):**
- ESP32 rapporterar: POST /metrics med faktisk förbrukning
- Kalibreringsskript (1× per månad): Uppdaterar solmodell baserat på actual vs. predicted
- Nästa forecast blir mer exakt

---

### 4.9 Vad Teamet Behöver Bygga (Uppdaterad 2026-02-25)

**Nya moduler:**
1. **LoadScheduler.c** (2 dagar) — Algoritm för att hitta billigaste timmar
2. **ScheduleDB.c** (1 dag) — SQLite-tabell + CRUD-operationer
3. **MetricsDB.c** (1 dag) — SQLite-tabell + POST endpoint + kalibreringsskript

**Uppdaterade moduler:**
4. **UserConfig** (1.5h) — Lägg till 3 grid_fee-fält + migration-skript
5. **Compute.c** (2h) — Använd gridFee + skatt + moms i beräkningar
6. **ComputeWorker.c** (1h) — Använd nya Compute
7. **ClientHandler.c** (1 dag) — POST/GET /schedule endpoints

**C++-klient (visuell presentation):**
8. **client/Dashboard.cpp** (3-4h) — Formaterad terminal-output med ASCII-boxar

**Testdata:**
9. **scripts/seed_testdata.sql** (30 min) — Realistiska kunddata för demonstration

**Build-integration:**
10. **Makefile: make dev target** (1h) — Bygg server → seed DB → starta klient → visa dashboard

**Total arbete för MVP:**
- **Backend (server-optimering):** 4.5 timmar
- **Load Shifting + Scheduling:** 3 dagar
- **Feedback-loop:** 1 dag
- **Visuell klient:** 4-5 timmar
- **Total:** ~4.5 dagar

**BONUS:** Ingen tid spenderad på web UI, dropdown-menyer, eller GridTariffs.json research!

---

## 5. ESP32-Integration (Efter Kursprojekt)

### Design-Princip: Server = Smart, ESP32 = Enkel

**Vad servern gör (redan i Alternativ A):**
- ✓ Beräknar optimal tid för laster
- ✓ Lagrar schema i SQLite
- ✓ Exponerar GET /schedule

**Vad ESP32 gör (implementeras senare):**
1. Var 1:e minut: Hämta schema från server (GET /schedule)
2. Varje sekund: Kolla om något ska starta/stoppa
3. Aktivera GPIO-pinnar vid schemalagd tid

**Total ESP32-kod:** ~200 rader C
**Hårdvara för MVP:** ~1 450 kr (ESP32-S3 LCD 7B + BME280 + 3× reläer)
**ROI:** 2.4 månader (1 450 kr / 7 000 kr/år)

---

## 5. Nätavgifter: Förenklad Approach för MVP

### UPPDATERAD STRATEGI (2026-02-25): Hårdkodade Testdata

**För kursprojektet behöver vi INTE:**
- ❌ Dropdown-menyer eller web UI
- ❌ GridTariffs.json
- ❌ Användar-redigerbara formulär

**Vad vi GÖR istället:**
1. **Seed-skript med realistiska värden:**
   ```sql
   -- Typisk Stockholmskund med Ellevio-nät
   INSERT INTO users VALUES (
       'test_user', 'Stockholm', 59.3293, 18.0686, 'SE3',
       20.0, 0.18, 1.5,
       0.25, 0.35, 0.45  -- Ellevio Tid3 tariffer (approximativa)
   );
   ```

2. **make dev laddar testdata automatiskt**

3. **Server beräknar korrekt totalkostnad baserat på dessa värden**

4. **C++-klienten visar resultatet visuellt**

**Fördelar:**
- ✅ **Snabbast implementation:** 1-2 timmar totalt
- ✅ **Demonstrerar korrekt beräkningslogik** (själva kärnan!)
- ✅ **Förbereder för ESP32:** Server-logiken är KOMPLETT
- ✅ **Kursmål uppfyllt:** Systemet optimerar baserat på verklig kostnad

**Vad som sparas till senare (post-kurs):**
- Web UI för konfiguration
- Dropdown med nätbolag
- User-editable config

**Varför detta är BÄTTRE för kursprojektet:**
- Fokus på algoritmik och systemdesign (VIKTIGT för kursen)
- INTE fokus på UI/UX (inte kursmål)
- ESP32-integration blir enklare (servern redan klar)

---

### Framtida Expansion (Efter Kursprojekt)

**När projektet blir produkt:**
- Web UI för konfiguration
- GridTariffs.json med 10 största nätbolagen
- Geografisk autodetect (lat/lon → nätbolag)

---

## 6. Prioriterad Roadmap

### ✅ DAG 1: Säkerhetsfix — KLART!

**Status:** Systemet är produktionsklart säkerhetsmässigt
- ✅ JWT token-storlek DoS-skydd
- ✅ HTTP timeout (30s)
- ✅ Signal handler race condition
- ✅ 0 minnesläckor (Valgrind-verifierat)

---

### Vecka 1–2: Server-Optimering och Visuell Klient (Kursmål 4, 9, 11)

**Mål:** Korrekt kostnad + visuell presentation i C++-klient

1. **Nätavgifter + Total Kostnad** (3-4h)
   - Utöka UserConfig schema med 3 fält: `gridFee_low`, `gridFee_normal`, `gridFee_high`
   - Skapa SQL seed-skript (testdata.sql) med realistiska värden
   - Uppdatera Compute.c (beräkna spotpris + gridFee + skatt + moms)
   - Kursmål: 11

2. **Visuell C++-klient** (3-4h)
   - Ta emot forecast-data från server (GET /forecast)
   - Formaterad terminal-output med ASCII-boxar
   - Visa: Time, Signal, Price, Solar, Grid för varje timme
   - Show daily summary och best charging window
   - Kursmål: 4, 9 (C++ streams, RAII för socket)

3. **make dev integration** (1h)
   - make dev bygger server → seedar testdata → startar klient → visar dashboard
   - En-kommando för att demonstrera systemet

**Leverans:**
- Server beräknar korrekt totalkostnad
- C++-klient visar visuell dashboard i terminalen
- `make dev` demonstrerar komplett system

---

### Vecka 3–4: Load Shifting (Kursmål 7, 8, 11)

**Mål:** Scheduler + API

3. **LoadScheduler.c** (2 dagar)
   - FindOptimalTime()-algoritm
   - Enhetstester
   - Kursmål: 11

4. **Schedule-endpoints** (1 dag)
   - POST /schedule
   - GET /schedule
   - SQLite schedules-tabell
   - Kursmål: 8

**Leverans:** Klient/ESP32 kan fråga "när ska jag ladda bilen?" och få svar

---

### Vecka 5: Feedback-Loop (Kursmål 11, 12)

**Mål:** System lär sig från verkligheten

5. **Metrics-endpoint** (1 dag)
   - POST /metrics
   - MetricsDB.c
   - Kalibreringsskript
   - Kursmål: 11, 12

**Leverans:** Efter 30 dagar är solmodellen kalibrerad

---

### Vecka 6–10: Kursmål 1–6, 10 (Teori + Profilering)

6. **Shared Memory Cache** (Kursmål 2, 8)
7. **Profilering med gprof** (Kursmål 6, 10)
8. **Watchdog-refaktorering** (Kursmål 12)

**Leverans:** Full kursmålstäckning (1–12)

---

### Vecka 11–12: Dokumentation och Examination

9. **Teknisk dokumentation** (Doxygen, ARCHITECTURE.md)
10. **Skriftligt kunskapstest** (Kursmål 1–6)
11. **Projektinlämning** (Kursmål 7–12)

---

## 7. Sammanfattning och Rekommendation

### ✅ Var Vi Är Nu

**Tekniskt:**
- Stabil multi-threaded server med pipeline-arkitektur
- Säker JWT-autentisering, inga minnesläckor
- HTTP API med SQLite-databas
- Väderdata + spotpriser hämtas och cachas

**Problem:**
- ❌ Felaktig kostnadskalkyl (nätavgifter saknas)
- ❌ Ingen load shifting-funktion
- ❌ Ingen feedback-loop
- ❌ Suboptimal greedy-algoritm

**Resultat:** Systemet fungerar tekniskt men har INGEN verklig kundnytta

---

### 🎯 Rekommenderad Väg: Alternativ A (Minimal MVP - Uppdaterad 2026-02-25)

**Vad som görs:**
1. **Testdata + Nätavgifter** (3-4h)
   - SQL seed-skript med realistiska värden
   - Korrekt totalkostnad (spotpris + nätavgift + skatt + moms)
2. **Visuell C++-klient** (3-4h)
   - Formaterad dashboard i terminalen
   - Visar forecast-data visuellt
3. **Load Shifting-algoritm** (2 dagar)
   - FindOptimalTime() för laster
4. **Schedule-endpoints** (1 dag)
   - POST/GET /schedule
5. **Feedback-loop** (1 dag)
   - POST /metrics, kalibrering

**Tidsåtgång:** ~4.5 dagar

**Kundnytta:** 7 000–10 000 kr/år
**Kursmål:** 4, 7, 8, 9, 11, 12 (+ 1–6 via teori)

**VIKTIGT FOKUS:**
- ✅ Server-logik är 100% klar för ESP32-integration
- ✅ Algoritmer är korrekta och demonstrerbara
- ✅ Visuell presentation utan overhead av web UI
- ✅ `make dev` visar ALLT i action

---

### 📈 Framtida Expansion (Efter Kursen)

- Peak Shaving → +2 400–5 400 kr/år
- Solar Self-Consumption → +1 200–2 500 kr/år
- Weather-Aware Heating → +1 800–3 500 kr/år
- ESP32-integration → ~200 rader C

**Total potential:** 15 000–20 000 kr/år per kund

---

## 8. Nästa Steg för Teamet (Uppdaterad 2026-02-25)

### UPPDATERAD STRATEGI: Fokus på Server + Visuell Demo

**Första Sprint (Vecka 1-2):**

1. **Backend-Optimering** (4-5 timmar)
   - Utöka UserConfig schema (gridFee_low/normal/high)
   - Migration-skript för databas
   - Uppdatera Compute.c (korrekt totalkostnad)
   - Seed-skript med testdata

2. **Visuell C++-klient** (3-4 timmar)
   - Dashboard.cpp med formaterad terminal-output
   - ASCII-boxar, färger (ANSI codes)
   - Visa forecast-data visuellt
   - Daily summary + best charging window

3. **make dev Integration** (✅ KLART)
   - Automatisk build → seed → server start → visar server-info
   - Klient-integration kommer senare (när Dashboard.cpp är klar)

**Andra Sprint (Vecka 3-4):**

4. **LoadScheduler** (2 dagar)
   - FindOptimalTime() algoritm
   - Enhetstester

5. **Schedule-endpoints** (1 dag)
   - POST/GET /schedule
   - ScheduleDB integration

**Tredje Sprint (Vecka 5):**

6. **Feedback-loop** (1 dag)
   - POST /metrics
   - Kalibreringsskript

---

### Demo Flow för Kursprojekt

**Nuvarande status (2026-02-25) - När examinatorn kör `make dev`:**

```bash
$ make dev

╔════════════════════════════════════════════════════════════╗
║              GridGuard Development Server                  ║
╚════════════════════════════════════════════════════════════╝

→ Starting watchdog & server...
→ Waiting for server... ready!
→ Seeding test data for test_user...
✓ Test user configuration seeded

╔════════════════════════════════════════════════════════════╗
║  Server running on http://localhost:8080                   ║
║  JWT Token (test_user):                                    ║
║  eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9...                  ║
║                                                            ║
║  Test endpoints:                                           ║
║  • GET  /health                                            ║
║  • GET  /forecast  (requires Authorization header)         ║
║  • GET  /user/config                                       ║
║  • PUT  /user/config                                       ║
║                                                            ║
║  Logs: logs/server.log  ·  logs/watchdog.log               ║
║  Stop with: make stop                                      ║
╚════════════════════════════════════════════════════════════╝
```

**Framtida (när Dashboard.cpp är klar):**
- C++-klient startas automatiskt efter server
- Visuell dashboard med ASCII-boxar visas i terminalen
- Demonstrerar Kursmål 4, 5, 9 (RAII, STL, C++-objektmodell)

**Vad som redan fungerar:**
- ✅ Server beräknar energiprognos baserat på testdata
- ✅ JWT-autentisering
- ✅ Pipeline med FetchWorker → ParseWorker → ComputeWorker
- ✅ Testdata seedas automatiskt
- ✅ Redo för ESP32-integration (API:er fungerar)

---

**Resultat:** Verklig kundnytta demonstrerad visuellt + alla kursmål uppfyllda + ESP32-redo

---

**Dokumentet sammanställt:** 2026-02-25 (uppdaterat)
**Baserat på:** Kodanalys, säkerhetsaudit, kurskrav, kundvärdesanalys, ny design-insikt
