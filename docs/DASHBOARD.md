# GridGuard Dashboard - Visuell Energiöversikt i Realtid

## Översikt

GridGuard Dashboard är din kontrollpanel för energioptimering. Se alla dina besparingsmöjligheter på en enda skärm — uppdaterad var 60:e sekund med de senaste priserna och väderdata.

**Nyckelfunktioner:**
- 🟢 **Tydliga beslutssignaler** — Varje 15-minutersperiod är färgkodad: Köp, Sälj eller Vänta
- 💰 **Realtidsbesparingar** — Se exakt hur mycket du sparar per timme
- 📊 **48-timmars översikt** — Planera kommande 2 dygn (192 kvartar × 15 min)
- ⚡ **Auto-refresh** — Alltid uppdaterad data utan manuella klick

---

## Snabbstart

```bash
# Starta dashboard med auto-refresh (60 sekunder)
bin/GridGuard-client --token $TOKEN forecast --watch --interval 60

# Engångsvy (ingen auto-refresh)
bin/GridGuard-client --token $TOKEN forecast
```

**Kontroller:**
- `Ctrl+C` — Avsluta dashboard
- Auto-refresh enligt `--interval` (standard: 60s)

---

## Dashboard-layout

**HEADER**
```
GridGuard                          13.1C  Sol: 90.57 kW
SAAB_ARENA  SE3  Linkoping
```

**DAGENS SIGNALER** — Alla köp/sälj-tillfällen för kommande 48 timmar
```
03-21 02:00  BUY   0.32 kr  [====      ]  90 kWh   -54%
03-21 13:00  BUY   0.39 kr  [======    ]  12 kWh   -32%
03-21 12:00  SELL  1.85 kr  [========= ]  12 kWh   +45%
03-21 17:00  SKIP  2.15 kr  [==========]   4 kWh   +63%
```

**BÄSTA KÖPTILLFÄLLEN** — Top 5 billigaste perioderna
```
#1  03-21 02:00  0.32 kr/kWh  (-54%)
#2  03-21 13:00  0.39 kr/kWh  (-32%)
#3  03-21 22:30  0.42 kr/kWh  (-28%)
```

**SAMMANFATTNING**
```
Signaler:  7 buy   0 sell   6 skip

Import: 1612 kWh   Export: 0 kWh   Kostnad: 3568 kr
Bästa köp: 03-21 02:00 (0.32 kr)   Avg -37% vs median
```

**SCHEMALAGDA LASTER**
```
Elbilsflotta    02:00  4h    320 kr   sparat: 580 kr
Zamboni varme   13:00  1.5h   40 kr   sparat:  18 kr
HVAC forvarmn   15:00  2h     85 kr   sparat:  42 kr

Total daglig besparing: 640 kr
```

---

## Sektion för sektion

### 📍 Header — Platsinfo och totaler

```
╭──────────────────────────────────────────────────────────────╮
│  GridGuard                            13.1°C   90.57 kW      │
│  SAAB_ARENA  SE3  Linköping                                  │
╰──────────────────────────────────────────────────────────────╯
```

**Vad visas:**
- **Location:** `SAAB_ARENA` (ditt användar-ID)
- **Elområde:** `SE3` (Södra Mellansverige)
- **Stad:** `Linköping` (konfigurerad via `/user/config`)
- **Temperatur:** `13.1°C` (nuvarande väder från Open-Meteo)
- **Sol-ikon:** `☀` (visas endast om du har solceller)
- **Aktuell solproduktion:** `90.57 kW` (momentant effektuttag just nu)

**Vad betyder det:**
Se direkt om din location är korrekt konfigurerad och hur mycket solenergi du kan förvänta dig idag.

---

### 🎯 Dagens Signaler — Alla beslutspunkter

```
│  03-21 02:00  🟢 BUY   0.32 kr  ░░░░░░░░  90 kWh   -54%      │
│  03-21 13:00  🟢 BUY   0.39 kr  ██░░░░░░  12 kWh   -32%      │
│  03-21 12:00  🔵 SELL  1.85 kr  ████████  12 kWh   +45%      │
│  03-21 17:00  🔴 SKIP 2.15 kr  ██████████  4 kWh   +63%      │
```

**Kolumner:**

| Kolumn | Vad det visar | Exempel |
|--------|---------------|---------|
| **TIME** | Datum och tid | `03-21 02:00` |
| **SIGNAL** | Beslut (BUY/SELL/SKIP/IDLE) | `🟢 BUY` |
| **PRICE** | Total kostnad per kWh | `0.32 kr` (inkl. allt) |
| **BAR** | Visuell prisnivå (10 block) | `░░░░` (lågt pris) |
| **SOLAR** | Solproduktion denna period | `90 kWh` (15 min) |
| **VSAVG** | Avvikelse från medianpris | `-54%` (mycket billigt!) |

