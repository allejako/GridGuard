# LEOP Arkitekturdokumentation

## Systemöversikt

LEOP är ett lokalt energioptimeringsystem med en multi-threaded server-klient-arkitektur. Systemet är designat för att vara modulärt, skalbart och underhållbart.

## Arkitekturprinciper

1. **Separation of Concerns:** Tydlig separation mellan datahämtning, beräkning och presentation
2. **Modularitet:** Varje komponent har ett väl definierat ansvar
3. **Skalbarhet:** Design för att hantera ökad last och fler datakällor
4. **Robusthet:** Omfattande felhantering och loggning
5. **Prestanda:** Optimerad för låg latens och hög genomströmning

## Högnivå-arkitektur

```
┌─────────────────────────────────────────────────────────────┐
│                         LEOP System                          │
├─────────────────────────────────────────────────────────────┤
│                                                               │
│  ┌──────────────┐         ┌──────────────┐                  │
│  │   Client     │◄────────┤    Server    │                  │
│  │   (C++)      │  Unix   │     (C)      │                  │
│  │              │ Sockets │              │                  │
│  └──────────────┘         └──────┬───────┘                  │
│                                   │                          │
│                         ┌─────────┴─────────┐                │
│                         │                   │                │
│                   ┌─────▼──────┐     ┌─────▼──────┐         │
│                   │   Fetch    │     │   Parse    │         │
│                   │  Process   │────►│  Process   │         │
│                   │  (Thread)  │Pipe │  (Thread)  │         │
│                   └────────────┘     └─────┬──────┘         │
│                                             │                │
│                                       ┌─────▼──────┐         │
│                                       │  Compute   │         │
│                                       │  Process   │         │
│                                       │  (Thread)  │         │
│                                       └─────┬──────┘         │
│                                             │                │
│                                       ┌─────▼──────┐         │
│                                       │   Cache    │         │
│                                       │  (Shared)  │         │
│                                       └────────────┘         │
│                                                               │
└─────────────────────────────────────────────────────────────┘
```

## Komponentbeskrivning

### 1. Server (C)

**Ansvar:**
- Huvudprocess som koordinerar systemet
- Hanterar klientanslutningar via Unix domain sockets
- Skapar och hanterar arbetartrådar/processer
- Centraliserad loggning och konfigurationshantering

**Teknisk Implementation:**
- Multi-threaded med POSIX threads
- Fork-baserad processhantering där relevant
- Signalhantering för graciös nedstängning (SIGINT, SIGTERM)
- Non-blocking I/O med select()/poll() för sockets

**Filstruktur:**
```
src/server/
├── main.c              # Entry point och main loop
├── server.c/h          # Server-logik
├── connection.c/h      # Klientanslutningshantering
├── config.c/h          # Konfigurationshantering
└── logger.c/h          # Loggningssystem
```

### 2. Fetch Process/Thread

**Ansvar:**
- Hämta väderdata från externa API:er
- Hämta spotprisdata från elnätsleverantörer
- Hantera API-fel, timeout och rate limiting
- Serialisera data för vidare bearbetning

**Teknisk Implementation:**
- Separat process kommunicerar via pipes
- Alternativt: dedikerad tråd i server
- Använder libcurl eller liknande för HTTP-requests
- Implementerar exponentiell backoff vid fel

**Dataflöde:**
```
External API ──► HTTP Request ──► Raw JSON ──► Pipe ──► Parse Process
```

### 3. Parse Process/Thread

**Ansvar:**
- Tolka JSON-data från fetch-processen
- Validera datakvalitet
- Konvertera till interna datastrukturer
- Vidarebefordra till beräkningssteget

**Teknisk Implementation:**
- Använder JSON-parser (t.ex. cJSON eller liknande)
- Validering av obligatoriska fält
- Felhantering för malformad data

**Datastrukturer:**
```c
typedef struct {
    time_t timestamp;
    double solar_irradiance;    // W/m²
    double cloud_cover;         // %
    double temperature;         // °C
    int valid;
} WeatherData;

typedef struct {
    time_t timestamp;
    double price_sek_per_kwh;
    char region[32];
    int valid;
} SpotPrice;
```

### 4. Compute Process/Thread

**Ansvar:**
- Beräkna solcellsproduktion baserat på väderdata
- Matcha produktion mot spotpriser
- Generera optimal energiplan
- Implementera optimeringsalgoritmer

**Beräkningslogik:**
```
Förväntad produktion = f(solinstrålning, molnighet, paneleffekt, orientering)

Optimal strategi:
- Om (produktion > konsumtion) && (pris > tröskelvärde) → Sälj överskott
- Om (produktion < konsumtion) && (pris < tröskelvärde) → Köp från nät
- Om (produktion > konsumtion) && (pris < tröskelvärde) → Ladda batteri
- Om (produktion < konsumtion) && (pris > tröskelvärde) → Använd batteri
```

**Output:**
```c
typedef struct {
    time_t start_time;
    time_t end_time;
    enum {
        ACTION_BUY,
        ACTION_SELL,
        ACTION_CHARGE_BATTERY,
        ACTION_USE_BATTERY,
        ACTION_DIRECT_USE
    } action;
    double energy_kwh;
    double estimated_cost_sek;
} EnergyPlanEntry;
```

### 5. Cache (Shared Memory)

**Ansvar:**
- Lagra senaste prognoser med TTL
- Ge snabb åtkomst till beräknade energiplaner
- Minimera onödiga API-anrop
- Thread-safe åtkomst

