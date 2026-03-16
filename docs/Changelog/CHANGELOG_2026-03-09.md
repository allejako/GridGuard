# Changelog 2026-03-09


## Sammanfattning

Förbättringar av energiplan-beräkningen och införande av cache short-circuit för forecast-requests. Huvudfokus har varit bättre användarsignaler, realistiska konsumtionsprofiler och dramatisk latensförbättring vid upprepade requests.

**Vad som gjordes:**
- Ny AVOID-signal för dyra timmar
- Time-of-day load profile (svenska hushåll)
- BUY/SELL quality gates mot falsklarm
- Bästa BUY-fönster med SEK-besparingar
- JSON-serialisering med cJSON
- Dag-grupperad forecast-response
- **Cache short-circuit — 1–2 s → ~2 ms vid träff**

---

## Nya funktioner

### AVOID-signal för dyra timmar

**Problem:** Systemet sa bara när det var billigt (BUY) eller när man skulle sälja (SELL). Inget varning för dyra timmar.

**Lösning:** Lagt till `ACTION_AVOID_HIGH_PRICE` som triggas när priset är i topp-30% OCH minst 10% över median. Dubbelkollen förhindrar falsklarm på platta prisdagar.

**Resultat:** Användare får nu tre tydliga signaler:
- **BUY** — kör tvättmaskinen nu
- **AVOID** — skjut upp om möjligt
- **SELL** — exportera överskott

---

### Realistisk time-of-day load profile

**Problem:** Konstant konsumtion hela dygnet — inte hur riktiga hushåll beter sig.

**Lösning:** Infört consumption_factor() baserat på svensk statistik:
- Natt (00–06): 40% av medel
- Dag (07–16): 100%
- Kväll (17–22): 160% (matlagning, EV-laddning)
- Sen kväll (23): 70%

**Resultat:** BUY-signaler prioriterar automatiskt kvällstimmar där faktisk konsumtion är högst. Besparingsberäkningar blir korrekta.

---

### BUY och SELL quality gates

**BUY-gate:** Kräver att p30-tröskeln är minst 10% under median. Förhindrar att systemet säger "köp nu!" när det bara är 1% billigare än genomsnittet.

**SELL-gate:** Kräver spotpris >= 0.05 SEK/kWh. Vid negativa priser (överutbud) betalar man för att exportera — då är det bättre att ladda batteri eller förskjuta last.

---

### Best BUY Window-detektion

Systemet hittar nu det längsta sammanhängande tidsfönstret med BUY-signal och beräknar total SEK-besparing vs att köpa vid median-timme.

Exempel output:
```json
"best_buy_window": {
    "start": "2026-03-09T02:00:00Z",
    "end": "2026-03-09T06:00:00Z",
    "hours": 5,
    "avg_cost_sek": 1.28,
    "savings_sek": 12.45
}
```

Användaren ser direkt: "Ladda elbilen 02–06, spara 12 kr."

---

### JSON-serialisering med cJSON

Bytte från manuell snprintf-kedja (50+ rader, felbenäget) till cJSON-biblioteket. 60% mindre kod, automatisk escape-hantering, garanterat välformad JSON.

---

### Dag-grupperad forecast

96-timmars forecast returnerades som en flat lista med 96 objekt. Nu grupperas entries per datum (2026-03-09, 2026-03-10 etc.) vilket gör det mycket lättare att rendera i UI:t.

---

### Cache short-circuit (största förbättringen)

**Problem:** Varje forecast-request körde hela Fetcher → Parser → Compute-pipelinen även om identisk data redan fanns cachad (TTL: 15 min). Detta innebar 1–2 sekunder väntan på nätverksanrop varje gång.

**Identifierad i:** Profileringsrapport vecka 10 som högsta prioritet.

**Lösning:** Lagt till dedikerad `forecastCache` som lookar upp färdiga JSON-responses före pipeline-start.

**Så det fungerar:**

1. Request kommer in med användarens config (lat/lon/region/solar/fees)
2. Generera cache-nyckel: `fc:59.3293:18.0686:SE3:20.0:0.18:1.50:0.25:0.35:0.45`
3. Kolla cache:
   - **HIT:** Returnera direkt, skip Fetcher+Parser
   - **MISS:** Kör full pipeline, lagra resultat

**Loggexempel:**

Cache MISS (första gången):
```
[20:08:30] INFO  ClientHandler: Cache MISS for user=test_user key=fc:59.3293:... (running full pipeline)
[20:08:30] INFO  Fetcher: Fetching weather...  [~1500 ms]
[20:08:32] INFO  Compute: 96 entries, cost 153.11 SEK
```