#### 🟢 BUY — Köp el nu!

**Betydelse:** Detta är de **billigaste 33%** av alla perioder. Perfekt för:
- Ladda elbilar
- Kör tvättmaskin, diskmaskin
- Förvärm lokaler (HVAC)
- Ladda batterier

**Färgkod:** Grön text
**Pris:** Oftast 30-60% billigare än genomsnittet

**Exempel:**
```
03-21 02:00  🟢 BUY   0.32 kr   -54%
```
→ Mitt i natten, lågt spotpris + låg nätavgift = **perfekt laddningstid**

#### 🔵 SELL — Sälj solenergi!

**Betydelse:** Du producerar **mer än du konsumerar** OCH priset är **högt** (≥70:e percentilen). Exportera till nätet!

**Färgkod:** Cyan text
**När:** Solig middag (11:00-14:00) + höga spotpriser

**Exempel:**
```
03-21 12:00  🔵 SELL  1.85 kr   +45%
```
→ Solproduktion 12 kWh/15min, pris 45% över median = **sälj nu!**

**OBS:** Kräver godkännande från elnätsbolag för att exportera.

#### 🔴 SKIP — Undvik förbrukning!

**Betydelse:** De **dyraste 30%** av alla perioder. Skjut upp icke-kritiska laster.

**Färgkod:** Röd text
**När:** Morgon topplast (07-09), kväll topplast (17-20)

**Exempel:**
```
03-21 17:00  🔴 SKIP  2.15 kr   +63%
```
→ Kvällstopplast, pris 63% över median = **vänta med laddning!**

**Kritiska system** (kyla, belysning under match) måste köras oavsett.

#### ⚪ IDLE — Neutral period

**Betydelse:** Varken billigt eller dyrt. Normal drift.

**Färgkod:** Gul text
**När:** Mittenfältet (~40% av perioder)

---

### 🥇 Bästa Köptillfällen — Top 5 billigast

```
 🥇 03-21 02:00  0.32 kr/kWh  (-54%)                         
 🥈 03-21 13:00  0.39 kr/kWh  (-32%)                         
 🥉 03-21 22:30  0.42 kr/kWh  (-28%)                         
  #4 03-21 03:15  0.44 kr/kWh  (-25%)                         
  #5 03-21 14:45  0.47 kr/kWh  (-22%)                         
```

**Syfte:** Snabbnavigering till de absolut billigaste perioderna.

**Användning:**
1. Kolla top 3 köptillfällena
2. Schemalägg tunga laster till dessa tider
3. Spara automatiskt 30-60% på laddningskostnader

**Praktiskt exempel (SAAB Arena):**
- 🥇 Plats #1: `02:00` — Starta elbilsladdning (10 platser, 4 timmar)
- 🥈 Plats #2: `13:00` — Förvärm zamboni (90 minuter innan match)
- 🥉 Plats #3: `22:30` — Kör tvättmaskiner för personalens arbetskläder

**Besparing:** 580 kr/dag (endast elbilsladdning)

---

### 📊 Sammanfattning — Nyckeltal för perioden

```
│  🟢 7 buy   🔵 0 sell   🔴 6 skip                          
│                                                              │
│  Import: 1612 kWh   Export: 0 kWh   Kostnad: 3568 kr         │
│  Bästa köp: 03-21 02:00 (0.32 kr)   Avg -37% vs median       │
```

#### Rad 1: Signal-dots

**Antal perioder per signal-typ (48 timmar):**
- 🟢 `7 buy` → 7 BUY-signaler (billiga perioder)
- 🔵 `0 sell` → Inga säljtillfällen (inget batteri eller låg export)
- 🔴 `6 skip` → 6 SKIP-signaler (dyra perioder)

**Tolkning:**
- **Många BUY:** Bra prisläge, många besparingsmöjligheter
- **Få SKIP:** Platt priskurva, svårare att optimera
- **SELL > 0:** Du har solöverskott att exportera

#### Rad 2: Energiflöde

| Metrik | Betydelse | Exempel |
|--------|-----------|---------|
| **Import** | Total el från nätet (48h) | `1612 kWh` |
| **Export** | Total el till nätet (48h) | `0 kWh` (ingen export) |
| **Kostnad** | Total kostnad för perioden | `3568 kr` |

