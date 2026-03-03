# GridGuard Kundnytta - Utan Batteri-kännedom

**Datum:** 2026-03-03
**Designprincip:** GridGuard ska ALDRIG ha kännedom om kundens batteri
**Fokus:** Maximera kundnytta genom smart scheduling av flexibla laster

---

## Varför INTE batteri-kännedom?

### Designrationalisering

**Privacy-first approach:**
- ✅ Kunden behåller full kontroll över sitt batteri
- ✅ Inget behov av att dela känslig batteridata (SOC, degradation, kapacitet)
- ✅ Enklare GDPR-compliance (mindre persondata)
- ✅ Produkten fungerar lika bra för kunder MED och UTAN batteri

**Teknisk enkelhet:**
- ✅ Mindre komplexitet i algoritmen
- ✅ Inga edge cases för olika batterityper/storlekar
- ✅ Fokus på det vi KAN påverka (scheduling av laster)

**Marknadspositionering:**
- GridGuard = "Smart scheduling platform", INTE "Battery management system"
- Kompletterar befintliga BMS-system (Tesla Powerwall, Sonnen, etc.)
- Bredare målgrupp (alla med flexibla laster, inte bara de med batteri)

---

## Vad GridGuard GÖR istället

### 1. Smart Load Scheduling (Kärnfunktionalitet)

**Flexibla laster vi optimerar:**

| Last | Typisk effekt | Varaktighet | Flexibilitet | Årsbesparing |
|------|---------------|-------------|--------------|--------------|
| **EV-laddning** | 3.6-11 kW | 4-12h | Hög (natt-morgon) | 800-1500 kr/år |
| **Värmepumpa** | 2-5 kW | 2-6h | Medel (förvärm vatten) | 500-800 kr/år |
| **Varmvattenberedare** | 2-3 kW | 1-3h | Hög (kan vänta 12h) | 300-500 kr/år |
| **Diskmaskin** | 1.5-2 kW | 2h | Hög (kan köras nattetid) | 200-300 kr/år |
| **Tvättmaskin** | 1.5-2.5 kW | 1.5h | Medel | 150-250 kr/år |
| **Torktumlare** | 2-3 kW | 1-2h | Medel | 200-300 kr/år |
| **Pool-pump** | 0.5-1.5 kW | 4-8h | Hög (kan delas upp) | 400-600 kr/år |

**Total potential besparing:** 2,550-4,250 kr/år för en typisk villa med flera flexibla laster

---

### 2. Konkret exempel: EV-laddning

**Scenario:** Tesla Model 3, behöver 40 kWh laddning senast 07:00 nästa morgon

**Utan GridGuard (ladda direkt kl 18:00):**
```
Tid      Spotpris  Grid  Tax   VAT   Total    kWh   Kostnad
18:00    1.80      0.45  0.40  25%   3.31     5     16.56 kr
19:00    2.20      0.45  0.40  25%   3.81     5     19.06 kr
20:00    2.50      0.45  0.40  25%   4.19     5     20.94 kr
21:00    2.30      0.45  0.40  25%   3.94     5     19.69 kr
22:00    1.90      0.45  0.40  25%   3.44     5     17.19 kr
23:00    1.50      0.45  0.40  25%   2.94     5     14.69 kr
00:00    1.20      0.25  0.40  25%   2.31     5     11.56 kr
01:00    0.90      0.25  0.40  25%   1.94     5      9.69 kr
---
TOTALT: 40 kWh × genomsnitt 3.23 kr/kWh = 129.20 kr
```

