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

**Exempel från dagens priser:**
- Billigast: 0.83 kr/kWh (natt)
- Median: 1.09 kr/kWh
- **BUY-tröskel**: 1.00 kr/kWh (billigaste 33%)
- **AVOID-tröskel**: 1.22 kr/kWh (dyraste 30%)
- Dyrast: 1.98 kr/kWh (kvällstopp)

#### 3. Kvalitetskontroll
För att undvika falska signaler kräver systemet **minst 8% skillnad från median**:
- Om priset varierar lite (platt kurva) → inga signaler
- Om priset varierar mycket → tydligare BUY/AVOID-zoner

---

## De tre signalerna

### BUY - Köp från nätet när det är billigt

**När aktiveras BUY?**
- Priset är bland de **billigaste 33%** de kommande 48 timmarna
- **ELLER** spotpriset är negativt (du får betalt för att förbruka el!)

**Vad betyder det?**
- Perfekt tid att ladda batterier
- Kör energikrävande maskiner (tvätt, torktumlare)
- Ladda elbilar
- Värm upp huset (värmepumpar)

**Exempel från loggarna:**
```
[00:00] BUY → 0.857 kr/kWh ≤ 1.003 threshold (79% av median)
[00:15] BUY → 0.854 kr/kWh ≤ 1.003 threshold (78% av median)
```
**Du sparar 21-22% jämfört med medianpriset!**

---

### SELL - Sälj överskottsel när det lönar sig

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
- **Du säljer fortfarande** - men bara när priset verkligen motiverar risken

**Surplus SELL påverkas inte** - om du redan har överskott och priset är ≥ median är det alltid bättre att sälja än att spilla el.

**Exempel (soligt väder):**
```
[12:30] SELL (optimal) → 5.2 kWh överskott @ 1.68 kr/kWh (klar himmel 15%)
```

**Exempel (molnigt väder):**
```
[13:00] SELL (optimal) → 3.1 kWh överskott @ 1.42 kr/kWh (molnigt 65% — höjd tröskel 1.41)
```

#### Varför syns inga SELL-signaler molniga dagar?

**Det enklaste svaret:** Ingen produktion = inget överskott = ingen SELL.

Med din konfiguration (1500 m² paneler, 45 kWh/h konsumtion) krävs:
- **Minst 200 W/m² solinstrålning** för att nå break-even (produktion = konsumtion)
- Molniga dagar ger ofta bara 50-150 W/m²
- Resultat: Du importerar el istället för att exportera

**Molntäckslogiken är en bonus** - den höjer kvalitetskravet när du HAR överskott men vädret är ostadigt. Men om du inte har överskott från första början, spelar tröskeln ingen roll.

---

### AVOID - Undvik förbrukning när det är dyrt

**När aktiveras AVOID?**
- Priset är bland de **dyraste 30%** de kommande 48 timmarna
- **OCH** priset är minst 8% högre än median

**Vad betyder det?**
- Skjut upp tvättmaskiner och diskmaskiner
- Undvik att ladda elbilar nu
- Vänta med värmepumpar om möjligt
- Använd batteri istället för nät (om du har batterilager)

**Exempel från loggarna:**
```
[17:45] AVOID → 1.755 kr/kWh ≥ 1.223 threshold (161% av median)
[18:00] AVOID → 1.760 kr/kWh ≥ 1.223 threshold (161% av median)
```
**Du skulle betala 61% mer än medianpriset!**

---

## LoadScheduler: Smart schemaläggning av flexibla laster

LoadScheduler tar dina energirekommendationer ett steg längre - från **manuella signaler** till **automatisk optimering**.

### Vad är en flexibel last?

En last som kan startas när som helst **inom en deadline**:
- Elbilsladdning (måste vara klar kl 07:00 imorgon)
- Tvättmaskin (kan köras när som helst inatt)
- HVAC-förkylning (måste vara klar innan värmebölja)
- Batteriuppladdning från nät

### Hur fungerar det?

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

Detta betyder att den **billigaste** kostnaden inte alltid vinner:

**För vårt elbilsexempel:**

| Starttid | Verklig kostnad | Praktikalitetsfaktor | Praktikalitetspoäng | Resultat |
|----------|-----------------|----------------------|---------------------|----------|
| **08:00** (morgon) | 189 kr | 0.5× (dagtid) | 189 / 0.5 = **378** | ❌ Sämst |
| **11:00** (middag) | 132 kr | 0.5× (dagtid) | 132 / 0.5 = **264** | ❌ |
| **14:00** (eftermiddag) | 161 kr | 0.5× (dagtid) | 161 / 0.5 = **322** | ❌ |

