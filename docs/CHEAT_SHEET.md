# GridGuard - Redovisning Cheat Sheet

## Systemöversikt

**Vad är GridGuard?**
Ett energiplaneringssystem för hem med solpaneler. Systemet hämtar väderdata och spotpriser, beräknar optimal solproduktion, och ger rekommendationer när man ska köpa från nätet, sälja till nätet, eller vara idle.

**Huvudfunktioner:**
- REST API med JWT-autentisering
- 96-timmars energiprognos
- BUY/SELL/IDLE-beslut per timme baserat på spotpris och solproduktion
- Optimering av laddningsscheman (t.ex. elbilsladdning)

**Teknisk arkitektur:**
4 separata processer som kommunicerar via IPC-mekanismer. Watchdog övervakar servern, som spawnar Fetcher och Parser. Detta för att täcka alla kursmål från vecka 1-5.

---

## Processarkitekturen (viktigast att förklara)

### Varför fyra processer istället för trådar?

**Ursprunglig design:** En process med flera trådar (Fetch-tråd, Parse-tråd, Compute-tråd)
- Fungerade tekniskt bra
- Men täckte bara kursmål vecka 2-3 (pthread, mutex, condition variables)

**Problem:** Kursmålen kräver IPC mellan PROCESSER
- Vecka 1: fork(), exec(), waitpid()
- Vecka 4: Pipes, FIFO
- Vecka 5: Unix sockets, shared memory

**Lösning:** Multi-process arkitektur med watchdog
- GridGuard-watchdog (övervakare): Startar och övervakar server-processen
- GridGuard-server (main process): HTTP-server + Compute-tråd, spawnar Fetcher/Parser
- GridGuard-fetcher (data-hämtare): Hämtar väder + spotpriser
- GridGuard-parser (JSON-parser): Parsar JSON-data

Varje process är en fristående binär med egen main(). Ingen delad kod, all kommunikation via POSIX IPC.

### De fyra processerna

**0. GridGuard-watchdog (infrastructure/processes/watchdog/)**
- Startar server-processen via fork()+exec()
- Övervakar via heartbeat pipe (varje 5 sekunder)
- Startar om server automatiskt vid krasch (max 5 försök på 5 minuter)
- Skriver status till FIFO för extern monitoring

**1. GridGuard-server (main.c, Server.c, GridGuard.c)**
- Tar emot HTTP-requests på port 8080
- ThreadPool med 20 HTTP-workers (work-queue modell)
- En Compute-tråd som kopplar till Parser via Unix socket
- Spawnar Fetcher och Parser med fork()+exec()

**2. GridGuard-fetcher (infrastructure/processes/fetcher/)**
- Läser WorkRequest från stdin (som är en pipe från server)
- Gör HTTP-requests till Open-Meteo (väder) och Elpriset.se (spotpriser)
- Cachar API-svar i POSIX shared memory (TTL 15 min för väder, 24h för priser)
- Skriver FetchResult till FIFO (named pipe)

**3. GridGuard-parser (infrastructure/processes/parser/)**
- Läser FetchResult från FIFO
- Parsar JSON med cJSON-biblioteket
- Startar en Unix domain socket-server
- Väntar på att Compute-tråden kopplar upp sig
- Skickar ParseResult via socketen

---

## IPC-flödet (följ data genom systemet)

**Steg för steg:**

1. **HTTP Request** kommer in → ThreadPool tar emot
2. **JWT-validering** → Extrahera user_id från token
3. **Databas-lookup** → Hämta användarens lat/lon, region, solpanel-config
4. **WorkRequest skapas** → Struct med all nödvändig data
5. **Anonymous Pipe** → write() till pipe, Fetcher läser från stdin
6. **Fetcher hämtar** → HTTP till Open-Meteo + Elpriset, cache i shared memory
7. **FIFO (named pipe)** → Fetcher skriver FetchResult, Parser läser
8. **JSON-parsing** → Parser använder cJSON för att extrahera väder + priser
9. **Unix Socket** → Parser väntar på connection, Compute kopplar upp
10. **Compute genererar plan** → 96 timmar med BUY/SELL/IDLE beslut
11. **HTTP Response** → JSON skickas tillbaka till klienten

