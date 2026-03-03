# Detaljerad analys och färdplan för GridGuard

**Datum:** 2026-03-03
**Syfte:** Specificera brister, matcha mot kurskrav, och ge konkret färdplan

---

## Del 1: Specificering av identifierade brister

### A. "Begränsad felåterställning" - Konkreta scenarion

#### 1. **Processavbrott utan återhämtning**

**Problem:**
```c
// GridGuard.c - Endast Server-processen övervakas av Watchdog
// Om Fetcher eller Parser kraschar:
if (app->fetchPid == 0) {
    execl(app->fetcherBin, "GridGuard-fetcher", app->fifoPath, NULL);
    // Om exec misslyckas → process dör tyst
}
```

**Konsekvenser:**
- Fetcher kraschar → Inga väderdata hämtas → Systemet returnerar gamla/inga resultat
- Parser kraschar → Compute-tråden hänger på Unix socket connect
- HTTP-requests timeout efter 30 sekunder utan svar till användaren

**Saknad funktionalitet:**
- Ingen heartbeat-övervakning av child-processer
- Ingen automatisk restart av Fetcher/Parser
- Ingen graceful degradation (returnera cached data vid process-fel)

**Konkret exempel:**
```bash
# Simulera Fetcher-krasch:
$ kill -9 $(pgrep GridGuard-fetcher)

# Resultat:
# - Server fortsätter köra
# - Requests hänger i 30s (socket timeout)
# - Ingen automatisk recovery
# - Systemet dysfunktionellt tills manuell restart
```

---

#### 2. **Shared memory korruption**

**Problem:**
```c
// SharedCache.c:122-136 - Ingen checksumma eller validering
sem_wait(cache->sem);
for (int i = 0; i < SHARED_CACHE_MAX_ENTRIES; i++)
{
    if (r->entries[i].occupied && strcmp(r->entries[i].key, key) == 0)
    {
        strncpy(r->entries[i].data, data, SHARED_CACHE_DATA_MAX - 1);
        r->entries[i].createdAt = time(NULL);
        sem_post(cache->sem);
        return 0;
    }
}
```

**Vad kan gå fel:**
- Process kraschar medan semaphore är låst → permanent deadlock
- Partial write (process dödad mitt i memcpy) → korrupt data
- Ingen magic number-validering vid läsning
- Ingen CRC/checksum för att detektera korruption

**Konsekvenser:**
- Korrupt väderdata → felaktiga energiplaner
- Deadlock → hela systemet hänger
- Ingen automatisk recovery → kräver manuell `shm_unlink()`

---

#### 3. **Database-fel utan rollback**

**Problem:**
```c
// Database.c - Ingen transaction-hantering
int Database_Initiate(Database *db, const char *path)
{
    if (sqlite3_open_v2(path, &db->db, flags, NULL) != SQLITE_OK)
    {
        LOG_ERROR("Failed to open '%s': %s", path, sqlite3_errmsg(db->db));
        sqlite3_close(db->db);
        return -1;
    }
    // Ingen BEGIN TRANSACTION
    // Ingen error recovery vid disk full / corruption
}
```

**Saknade mekanismer:**
- Ingen SQLite WAL-mode (Write-Ahead Logging) för crash recovery
- Ingen backup-mekanism
- Ingen validering av schema-version
- Om migration failar (Database.c:79-89) → partial state

---

#### 4. **Network timeout utan exponential backoff**

**Problem:**
```c
// HTTPClient.c:219 - En enda försök, ingen retry
int fd = tcp_connect(host, port, timeoutSec);
if (fd < 0)
    return -1;  // Direkt fail, ingen retry
```

**Konsekvenser:**
- Tillfällig nätverksstörning → request misslyckas permanent
- API-ratelimit → inga nya försök
- Ingen cache-fallback vid network-fel

**Bättre approach:**
```c
// Pseudo-code
for (int attempt = 0; attempt < MAX_RETRIES; attempt++)
{
    int fd = tcp_connect(host, port, timeout);
    if (fd >= 0) break;

    int backoff = MIN(INITIAL_BACKOFF * (1 << attempt), MAX_BACKOFF);
    sleep(backoff);  // Exponential: 1s, 2s, 4s, 8s...
}
```

---

#### 5. **Race condition vid startup**

**Problem:**
```c
// GridGuard.c:166, 195
LOG_INFO("Fetch process started (PID %d)", app->fetchPid);
sleep(1);  // "Ge fetch-processen tid att öppna FIFO write end"
```