**Med GridGuard (optimal scheduling):**
```
GridGuard analyserar priser + grid fees + timing constraints
→ Hittar billigaste 8-timmars fönster som slutar innan 07:00

Optimal start: 23:00 (billiga natttimmar + låg grid fee)

Tid      Spotpris  Grid  Tax   VAT   Total    kWh   Kostnad
23:00    1.50      0.25  0.40  25%   2.69     5     13.44 kr
00:00    1.20      0.25  0.40  25%   2.31     5     11.56 kr
01:00    0.90      0.25  0.40  25%   1.94     5      9.69 kr
02:00    0.70      0.25  0.40  25%   1.69     5      8.44 kr
03:00    0.60      0.25  0.40  25%   1.56     5      7.81 kr
04:00    0.65      0.25  0.40  25%   1.63     5      8.13 kr
05:00    0.80      0.25  0.40  25%   1.81     5      9.06 kr
06:00    1.00      0.25  0.40  25%   2.06     5     10.31 kr
---
TOTALT: 40 kWh × genomsnitt 1.96 kr/kWh = 78.44 kr

BESPARING: 129.20 - 78.44 = 50.76 kr per laddning
Vid 3 laddningar/vecka: 50.76 × 156 = 7,918 kr/år! 🎯
```

**Key insights:**
1. **Grid fee-optimering:** Undviker peak hours (17-23) med 0.45 kr/kWh
2. **Spot price arbitrage:** Laddar vid nattetid (0.60-1.20 kr/kWh vs 1.80-2.50)
3. **Deadline-aware:** Garanterar full laddning till 07:00
4. **Automatisk:** Användaren behöver bara sätta "deadline" - algoritmen gör resten

---

### 3. Multi-Load Optimization

**Scenario:** Hushåll med flera laster samma kväll

**Input till GridGuard:**
```json
POST /api/optimize
{
  "userId": "user123",
  "loads": [
    {
      "id": "ev_charging",
      "power_kw": 5.0,
      "duration_minutes": 480,
      "deadline": "2026-03-04T07:00:00Z",
      "priority": "high"
    },
    {
      "id": "water_heater",
      "power_kw": 2.5,
      "duration_minutes": 120,
      "deadline": "2026-03-04T06:00:00Z",
      "priority": "medium"
    },
    {
      "id": "dishwasher",
      "power_kw": 1.8,
      "duration_minutes": 120,
      "deadline": "2026-03-04T08:00:00Z",
      "priority": "low"
    }
  ]
}
```

**GridGuard's optimization:**
```
Analyserar:
  - 96-timmars prisprognoser
  - Grid fee time-of-use tariffs
  - Overlapping constraints
  - Total capacity (max 11 kW från elnätet)

Resultat:
┌─────────────────────────────────────────────────┐
│ Timeline (kl 18:00 → 08:00)                     │
├─────────────────────────────────────────────────┤
│ 18:00-23:00  [ingen schemalagd last]            │
│              (dyra peak hours - undvik!)        │
│                                                  │
│ 23:00-01:00  [Varmvatten 2.5kW] ████            │
│              Kostnad: 5.38 kr                    │
│              Besparing: 3.12 kr vs 18:00        │
│                                                  │
│ 01:00-03:00  [Diskmaskin 1.8kW] ███             │
│              Kostnad: 6.98 kr                    │
│              Besparing: 4.22 kr vs 20:00        │
│                                                  │
│ 23:00-07:00  [EV-laddning 5kW]  ████████        │
│              Kostnad: 78.44 kr                   │
│              Besparing: 50.76 kr vs 18:00       │
│                                                  │
│ 07:00-08:00  [morgonförbrukning]                │
└─────────────────────────────────────────────────┘

SAMMANFATTNING:
  Total kostnad:     90.80 kr
  Utan optimering:  148.90 kr
  TOTAL BESPARING:   58.10 kr (39% billigare!)
```

**Värde för kunden:**
- ✅ **Automatisk koordinering:** GridGuard hanterar överlapp och kapacitetsgränser
- ✅ **Garanterad completion:** Alla laster klara innan sina deadlines
- ✅ **Transparent:** Visar exakt vad som körs när och varför
- ✅ **Set-and-forget:** Användaren sätter bara deadlines, resten är automatiskt

---

### 4. Solar Export Optimization (UTAN batteri)

**Hur det fungerar utan batteri-kännedom:**

**Scenario:** Solig sommardag, 20 m² paneler (18% efficiency)