**Varför så komplicerat?**
Varje IPC-mekanism täcker ett specifikt kursmål:
- Anonymous pipe: Vecka 4 (fork+dup2 för stdin-redirect)
- Named FIFO: Vecka 4 (mkfifo för kommunikation mellan oberoende processer)
- Unix socket: Vecka 5 (socket programming)
- Shared memory: Vecka 5 (shm_open, mmap för cache)

---

## Kursmål-mappning (viktigt för betygssättning)

### Vecka 1: Process Management

**GridGuard.c rad 127-192**
- `fork()` för Fetcher-process (rad 127)
- `fork()` för Parser-process (rad 169)
- `execl()` med dynamisk path via `/proc/self/exe` (rad 155, 187)
- `waitpid()` i shutdown för att reapa children (rad 294, 301)

**Varför dynamisk path?**
Daemon-mode ändrar cwd till `/`, vilket bröt relativa paths. Lösning: `readlink("/proc/self/exe")` + `dirname()` för att hitta bin-katalogen oavsett var projektet ligger.

### Vecka 2-3: Threads & Synchronization

**ThreadPool.c**
- 20 HTTP-workers med pthread_create
- Work-queue modell: alla workers blockerar på Queue_Pop()
- Bättre än select() per worker för skalbarhet

**CompletionRegistry.c**
- Mutex + condition variable för att vänta på svar från pipeline
- HTTP-worker sover tills Compute-tråden signalerar att data är klar
- pthread_cond_wait() / pthread_cond_signal()

### Vecka 4: Pipes & FIFO

**Anonymous pipe (GridGuard.c rad 114-124)**
- `pipe()` skapar pipe mellan parent och child
- `dup2()` redirectar Fetcher stdin till pipe read-end
- Main process skriver WorkRequest, Fetcher läser från stdin

**Named FIFO (GridGuard.c rad 98-107)**
- `mkfifo()` skapar `/tmp/gridguard_fetch_to_parse.fifo`
- Fetcher skriver FetchResult
- Parser läser från FIFO
- Används för processer som inte har parent-child relation

### Vecka 5: Sockets & Shared Memory

**Unix Domain Socket (parser.c rad 133-172)**
- Parser är server: `socket()`, `bind()`, `listen()`, `accept()`
- Compute är client: `socket()`, `connect()`
- Skickar ParseResult struct via socketen
- Socket path: `/tmp/gridguard_parse_to_compute.sock`

**POSIX Shared Memory (SharedCache.c)**
- `shm_open("/gridguard_weather")` och `shm_open("/gridguard_price")`
- `mmap()` för att mappa in shared memory i adressrymd
- `sem_open()` för semaforer som synkroniserar access
- Cache överlever process-restart (persistent tills shm_unlink())

---

## Viktiga datastrukturer

### WorkRequest
Skickas från HTTP-handler till Fetcher via pipe.
Innehåller: userId, location, lat, lon, region, solarAreaM2, solarEfficiency, consumptionKwh, grid_fee_low/normal/high.

### FetchResult
Skickas från Fetcher till Parser via FIFO.
Innehåller: userId, openMeteoJson (rå JSON-string 16KB), elprisetJson (rå JSON-string 32KB), grid fees, solar config.

### ParseResult
Skickas från Parser till Compute via Unix socket.
Innehåller: userId, ForecastData (96 parsed entries), grid fees, solar config.

### ForecastData
96 timmar framåt.
Varje entry: timestamp, spotPriceSEK, irradiance (W/m²), tempCelsius.

---

## WorkCompletion & CompletionRegistry

### Problemet vi löste
**Ursprunglig design (Scheduler.c):**
- Hade en global kö där Compute-tråden la resultat
- HTTP-workers pollade kön för att hitta sitt svar
- Ineffektivt: alla workers måste iterera genom kön
- Race conditions: svårt att säkerställa rätt worker fick rätt svar

**Varför vi bytte till WorkCompletion:**
Efter övergång till multi-process arkitektur förlorade vi direkta pekare mellan HTTP-worker och Compute-tråd. Vi behövde ett mönster som:
1. Låter HTTP-worker **blockera** (inte polla) tills svar finns
2. Låter Compute-tråd **hitta rätt worker** via userId (som kommer via IPC)
3. Är **thread-safe** och **effektivt**