**Varför detta kan failar:**
- På långsam maskin kan 1s inte räcka
- På snabb maskin slösar vi 1s i onödan
- Ingen garanti att FIFO faktiskt är öppnad

**Konsekvenser:**
- Server skriver till FIFO innan Fetcher har öppnat → SIGPIPE eller blocking
- Parser försöker läsa innan Fetcher har skrivit → läser från stale FIFO

**Korrekt lösning:**
```c
// Använd pipe för readiness signaling
int ready_pipe[2];
pipe(ready_pipe);

if (fork() == 0) {
    // Child: Fetcher
    close(ready_pipe[0]);

    // Setup FIFO...
    fifo_fd = open(fifoPath, O_WRONLY);

    // Signal readiness
    write(ready_pipe[1], "R", 1);
    close(ready_pipe[1]);

    // Continue normal operation...
}

// Parent
close(ready_pipe[1]);
char ready;
read(ready_pipe[0], &ready, 1);  // Block until child is ready
close(ready_pipe[0]);
LOG_INFO("Fetch process is ready");
```

---

### B. "Ofullständiga funktioner (TODOs)" - Komplett lista

#### TODOs i er egen kod (3 st):

**1. Solmodell saknar tilt/azimuth-korrigering** (Compute.c:255-256)
```c
// TODO: remaining uncorrected factors — panel tilt/azimuth,
//       seasonal albedo, shading, and inverter clipping at low load.
```

**Impact:**
- Solpaneler som inte är perfekt södervänd/45° lutning får felaktiga produktionsestimat
- Kan vara ±20-30% fel beroende på installationens orientation

**Fix komplexitet:** Medium
- Kräver user input: panel azimuth (0-360°), tilt (0-90°)
- Formula: `irradiance_corrected = irradiance * cos(angle_between_sun_and_panel)`
- Solens position: beräkna från lat/lon + timestamp (solpos-algoritm)

**Är det värt det för kursen?** Nej, kan noteras som "known limitation"

---

**2. Batteri-laddningslogik** (Compute.c:284-285)
```c
// TODO: if a battery is present, this is ideal time to
// charge it rather than export at a loss.
```

**Impact:**
- Vid negativt elpris (systemet betalar för att exportera) → bättre att lagra i batteri
- Potentiell besparing: 5-10% av årskostnad för kunder med batteri

**Fix komplexitet:** Medium-High
- Kräver batteri state-of-charge tracking
- Kräver laddnings-/urladdningsbeslut per timme
- Kräver ny tabell i database: `battery_state`

**Är det värt det för kursen?** Nej (användaren sa explicit "utan att veta om batteri")

---

**3. BUY-signal inte viktad mot kapacitet** (Compute.c:292-294)
```c
// TODO: BUY should be weighted by available flexible load
// capacity (battery SOC, shiftable load queue) so the signal
// reflects actionable demand, not just price alone.
```

**Impact:**
- Systemet signalerar BUY även om användaren redan har schemalagt all flexibel last
- Redundanta BUY-signaler skapar "alert fatigue"

**Fix komplexitet:** Low-Medium
```c
// Pseudo-code fix:
double scheduled_load_kwh = get_scheduled_loads_for_hour(timestamp);
double available_capacity = user_config->max_flexible_load_kwh - scheduled_load_kwh;

if (cost <= buyThreshold && available_capacity > 0.5)  // Only signal if capacity exists
    action = ACTION_BUY_FROM_GRID;
```

**Är det värt det för kursen?** **JA!** - Enkel fix som förbättrar kundnytta

---

#### TODOs i tredjepartsbibliotek (cJSON.c - 2 st):

**4. cJSON overflow** (cJSON.c:1902)
```c
/* FIXME: Can overflow here. Cannot be fixed without breaking the API */
```

**Impact:** Potentiell buffer overflow vid extremt långa tal
**Fix:** Uppdatera till senaste cJSON (v1.7.18) eller byt till modernt bibliotek

**5. cJSON O(n²) performance** (cJSON.c:3145)
```c
/* TODO This has O(n^2) runtime, which is horrible! */
```

**Impact:** Långsam parsing av stora JSON-objekt (>1000 keys)
**Relevans:** GridGuard's JSON är små (~100 entries) → försumbar impact

---

### C. "TLS-certifikatvalidering inaktiverad" - Djupdykning

#### Säkerhetsimplikation:

**Kod:**
```c
// HTTPClient.c:191-192
// Vi verifierar inte servercertifikatet — inga CA-certs behövs på embedded.
mbedtls_ssl_conf_authmode(&client->sslConf, MBEDTLS_SSL_VERIFY_NONE);
```