**Beräkning:**
```
För SAAB Arena (basförbrukning 11.25 kWh/15min = 45 kWh/h):
- 48 timmar = 192 kvartar × 15 min
- Förbrukning: 192 × 11.25 kWh = 2 160 kWh
- Solproduktion: ~548 kWh (ca 25% av behov)
- Import: 2 160 − 548 ≈ 1 612 kWh (netto efter sol)
- Kostnad: Summan av (import × pris) för varje kvart
```

#### Rad 3: Bästa köp & genomsnittlig rabatt

**Bästa köp:**
```
03-21 02:00  (0.32 kr)
```
→ Den absolut billigaste perioden. **Schemalägg tunga laster hit!**

**Avg -37% vs median:**
```
Genomsnittlig besparing för alla BUY-signaler: -37.3%
```

**Tolkning:**
- BUY-perioderna är i genomsnitt **37.3% billigare** än medianpriset
- Högre negativt värde = Bättre besparingsmöjligheter
- Om värdet är nära 0% → Platt priskurva (svårt att optimera)

**Exempel-kalkyl:**
```
Medianpris:       1.23 kr/kWh
Avg BUY-pris:     0.77 kr/kWh
Skillnad:         -0.46 kr/kWh  (-37.3%)

Vid 440 kWh laddning (elbilsflotta):
440 kWh × 0.46 kr = 202 kr besparing
```

---

### ⚡ Schemalagda Laster — Dina automatiska besparingar

```
  ✓ Elbilsflotta    02:00  4h   320 kr   sparat: 580 kr     
  ✓ Zamboni värme   13:00  1.5h  40 kr   sparat:  18 kr     
  ✓ HVAC förvärmn   15:00  2h    85 kr   sparat:  42 kr     
                                                             
  💰 Total daglig besparing: 640 kr                          
```

**Kolumner:**

| Kolumn | Förklaring | Exempel |
|--------|------------|---------|
| **Load** | Last-ID (trunkerat till 15 tecken) | `Elbilsflotta` |
| **Start** | Optimal starttid (automatiskt beräknad) | `02:00` |
| **Dur** | Duration (timmar) | `4h` |
| **Cost** | Estimerad kostnad vid optimal tid | `320 kr` |
| **Saving** | Besparing vs omedelbar start | `580 kr` 🟢 |

#### Hur fungerar schemaläggningen?

**Exempel: Elbilsflotta (10 bilar, 11 kW/plats, 4 timmar)**

```
Användaren gör:
$ bin/GridGuard-client --token $TOKEN schedule add \
    --load ev_fleet --duration 240 --power 110

GridGuard analyserar:
1. Hitta alla BUY-perioder kommande 48h
2. Identifiera 4-timmarsfönster
3. Beräkna kostnad för varje fönster
4. Viktning med "practicality score":
   - Natt (22-07):  1.0× (acceptabelt, tidsinställning)
   - Kväll (17-22): 1.5× (bäst, hemma och vaken)
   - Dag (07-17):   0.5× (sämst, ofta borta)
5. Välj fönster med lägst vägrad kostnad

Resultat:
- Optimal start: 02:00 (natt, lågt spotpris)
- Kostnad: 320 kr (440 kWh × 0.73 kr/kWh)
- Besparing: 580 kr (vs start kl 16:00 topplast)
```

#### Status-färger

| Status | Färg | Betydelse |
|--------|------|-----------|
| `pending` | 🟡 Gul | Väntar på att starta |
| `running` | 🔵 Cyan | Körs just nu |
| `completed` | 🟢 Grön | Avslutad |

#### Total daglig besparing

```
💰 Total daglig besparing: 640 kr
```

**Beräkning:**
- Elbilsflotta: 580 kr
- Zamboni värme: 18 kr
- HVAC förvärmning: 42 kr
- **Totalt: 640 kr/dag**

**Årlig besparing:** 640 kr × 365 dagar = **233 600 kr/år**

---

## Användarscenarier

### 🎯 Scenario 1: Morgonkontroll (07:00)

**Du öppnar dashboard:**

```bash
bin/GridGuard-client --token $TOKEN forecast --watch
```

**Du ser:**
```
🟢 02:00–05:45  BUY   0.32 kr  (-54%)
🔴 17:00–20:00  SKIP 2.15 kr  (+63%)
🔵 12:00–14:00  SELL  1.85 kr  (+45%)
```

**Beslut:**
- ✅ Elbilsladdning startade automatiskt kl 02:00 (schemalagd igår)
- ✅ Planera tunga laster till lunch (disk, tvätt)
- ⛔ Undvik att köra tunga laster efter 17:00 (kvällstopplast)
- 💰 Förväntat solöverskott kl 12-14 → säljmöjlighet