```
Kl 12:00:
  Solproduktion:    4.2 kWh
  Basförbrukning:   0.5 kWh
  Överskott:        3.7 kWh
  Spotpris:         0.80 kr/kWh (positivt)

GridGuards beslut: SELL (exportera till grid)
  → Export: 3.7 kWh × 0.80 kr/kWh = 2.96 kr intäkt

Kl 14:00:
  Solproduktion:    4.5 kWh
  Basförbrukning:   0.5 kWh
  Överskott:        4.0 kWh
  Spotpris:         -0.20 kr/kWh (NEGATIVT!) ⚠️

GridGuards beslut: IDLE (exportera INTE)
  → Sparar: 4.0 kWh × 0.20 kr/kWh = 0.80 kr (undviker kostnad)

  Men vad göra med överskottet?
  → GridGuard ger BUY-signal för flexibla laster!
  → "Kör diskmaskin NU (gratis el från solpaneler)"
  → "Förvärm varmvatten NU"
  → Användaren manuellt startar laster
```

**Key insight:** Även utan batteri kan vi **maximera egenförbrukning** vid negativt pris!

**Värde:**
- ✅ Undviker att BETALA för export (negativt pris = du betalar grid operator)
- ✅ Guidar användaren att konsumera överskott istället
- ✅ Proaktiva notifikationer: "Gratis el tillgänglig nu!"

---

### 5. Price Alerts & Recommendations

**Typ 1: High Price Warning**
```
🔴 VARNING: Extremt höga elpriser kl 18-20 idag!

Prognos:
  kl 18:00  3.45 kr/kWh (172% över medel)
  kl 19:00  3.80 kr/kWh (190% över medel)
  kl 20:00  3.25 kr/kWh (162% över medel)

Rekommendation:
  ❌ Undvik onödig förbrukning (torktumlare, elvärme)
  ✅ Vänta till 23:00 (1.20 kr/kWh, -40% vs medel)
  💰 Potential besparing: 15-30 kr genom att vänta
```

**Typ 2: Optimal Load Signal**
```
🟢 BILLIGA TIMMAR INATT!

Bästa tiderna för EV-laddning:
  1. kl 03:00-04:00  0.60 kr/kWh ⭐⭐⭐⭐⭐
  2. kl 04:00-05:00  0.65 kr/kWh ⭐⭐⭐⭐
  3. kl 02:00-03:00  0.70 kr/kWh ⭐⭐⭐⭐

Potential besparing vs charging nu (18:00):
  40 kWh × (3.31 - 1.69) kr/kWh = 64.80 kr

[Schemalägg EV-laddning 03:00] [Ignorera]
```

**Typ 3: Solar Surplus Alert**
```
☀️ SOLÖVERSKOTT TILLGÄNGLIGT!

Just nu (13:00):
  Produktion:   4.8 kWh
  Förbrukning:  0.6 kWh
  Överskott:    4.2 kWh

Spotpris: -0.15 kr/kWh (NEGATIVT!)
GridGuards råd: Exportera INTE, använd själv istället!

Förslag:
  ✅ Kör diskmaskin (1.8 kW) - GRATIS el
  ✅ Förvärm varmvatten (2.5 kW) - GRATIS el
  ✅ Ladda EV några timmar (3.6 kW) - GRATIS el

Spara: 4.2 kWh × 0.15 kr/kWh = 0.63 kr genom att INTE exportera
```

---

### 6. Historical Insights & ROI Tracking

**Monthly Summary (visar verklig besparing):**