**Attackscenario:**

```
[GridGuard] --HTTPS--> [Evil Router/MITM] --HTTPS--> [api.open-meteo.com]
                              ↑
                    Fake certificate
                    accepted without validation
```

**Vad en angripare kan göra:**
1. **Manipulera väderdata:**
   ```json
   {
     "hourly": {
       "temperature_2m": [999, 999, 999],  // Fake extreme temp
       "solar_irradiance": [0, 0, 0]        // Fake no sun
     }
   }
   ```
   → Felaktiga energiplaner → användaren laddar vid dyra timmar

2. **Manipulera elpriser:**
   ```json
   {
     "prices": [
       {"hour": "2026-03-03T14:00", "price": 0.10},  // Fake low price
       // ... attacker makes user charge during ACTUAL expensive hours
     ]
   }
   ```
   → Användaren förlorar pengar

3. **DoS genom malformed JSON:**
   ```json
   {"hourly": {"temperature_2m": [1,2,3, /* ... 1 million entries ... */]}}
   ```
   → Kraschar parser-processen → systemet offline

**Sannolikhet:**
- Låg för home network (kräver komprometterad router)
- Medel för public WiFi
- Hög för enterprise/shared networks

**Impact:** Medel-Hög (ekonomisk förlust + systemstabilitet)

---

#### Så fixar ni det:

**Steg 1: Aktivera certifikatvalidering**
```c
// HTTPClient.c - BEFORE fix
mbedtls_ssl_conf_authmode(&client->sslConf, MBEDTLS_SSL_VERIFY_NONE);

// AFTER fix
mbedtls_ssl_conf_authmode(&client->sslConf, MBEDTLS_SSL_VERIFY_REQUIRED);
```

**Steg 2: Ladda CA-certifikat**
```c
// HTTPClient.h - Add member
typedef struct {
    mbedtls_ssl_config sslConf;
    mbedtls_x509_crt   caCert;      // NEW
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctrDrbg;
    bool initialized;
} HTTPClient;

// HTTPClient.c - Initiate()
mbedtls_x509_crt_init(&client->caCert);

// Load system CA bundle
const char *ca_bundle = "/etc/ssl/certs/ca-certificates.crt";  // Debian/Ubuntu
// Or: "/etc/pki/tls/certs/ca-bundle.crt" on Fedora/RHEL

int ret = mbedtls_x509_crt_parse_file(&client->caCert, ca_bundle);
if (ret != 0)
{
    LOG_ERROR("Failed to load CA certificates: -0x%04x", -ret);
    return -1;
}

mbedtls_ssl_conf_ca_chain(&client->sslConf, &client->caCert, NULL);
```

**Steg 3: Cleanup**
```c
void HTTPClient_Shutdown(HTTPClient *client)
{
    mbedtls_x509_crt_free(&client->caCert);  // NEW
    mbedtls_ssl_config_free(&client->sslConf);
    // ... rest
}
```

**Steg 4: Testa**
```bash
# Borde fungera (valid cert):
$ ./bin/GridGuard-server

# Borde failar (self-signed cert):
$ curl --insecure https://self-signed.badssl.com/  # Din kod ska VÄGRA
```

**Komplexitet:** Low (15 raders kod)
**Tid:** 30 minuter
**Prioritet:** **KRITISK för produktion**

---

## Del 2: Analys mot kursplanering

### Kursmål GridGuard täcker (8/12):

| Mål | Status | Bevis i GridGuard |
|-----|--------|-------------------|
| **1. Processer, trådar, synk, minne** | ✅ | fork/exec (GridGuard.c:127), pthread (ThreadPool.c), mutex/cond (Queue.c:52-68), shared memory (SharedCache.c) |
| **2. IPC: pipes, sockets, shared mem** | ✅ | Anonymous pipe (GridGuard.c:114), FIFO (GridGuard.c:98), Unix socket (parser.c:78), shm_open (SharedCache.c:28) |
| **3. C vs C++ skillnader** | ❌ | **Helt frånvarande - inget C++!** |
| **4. C++ objektmodell och RAII** | ❌ | **Helt frånvarande** |
| **5. STL: vector, string, unique_ptr** | ❌ | **Helt frånvarande** |
| **6. Profilering för optimering** | ⚠️ | Makefile har `make valgrind`, `make helgrind`, `make profile` targets men **inga körda rapporter** |
| **7. Flertrådad C/C++** | ✅ | ThreadPool med 20 workers (ThreadPool.c:37), producer-consumer (Queue.c) |
| **8. IPC i systemnära program** | ✅ | Fullständig multi-process pipeline med alla IPC-typer |
| **9. C++ med RAII och STL** | ❌ | **Helt frånvarande** |
| **10. Utföra profilering** | ⚠️ | Targets finns, men **inga faktiska profileringsresultat** |
| **11. Optimera baserat på mätdata** | ❌ | **Ingen faktisk optimering dokumenterad** |
| **12. Dokumentera design/prestanda** | ✅ | Excellent: 11 MD-filer, 6,200+ rader docs |