**Besparing idag:** 640 kr (automatiskt via schemaläggning)

### 🎯 Scenario 2: Schemalägg ny last (14:00)

**Behov:** Ladda servicefordon (22 kW, 3 timmar)

**Steg:**
1. Kolla "Bästa köptillfällen" i dashboard:
   ```
   🥇 22:30  0.42 kr/kWh  (-28%)
   🥈 02:15  0.44 kr/kWh  (-25%)
   ```

2. Schemalägg:
   ```bash
   bin/GridGuard-client --token $TOKEN schedule add \
       --load service_van --duration 180 --power 22
   ```

3. GridGuard svarar:
   ```
   ✓ Optimal start: 22:30
   ✓ Kostnad: 28 kr (66 kWh × 0.42 kr)
   ✓ Besparing: 42 kr (vs omedelbar start)
   ```

4. Bekräfta i dashboard:
   ```
   ✓ service_van  22:30  3h  28 kr  sparat: 42 kr
   ```

**Total daglig besparing:** 640 kr → 682 kr (+42 kr)

### 🎯 Scenario 3: Solproduktion-dag (solig sommardag)

**Dashboard visar:**
```
🔵 12:00–14:00  SELL  1.85 kr  (+45%)  Solar: 12.5 kWh/15min
🟢 13:00–14:15  BUY   0.39 kr  (-32%)  Solar: 12.5 kWh/15min
```

**Tolkning:**
- 12:00–13:00: Pris högt (1.85 kr) + solöverskott → **SELL**
- 13:00–14:15: Pris lågt (0.39 kr) + solöverskott → **BUY** (ladda batteri istället för att sälja)

**Beslut:**
1. ✅ Exportera till nätet 12:00–13:00 (högsta pris)
2. ✅ Ladda batteri 13:00–14:15 (lågt pris + sol = maximalt överskott)
3. ⚠️ Kontrollera elnätsbolag-avtal för exportgodkännande

**Intäkt:** ~12.5 kWh × 1.85 kr × 4 kvartar = ~92 kr (1 timme export)

---

## Färgkoder och Symboler

### Signal-färger

| Färg | Symbol | Signal | Användning |
|------|--------|--------|------------|
| 🟢 Grön | `▸` | BUY | Billiga perioder — köp el nu |
| 🔴 Röd | `▸` | SKIP | Dyra perioder — undvik el nu |
| 🔵 Cyan | `▸` | SELL | Solöverskott — exportera nu |
| 🟡 Gul | `▸` | IDLE | Neutral — ingen stark signal |

### Pris-bar (10 block)

```
░░░░░░░░░░  = Billigast (0/10 block)
█████░░░░░  = Medel (5/10 block)
██████████  = Dyrast (10/10 block)
```

**Tolkning:**
- Färre block = Lägre pris (bättre)
- Fler block = Högre pris (sämre)
- Visuellt lätt att se prisvariation

### Status-symboler

| Symbol | Betydelse |
|--------|-----------|
| ✓ | Schemalagd last (pending/completed) |
| 🟢 | Grön dot (BUY-signal count) |
| 🔴 | Röd dot (SKIP-signal count) |
| 🔵 | Cyan dot (SELL-signal count) |
| 💰 | Pengar (besparingar, kostnader) |
| ⚡ | Energi (förbrukning, produktion) |
| ☀ | Sol (solcellsproduktion) |

---

## FAQ — Vanliga frågor

### Q: Varför visar dashboard bara 13 signaler för 48 timmar?

**A:** Signaler filtreras smart — **IDLE-perioder visas inte** eftersom de inte är actionabla. Systemet grupperar även sammanhängande kvartar:

```
Rådata (192 kvartar):
02:00 BUY
02:15 BUY
02:30 BUY  ─┐
02:45 BUY  ─┤─ Grupperas till: "02:00–05:45 BUY"
03:00 BUY  ─┤
... (23 kvartar)
05:45 BUY  ─┘

Dashboard:
🟢 02:00–05:45  BUY  (1 rad)
```

**Resultat:** 192 råkvartar → 50-100 actionabla fönster (lättare att överblicka)

### Q: Vad betyder "avg buy -37.3% vs median"?

**A:** Genomsnittet av alla BUY-signalers avvikelse från medianpriset.

**Exempel:**
```
Medianpris (hela perioden): 1.23 kr/kWh

BUY-signaler:
- 02:00: 0.32 kr → -74% vs median
- 13:00: 0.39 kr → -68% vs median
- 22:30: 0.77 kr → -37% vs median
... (7 signaler totalt)

Genomsnitt: (-74 -68 -37 ...) / 7 = -37.3%
```

