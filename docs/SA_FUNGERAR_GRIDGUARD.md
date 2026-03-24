# GridGuard: Compute-algoritm & LoadScheduler
## Teknisk dokumentation för kunder

Detta dokument förklarar de två kärnteknologierna i GridGuard:
1. **Compute-algoritmen** - Hur BUY, SELL och AVOID-signaler genereras
2. **LoadScheduler** - Smart schemaläggning av flexibla laster

---

## Del 1: Compute-algoritmen

### Kärnprincipen: Relativ prisanalys

GridGuard använder **inte statiska prisgränser** som blir obsoleta vid prisförändringar. Istället analyserar vi de kommande **192 kvartalen** (48 timmar × 4 per timme) och klassificerar varje period relativt prisfördelningen.

### Algoritmen i tre steg:

#### Steg 1: Beräkna total elkostnad per 15-minuters kvartal

För varje kvartal beräknas den verkliga kostnaden för slutkunden:

```
Total kostnad = (Spotpris + Nättariff + Energiskatt) × (1 + Moms)
```

**Exempel:**
- Spotpris: 0.65 kr/kWh
- Nättariff (kväll): 0.45 kr/kWh
- Energiskatt: 0.40 kr/kWh
- **Summa före moms:** 1.50 kr/kWh
- **Summa efter 25% moms:** **1.88 kr/kWh** ← Detta är vad kunden faktiskt betalar

#### Steg 2: Percentilbaserad klassificering

Systemet analyserar de kommande **192 kvartalen** (48 timmar × 4 per timme):

```
Billigaste 33%  →  BUY-zon     (P33)
Mitten 33%      →  NORMAL-zon  (P33-P70)
Dyraste 30%     →  AVOID-zon   (P70+)
```

**Exempel från verkliga priser:**
- Billigast: 0.83 kr/kWh (natt)
- Median: 1.09 kr/kWh
- **BUY-tröskel**: 1.00 kr/kWh (billigaste 33%)
- **AVOID-tröskel**: 1.22 kr/kWh (dyraste 30%)
- Dyrast: 1.98 kr/kWh (kvällstopp)

#### Steg 3: Kvalitetskontroll

För att undvika falska signaler kräver systemet **minst 8% skillnad från median**:
- Om priset varierar lite (platt kurva) → inga signaler
- Om priset varierar mycket → tydligare BUY/AVOID-zoner

---

### BUY-signal: Köp från nätet när det är billigt

**När aktiveras BUY?**
- Priset är bland de **billigaste 33%** de kommande 48 timmarna
- **ELLER** spotpriset är negativt (du får betalt för att förbruka el)

**Exempel:**
```
[00:00] BUY → 0.857 kr/kWh ≤ 1.003 threshold (79% av median)
[00:15] BUY → 0.854 kr/kWh ≤ 1.003 threshold (78% av median)
```

---

### SELL-signal: Sälj överskottsel när det lönar sig

**När aktiveras SELL?**
Systemet kräver **alla tre villkor samtidigt**:

1. **Överskott**: Du producerar minst **0.5 kWh mer** än du förbrukar (per 15 min)
2. **Positivt pris**: Spotpriset är minst 0.01 kr/kWh (inga negativa priser)
3. **Lönsamt pris**: En av två nivåer:
   - **Optimal SELL**: Pris ≥ P70 (dyraste 30%)
   - **Surplus SELL**: Pris ≥ median OCH du har överskott ändå

#### Säkerhetsmekanismen: Molntäcksjustering

Vid **osäkra väderförhållanden** (>50% molntäcke) höjer systemet kravet för **Optimal SELL**:

```
Klart väder:     SELL vid pris ≥ P70 (t.ex. 1.22 kr/kWh)
Molnigt väder:   SELL vid pris ≥ P70 × 1.15 (t.ex. 1.41 kr/kWh)
```

**Varför?**
- Molnig väderprognos = osäker produktion
- Om molnen plötsligt tjocknar kan produktionen rasa
- Genom att kräva högre pris skyddas du från att sälja precis innan produktionen sjunker
- **Surplus SELL påverkas inte** - om priset är ≥ median är det alltid bättre att sälja än att spilla el