```
═══════════════════════════════════════════════════════
        FEBRUARI 2026 - DIN ENERGIRAPPORT
═══════════════════════════════════════════════════════

📊 ÖVERSIKT
───────────────────────────────────────────────────────
  Total förbrukning:        450 kWh
  Grid import:               85 kWh (19%)
  Solproduktion:            380 kWh
  Egenförbrukning:          365 kWh (96%)
  Grid export:               15 kWh (4%)

💰 EKONOMI
───────────────────────────────────────────────────────
  Faktisk kostnad:         247.50 kr
  Utan GridGuard:          612.30 kr (estimat)
  TOTAL BESPARING:         364.80 kr

  Breakdown:
    - Smart EV-laddning:    203.04 kr
    - Optimal export-timing: 87.60 kr
    - Varmvatten-scheduling: 52.16 kr
    - Undvikit negativt pris: 22.00 kr

📈 TRENDER
───────────────────────────────────────────────────────
  vs Januari 2026:  +12% besparing (bättre algoritm)
  vs Februari 2025: +24% besparing (mer sol, bättre pris)

🏆 BÄSTA BESLUT
───────────────────────────────────────────────────────
  1. EV-laddning 2026-02-15 03:00
     Besparing: 67.20 kr vs peak hour

  2. Varmvatten förvärm 2026-02-22 14:00
     Utnyttjade solöverskott vid negativt pris
     Sparade: 12.50 kr

  3. Diskmaskin nattkörning 2026-02-08 02:00
     Spotpris: 0.42 kr/kWh (-73% vs peak)
     Besparing: 8.94 kr

═══════════════════════════════════════════════════════
💡 GridGuard har sparat dig 364.80 kr denna månad!
   Årsprognos: 4,377 kr/år i besparingar
═══════════════════════════════════════════════════════
```

**Värde:**
- ✅ **Kvantifierbar ROI:** Kunden ser EXAKT hur mycket de sparar
- ✅ **Behavioral insights:** Vilka beslut var bäst?
- ✅ **Trend analysis:** Förbättras optimering över tid?
- ✅ **Motivation:** Konkreta bevis att systemet fungerar

---

## 7. Teknisk Implementation (utan batteri)

### 7.1 Reviderade TODOs

**TODO 1: Panel tilt/azimuth (Compute.c:255)**
- Status: Optional (kan skippa för MVP)
- Impact: ±20-30% accuracy för icke-optimala installationer
- Recommendation: Dokumentera som "known limitation"

**TODO 2: Batteri vid negativt pris (Compute.c:284)**
- ~~Status: Skippa helt~~ → **ERSÄTT MED:**
- **NYA TODO: Egen-förbruknings-rekommendation vid negativt pris**

```c
// Current (Compute.c:284):
if (netKwh > SOLAR_SURPLUS_MIN_KWH) {
    if (fc->spotPriceSek >= 0.0)
        action = ACTION_SELL_TO_GRID;
    else
        action = ACTION_IDLE;  // TODO: if battery present, charge
}

// NEW APPROACH (utan batteri):
if (netKwh > SOLAR_SURPLUS_MIN_KWH) {
    if (fc->spotPriceSek >= 0.0)
        action = ACTION_SELL_TO_GRID;
    else {
        // Negativt pris: Rekommendera egenförbrukning istället
        action = ACTION_USE_SURPLUS;  // NEW enum value!

        // Log recommendation för customer notification
        LOG_INFO("SURPLUS ALERT: %.2f kWh available at negative price %.2f kr/kWh",
                 netKwh, fc->spotPriceSek);
    }
}
```

**Ny enum:**
```c
typedef enum {
    ACTION_BUY_FROM_GRID,    // Cheap hour - run flexible loads
    ACTION_SELL_TO_GRID,     // Solar surplus + positive price → export
    ACTION_USE_SURPLUS,      // Solar surplus + NEGATIVE price → self-consume! ← NEW
    ACTION_IDLE              // Normal hour
} EnergyAction;
```

**Output till kund:**
```json
{
  "time": "2026-03-03T14:00:00Z",
  "signal": "USE_SURPLUS",
  "surplus_kwh": 4.2,
  "spot_price_sek_kwh": -0.15,
  "recommendation": "Don't export! Use this free solar energy for flexible loads (dishwasher, water heater, EV charging)",
  "potential_savings_sek": 0.63
}
```

**TODO 3: BUY-viktning (Compute.c:292)** ⭐ **CRITICAL!**
- Status: MUST FIX
- Implementation: Se sektion 7.2

---

### 7.2 BUY-viktning med Flexible Load Capacity

**Implementation:**