**Betyg: 5.5/12 helt klara, 2/12 delvis**

---

### Kritiska saknade delar (Kursmål 3-5, 9):

**Problem:** GridGuard är 100% C - kursen heter "Systemprogrammering **och introduktion till C++**"

**Lösning:** Ni behöver **inte** skriva om hela projektet i C++. Istället:

#### **Hybrid approach - Lägg till C++-komponenter:**

**1. Skriv en C++ wrapper för Load Scheduling (Kursmål 9 - RAII + STL)**

```cpp
// src/application/services/LoadSchedulerCpp.hpp
#ifndef LOAD_SCHEDULER_CPP_HPP
#define LOAD_SCHEDULER_CPP_HPP

#include <vector>
#include <memory>
#include <string>
#include <chrono>

namespace gridguard {

// RAII-klass för schedule entry
class ScheduleEntry {
public:
    ScheduleEntry(std::string loadId,
                  std::chrono::system_clock::time_point start,
                  int durationMinutes,
                  double powerKw);
    ~ScheduleEntry() = default;  // RAII: automatic cleanup

    // Move semantics (Kursmål 4)
    ScheduleEntry(ScheduleEntry&&) noexcept = default;
    ScheduleEntry& operator=(ScheduleEntry&&) noexcept = default;

    // Copy prevention
    ScheduleEntry(const ScheduleEntry&) = delete;
    ScheduleEntry& operator=(const ScheduleEntry&) = delete;

    double estimatedCost() const;
    double savings() const;

private:
    std::string loadId_;
    std::chrono::system_clock::time_point scheduledStart_;
    int durationMinutes_;
    double powerKw_;
    double estimatedCostSek_;
    double savingsSek_;
};

// STL usage: vector + unique_ptr (Kursmål 5)
class LoadScheduler {
public:
    LoadScheduler() = default;

    // Smart pointers för resurshantering (Kursmål 5)
    void addSchedule(std::unique_ptr<ScheduleEntry> entry);

    // STL containers (Kursmål 5)
    std::vector<std::unique_ptr<ScheduleEntry>> getActiveSchedules() const;

    // Optimal scheduling algorithm
    std::unique_ptr<ScheduleEntry> findOptimalWindow(
        const std::vector<double>& hourlyPrices,
        int durationMinutes,
        double powerKw
    );

private:
    std::vector<std::unique_ptr<ScheduleEntry>> schedules_;
};

} // namespace gridguard

#endif
```

**Implementering:**
```cpp
// src/application/services/LoadSchedulerCpp.cpp
#include "LoadSchedulerCpp.hpp"
#include <algorithm>
#include <numeric>

namespace gridguard {

ScheduleEntry::ScheduleEntry(std::string loadId,
                             std::chrono::system_clock::time_point start,
                             int durationMinutes,
                             double powerKw)
    : loadId_(std::move(loadId))  // Move semantics
    , scheduledStart_(start)
    , durationMinutes_(durationMinutes)
    , powerKw_(powerKw)
    , estimatedCostSek_(0.0)
    , savingsSek_(0.0)
{
}

std::unique_ptr<ScheduleEntry>
LoadScheduler::findOptimalWindow(const std::vector<double>& hourlyPrices,
                                  int durationMinutes,
                                  double powerKw)
{
    // STL algorithms (Kursmål 5)
    int windowHours = (durationMinutes + 59) / 60;

    double minCost = std::numeric_limits<double>::max();
    size_t bestStart = 0;

    for (size_t i = 0; i + windowHours <= hourlyPrices.size(); ++i)
    {
        double windowCost = std::accumulate(
            hourlyPrices.begin() + i,
            hourlyPrices.begin() + i + windowHours,
            0.0
        );

        if (windowCost < minCost) {
            minCost = windowCost;
            bestStart = i;
        }
    }

    auto now = std::chrono::system_clock::now();
    auto scheduledStart = now + std::chrono::hours(bestStart);

    return std::make_unique<ScheduleEntry>(
        "optimal_load",
        scheduledStart,
        durationMinutes,
        powerKw
    );
}

} // namespace gridguard
```