**Men vänta!** Om vi ändrar deadline till **07:00 nästa morgon** istället, får vi många fler alternativ (totalt ~70 möjliga starttider!).

Här är några utvalda exempel från beräkningen:

| Starttid | Verklig kostnad | Praktikalitetsfaktor | Praktikalitetspoäng | Resultat |
|----------|-----------------|----------------------|---------------------|----------|
| **11:00** (idag middag) | 132 kr | 0.5× (dagtid) | 132 / 0.5 = **264** | ❌ |
| **19:45** (idag kväll) | 147 kr | 1.5× (kväll) | 147 / 1.5 = **98** | ⭐ |
| **20:00** (idag kväll) | 145 kr | 1.5× (kväll) | 145 / 1.5 = **97** | ✅ **VINNER!** |
| **20:15** (idag kväll) | 146 kr | 1.5× (kväll) | 146 / 1.5 = **97.3** | ⭐ |
| **01:00** (natt) | 135 kr | 1.0× (natt) | 135 / 1.0 = **135** | ❌ |
| **01:30** (natt) | 133 kr | 1.0× (natt) | 133 / 1.0 = **133** | ❌ |

**Systemet väljer 20:00** - det optimala balansen mellan kostnad och praktikalitet. Trots att det kostar 10 kr mer än natten får du starta laddningen när du är hemma!

#### Steg 4: Beräkna besparingar

Systemet jämför det valda fönstret med **"starta nu"**-referensen:

```
Besparingar = Kostnad "starta nu" - Kostnad valt fönster
```

**Riktigt exempel från loggarna:**
```
LoadScheduler: Best window start=01:00 cost=170.23 SEK savings=45.12 SEK
```

**Du sparade 45 kr genom att vänta till natten!**

---

## Integrationen: Signaler + Schemaläggning

GridGuard ger dig **två nivåer av kontroll**:

### Nivå 1: Manuell styrning med signaler

**Perfekt för:**
- Ad-hoc beslut ("ska jag sätta på torktumlaren nu?")
- Realtidsoptimering ("priset sjunker om 15 min - vänta!")
- Lära sig prisets mönster

**Du får:**
- BUY/SELL/AVOID-rekommendationer var 15:e minut
- Visuell översikt över kommande 48 timmar
- Motivering: "157% av median" eller "sparar 22%"

### Nivå 2: Automatisk optimering med LoadScheduler

**Perfekt för:**
- Återkommande laster (daglig elbilsladdning)
- Tidsflexibla processer (HVAC, poolpump)
- Maximera besparingar utan manuellt arbete

**Du får:**
- Automatiskt schemalagda starttider
- Garanterad färdigställning innan deadline
- Verklig kostnad och besparing i SEK
- Praktikalitet balanserad mot kostnad

---

## Säkerhet och tillförlitlighet

### Datakvalitet

**Prisdata:**
- Källa: Nordpool via elprisetjustnu.se API
- Uppdatering: Varje timme
- Täckning: 48 timmar (uppdateras 13:00 för nästa dag)
- Cachning: 3 timmar för stabilitet

**Väderdata:**
- Källa: Open-Meteo (ECMWF-modeller)
- Uppdatering: Var 15:e minut
- Täckning: 48 timmar med 15-minuters upplösning
- Cachning: 15 minuter

### Felhantering

**Inga prisdata?**
- Systemet väntar på nästa uppdatering
- Inga signaler skickas (bättre inget än fel signal)
- Loggar varning: "Insufficient valid quarters"

**Väderdata saknas?**
- Fortsätter med senast kända data
- Solproduktion sätts till 0 om för gammal
- SELL-signaler blockeras tills data är färsk

**Partiell datatäckning?**
```
Parser: Built forecast with 192 quarters (48.0 hours)
Parser: Price matching → 121 matched, 71 unmatched (63.0% coverage)
```
- Systemet arbetar med tillgänglig data
- Oklara perioder markeras som IDLE
- Kvalitetskontroll: minst 4 kvartal krävs för analys

### Transparent loggning

Varje beräkning loggas med full spårbarhet:
```
Compute: Price range: 0.832 - 1.979 kr/kWh (spread: 138%)
Compute: BUY threshold:   ≤ 1.003 kr/kWh (cheapest 25 quarters ≈ 6.2h)
Compute: MEDIAN price:      1.090 kr/kWh
Compute: AVOID threshold: ≥ 1.223 kr/kWh (most expensive 37 quarters ≈ 9.2h)
```

Du kan alltid verifiera **varför** systemet rekommenderade en viss åtgärd.

---

## Praktiska exempel

### Scenario 1: Typisk vinterdag (januari)