```c
// Step 1: Add to UserConfig.h
typedef struct {
    // ... existing fields ...

    // NEW: Flexible load configuration
    double maxFlexibleLoadKwh;    // Total daily flexible load capacity (kWh/day)
                                   // Ex: 35 kWh = 40 kWh EV + 5 kWh other loads
} UserConfig;

// Step 2: Add to Database migration
ALTER TABLE user_configs ADD COLUMN max_flexible_load_kwh REAL DEFAULT 35.0;

// Step 3: Track scheduled loads per hour
typedef struct {
    time_t hour;
    double scheduledKwh;
} HourlySchedule;

// Fetch from ScheduleDB
double ScheduleDB_GetScheduledLoadForHour(const char *userId, time_t hour)
{
    // Query schedules table:
    // SELECT SUM(power_kw) FROM schedules
    // WHERE user_id = ? AND scheduled_start <= ? AND scheduled_start + duration > ?
    // GROUP BY user_id
}

// Step 4: Update decision logic in Compute.c
static EnergyAction ComputeEnergyAction(
    const ForecastEntry *fc,
    const SolarProduction *solar,
    double consumptionKwh,
    double cost,
    double buyThreshold,
    const char *userId,           // NEW param
    double maxFlexibleLoadKwh)    // NEW param
{
    double netKwh = solar->productionKwh - consumptionKwh;

    // SELL logic (unchanged)
    if (netKwh > SOLAR_SURPLUS_MIN_KWH) {
        if (fc->spotPriceSek >= 0.0)
            return ACTION_SELL_TO_GRID;
        else
            return ACTION_USE_SURPLUS;  // NEW!
    }

    // BUY logic (IMPROVED with capacity check)
    if (cost <= buyThreshold) {
        // Check scheduled loads for this hour
        double scheduledThisHour = ScheduleDB_GetScheduledLoadForHour(userId, fc->timestamp);

        // Calculate available capacity (assuming even distribution over 24h)
        double dailyBudgetPerHour = maxFlexibleLoadKwh / 24.0;  // Ex: 35/24 = 1.46 kWh/h
        double availableCapacity = dailyBudgetPerHour - scheduledThisHour;

        if (availableCapacity > 0.5) {  // At least 0.5 kWh capacity left
            LOG_DEBUG("BUY signal: %.2f kWh capacity available at %.2f kr/kWh",
                      availableCapacity, cost);
            return ACTION_BUY_FROM_GRID;
        } else {
            LOG_DEBUG("BUY signal suppressed: only %.2f kWh capacity left",
                      availableCapacity);
            return ACTION_IDLE;  // Capacity exhausted
        }
    }

    return ACTION_IDLE;
}
```

**Värde:**
- ✅ **Ingen alert fatigue:** Bara relevanta BUY-signaler
- ✅ **Actionable:** Visar hur mycket användaren KAN schemalägga
- ✅ **Adaptive:** Justerar automatiskt baserat på redan schemalagda laster

**Example output:**
```json
{
  "time": "2026-03-03T03:00:00Z",
  "signal": "BUY",
  "total_cost_sek_kwh": 1.69,
  "savings_vs_median_sek_kwh": 0.62,
  "available_capacity_kwh": 1.2,
  "recommendation": "Good time to charge EV or run flexible loads. You have 1.2 kWh capacity available."
}

vs

{
  "time": "2026-03-03T04:00:00Z",
  "signal": "IDLE",
  "total_cost_sek_kwh": 1.75,
  "savings_vs_median_sek_kwh": 0.56,
  "available_capacity_kwh": 0.2,
  "recommendation": "Price is good, but you've already scheduled most flexible loads. Consider this for manual loads only."
}
```

---

### 7.3 Notifieringssystem (Proaktiv kundnytta)

**Implementation:**