**C-binding för att kalla från befintlig C-kod:**
```cpp
// src/application/services/LoadSchedulerCpp_C.h
#ifdef __cplusplus
extern "C" {
#endif

typedef struct LoadSchedulerCpp LoadSchedulerCpp;

LoadSchedulerCpp* LoadSchedulerCpp_Create(void);
void LoadSchedulerCpp_Destroy(LoadSchedulerCpp* scheduler);

// Return optimal start hour (0-95 for 96-hour window)
int LoadSchedulerCpp_FindOptimalWindow(
    LoadSchedulerCpp* scheduler,
    const double* hourlyPrices,
    int priceCount,
    int durationMinutes,
    double powerKw
);

#ifdef __cplusplus
}
#endif
```

**Bygga:**
```makefile
# Makefile - lägg till C++ targets
CXX = g++
CXXFLAGS = -Wall -Wextra -Werror -std=c++17 -pthread

CPP_SOURCES = src/application/services/LoadSchedulerCpp.cpp \
              src/application/services/LoadSchedulerCpp_C.cpp

build/LoadSchedulerCpp.o: src/application/services/LoadSchedulerCpp.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# Link både C och C++ objekt
$(BIN_DIR)/GridGuard-server: $(SERVER_OBJECTS) $(CPP_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)
```

**Detta täcker:**
- ✅ Kursmål 3: C vs C++ skillnader (namespace, referenser, klasser)
- ✅ Kursmål 4: RAII (ScheduleEntry med automatic cleanup)
- ✅ Kursmål 5: STL (vector, unique_ptr, algorithms)
- ✅ Kursmål 9: C++ komponenter med RAII och STL

**Tid:** 4-6 timmar
**Komplexitet:** Låg (ni har redan C-implementation att konvertera)

---

**2. Profilering och optimering (Kursmål 6, 10, 11)**

**Kör faktiska profileringsverktyg:**

```bash
# Steg 1: Bygg med profiling flags
make clean
make profile

# Steg 2: Kör server med typisk last
./bin/GridGuard-server &
SERVER_PID=$!

# Generate load
for i in {1..100}; do
    curl -H "Authorization: Bearer $JWT" \
         "http://localhost:8080/api/energy?userId=test_user" &
done
wait

# Steg 3: Stoppa server (genererar gmon.out)
kill -SIGTERM $SERVER_PID

# Steg 4: Analysera med gprof
gprof bin/GridGuard-server gmon.out > docs/PROFILING_REPORT.txt

# Steg 5: Memory profiling
valgrind --tool=massif --massif-out-file=massif.out ./bin/GridGuard-server
# ... run test load ...
ms_print massif.out > docs/MEMORY_PROFILE.txt

# Steg 6: Thread analysis
valgrind --tool=helgrind ./bin/GridGuard-server 2> docs/HELGRIND_REPORT.txt
```

**Dokumentera i docs/PROFILING_RESULTS.md:**
```markdown
# Profileringsresultat - GridGuard

## CPU-profilering (gprof)

### Top 5 tidskrävande funktioner:
1. `cJSON_Parse()` - 34.2% (JSON parsing)
2. `HTTPClient_Get()` - 21.7% (Network I/O)
3. `Compute_GenerateEnergyPlan()` - 15.3% (Algorithm)
4. `pthread_cond_wait()` - 12.1% (Thread sync)
5. `SharedCache_Lookup()` - 8.4% (Cache lookup)

### Identifierade flaskhalsar:
- Parser-processen är single-threaded → bottleneck vid hög last
- cJSON parsing körs för varje request → saknar query result cache

## Optimeringar implementerade:

### 1. Query result caching (Mål 11)
**Före:** Varje /api/energy request parsade JSON igen (15.3% CPU)
**Efter:** Cache hela `EnergyData` response i 5 minuter
**Resultat:** Response time 230ms → 12ms (-95%)

### 2. Connection pooling (Mål 11)
**Före:** sqlite3_open/close per request (8.1% CPU)
**Efter:** Connection pool med 5 connections
**Resultat:** Database latency 5ms → 0.8ms (-84%)

## Minnesprofilering (Massif)

Peak heap: 4.2 MB
Största allocations:
1. cJSON parse tree: 1.8 MB
2. HTTPClient response buffers: 1.2 MB
3. ThreadPool workers: 0.6 MB

Inga memory leaks detekterade.
```

**Detta täcker:**
- ✅ Kursmål 6: Förklara hur profilering används
- ✅ Kursmål 10: Utföra profilering och tolka resultat
- ✅ Kursmål 11: Optimera baserat på mätdata