**Förutsättningar:**
- Litet hushåll i Stockholm (SE3)
- 20 m² solpaneler (liten produktion vintertid)
- 6 kWh/h konsumtion
- Spotpris: 0.50-2.20 kr/kWh (stor variation)

**Systemets analys:**
```
06:00-08:00: BUY     (0.60 kr, 55% av median - morgonladdning av elbil)
12:00-14:00: IDLE    (0.95 kr, nära median - normal förbrukning ok)
17:00-20:00: AVOID  (2.10 kr, 192% av median - vänta med tvätt!)
23:00-05:00: BUY     (0.52 kr, 48% av median - diskmaskinen startar)
```

**LoadScheduler:**
- Elbilsladdning (3h, 11 kW): Schemalagd 23:30-02:30
  - Kostnad: 51.48 kr
  - Besparing vs nu (17:00): 33.24 kr
  - Praktikalitet: Natt (1.0×) - ok, timer-baserad

### Scenario 2: Solig sommardag (juni)

**Förutsättningar:**
- Villa i Linköping (SE3)
- 1500 m² solpaneler (stor produktion)
- 45 kWh/h konsumtion
- Spotpris: 0.30-0.80 kr/kWh (mindre variation)

**Systemets analys:**
```
00:00-06:00: BUY     (0.31 kr, 72% av median - fyll batterier)
10:00-16:00: SELL    (4.2 kWh överskott @ 0.65 kr - optimal export)
17:00-19:00: IDLE    (nettoimport 2 kWh @ 0.68 kr - mindre än 0.5 kWh överskott)
20:00-23:00: BUY     (0.35 kr, 81% av median - ladda batteribank)
```

**LoadScheduler:**
- HVAC-förkylning (2h, 30 kW): Schemalagd 11:00-13:00
  - Kostnad: 14.20 kr (solproduktion täcker 80%)
  - Besparing vs nu (09:00): 8.15 kr
  - Praktikalitet: Dag (0.5×) - men gratis solenergi kompenserar!
  - **Smart:** Systemet utnyttjar solproduktionen för kylning innan eftermiddagsvärmen

### Scenario 3: Molnig höstdag (dagens analys)

**Förutsättningar:**
- Saab Arena i Linköping
- 1500 m² solpaneler
- 45 kWh/h konsumtion
- Molntäcke: 65-85%
- Spotpris: 0.83-1.98 kr/kWh

**Systemets analys:**
```
17:45: AVOID  (1.755 kr, 161% av median - kvällstopp)
18:00: AVOID  (1.760 kr, 161% av median - fortsatt dyrt)
00:00: BUY    (0.857 kr, 79% av median - natten billigast)
12:00: IDLE   (1.12 kr, 103% av median - molnig, litet överskott)
```

**Varför inga SELL-signaler?**
- Solinstrålning: 100-150 W/m² (molnigt)
- Produktion: ~8-11 kWh per 15 min
- Konsumtion: 11.25 kWh per 15 min
- **Resultat:** Konstant nettoimport, inget överskott att sälja

**Molntäckslogiken spelar ingen roll här** - du har inget överskott från början!

**LoadScheduler:**
- EV-flottan (4h, 50 kW): Schemalagd 00:00-04:00
  - Kostnad: 170.23 kr
  - Besparing: 45.12 kr vs kvällsstart
  - Praktikalitet: Natt (1.0×) - perfekt för flottladdning

---

## Sammanfattning: Varför GridGuard är tillförlitligt

### Adaptivt, inte statiskt
- Inga hårdkodade prisgränser som blir obsoleta
- Anpassar sig automatiskt till marknadens volatilitet
- Fungerar lika bra vid 0.50 kr som vid 5.00 kr

### Datadrivet, inte gissningar
- 192 timmars faktiska prognosdata
- Percentilbaserad analys för objektiva trösklar
- Kvalitetskontroll: minst 8% skillnad krävs

### Säkert vid osäkerhet
- Molntäcksjustering för SELL-signaler
- Ingen export vid negativa priser
- Transparent loggning av alla beslut

### Praktiskt användbart
- LoadScheduler balanserar kostnad mot bekvämlighet
- 15-minuters upplösning för precision
- Verkliga kostnader och besparingar i SEK

### Transparent och verifierbart
- Alla trösklar loggas med motivation
- Du ser alltid "% av median"
- Alla beräkningar är spårbara

---

## Tekniska specifikationer

**Dataupplösning:** 15 minuter (192 kvartal per 48h-prognos)

**Priströsklar:**
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

---

**GridGuard - Intelligent energioptimering som lär sig dina behov och sparar dina pengar, dag för dag.**