**Tolkning:**
- `-37.3%` = BUY-perioder är i genomsnitt **37.3% billigare**
- Högre negativt värde = Bättre besparingsmöjligheter
- Nära 0% = Platt priskurva (svårt att optimera)

### Q: Varför är solar 0 kWh på natten men ändå BUY-signal?

**A:** BUY-signaler baseras på **totalkostnad** (spotpris + avgifter), inte solproduktion.

**Exempel:**
```
03-21 02:00 (natt):
- Spotpris: 0.10 kr/kWh (lågt)
- Nätavgift: 0.25 kr/kWh (låg nattvgift)
- Energiskatt: 0.40 kr/kWh
- Moms: 25%
─────────────────────
Total: 0.94 kr/kWh → BUY-signal

Solar: 0 kWh (natt, ingen sol)
```

**Natt är ofta billigast** trots att du inte producerar egen el.

### Q: Kan jag lita på prognoserna?

**A:** Ja! Systemet uppdaterar automatiskt när ny data finns:

| Datakälla | Uppdateringsfrekvens | Hur det fungerar |
|-----------|----------------------|------------------|
| **Spotpriser** | Dagligen kl 13:00 | Imorgondagens priser publiceras → automatisk cache-invalidering |
| **Väderdata** | Var 15:e minut | Open-Meteo uppdateras → fresh forecast |
| **Prognos** | Vid ny data | Cache TTL 30 min, men invalideras omedelbart vid ny pris/väderdata |
| **Dashboard** | `--interval 60` | Klienten hämtar ny data var 60:e sekund |

**Praktiskt:**
- ✅ Nästa dygn: Exakta spotpriser (kända sedan igår 13:00)
- ⚠️ Dag 2: Väderprognos (något osäkrare sol, men priserna uppdateras automatiskt)

### Q: Hur schemaläggs laster automatiskt?

**A:** När du anropar `schedule add` hittar GridGuard det **billigaste fönstret** som passar:

```
Algoritm:
1. Filtrera alla BUY-perioder (kommande 48h)
2. Hitta sammanhängande block ≥ duration
3. Beräkna kostnad för varje block
4. Viktning med practicality (natt 1.0×, kväll 1.5×, dag 0.5×)
5. Välj block med lägst vägrad kostnad
```

**Resultat:** Automatiskt optimerad starttid, ingen manuell planering krävs.

---

## Tips och Tricks

### 💡 Maximal besparing

1. **Kolla dashboard varje morgon** — identifiera dagens billigaste perioder
2. **Schemalägg tunga laster** (EV, tvätt, disk) till BUY-signaler
3. **Undvik topplast** (17-20) för icke-kritiska laster
4. **Utnyttja solproduktion** — exportera vid SELL-signaler (högt pris)
5. **Auto-refresh** — använd `--watch --interval 60` för realtidsdata

### ⚡ Snabbkommandon

```bash
# Live dashboard (60s refresh)
bin/GridGuard-client --token $TOKEN forecast --watch

# Schemalägg elbilsladdning
bin/GridGuard-client --token $TOKEN schedule add \
    --load ev_charger --duration 240 --power 11

# Lista alla scheman
bin/GridGuard-client --token $TOKEN schedule list

# Radera schema
bin/GridGuard-client --token $TOKEN schedule delete <schedule_id>
```

### 🔍 Felsökning

**Problem:** Dashboard visar "No forecast data"
```bash
# 1. Kontrollera server
curl http://localhost:8080/health

# 2. Verifiera token
bin/GridGuard-client --token $TOKEN health

# 3. Kolla loggar
tail -f logs/server.log
```

**Problem:** Alla priser är lika (ingen variation)
```bash
# Vänta på dagens spotpriser (publiceras 13:00)
# Eller kontrollera region:
bin/GridGuard-client --token $TOKEN config get
```

**Problem:** Solar alltid 0 kWh
```bash
# Sätt solcellskonfiguration:
bin/GridGuard-client --token $TOKEN config set \
    --solar-area 20 --solar-eff 0.18
```

---

## Sammanfattning

GridGuard Dashboard ger dig:
- 🎯 **Tydliga beslut** — Ingen gissning, systemet säger när du ska agera
- 💰 **Automatiska besparingar** — 640 kr/dag (SAAB Arena)
- 📊 **Full översikt** — 48 timmar i en enda vy
- ⚡ **Realtid** — Auto-refresh var 60:e sekund

**Öppna dashboard nu:**
```bash
bin/GridGuard-client --token $TOKEN forecast --watch --interval 60
```

_Spara pengar utan att tänka på det._