**Tid:** 3-4 timmar (inkl dokumentation)

---

## Del 3: Färdplan för att slutföra projektet

### Prioriterad TODO-lista:

#### **Kritisk prioritet (MÅSTE göras för G):**

| # | Task | Tid | Kursmål | Beskrivning |
|---|------|-----|---------|-------------|
| 1 | **Lägg till C++-komponent** | 6h | 3,4,5,9 | LoadSchedulerCpp med RAII + STL |
| 2 | **Kör profilering** | 3h | 6,10,11 | gprof + valgrind → dokumentera resultat |
| 3 | **Fix race conditions** | 2h | 7 | Ersätt `sleep()` med readiness signaling |
| 4 | **Aktivera TLS cert validation** | 1h | - | Security fix |
| 5 | **Dokumentera C++ integration** | 1h | 12 | Förklara RAII, STL-användning |

**Total tid:** ~13 timmar

---

#### **Hög prioritet (för VG + kundnytta):**

| # | Task | Tid | Kursmål | Kundnytta |
|---|------|-----|---------|-----------|
| 6 | **Implementera query result cache** | 3h | 11 | -95% response time |
| 7 | **Process heartbeat monitoring** | 4h | 7 | Auto-restart Fetcher/Parser |
| 8 | **Exponential backoff för API** | 2h | - | Resilience vid network-fel |
| 9 | **BUY signal viktning** | 2h | - | Fix TODO i Compute.c:292 |
| 10 | **Notifieringar för höga priser** | 3h | - | Proaktiv varning till användare |

**Total tid:** ~14 timmar

---

#### **Låg prioritet (nice-to-have):**

| # | Task | Tid | Beskrivning |
|---|------|-----|-------------|
| 11 | Database connection pool | 3h | Performance boost |
| 12 | Multi-threaded Parser | 6h | Högt komplext |
| 13 | Historik och statistik | 4h | Nya API endpoints |
| 14 | Panel tilt/azimuth | 3h | Bättre solmodell |
| 15 | WebSocket för real-time updates | 5h | Modern UX |

---

## Del 4: Förbättra kundnytta UTAN batteri

### Realistiska förbättringar (2-3 dagars arbete):

#### **1. Smart Load Scheduling (Kursmål 9 - C++ komponent)**

**Nuläge:** Systemet ger BUY/SELL/IDLE signaler, men ingen konkret schemaläggning

**Förbättring:**
```cpp
// Ny funktion i LoadSchedulerCpp
struct OptimalSchedule {
    std::chrono::system_clock::time_point start_time;
    int duration_minutes;
    double estimated_cost_sek;
    double savings_vs_average_sek;
};

OptimalSchedule scheduleEVCharging(
    double required_kwh,        // Ex: 40 kWh för Tesla Model 3
    int max_charge_time_hours,  // Ex: 8h (overnight)
    std::chrono::system_clock::time_point deadline  // Ex: 07:00 tomorrow
);
```

**Användargränssnitt:**
```bash
$ curl -X POST http://localhost:8080/api/schedule/ev \
  -H "Authorization: Bearer $JWT" \
  -d '{
    "userId": "user123",
    "required_kwh": 40,
    "max_charge_hours": 8,
    "deadline": "2026-03-04T07:00:00Z"
  }'

Response:
{
  "schedule_id": "sched_abc123",
  "start_time": "2026-03-03T23:00:00Z",
  "end_time": "2026-03-04T07:00:00Z",
  "estimated_cost": 124.50,
  "savings_vs_immediate": 47.80,
  "savings_percent": 27.7
}
```

**Kundnytta:**
- **Konkret besparingsestimat:** "Du sparar 48 kr genom att vänta till 23:00"
- **Automatisk optimering:** Ingen manuell planering
- **Flexibilitet:** Användaren sätter deadline, systemet hittar billigaste fönster

**Implementeringstid:** 4-5 timmar (C++ klass + API endpoint)

---

#### **2. Price Alert System**

**Nuläge:** Användaren måste aktivt kolla priser

**Förbättring:**
```c
// src/application/services/PriceAlertService.c

typedef struct {
    char userId[64];
    double threshold_sek_per_kwh;  // Ex: 2.0 SEK/kWh
    AlertType type;  // ALERT_HIGH_PRICE, ALERT_NEGATIVE_PRICE
} PriceAlert;

void PriceAlert_Check(const SpotPrice *prices, int count)
{
    for (int i = 0; i < count; i++)
    {
        if (prices[i].priceSek > alert->threshold_sek_per_kwh)
        {
            // Send notification (webhook, email, eller spara för polling)
            LOG_INFO("ALERT: High price %.2f SEK/kWh at hour %d",
                     prices[i].priceSek, i);
        }
    }
}
```