**Teknisk Implementation:**
- POSIX shared memory (`shm_open`, `mmap`)
- Synkronisering med mutexer eller semaforer
- LRU (Least Recently Used) cache-strategi
- Konfigurbar TTL per datatyp

**Cache-struktur:**
```c
typedef struct {
    pthread_mutex_t lock;
    time_t last_updated;
    int ttl_seconds;
    WeatherData weather[96];        // 24h * 4 (15-min intervall)
    SpotPrice prices[96];
    EnergyPlanEntry plan[288];      // 72h * 4
    int valid;
} SystemCache;
```

### 6. Client (C++)

**Ansvar:**
- CLI-interface för användare
- Ansluta till server via Unix socket
- Presentera data i läsbart format
- Hantera användarkommandon

**Teknisk Implementation:**
- C++ med STL (std::string, std::vector)
- RAII för resurshantering
- Exception handling för fel
- Formaterad output med tabeller och färger

**Kommandon:**
```bash
leop-client forecast              # Visa solprognos
leop-client spotprice            # Visa elpriser
leop-client energyplan           # Visa optimal energiplan
leop-client status               # Visa systemstatus
```

## IPC-mekanismer

### Unix Domain Sockets
- **Användning:** Klient-server-kommunikation
- **Protokoll:** Textbaserat, line-delimited JSON
- **Socket-fil:** `/tmp/leop.sock`

### Pipes
- **Användning:** Fetch ↔ Parse ↔ Compute pipeline
- **Typ:** Anonyma pipes mellan relaterade processer
- **Dataformat:** Serialiserad binär data eller JSON

### Shared Memory
- **Användning:** Cache tillgänglig för alla komponenter
- **Synkronisering:** Pthread mutexer i shared memory
- **Namn:** `/leop_cache`

## Protokoll

### Klient-Server-protokoll

Request format:
```json
{
    "command": "forecast|spotprice|energyplan|status",
    "parameters": {
        "hours": 24,
        "location": "Stockholm"
    }
}
```

Response format:
```json
{
    "status": "success|error",
    "data": { ... },
    "error_message": "..."
}
```

## Felhantering

### Felkategorier

1. **API-fel:** Timeout, HTTP-fel, rate limiting
   - Strategi: Exponentiell backoff, fallback till cache

2. **Parsing-fel:** Malformad JSON, saknade fält
   - Strategi: Validering, logga och skippa felaktig data

3. **Beräkningsfel:** Division med noll, overflow
   - Strategi: Sanity checks, default-värden

4. **IPC-fel:** Stängda pipes, full queue
   - Strategi: Timeout, återanslutning, buffring

5. **Resursfel:** Out of memory, full disk
   - Strategi: Graciös degradering, användarvänliga meddelanden

### Loggning

Loggnivåer:
- **ERROR:** Kritiska fel som kräver åtgärd
- **WARN:** Problem som kan påverka funktionalitet
- **INFO:** Normal drift, viktiga händelser
- **DEBUG:** Detaljerad information för felsökning

Loggformat:
```
[2025-01-12 14:32:45] [ERROR] [fetch_weather] API timeout after 30s: api.openweathermap.org
```

## Konfiguration

### server.conf
```ini
[server]
port = 8080
socket_path = /tmp/leop.sock
max_connections = 10
log_level = INFO
log_file = /var/log/leop/server.log

[weather]
api_url = https://api.openweathermap.org/data/2.5/forecast
api_key = YOUR_API_KEY
update_interval = 3600
timeout = 30

[spotprice]
api_url = https://www.elprisetjustnu.se/api/v1/prices
region = SE3
update_interval = 3600

[solar]
panel_efficiency = 0.18
panel_area_m2 = 20
orientation_degrees = 180
tilt_degrees = 45

[battery]
capacity_kwh = 10
max_charge_rate_kw = 5
max_discharge_rate_kw = 5
min_soc_percent = 20

[cache]
weather_ttl = 3600
price_ttl = 3600
plan_ttl = 1800
```

## Säkerhet

### Åtgärder
1. Input-validering på alla gränssnitt
2. Begränsad bufferstorlek för att undvika overflow
3. Säker stränghantering (strncpy, snprintf)
4. Privilege separation (servern kör inte som root)
5. Filbehörigheter på socket och config-filer

## Prestanda

### Optimeringsmål
- Svarstid klient-server: < 100ms
- API-responsetid: < 2s (med timeout)
- Cache-lookup: < 1ms
- Minne: < 50MB RAM under normal drift

### Flaskhalsar (förväntade)
- API-anrop (externa beroenden)
- JSON-parsing (stora payloads)
- Mutex contention i cache vid hög last

## Testning

### Enhetstester
- Varje modul har egna tester
- Mock API-responses för deterministisk testning
- Memory leak detection med Valgrind

### Integrationstester
- End-to-end test: klient → server → cache
- IPC-tester för alla kommunikationskanaler
- Stresstester för multi-threaded komponenter

### Profileringstester
- Gprof för hotspot-identifiering
- Perf för CPU-profiling
- Cachegrind för cache-analys

## Framtida Utökningar

### Möjliga Förbättringar
1. Webbgränssnitt för visualisering
2. Databas för historisk data
3. Maskininlärning för bättre prognoser
4. Distribuerad arkitektur för flera installationer
5. REST API för extern integration

## Revisionshistorik

| Version | Datum | Ändringar | Författare |
|---------|-------|-----------|------------|
| 0.1 | 2025-01-XX | Initial arkitekturdesign | [Team] |