### Hur WorkCompletion fungerar
**WorkCompletion.h/c** (inspirerat av Linux kernel's completion pattern):
- En stack-allokerad struct per HTTP-request
- Innehåller: mutex, condition variable, resultat-buffer (32KB JSON), done-flagga
- HTTP-worker anropar `WorkCompletion_Wait()` → blockerar på `pthread_cond_wait()`
- Compute-tråd anropar `WorkCompletion_Signal()` → kopierar JSON, sätter done=1, `pthread_cond_signal()`
- **Fördel:** Ingen polling, worker väcks direkt när data är klar

**CompletionRegistry.h/c:**
- Global hashtable: userId → WorkCompletion-pekare
- Thread-safe via intern mutex
- `RegisterCompletion()` när HTTP-worker startar pipeline
- `FindCompletionByUserId()` när Compute-tråd får ParseResult via socket
- `UnregisterCompletion()` när HTTP-worker är klar

**Flöde:**
1. HTTP-worker skapar `WorkCompletion wc` på stack
2. `RegisterCompletion(userId, &wc)`
3. `GridGuard_SubmitRequest()` → data går via IPC-pipeline
4. `WorkCompletion_Wait(&wc)` → worker sover
5. Compute-tråd får ParseResult med userId via Unix socket
6. `FindCompletionByUserId(userId)` → hittar rätt WorkCompletion
7. `WorkCompletion_Signal(wc, json)` → worker vaknar
8. HTTP-worker skickar JSON till klient
9. `UnregisterCompletion(userId)`

**Kod-referenser:**
- WorkCompletion: `src/concurrency/sync/WorkCompletion.h:9-20`
- CompletionRegistry: `src/concurrency/sync/CompletionRegistry.h:6-9`
- Användning i ClientHandler: `src/server/ClientHandler.c` (registrering + wait)
- Användning i ComputeWorker: `src/application/workers/ComputeWorkerHybrid.c` (find + signal)

---

## BUY / SELL / IDLE-algoritmen

### Hur besluten fattas
Vår algoritm analyserar 96-timmars prognos och fattar beslut per timme baserat på:

**1. SELL-signal:**
- Solproduktion > konsumtion (surplus)
- Spotpris >= 0 SEK/kWh (positivt pris)
- **Varför:** Exportera överskott till nätet när det är lönsamt
- **Edge case:** Vid negativt spotpris får producenter betala för att exportera → Bättre att self-consume eller ladda batteri

**2. BUY-signal:**
- Total konsumtionskostnad (spotpris + nätavgift) är i lägsta 30% av forecast-fönstret
- **Varför:** Detta är optimala timmar för att köra flexibla laster (elbilsladdning, diskmaskin, värmepump)
- **Percentilen (30%):** Baserat på typiskt hushåll med ~6-8 flyttbara timmar per dag (30% av 24h ≈ 7h)

**3. IDLE-signal:**
- Alla andra timmar
- **Varför:** Undvik diskretionär konsumtion, kör bara baslast

**Implementering (Compute.c):**
```c
#define BUY_PERCENTILE 0.30  // Lägsta 30% får BUY-signal

// Steg 1: Beräkna total konsumtionskostnad per timme
// Steg 2: Sortera alla kostnader, hitta 30:e percentilen
// Steg 3: För varje timme:
if (solar_surplus && spot_price >= 0)
    → SELL
else if (consumption_cost <= buy_threshold)
    → BUY
else
    → IDLE
```

**Kod-referenser:**
- Algoritm: `src/application/services/Compute.c:196-306`
- Percentil-beräkning: `src/application/services/Compute.c:207`
- Beslut-logik: `src/application/services/Compute.c:275-300`

### Förbättringsförslag för framtiden

**1. Batteriintegration (PRIO 1):**
- **Problem:** Vid negativt spotpris har vi IDLE-signal, men borde ladda batteri med billig sol
- **Lösning:** Lägg till batteristatus (SOC, kapacitet) i UserConfig
- **Ny logik:** `if (solar_surplus && spot_price < 0 && battery_not_full) → CHARGE_BATTERY`

**2. Viktad BUY-signal (PRIO 2):**
- **Problem:** BUY-signalen tar inte hänsyn till faktisk flexibel last
- **Lösning:** Lägg till schemaläggningsdata (pending EV charge, heat pump cycle)
- **Ny logik:** BUY-percentilen justeras baserat på kvarvarande flexibel last
- **Exempel:** Om 11kW EV-laddning behöver 4 timmar → BUY-signal för bästa 4 timmarna, inte alla inom 30%-percentilen

**3. Dynamisk nätavgift (PRIO 3):**
- **Problem:** Vi har statiska grid fees (low/normal/high), men vissa leverantörer har dynamiska nätavgifter
- **Lösning:** Stöd för API-integration med nätbolag (t.ex. Ellevio, Vattenfall Eldistribution)
- **Nytta:** Mer exakta kostnadsprognoser, bättre BUY-signaler

**4. Historisk analys & ML (PRIO 4):**
- **Problem:** 30%-percentilen är fix, men hushållets behov varierar
- **Lösning:** Logga faktisk konsumtion, lär modellen att förutsäga optimal percentil per användareprofil
- **Exempel:** Barnfamilj med hög morgon/kväll-last kanske behöver 40%-percentil för att få tillräckligt många BUY-timmar

---

## Load Shifting & Kundnytta

### Vad är load shifting?
**Load shifting** = Flytta flexibel elförbrukning från dyra timmar till billiga timmar.

**Flexibla laster (typiskt hushåll):**
- Elbilsladdning (7-11 kW, 2-8 timmar)
- Diskmaskin (1.5 kW, 2 timmar)
- Tvättmaskin (2 kW, 1 timme)
- Varmvattenberedare (2 kW, 2-4 timmar)
- Värmepump (3-5 kW, variabelt)

### Hur GridGuard levererar kundnytta

**1. `/forecast` endpoint:**
- Ger 96-timmars prognos med BUY/SELL/IDLE per timme
- Visar `savings_vs_median_sek_kwh` → hur mycket kunden sparar genom att agera på BUY-signal
- **Exempel:** Om medianpris är 1.50 SEK/kWh och BUY-timme kostar 0.80 SEK/kWh → 0.70 SEK/kWh besparing

**2. `/schedule` endpoint (Load Scheduler):**
- Kunden anger: last-ID, effekt (kW), duration (minuter), deadline (timestamp)
- GridGuard hittar billigaste start-tid inom deadline-fönstret
- **Exempel:**
  ```json
  {
    "load_id": "ev_charger",
    "power_kw": 11.0,
    "duration_minutes": 240,  // 4 timmar
    "deadline": 1709366400     // Imorgon 07:00
  }
  ```
- **Response:**
  ```json
  {
    "optimal_start": "2026-03-02T02:00:00Z",  // Starta 02:00
    "cost_sek": 28.50,
    "savings_sek": 12.30,                      // vs att ladda direkt nu
    "completion_time": "2026-03-02T06:00:00Z"
  }
  ```

**3. Verklig kundnytta:**
- **Månad 1 (baseline):** Kunden laddar elbil när hen kommer hem (18:00) → hög nätavgift + högt spotpris
- **Månad 2 (med GridGuard):** Kunden använder `/schedule`, laddar 02:00-06:00 → låg nätavgift + lågt spotpris
- **Besparing:** ~15-25 SEK/laddning × 20 laddningar/månad = **300-500 SEK/månad**
- **Årsbesparing:** 3600-6000 SEK (täcker in en laddbox på 2-3 år)

**4. Framtida integration:**
- Smart home automation (Home Assistant, IFTTT)
- Direkt API-kontroll av laddboxar (Zaptec, Easee, Garo)
- Push-notifikationer: "BUY-signal aktiv nu, starta diskmaskin!"

---

## Zombie-process problemet (bra story)

**Problem:**
Efter ombyggnad till multi-process visades Fetcher och Parser som zombies direkt efter fork. Pipeline fungerade inte alls.

**Orsak:**
Daemon_Init() byter cwd till `/` (standard för daemons). När GridGuard.c körde `execl("./bin/GridGuard-fetcher")` letade systemet i `/bin/` istället för projektets bin-katalog. Child-processen dog direkt, blev zombie.

**Försök 1:**
Hårdkodade absoluta paths. Fungerade men inte portabelt.

**Slutlig lösning:**
Använd `/proc/self/exe` för att hitta egen binär, sedan `dirname()` för att extrahera bin-katalogen. Nu fungerar det oavsett var projektet ligger.

GridGuard.c rad 37-52 visar implementationen.

---

## Database & JWT

### SQLite Database
En tabell: `user_configs`
- Primärnyckel: user_id (från JWT sub)
- Innehåller: lat, lon, region, solar_area_m2, solar_efficiency, consumption_kwh, grid fees
- Database.c öppnar med FULLMUTEX för thread-safety
- UserConfigDB.c hanterar queries

### JWT Validation
- Stateless autentisering med HMAC-SHA256 (mbedtls)
- Delad hemlig nyckel: GRIDGUARD_JWT_SECRET miljövariabel
- Token innehåller: sub (user_id), exp (expiration timestamp)
- Ingen databas-lookup för varje request, bara signatur-validering
- JWTValidator.c:236 visar validering

---

## Cache & Performance

### Shared Memory Cache
Två cacher: `/gridguard_weather` och `/gridguard_price`
- TTL: 15 minuter för väder, 24 timmar för spotpriser
- Delas mellan alla Fetcher-processer (om man skulle skala horisontellt)
- Semaforer synkroniserar read/write
- Cache HIT sparar ~400ms API-latency

### Demonstration
Kör samma forecast-request två gånger:
- Första gången: ~500ms (API-anrop till Open-Meteo + Elpriset)
- Andra gången: ~50ms (cache hit, ingen HTTP)

---

## Watchdog

### Varför Watchdog?
Server kan krascha. Watchdog övervakar och startar om automatiskt.

### Hur fungerar det?
- Watchdog spawnar server som daemon
- Heartbeat via pipe: server skriver byte varje 5 sekunder
- Om ingen heartbeat på 15 sekunder → server antas fryst
- Watchdog skickar SIGTERM, väntar 5 sek, sedan SIGKILL om behövs
- Max 5 omstarter inom 5 minuter, sedan ger watchdog upp

### Status FIFO
Watchdog skriver status till `/tmp/gridguard.status` (named pipe)
- Format: `START pid=123`, `CRASH code=1`, `RESTART attempt=2/5`
- External monitoring kan läsa denna FIFO i real-time
- **OBS:** FIFO är en pipe, inte en fil - data försvinner när den lästs
- **För demo:** Använd `tail -f /tmp/gridguard.status &` INNAN du startar watchdog
- **Enklare:** Läs `logs/watchdog.log` istället (vanlig fil med samma info)

---

## Demo-flöde för redovisning

### 1. Starta systemet
```bash
make dev
```
Output: 4 PIDs (watchdog, server, fetcher, parser), IPC-resurser i /tmp, JSON-svar med 96-timmars prognos

### 2. Visa processer
```bash
ps aux | grep GridGuard
```
Visar: GridGuard-watchdog, GridGuard-server, GridGuard-fetcher, GridGuard-parser med sina PID:er

### 3. Visa IPC-resurser
```bash
ls -lh /tmp/gridguard*
```
Visar: FIFO, socket, PID-filer, status FIFO

### 4. Öppna ny terminal - Test requests

**OBS:** JWT-token genereras automatiskt av `make dev` och sparas i miljövariabeln `$TOKEN`. Servern använder `gridguard-test-secret` som JWT secret, så tokens fungerar direkt utan manuell setup.

### 5. Test requests (kopiera dessa till ny terminal)

**A. Health check (ingen auth krävs):**
```bash
curl http://localhost:8080/health
```
Output: `{"status":"healthy"}`

**B. Hämta användarkonfiguration:**
```bash
curl -X GET http://localhost:8080/user/config \
  -H "Authorization: Bearer $TOKEN" | python3 -m json.tool
```
Output: JSON med lat, lon, region, solar_area_m2, solar_efficiency, consumption_kwh, grid fees

**C. Uppdatera användarkonfiguration:**
```bash
curl -X PUT http://localhost:8080/user/config \
  -H "Authorization: Bearer $TOKEN" \
  -H "Content-Type: application/json" \
  -d '{
    "latitude": 59.3293,
    "longitude": 18.0686,
    "region": "SE3",
    "solar_area_m2": 25.0,
    "solar_efficiency": 0.20,
    "consumption_kwh": 2.0,
    "grid_fee_low": 0.25,
    "grid_fee_normal": 0.50,
    "grid_fee_high": 0.85
  }' | python3 -m json.tool
```
Output: Uppdaterad config

**D. Hämta energi-prognos (visar hela pipeline):**
```bash
curl -X GET http://localhost:8080/forecast \
  -H "Authorization: Bearer $TOKEN" | python3 -m json.tool | head -50
```
Output: 96-timmars prognos med BUY/SELL/IDLE, spotpriser, solproduktion, total kostnad

**E. Skapa laddningsschema (optimerad EV-laddning):**
```bash
# Beräkna deadline (imorgon kl 07:00)
DEADLINE=$(python3 -c "import time,datetime; t=datetime.datetime.now(datetime.timezone.utc)+datetime.timedelta(days=1); print(int(datetime.datetime(t.year,t.month,t.day,7,0,0,tzinfo=datetime.timezone.utc).timestamp()))")

curl -X POST http://localhost:8080/schedule \
  -H "Authorization: Bearer $TOKEN" \
  -H "Content-Type: application/json" \
  -d "{
    \"load_id\": \"ev_charger\",
    \"duration_minutes\": 240,
    \"power_kw\": 11.0,
    \"deadline\": $DEADLINE
  }" | python3 -m json.tool
```
Output: Optimal starttid, kostnad, besparingar jämfört med att ladda direkt

**F. Visa cache i action:**

**Steg 1: Rensa cachen först (viktigt!):**
```bash
# Stoppa servern först
pkill GridGuard

# Ta bort shared memory cache
rm -f /dev/shm/gridguard_*

# Starta servern igen
make run-server &
sleep 2
```

**Steg 2: Kör requests och mät tid:**
```bash
# Första gången - långsam (API-anrop till Open-Meteo + Elpriset)
time curl -s http://localhost:8080/forecast \
  -H "Authorization: Bearer $TOKEN" > /dev/null

# Andra gången - snabb (cache hit, ingen HTTP)
time curl -s http://localhost:8080/forecast \
  -H "Authorization: Bearer $TOKEN" > /dev/null
```

**Förväntad output:**
- Första: `real 0m0.500s` (500ms med API-anrop)
- Andra: `real 0m0.030s` (30ms från cache)
- Skillnad: ~16x snabbare!

**Verifiera cache:**
```bash
ls -lh /dev/shm/gridguard_*
```
Visar två filer: `gridguard_price` och `gridguard_weather` (~514KB var)

**G. Felhantering - ogiltig JWT:**
```bash
curl -X GET http://localhost:8080/forecast \
  -H "Authorization: Bearer invalid_token_here"
```
Output: `{"error":"Unauthorized"}`

**H. Felhantering - saknad auth header:**
```bash
curl -X GET http://localhost:8080/forecast
```
Output: `{"error":"Unauthorized"}`

### 6. Visa loggar live (parallell terminal)

**Server log:**
```bash
tail -f logs/server.log
```
Visar: JWT validation, WorkRequest skapas, CompletionRegistry, HTTP response

**Fetcher log:**
```bash
tail -f logs/fetcher.log
```
Visar: WorkRequest mottagen, cache HIT/MISS, API-anrop, FetchResult skriven till FIFO

**Parser log:**
```bash
tail -f logs/parser.log
```
Visar: FetchResult läst från FIFO, JSON parsing, socket connection från Compute

### 7. Cache-hantering

**Visa cache-filer:**
```bash
ls -lh /dev/shm/gridguard_*
```
Output: `gridguard_price` (24h TTL) och `gridguard_weather` (15min TTL)

**Rensa cache (för demo):**
```bash
# Måste stoppa server först (annars kommer den skapa om cachen)
pkill GridGuard
rm -f /dev/shm/gridguard_*

# Starta om
make run-server &
```

**Kolla cache-statistik i loggar:**
```bash
grep -E "cache.*HIT|cache.*MISS" logs/fetcher.log | tail -5
```

### 8. Visa databas
```bash
sqlite3 gridguard.db
```
```sql
.schema user_configs
SELECT * FROM user_configs;
.quit
```

**Eller one-liner:**
```bash
sqlite3 gridguard.db "SELECT user_id, latitude, longitude, region, solar_area_m2 FROM user_configs;"
```

### 9. Debug commands

**Visa alla IPC-resurser:**
```bash
ls -lh /tmp/gridguard*
```

**Kolla om processer kör:**
```bash
ps aux | grep -E "GridGuard-(server|fetcher|parser)" | grep -v grep
```

**Kolla vilken port som används:**
```bash
lsof -i :8080
```

**Läs watchdog status (OBS: FIFO, inte vanlig fil!):**

Status FIFO är en named pipe - data passerar genom men sparas inte. För att se status:

**Alternativ 1: Läs watchdog.log istället (enklast):**
```bash
grep -E "START|CRASH|RESTART|Daemon" logs/watchdog.log | tail -10
```

**Alternativ 2: Lyssna på FIFO live (för demo):**
```bash
# Terminal 1: Starta listener INNAN watchdog
tail -f /tmp/gridguard.status &

# Terminal 2: Starta watchdog (trigger START event)
make dev

# Nu ser du: "START pid=12345" i terminal 1
```

**Alternativ 3: Se aktuell watchdog PID:**
```bash
cat /tmp/gridguard-watchdog.pid
```

### 10. Shutdown
```bash
make stop
```
Visar: Killed watchdog, killed server, cleanup av IPC-resurser

---

## Vanliga frågor under redovisning

**Q: Varför inte bara trådar?**
A: Kursmål kräver IPC mellan processer (pipes, FIFO, sockets, shm), inte bara shared memory mellan trådar.

**Q: Varför tre processer specifikt?**
A: För att demonstrera alla IPC-typer. Fetcher och Parser kunde varit en process, men då missar vi FIFO-kursmålet.

**Q: Vad händer om en process kraschar?**
A: Watchdog startar om server. Fetcher/Parser ägs av server, så de dör och startas om också.

**Q: Är shared memory nödvändigt?**
A: Tekniskt nej, men visar kursmål vecka 5. Cache förbättrar också performance rejält.

**Q: Varför Unix socket istället för FIFO för Parser→Compute?**
A: Parser behöver veta när Compute vill ha data. Socket med accept() ger den signalen. FIFO skulle blockera.

**Q: Production-ready?**
A: Nej, educational. Saknas: TLS, rate limiting, robust error handling, horizontal scaling.

---

## Tips för redovisning

1. **Börja med big picture** → Visa processes/README.md diagram
2. **Följ en request** → Öppna GridGuard.c, visa fork()+exec()
3. **Visa IPC-kod** → pipe() i GridGuard.c:114, mkfifo() i rad 98, socket() i parser.c:133
4. **Kör live** → make dev i ena terminalen, curl i andra
5. **Visa loggar** → tail -f för att visa data flow
6. **Förklara zombie-fix** → GridGuard.c:37-52, `/proc/self/exe` tricket
7. **Cache-demo** → Kör samma request två gånger, visa tidsskillnad
8. **Shutdown** → make stop, förklara waitpid() i GridGuard.c:294

---

## Fil-navigering (snabba hopp)

| Koncept | Fil | Rad |
|---------|-----|-----|
| fork Fetcher | GridGuard.c | 127 |
| fork Parser | GridGuard.c | 169 |
| execl med /proc/self/exe | GridGuard.c | 155 |
| pipe() skapas | GridGuard.c | 114 |
| dup2() stdin redirect | GridGuard.c | 147 |
| mkfifo() | GridGuard.c | 98 |
| socket() server | parser.c | 133 |
| socket() client | ComputeWorkerHybrid.c | 53 |
| shm_open() | SharedCache.c | 42 |
| mmap() | SharedCache.c | 59 |
| pthread_create workers | ThreadPool.c | 64 |
| pthread_cond_wait | CompletionRegistry.c | 63 |
| JWT validation | JWTValidator.c | 131 |
| SQLite query | UserConfigDB.c | 19 |
| Compute algorithm | Compute.c | 47 |
| waitpid() | GridGuard.c | 294, 301 |

---

## Kursmål täckning

✅ **Vecka 1:** fork, exec, waitpid - GridGuard.c:127-192
✅ **Vecka 2:** pthreads - ThreadPool.c:64
✅ **Vecka 3:** mutex, cond - CompletionRegistry.c:18-19
✅ **Vecka 4:** pipes, FIFO - GridGuard.c:98-124
✅ **Vecka 5:** sockets, shm - parser.c:133, SharedCache.c:42
✅ **Extra:** JWT (mbedtls), SQLite, HTTP REST API, Watchdog