**Exempel:**
```
[12:30] SELL (optimal) → 5.2 kWh överskott @ 1.68 kr/kWh (klar himmel 15%)
[13:00] SELL (optimal) → 3.1 kWh överskott @ 1.42 kr/kWh (molnigt 65% — höjd tröskel)
```

**Varför inga SELL-signaler vissa dagar?**

Ingen produktion = inget överskott = ingen SELL. Med 1500 m² paneler och 45 kWh/h konsumtion krävs:
- **Minst 200 W/m² solinstrålning** för break-even (produktion = konsumtion)
- Molniga dagar ger ofta bara 50-150 W/m² → konstant nettoimport

---

### AVOID-signal: Undvik förbrukning när det är dyrt

**När aktiveras AVOID?**
- Priset är bland de **dyraste 30%** de kommande 48 timmarna
- **OCH** priset är minst 8% högre än median

**Exempel:**
```
[17:45] AVOID → 1.755 kr/kWh ≥ 1.223 threshold (161% av median)
[18:00] AVOID → 1.760 kr/kWh ≥ 1.223 threshold (161% av median)
```

---

## Del 2: LoadScheduler - Smart schemaläggning

LoadScheduler optimerar **flexibla laster** - laster som kan startas när som helst inom en deadline:
- Elbilsladdning (måste vara klar kl 07:00 imorgon)
- Tvättmaskin (kan köras när som helst inatt)
- HVAC-förkylning (måste vara klar innan värmebölja)
- Batteriuppladdning från nät

### Algoritmen i fyra steg:

**Exempel:** Klockan är 08:00. Du vill ladda elbilen (4 timmar, 50 kW) och den måste vara klar senast kl 18:00 idag.

#### Steg 1: Hitta alla möjliga tidsfönster

LoadScheduler skannar de kommande 48 timmarna **med 15-minuters precision** och hittar alla fönster där:
- Lasten **hinner bli klar** innan deadline (18:00)
- Fönstret inte redan har passerat (efter 08:00)

**Systemet utvärderar varje 15-minutersperiod:**
- 08:00 → klar 12:00 ✅
- 08:15 → klar 12:15 ✅
- 08:30 → klar 12:30 ✅
- ... (totalt 40 möjliga starttider)
- 13:45 → klar 17:45 ✅
- 14:00 → klar 18:00 ✅ (sista möjliga)
- 14:15 → klar 18:15 ❌ (efter deadline - blockeras)

**Detta ger extremt finmaskig optimering** - systemet kan hitta det billigaste 4-timmarsfönstret oavsett om det börjar på hel, halv eller kvart!

#### Steg 2: Beräkna verklig kostnad per fönster

För varje giltigt fönster beräknas:
```
Total kostnad = Σ (nätimport × totalkostnad)

där nätimport = (lastens effekt - solproduktion)
```

**Kostnadsberäkning för elbilsexemplet (200 kWh totalt):**

Systemet beräknar kostnaden för **varje möjlig starttid**. Här är några exempel:

| Starttid | Solproduktion | Nätimport | Genomsnittspris | Total kostnad |
|----------|---------------|-----------|-----------------|---------------|
| **08:00** | 20 kWh | 180 kWh | 1.05 kr/kWh | **189 kr** |
| **08:45** | 35 kWh | 165 kWh | 1.03 kr/kWh | **170 kr** |
| **11:00** | 80 kWh | 120 kWh | 1.10 kr/kWh | **132 kr** |
| **11:30** | 75 kWh | 125 kWh | 1.08 kr/kWh | **135 kr** |
| **13:45** | 65 kWh | 135 kWh | 1.12 kr/kWh | **151 kr** |
| **14:00** | 60 kWh | 140 kWh | 1.15 kr/kWh | **161 kr** |

**Notera:** Även 15-minuters skillnad kan ge olika kostnad pga varierande spotpriser och solproduktion mellan kvartalen!

#### Steg 3: Applicera praktikalitetsfaktor

**Här blir det smart!** Systemet väger inte bara **kostnad** utan också **bekvämlighet**:

| Tid | Praktikalitetsfaktor | Varför? |
|-----|----------------------|---------|
| **17:00-22:00** | **1.5×** | Du är hemma och kan övervaka laddningen |
| **22:00-07:00** | **1.0×** | Natt - perfekt för timer-baserade laster |
| **07:00-17:00** | **0.5×** | Du är troligen borta - sämre för laster som kräver tillsyn |

**Praktikalitetspoäng = Verklig kostnad ÷ Faktor**

**Exempel med deadline 07:00 nästa morgon:**

Systemet utvärderar ~70 möjliga starttider. Här är utvalda exempel:

| Starttid | Verklig kostnad | Praktikalitetsfaktor | Praktikalitetspoäng | Resultat |
|----------|-----------------|----------------------|---------------------|----------|
| **11:00** (idag middag) | 132 kr | 0.5× (dagtid) | 132 / 0.5 = **264** | ❌ |
| **19:45** (idag kväll) | 147 kr | 1.5× (kväll) | 147 / 1.5 = **98** | ⭐ |
| **20:00** (idag kväll) | 145 kr | 1.5× (kväll) | 145 / 1.5 = **97** | ✅ **VINNER!** |
| **20:15** (idag kväll) | 146 kr | 1.5× (kväll) | 146 / 1.5 = **97.3** | ⭐ |
| **01:00** (natt) | 135 kr | 1.0× (natt) | 135 / 1.0 = **135** | ❌ |
| **01:30** (natt) | 133 kr | 1.0× (natt) | 133 / 1.0 = **133** | ❌ |

**Systemet väljer 20:00** - optimal balans mellan kostnad och praktikalitet. Trots att det kostar 10 kr mer än natten får du starta laddningen när du är hemma!

#### Steg 4: Beräkna besparingar

Systemet jämför det valda fönstret med **"starta nu"**-referensen:

```
Besparingar = Kostnad "starta nu" - Kostnad valt fönster
```

**Exempel från loggarna:**
```
LoadScheduler: Best window start=20:00 cost=145.00 SEK savings=44.00 SEK
```

---

## Tekniska specifikationer

**Dataupplösning:** 15 minuter (192 kvartal per 48h-prognos)

**Priströsklar (Compute-algoritmen):**
- BUY: P33 (billigaste 33%) med minst 8% rabatt vs median
- SELL (optimal): P70 (dyraste 30%), höjs till P85 vid >50% molntäcke
- SELL (surplus): Medianpris + överskott ≥ 0.5 kWh
- AVOID: P70 (dyraste 30%) med minst 8% premie vs median

**Solpanelsmodell:**
- Standard: IEC 61724 (kristallint kisel)
- Verklig verkningsgrad: 75% (inkl. ledningar, växelriktare, förluster)
- Temperaturkompensation: -0.45% per °C över 25°C
- Vindkylning: 4% per m/s vindstyrka

**Tariffer (Sverige 2024):**
- Energiskatt: 0.40 kr/kWh
- Moms: 25%
- Nättariffer: Konfigurerbara per användare (låg/normal/hög)

**Praktikalitetsfaktorer (LoadScheduler):**
- Kväll (17-22): 1.5× (bäst)
- Natt (22-07): 1.0× (standard)
- Dag (07-17): 0.5× (sämst)

**Datakällor:**
- Prisdata: Nordpool via elprisetjustnu.se API (uppdatering varje timme, 48h täckning)
- Väderdata: Open-Meteo ECMWF-modeller (uppdatering var 15:e minut, 48h täckning)

**Transparent loggning:**

Varje beräkning loggas med full spårbarhet:
```
Compute: Price range: 0.832 - 1.979 kr/kWh (spread: 138%)
Compute: BUY threshold:   ≤ 1.003 kr/kWh (cheapest 25 quarters ≈ 6.2h)
Compute: MEDIAN price:      1.090 kr/kWh
Compute: AVOID threshold: ≥ 1.223 kr/kWh (most expensive 37 quarters ≈ 9.2h)
```

Du kan alltid verifiera **varför** systemet rekommenderade en viss åtgärd eller valde en specifik starttid.