**API:**
```bash
$ curl -X POST http://localhost:8080/api/alerts \
  -d '{
    "userId": "user123",
    "type": "high_price",
    "threshold_sek_per_kwh": 2.0
  }'

# Hämta aktiva alerts
$ curl http://localhost:8080/api/alerts?userId=user123

Response:
{
  "alerts": [
    {
      "timestamp": "2026-03-03T17:00:00Z",
      "type": "high_price",
      "message": "Pris når 2.45 SEK/kWh kl 17-18. Undvik onödig förbrukning.",
      "severity": "warning"
    }
  ]
}
```

**Kundnytta:**
- **Proaktiv varning:** "Höga priser om 2 timmar - ladda nu istället"
- **Kostnadsmedvetenhet:** Användaren lär sig sitt konsumtionsmönster
- **Behavioral nudging:** Minska förbrukning vid peak times

**Implementeringstid:** 3 timmar

---

#### **3. Historical Analysis & Statistics**

**Nuläge:** Ingen data persistas efter 96-timmarsfönstret

**Förbättring:**
```sql
-- Ny tabell
CREATE TABLE energy_history (
    timestamp INTEGER PRIMARY KEY,
    user_id TEXT NOT NULL,
    action TEXT,  -- BUY/SELL/IDLE
    production_kwh REAL,
    consumption_kwh REAL,
    spot_price_sek REAL,
    total_cost_sek REAL,
    savings_sek REAL
);
```

**API:**
```bash
# Monthly summary
$ curl "http://localhost:8080/api/history/monthly?userId=user123&month=2026-02"

Response:
{
  "month": "2026-02",
  "total_consumption_kwh": 450.2,
  "total_production_kwh": 380.5,
  "total_cost_sek": 1247.80,
  "estimated_cost_without_optimization_sek": 1580.40,
  "total_savings_sek": 332.60,
  "savings_percent": 21.0,

  "breakdown": {
    "grid_import_kwh": 69.7,
    "grid_export_kwh": 0.0,
    "self_consumption_kwh": 380.5
  },

  "best_decision": {
    "date": "2026-02-15T02:00",
    "action": "Charged EV during negative prices",
    "savings_sek": 45.20
  }
}
```

**Kundnytta:**
- **Kvantifierbar ROI:** "Du har sparat 332 kr denna månad genom GridGuard"
- **Behavioral feedback:** "Dina bästa beslut var X, Y, Z"
- **Trend analysis:** "Din förbrukning ökar 12% vs förra året"

**Implementeringstid:** 4-5 timmar

---

#### **4. Improved Load Recommendations**

**Fix TODO:** Compute.c:292 - BUY signal viktning

**Nuläge:**
```c
// GridGuard signalerar BUY även om inget går att schemalägga
if (cost <= buyThreshold)
    action = ACTION_BUY_FROM_GRID;
```

**Efter fix:**
```c
// Kolla om användaren har flexibel last kvar att schemalägga
double scheduled_this_hour = ScheduleDB_GetScheduledLoad(userId, timestamp);
double max_flexible_load = userConfig->max_flexible_load_kwh;  // Ny config param

if (cost <= buyThreshold && scheduled_this_hour < max_flexible_load * 0.9)
{
    action = ACTION_BUY_FROM_GRID;
    // Logga rekommendation
    LOG_INFO("BUY signal: %.1f kWh capacity available at %.2f SEK/kWh",
             max_flexible_load - scheduled_this_hour, cost);
}
else
{
    action = ACTION_IDLE;
}
```

**Ny user_config parameter:**
```sql
ALTER TABLE user_configs ADD COLUMN max_flexible_load_kwh REAL DEFAULT 10.0;
-- Ex: 10 kWh = 2h EV-laddning @ 5kW + varmvatten + diskmaskin
```

**Kundnytta:**
- **Mindre alert fatigue:** Bara relevanta BUY-signaler
- **Actionable insights:** "Du kan ladda 7.5 kWh mer just nu"

**Implementeringstid:** 2 timmar

---

#### **5. Export av data för analys**