```c
// New service: NotificationService.c

typedef enum {
    NOTIFY_HIGH_PRICE_WARNING,   // "Avoid consumption 18-20 today"
    NOTIFY_LOW_PRICE_OPPORTUNITY, // "Charge EV tonight for 60 kr savings"
    NOTIFY_SURPLUS_AVAILABLE,     // "Use solar surplus now (negative price!)"
    NOTIFY_MONTHLY_SUMMARY        // "You saved 364 kr this month"
} NotificationType;

typedef struct {
    NotificationType type;
    time_t timestamp;
    char userId[128];
    char message[512];
    double potentialSavingsSek;
    bool dismissed;
} Notification;

// Example: High price warning
void NotificationService_CheckHighPrices(const EnergyData *plan, const char *userId)
{
    // Find hours with cost > 150% of median
    double threshold = plan->entries[0].savingsVsMedianSek * -0.5;  // Median + 50%

    for (int i = 0; i < plan->count; i++) {
        const EnergyDataEntry *e = &plan->entries[i];

        if (e->totalCostSek > (threshold * 1.5)) {
            // Create notification
            Notification notif;
            notif.type = NOTIFY_HIGH_PRICE_WARNING;
            notif.timestamp = e->timestamp;
            strncpy(notif.userId, userId, sizeof(notif.userId) - 1);

            snprintf(notif.message, sizeof(notif.message),
                     "⚠️ High price alert: %.2f kr/kWh at %s. Avoid non-essential loads.",
                     e->totalCostSek, format_time(e->timestamp));

            NotificationDB_Store(&notif);
        }
    }
}

// Example: Surplus alert (real-time)
void NotificationService_OnSurplusDetected(double surplusKwh, double spotPrice)
{
    if (spotPrice < 0.0) {  // Negative price
        Notification notif;
        notif.type = NOTIFY_SURPLUS_AVAILABLE;
        notif.timestamp = time(NULL);
        notif.potentialSavingsSek = surplusKwh * (-spotPrice);

        snprintf(notif.message, sizeof(notif.message),
                 "☀️ %.1f kWh solar surplus available! Spot price is NEGATIVE (%.2f kr/kWh). "
                 "Use this free energy now instead of exporting. Save %.2f kr!",
                 surplusKwh, spotPrice, notif.potentialSavingsSek);

        // Push notification (webhook, email, or poll via API)
        NotificationDB_Store(&notif);
    }
}
```

**API endpoint:**
```http
GET /api/notifications?userId=user123&unread=true

Response:
{
  "notifications": [
    {
      "id": "notif_abc123",
      "type": "surplus_available",
      "timestamp": "2026-03-03T14:23:00Z",
      "message": "☀️ 4.2 kWh solar surplus available! Spot price is NEGATIVE (-0.15 kr/kWh)...",
      "potential_savings_sek": 0.63,
      "dismissed": false
    },
    {
      "id": "notif_def456",
      "type": "low_price_opportunity",
      "timestamp": "2026-03-03T18:00:00Z",
      "message": "🟢 Cheap electricity tonight! Charge EV starting 03:00 and save 67 kr.",
      "potential_savings_sek": 67.20,
      "dismissed": false
    }
  ],
  "unread_count": 2
}
```

---

## 8. Kundnytta - Sammanfattning

### Kvantifierbar värde (årligen)

**För en typisk svensk villa med:**
- 20 m² solpaneler (3.6 kWp)
- Elbil (3 laddningar/vecka)
- Värmepump
- Normal hushållsförbrukning

**Utan GridGuard:**
```
Årlig elkostnad:       ~15,000 kr
Grid export intäkt:    ~1,200 kr
Netto:                 ~13,800 kr
```

**Med GridGuard:**
```
Besparingar:
  - Smart EV-laddning:      7,918 kr/år (3× 52.74 kr/vecka)
  - Optimal exportering:    1,200 kr/år (timing + undvik negativt pris)
  - Varmvatten/diskmaskin:    900 kr/år (night scheduling)
  - Peak hour avoidance:      800 kr/år (alerts + behavior change)
  - Pool pump optimization:   600 kr/år (run during solar hours)
  ─────────────────────────────────────
  TOTAL BESPARING:         11,418 kr/år

Årlig elkostnad:         ~2,382 kr
Grid export intäkt:      ~1,200 kr
Netto:                   ~1,182 kr

TOTAL FÖRBÄTTRING: 13,800 - 1,182 = 12,618 kr/år (91% reduction!)
```

### Icke-monetärt värde

**Convenience:**
- ✅ Set-and-forget scheduling (sätt deadline, glöm resten)
- ✅ Proaktiva notifikationer (ingen manual checking)
- ✅ Automatisk koordinering av flera laster

**Insights:**
- ✅ Förståelse av eget konsumtionsmönster
- ✅ ROI-tracking (ser att systemet funkar)
- ✅ Behavioral nudging (lär sig optimalt beteende)