Cache HIT (andra gången inom 15 min):
```
[20:10:15] INFO  ClientHandler: Cache HIT for user=test_user key=fc:59.3293:... (skipping Fetch+Parse)
```

**Prestanda:**

| Scenario | Latens före | Latens efter | Förbättring |
|----------|-------------|--------------|-------------|
| Cache MISS (första request) | ~1–2 s | ~1–2 s | Oförändrat (nätverket är flaskhalsen) |
| Cache HIT (upprepade requests) | ~1–2 s | **~2–5 ms** | **×400–1000** |

För en mobilapp som uppdaterar var 30:e sekund: ~96% träffprocent (30 requests per 15-min cache-entry).

**Påverkan på andra caches:**
- `weatherCache` — rådata från väder-API (används fortfarande av Fetcher)
- `priceCache` — rådata från elpris-API (används fortfarande av Fetcher)
- `forecastCache` — färdiga JSON-responses (NY)

---

## Tekniska detaljer

### Filer ändrade

**Nya funktioner:**
- `src/domain/Energy.h` — ACTION_AVOID_HIGH_PRICE enum
- `src/compute/Compute.c` — AVOID threshold-logik, consumption_factor(), BuyWindow-scan
- `src/server/GridGuard.h` — forecastCache tillagd
- `src/server/GridGuard.c` — forecastCache init/cleanup
- `src/server/ClientHandler.c` — cache lookup före pipeline, store efter Compute

**Borttaget:**
- `src/domain/Energy.c` — flyttat till Compute.c för bättre separation

### Performance-mätningar

**Compute-latens (oförändrad):**
- Compute_GenerateEnergyPlan: 318 µs medel (10k iterationer)
- BuyWindow-scan: +2 µs
- Quality gates: <1 ns overhead

**End-to-end latens:**
- Cache MISS: 1–2 s (nätverksbegränsat)
- Cache HIT: 2–5 ms (HTTP + 1 µs cache-lookup)

## Testing

### Verifiera cache short-circuit

**Starta systemet:**
```bash
make clean && make
make dev
```

`make dev` seedar databaser, genererar token, startar server och kör ett test-request som fyller cachen.

**Testa cache MISS vs cache HIT:**

För att se skillnaden mellan en kall cache (MISS) och en varm cache (HIT), starta om servern och kör två requests:

```bash
# Stoppa och starta om servern (tömmer cachen)
make stop
make run-server &
sleep 2

# Generera token för test_user (samma secret som servern använder)
TOKEN=$(GRIDGUARD_JWT_SECRET="gridguard-test-secret" \
  python3 scripts/generate_jwt.py platform.db test_user)

# Första requesten (cache MISS - tar ~1-2s)
time curl -s http://localhost:8080/forecast \
  -H "Authorization: Bearer $TOKEN" > /dev/null

# Andra requesten direkt efter (cache HIT - tar <10ms)
time curl -s http://localhost:8080/forecast \
  -H "Authorization: Bearer $TOKEN" > /dev/null
```

**Verifiera i server-loggen:**
```bash
tail -30 logs/server.log | grep "Cache"
```

**Förväntat:**

Första request:
```
INFO  ClientHandler: Cache MISS for user=test_user key=fc:59.3293:... (running full pipeline)
```

Andra request (inom 15 min):
```
INFO  ClientHandler: Cache HIT for user=test_user key=fc:59.3293:... (skipping Fetch+Parse)
```

**Tidsskillnad:**
- Cache MISS: ~1–2 sekunder (nätverksanrop)
- Cache HIT: <10 millisekunder (direkt från cache)

**Stoppa server:**
```bash
make stop
```

### Benchmarks

Compute-prestanda oförändrad:
```bash
make bench-compute
# Resultat: avg 318 µs, p95 491 µs
```

---

## Kvarvarande optimeringar (vecka 11)

1. ~~Cache short-circuit~~ — ✅ Klart
2. **pthread_rwlock i SharedCache** — Ersätt semaforen för concurrent reads (25 µs → 1 µs vid 8 trådar)
3. **poll() timeout i ComputeWorker** — Robusthet om Parser hänger
4. **SIMD för FP-loop** (stretch) — AVX2 för 4× throughput på prisberäkningar

---

**Sammanfattning:** Betydande förbättringar i användarupplevelse (AVOID-signal, BUY-fönster, cache short-circuit), kodkvalitet (cJSON, dag-gruppering) och algoritmisk korrekthet (quality gates, realistisk load profile). Cache short-circuit ger 400–1000× snabbare svar vid upprepade requests — den största prestandaförbättringen i projektet hittills.