**API för CSV/JSON export:**
```bash
$ curl "http://localhost:8080/api/export/csv?userId=user123&start=2026-02-01&end=2026-02-29" \
  -o february_energy.csv

# CSV format:
# timestamp,action,production_kwh,consumption_kwh,spot_price,cost,savings
# 2026-02-01T00:00,IDLE,0.0,0.5,1.2,0.75,0.0
# 2026-02-01T01:00,BUY,0.0,0.5,0.8,0.50,0.25
# ...
```

**Kundnytta:**
- **Integration med Excel/PowerBI:** Egna analyser
- **Skattedeklaration:** Export av solproduktion
- **Transparens:** Full kontroll över sin data

**Implementeringstid:** 2 timmar

---

### Sammanfattning förbättringar:

| Förbättring | Tid | Kundnytta (1-5) | Kursmål |
|-------------|-----|-----------------|---------|
| Smart Load Scheduling (C++) | 5h | ⭐⭐⭐⭐⭐ | 3,4,5,9 |
| Price Alerts | 3h | ⭐⭐⭐⭐ | - |
| Historical Analysis | 5h | ⭐⭐⭐⭐⭐ | - |
| BUY signal viktning (TODO-fix) | 2h | ⭐⭐⭐ | - |
| Export functionality | 2h | ⭐⭐⭐ | - |

**Total tid:** 17 timmar
**Combined with kurskrav:** 13h + 17h = 30 timmar (≈4 arbetsdagar)

---

## Del 5: Rekommenderad implementation-ordning

### Vecka 1 (Kurskrav):
**Mål:** Täck alla 12 kursmål

1. **Dag 1-2:** C++ LoadScheduler komponent (6h)
   - Täcker kursmål 3, 4, 5, 9
   - RAII-klasser, STL-usage, hybrid C/C++

2. **Dag 3:** Profilering och optimering (4h)
   - Täcker kursmål 6, 10, 11
   - gprof, valgrind, dokumentera flaskhalsar
   - Implementera 1-2 optimeringar baserat på resultat

3. **Dag 4:** Cleanup och robusthet (3h)
   - Fix race conditions (sleep → readiness pipe)
   - Aktivera TLS cert validation
   - Täcker kursmål 7 (bättre thread safety)

**Resultat:** Alla kursmål täckta, projekt klart för examination

---

### Vecka 2 (Kundnytta):
**Mål:** Production-ready features

4. **Dag 5:** Smart scheduling + alerts (8h)
   - EV charging optimization med C++ klass
   - Price alert system

5. **Dag 6:** Historical data + analytics (5h)
   - Database schema för historik
   - Monthly summary API
   - Export to CSV

6. **Dag 7:** Final polish (4h)
   - Fix TODO: BUY signal viktning
   - Process heartbeat monitoring
   - Comprehensive testing

**Resultat:** Production-ready system med real kundnytta

---

## Slutsats

### Svar på dina frågor:

**1. "Begränsad felåterställning" specificerat:**
- ❌ Process crashes (Fetcher/Parser) → ingen auto-restart
- ❌ Shared memory corruption → ingen recovery
- ❌ Database errors → ingen transaction rollback
- ❌ Network failures → ingen exponential backoff
- ❌ Race conditions → hard-coded sleep() istället för signaling

**2. "Ofullständiga funktioner" specificerat:**
- 3 TODOs i er kod (solmodell, batteri, BUY-viktning)
- 2 TODOs i cJSON (kan ignoreras eller uppdatera lib)
- **Viktigast att fixa:** BUY-viktning (2h arbete, stor kundnytta)

**3. "TLS-certifikatvalidering":**
- ❌ MBEDTLS_SSL_VERIFY_NONE → MITM-sårbar
- ✅ Fix: 30 min arbete, critical för produktion
- Konkret kod för fix given ovan

**4. "Vad saknas mot kursplanering":**
- ❌ Kursmål 3-5, 9: Inget C++ (0/12 veckors innehåll)
- ⚠️ Kursmål 6, 10, 11: Profilering-targets finns men inga resultat
- ✅ Kursmål 1-2, 7-8, 12: Excellent coverage

**5. "Hur slutföra + förbättra kundnytta":**
- **Kurskrav:** 13h (C++ komponent + profilering)
- **Kundnytta:** 17h (scheduling, alerts, history, analytics)
- **Total:** 30h ≈ 4 arbetsdagar
- **Rekommendation:** Gör kurskrav först (vecka 1), sen kundnytta (vecka 2)

---

**Nästa steg:**
1. Skapa GitHub issues från TODO-listan
2. Starta med C++ LoadScheduler (högst prioritet)
3. Kör profilering och dokumentera
4. Fix race conditions
5. Deploy och testa

Lycka till! 🚀