**Peace of mind:**
- ✅ Aldrig mer "glömde ladda bilen" (garanterad completion)
- ✅ Aldrig mer oväntat hög elräkning (price alerts)
- ✅ Vet att de optimerar varje krona

---

## 9. Jämförelse med konkurrenter

| Feature | GridGuard | Tibber | Ferroamp | Tesla App |
|---------|-----------|--------|----------|-----------|
| **Smart EV scheduling** | ✅ Optimal | ⚠️ Basic | ✅ Good | ⚠️ Time-based only |
| **Multi-load coordination** | ✅ | ❌ | ⚠️ Limited | ❌ |
| **Grid fee optimization** | ✅ ToU-aware | ❌ Spot only | ✅ | ❌ |
| **Negative price handling** | ✅ | ⚠️ Limited | ❌ | ❌ |
| **Historical ROI tracking** | ✅ Detailed | ⚠️ Basic | ❌ | ⚠️ Basic |
| **Proactive alerts** | ✅ | ⚠️ Basic | ❌ | ❌ |
| **Batteri-oberoende** | ✅ | ❌ (kräver Tibber Pulse) | ❌ (kräver Ferroamp) | ❌ (kräver Powerwall) |
| **Privacy-first** | ✅ | ⚠️ | ⚠️ | ❌ |
| **Open API** | ✅ | ⚠️ Limited | ❌ | ❌ |

**GridGuards unika värde:**
- Fungerar UTAN proprietary hardware (Tibber Pulse, Ferroamp EnergyHub, Tesla Gateway)
- Privacy-first (ingen batteri-data, mindre dependencies)
- Multi-vendor compatibility (any EV, any solar inverter, any grid)
- Focus på ACTION (scheduling) inte bara insight (price display)

---

## 10. Rekommenderad Development Roadmap

### Phase 1: MVP (Vecka 1-2)

**Must-have:**
1. ✅ Fix BUY-viktning TODO (2h)
2. ✅ Implementera ACTION_USE_SURPLUS (1h)
3. ✅ Basic LoadScheduler integration (3h)
4. ✅ Monthly summary endpoint (3h)

**Total: 9 timmar**

### Phase 2: Enhanced UX (Vecka 3-4)

**Should-have:**
5. ✅ Notification service (4h)
6. ✅ Historical tracking DB schema (2h)
7. ✅ CSV export för tax reporting (2h)
8. ✅ Price alert thresholds (user-configurable) (2h)

**Total: 10 timmar**

### Phase 3: Polish (Vecka 5+)

**Nice-to-have:**
9. ⚠️ Panel tilt/azimuth (6h) - optional
10. ✅ Multi-load conflict resolution (3h)
11. ✅ Webhook notifications (2h)
12. ✅ Mobile app integration (out of scope for kursen)

---

## 11. Slutsats

**GridGuard levererar verklig kundnytta UTAN batteri-kännedom genom:**

1. **Smart Load Scheduling** (7,918 kr/år från bara EV)
2. **Grid Fee Optimization** (timing mot ToU-tariffer)
3. **Export Optimization** (undvik negativt pris, maximera intäkt)
4. **Proaktiva Insights** (alerts, recommendations, ROI-tracking)
5. **Set-and-Forget Automation** (deadlines, inte micro-management)

**Designprinciper som gör detta möjligt:**
- ✅ Focus på FLEXIBLA LASTER (det vi kan påverka)
- ✅ Privacy-first (ingen känslig batteridata)
- ✅ Vendor-agnostic (fungerar med any EV/solar/grid)
- ✅ Behavioral insights (visa savings, inte bara prices)
- ✅ Proaktiv (push notifications, inte bara pull API)

**Konkret next step:**
Implementera BUY-viktning TODO (2h) + ACTION_USE_SURPLUS (1h) = **3 timmar arbete för stor kundnyttaförbättring!**

---

**Genererad:** 2026-03-03
**Baserat på:** COMPUTE_ENERGY_ANALYS.md
**Uppdaterat för:** Battery-agnostic design
