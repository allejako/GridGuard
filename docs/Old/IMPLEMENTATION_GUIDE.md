# GridGuard Implementation Guide

**Datum:** 2026-02-18
**Arkitektur:** Hybrid Edge Computing + Central Cache
**Filosofi:** Privacy-First, Efficient API Usage, Scalable

---

## 📋 Innehåll

1. [System Overview](#system-overview)
2. [Arkitektur Principer](#arkitektur-principer)
3. [Tre Huvudkomponenter](#tre-huvudkomponenter)
4. [Data Flow](#data-flow)
5. [Implementation Roadmap](#implementation-roadmap)
6. [Vad Ska Implementeras](#vad-ska-implementeras)
7. [Privacy & Security](#privacy--security)

---

## System Overview

### Den Stora Bilden

GridGuard består av **tre separata system** som arbetar tillsammans:

```
┌─────────────────────────────────────────────────────────────┐
│  1️⃣ CENTRAL AUTH & CACHE SERVER                            │
│     - User accounts (PostgreSQL)                            │
│     - Shared weather/price cache (Redis)                    │
│     - API proxy för Open-Meteo/Elpriset                     │
│     - KÄNNER INTE TILL user configs!                        │
└─────────────────────────────────────────────────────────────┘
                           ▲
                           │ HTTPS/REST
                           │ (API key authentication)
                           │
        ┌──────────────────┼──────────────────┐
        │                  │                  │
        ▼                  ▼                  ▼
┌──────────────┐   ┌──────────────┐   ┌──────────────┐
│ 2️⃣ EDGE     │   │ 2️⃣ EDGE     │   │ 2️⃣ EDGE     │
│    DEVICES   │   │    DEVICES   │   │    DEVICES   │
│              │   │              │   │              │
│ - User config│   │ - User config│   │ - User config│
│   (lokal)    │   │   (lokal)    │   │   (lokal)    │
│ - Compute    │   │ - Compute    │   │ - Compute    │
│   (lokal)    │   │   (lokal)    │   │   (lokal)    │
│ - History    │   │ - History    │   │ - History    │
│   (lokal)    │   │   (lokal)    │   │   (lokal)    │
└──────────────┘   └──────────────┘   └──────────────┘
```

### Varför Denna Design?

| Requirement | Solution |
|-------------|----------|
| **Privacy** | User configs sparas ENDAST lokalt |
| **Efficiency** | Central cache delar weather/price data mellan users |
| **Scalability** | Compute händer på edge devices (gratis för er) |
| **API Limits** | 1000 users i Stockholm = 1 API-anrop till Open-Meteo |
| **Offline** | Efter initial fetch kan device cacha lokalt (optional) |

---

## Arkitektur Principer

### Princip 1: Separation of Concerns

```
CENTRAL SERVER:
  ✅ Authentication (email + password)
  ✅ Shared data cache (weather, prices)
  ❌ ALDRIG user configs
  ❌ ALDRIG computation
  ❌ ALDRIG user results

EDGE DEVICE:
  ✅ User configs (solar, battery, consumption)
  ✅ Computation (energy plans)
  ✅ Historical data
  ❌ ALDRIG authentication database
```

### Princip 2: Cache Sharing

```
Scenario: 1000 users i Stockholm

Without central cache:
  1000 users × 4 requests/h = 4000 API calls/h
  → Överskrider rate limits
  → Kostar pengar
  → Ineffektivt

With central cache:
  User A requests → Cache MISS → Fetch from API → Save (TTL 15min)
  User B requests → Cache HIT (from User A)
  User C requests → Cache HIT (from User A)
  ...
  User Z requests → Cache HIT (from User A)

  Result: 1 API call serves 1000 users ✅
```

### Princip 3: Zero-Knowledge Server

```
Central server vet:
  ✅ User begärde weather för lat=59.33, lon=18.07
  ✅ User begärde prices för region=SE3

Central server vet INTE:
  ❌ Solar panel config (20m², 0.18 efficiency)
  ❌ Battery size (10kWh)
  ❌ Consumption (15kWh/dag)
  ❌ Computed energy plan
  ❌ Historical data

Compute happens on device → Server aldrig ser resultaten
```

---

## Tre Huvudkomponenter

### 1️⃣ Central Server (Python/Node.js/Go)

**Ansvar:**
- User registration & login
- API key management
- **Central cache** för weather/price data
- **Proxy** till Open-Meteo/Elpriset APIs

**Databaser:**
```
PostgreSQL (auth):
  - users (email, password_hash, api_key)
  - devices (device_id, user_id, paired_at)

Redis (cache):
  - weather:{lat},{lon} → JSON (TTL: 15 min)
  - prices:{region}:{date} → JSON (TTL: 24 hours)
```

**Endpoints:**
```
POST /api/v1/auth/register
POST /api/v1/auth/login
POST /api/v1/devices/pair

GET  /api/v1/weather?lat={lat}&lon={lon}    ← Cache-proxy
GET  /api/v1/prices?region={region}&date={date}  ← Cache-proxy
```

---

### 2️⃣ Edge Device (C Daemon)

**Ansvar:**
- Store user config (LOKALT i SQLite)
- Fetch weather/prices från **central server** (cache-proxy)
- Compute energy plans (LOKALT)
- Store history (LOKALT)

**Databas (SQLite):**
```
/var/lib/gridguard/gridguard.db

Tables:
  - device_info (user_id, api_key, email)
  - user_config (solar, battery, consumption, location)
  - energy_plan_history (generated_at, plan_json)

⚠️ INGEN weather/price cache lokalt!
  (hämtas alltid från central server)
```

**Komponenter:**
```
AuthClient.c       → Talk to central server (auth + data fetch)
LocalDB.c          → Manage local SQLite
Compute.c          → Calculate energy plans
ConfigManager.c    → Manage user config
```

---

### 3️⃣ Web Platform (Next.js - Optional)

**Ansvar:**
- User-friendly interface för registration
- Device pairing
- View results (via device API)

**NOT in scope för kurs-projektet** (kan läggas till senare)

---

## Data Flow

### Flow 1: Initial Setup (En Gång)

```
1. User registrerar sig
   → POST /api/v1/auth/register {email, password}
   → Server: Skapar account, genererar API key
   → Response: {user_id, api_key}

2. Device setup
   → gridguard setup
   → User anger email + password
   → Device anropar: POST /api/v1/auth/login
   → Får API key
   → Sparar API key lokalt i SQLite

3. Device configuration
   → gridguard configure
   → User anger: location, solar, battery, consumption
   → Sparas LOKALT i SQLite
   → ⚠️ ALDRIG skickat till central server!

✅ Setup klar!
```

---

### Flow 2: Daily Usage (Varje Request)

```
User: gridguard forecast

Device daemon:

  1️⃣ Läs user config (LOKALT från SQLite)
     → Location: Stockholm (59.33, 18.07)
     → Solar: 20m², 0.18 efficiency
     → Battery: 10kWh
     → Consumption: 15kWh/dag

  2️⃣ Fetch weather från central server
     → GET https://api.gridguard.se/api/v1/weather?lat=59.33&lon=18.07
     → Header: Authorization: Bearer {api_key}

     Central server:
       → Kollar cache: redis.get("weather:59.33,18.07")
       → HIT? → Return cached data (1ms)
       → MISS? → Fetch från Open-Meteo → Cache (TTL 15min) → Return

  3️⃣ Fetch prices från central server
     → GET https://api.gridguard.se/api/v1/prices?region=SE3&date=2026-02-18
     → Header: Authorization: Bearer {api_key}

     Central server:
       → Kollar cache: redis.get("prices:SE3:2026-02-18")
       → HIT? → Return cached data
       → MISS? → Fetch från Elpriset → Cache (TTL 24h) → Return

  4️⃣ Compute energy plan (LOKALT på device)
     → Input: user_config + weather + prices
     → Output: Hourly energy plan
     → Beräkning händer LOKALT (server ser inte)

  5️⃣ Save result (LOKALT)
     → INSERT INTO energy_plan_history

  6️⃣ Return to user
     → Print forecast

✅ Central server blev ALDRIG tillfrågad om user config
✅ Computation hände lokalt
✅ Result finns bara på device
```

---

### Flow 3: Cache Efficiency (Multi-User)

```
Scenario: 1000 users i Stockholm, alla begär forecast samtidigt

Kl 10:00:00 - User A
  → Device: GET /api/v1/weather?lat=59.33&lon=18.07
  → Server: Cache MISS → Fetch från Open-Meteo → Save to Redis
  → Response time: 500ms (API fetch)

Kl 10:00:05 - User B
  → Device: GET /api/v1/weather?lat=59.33&lon=18.07
  → Server: Cache HIT (från User A) → Return från Redis
  → Response time: 5ms ⚡

Kl 10:00:10 - User C
  → Server: Cache HIT → 5ms ⚡

Kl 10:00:15 - User D
  → Server: Cache HIT → 5ms ⚡

... (996 more users)

Kl 10:00:20 - User Z
  → Server: Cache HIT → 5ms ⚡

Result:
  - 1 API call to Open-Meteo
  - 999 cache hits
  - Average response time: ~10ms
  - All users get DIFFERENT energy plans (based on their configs)

✅ Efficient
✅ Fast
✅ Scalable
```

---

## Implementation Roadmap

### Fas 0: Förstå Arkitekturen ✅

**Mål:** Team förstår systemet innan implementation

**Aktiviteter:**
- Läs denna guide
- Rita system på whiteboard
- Diskutera data flow
- Förstå separation of concerns

**Output:** Alla förstår vad som ska byggas

---

### Fas 1: Central Server Foundation

**Mål:** Bygg minimal central server med auth + cache

**Komponenter:**

1. **Auth Database (PostgreSQL)**
   - Users table
   - Devices table
   - Basic schema

2. **Auth Endpoints**
   - POST /api/v1/auth/register
   - POST /api/v1/auth/login
   - POST /api/v1/devices/pair

3. **Cache Setup (Redis)**
   - Install Redis
   - Configure TTL
   - Test basic set/get

**Test:**
```bash
curl -X POST https://api.gridguard.se/api/v1/auth/register \
  -d '{"email":"test@example.com","password":"test123"}'

Response: {"user_id":"...", "api_key":"sk_live_..."}
```

**Tid:** 1-2 dagar
**Ansvarig:** Backend-team (Python/Node.js)

---

### Fas 2: Cache Proxy Endpoints

**Mål:** Implementera weather/price cache-proxy

**Endpoints:**

1. **GET /api/v1/weather**
   - Kollar Redis cache först
   - Fetch från Open-Meteo om miss
   - Sparar till cache (TTL 15min)
   - Returnerar JSON

2. **GET /api/v1/prices**
   - Kollar Redis cache först
   - Fetch från Elpriset om miss
   - Sparar till cache (TTL 24h)
   - Returnerar JSON

**Pseudokod:**
```python
@app.get("/api/v1/weather")
def get_weather(lat: float, lon: float, api_key: str):
    # 1. Verify API key
    if not verify_api_key(api_key):
        return 401

    # 2. Check cache
    cache_key = f"weather:{lat},{lon}"
    cached = redis.get(cache_key)
    if cached:
        return json.loads(cached)

    # 3. Fetch from Open-Meteo
    data = fetch_open_meteo(lat, lon)

    # 4. Save to cache
    redis.setex(cache_key, 900, json.dumps(data))  # 15 min

    # 5. Return
    return data
```

**Test:**
```bash
curl https://api.gridguard.se/api/v1/weather?lat=59.33&lon=18.07 \
  -H "Authorization: Bearer sk_live_..."

First request: 500ms (fetches from API)
Second request: 5ms (cache hit)
```

**Tid:** 2-3 dagar
**Ansvarig:** Backend-team

---

### Fas 3: Edge Device - Local Database

**Mål:** Implementera SQLite databas för lokal data

**Komponenter:**

1. **Database.c/h** - SQLite wrapper
   - Init database
   - Execute queries
   - Error handling

2. **LocalDB.c/h** - Domain-specific operations
   - Save/load device info
   - Save/load user config
   - Save/load energy plans

**Schema:**
```sql
-- Device info (från registration)
CREATE TABLE device_info (
    device_id TEXT PRIMARY KEY,
    user_id TEXT,
    email TEXT,
    api_key TEXT  -- För att anropa central server
);

-- User config (PRIVAT - aldrig till server)
CREATE TABLE user_config (
    id INTEGER PRIMARY KEY CHECK (id = 1),
    location_name TEXT,
    latitude REAL,
    longitude REAL,
    region TEXT,
    solar_panel_area_m2 REAL,
    battery_capacity_kwh REAL,
    consumption_avg_daily_kwh REAL
);

-- Energy plan history
CREATE TABLE energy_plan_history (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    generated_at INTEGER,
    plan_json TEXT
);
```

**Test:**
```c
Database db;
Database_Init(&db, "/tmp/test.db");

DeviceInfo info = {
    .deviceId = "dev_123",
    .userId = "user_456",
    .apiKey = "sk_live_..."
};
LocalDB_SaveDeviceInfo(&db, &info);

// Verify
DeviceInfo loaded;
LocalDB_GetDeviceInfo(&db, &loaded);
assert(strcmp(loaded.apiKey, "sk_live_...") == 0);
```

**Tid:** 2-3 dagar
**Ansvarig:** C-team (ni!)

---

### Fas 4: Edge Device - Auth Client

**Mål:** Kommunicera med central server för auth

**Komponenter:**

1. **HttpClient.c/h** - Generic HTTP client
   - GET request
   - POST request
   - Header handling (Authorization)
   - JSON parsing (cJSON)

2. **AuthClient.c/h** - Auth-specific
   - Register()
   - Login()
   - PairDevice()

**Pseudokod:**
```c
int AuthClient_Login(const char *email, const char *password,
                     LoginResponse *responseOut) {
    // 1. Build JSON payload
    char payload[512];
    snprintf(payload, sizeof(payload),
             "{\"email\":\"%s\",\"password\":\"%s\"}",
             email, password);

    // 2. POST to server
    char response[2048];
    HttpClient_Post("https://api.gridguard.se/api/v1/auth/login",
                    payload, response);

    // 3. Parse JSON response
    cJSON *json = cJSON_Parse(response);
    strcpy(responseOut->userId, cJSON_GetString(json, "user_id"));
    strcpy(responseOut->apiKey, cJSON_GetString(json, "api_key"));

    return 0;
}
```

**Test:**
```c
LoginResponse resp;
AuthClient_Login("test@example.com", "test123", &resp);
printf("API Key: %s\n", resp.apiKey);
```

**Tid:** 2-3 dagar
**Ansvarig:** C-team

---

### Fas 5: Edge Device - Data Fetcher

**Mål:** Fetch weather/price från central server cache-proxy

**Komponenter:**

1. **DataFetcher.c/h** - Fetch via central server
   - FetchWeather(lat, lon, apiKey)
   - FetchPrices(region, date, apiKey)

**Pseudokod:**
```c
int DataFetcher_GetWeather(double lat, double lon,
                           const char *apiKey,
                           char *jsonOut) {
    // 1. Build URL
    char url[512];
    snprintf(url, sizeof(url),
             "https://api.gridguard.se/api/v1/weather?lat=%.2f&lon=%.2f",
             lat, lon);

    // 2. Add auth header
    char authHeader[256];
    snprintf(authHeader, sizeof(authHeader),
             "Authorization: Bearer %s", apiKey);

    // 3. GET request
    return HttpClient_Get(url, authHeader, jsonOut);
}
```

**Test:**
```c
char weatherJson[8192];
DataFetcher_GetWeather(59.33, 18.07, "sk_live_...", weatherJson);
printf("Weather: %s\n", weatherJson);
```

**Tid:** 1-2 dagar
**Ansvarig:** C-team

---

### Fas 6: Edge Device - Compute Engine

**Mål:** Beräkna energiplaner lokalt

**Komponenter:**

1. **Compute.c/h** - Energy calculations
   - ParseWeatherData()
   - ParsePriceData()
   - GenerateEnergyPlan()

**Input:**
- UserConfig (från lokal SQLite)
- Weather JSON (från central server)
- Price JSON (från central server)

**Output:**
- EnergyData struct (hourly plan)

**Flow:**
```c
void Compute_GeneratePlan(UserConfig *config,
                          const char *weatherJson,
                          const char *priceJson,
                          EnergyData *planOut) {
    // 1. Parse weather
    WeatherData weather;
    ParseWeatherData(weatherJson, &weather);

    // 2. Parse prices
    PriceData prices;
    ParsePriceData(priceJson, &prices);

    // 3. For each hour
    for (int hour = 0; hour < 24; hour++) {
        // Solar production
        double solar_kw = CalculateSolarProduction(
            config->solar,
            weather.radiation[hour],
            weather.temperature[hour]
        );

        // Battery optimization
        Action action = OptimizeBattery(
            solar_kw,
            config->consumption.baseLoadKw,
            prices.sek_per_kwh[hour],
            config->battery
        );

        // Save to plan
        planOut->hours[hour].action = action;
        planOut->hours[hour].solar_kwh = solar_kw;
        // ...
    }
}
```

**Test:**
```c
UserConfig config = {
    .solar = {.panelAreaM2 = 20.0, .panelEfficiency = 0.18},
    .battery = {.capacityKwh = 10.0},
    .consumption = {.baseLoadKw = 1.0}
};

EnergyData plan;
Compute_GeneratePlan(&config, weatherJson, priceJson, &plan);

// Verify
assert(plan.hours[12].solar_kwh > 0);  // Solar production at noon
```

**Tid:** 3-4 dagar
**Ansvarig:** C-team

---

### Fas 7: Integration & CLI

**Mål:** Sätt ihop alla delar + user-friendly CLI

**Komponenter:**

1. **Setup.c** - Device setup CLI
   - Register/login
   - Pair device
   - Configure user settings

2. **CLI.c** - Main CLI interface
   - `gridguard forecast`
   - `gridguard config view`
   - `gridguard history`

**Flow:**
```bash
# First time
$ sudo gridguard setup
  Email: john@example.com
  Password: ****
  ✅ Logged in
  ✅ Device paired

  Configure device:
  Location: Stockholm
  Solar: 20m²
  Battery: 10kWh
  ✅ Saved locally

# Daily use
$ gridguard forecast
  Fetching weather... ✅
  Fetching prices... ✅
  Computing plan... ✅

  Energy Plan for 2026-02-18:
  12:00 - CHARGE_BATTERY (Solar: 2.3kW, Price: 0.45 SEK)
  13:00 - CHARGE_BATTERY (Solar: 2.5kW, Price: 0.48 SEK)
  ...
```

**Tid:** 2-3 dagar
**Ansvarig:** C-team

---

### Fas 8: Testing & Documentation

**Mål:** Verifiera att allt fungerar + dokumentera

**Test Scenarios:**

1. **End-to-End Test**
   ```
   1. Register new user
   2. Setup device
   3. Configure device
   4. Request forecast
   5. Verify result
   ```

2. **Cache Test**
   ```
   1. User A requests weather
   2. Verify API call to Open-Meteo
   3. User B requests same weather
   4. Verify cache hit (no API call)
   ```

3. **Privacy Test**
   ```
   1. Setup device with solar config
   2. Request forecast
   3. Check server logs
   4. Verify server NEVER logged solar config
   ```

**Documentation:**
- README.md (how to use)
- API documentation (for central server)
- Architecture diagram (update)

**Tid:** 2-3 dagar
**Ansvarig:** Hela teamet

---

## Vad Ska Implementeras

### Central Server (Backend Team)

| Komponent | Språk | Tid | Prioritet |
|-----------|-------|-----|-----------|
| PostgreSQL schema | SQL | 1h | 🔴 Hög |
| User registration | Python/Node | 4h | 🔴 Hög |
| Login + JWT | Python/Node | 4h | 🔴 Hög |
| Device pairing | Python/Node | 2h | 🔴 Hög |
| Redis setup | Config | 1h | 🔴 Hög |
| Weather cache-proxy | Python/Node | 6h | 🔴 Hög |
| Price cache-proxy | Python/Node | 4h | 🔴 Hög |
| Deployment (Docker) | DevOps | 4h | 🟡 Medium |

**Total:** ~26 timmar (3-4 dagar)

---

### Edge Device (C Team - Er!)

| Komponent | Språk | Tid | Prioritet |
|-----------|-------|-----|-----------|
| Database.c (SQLite wrapper) | C | 4h | 🔴 Hög |
| LocalDB.c (domain logic) | C | 6h | 🔴 Hög |
| HttpClient.c (HTTP requests) | C | 8h | 🔴 Hög |
| AuthClient.c (login/register) | C | 4h | 🔴 Hög |
| DataFetcher.c (weather/price) | C | 4h | 🔴 Hög |
| Compute.c (energy calculations) | C | 12h | 🔴 Hög |
| Setup.c (CLI setup) | C | 6h | 🔴 Hög |
| CLI.c (daily commands) | C | 4h | 🔴 Hög |
| Unit tests | C | 8h | 🟡 Medium |
| Integration tests | C | 4h | 🟡 Medium |

**Total:** ~60 timmar (7-8 dagar)

---

### Optional (Senare)

| Komponent | Språk | Tid | Prioritet |
|-----------|-------|-----|-----------|
| Next.js web platform | React/Next.js | 40h | 🟢 Låg |
| Mobile app | React Native | 60h | 🟢 Låg |
| Grafana dashboards | Config | 4h | 🟢 Låg |
| Watchdog/Daemon | C | 12h | 🟡 Medium |

---

## Privacy & Security

### Vad Central Server Vet

```
✅ Email (för login)
✅ User ID
✅ API key
✅ Device ID
✅ Requests till /api/v1/weather med lat/lon
✅ Requests till /api/v1/prices med region
✅ Timestamp för requests
```

### Vad Central Server INTE Vet

```
❌ Solar panel config (20m², 0.18 efficiency)
❌ Battery size (10kWh, 5kW charge rate)
❌ Consumption patterns (15kWh/dag)
❌ Computed energy plans
❌ Historical data
❌ Actual energy production
❌ Real-time battery status
```

### Privacy Analys

**Risk:** Server kan gissa approximate location från lat/lon

**Mitigering:**
1. Använd 2 decimaler precision (10km accuracy)
   - 59.33,18.07 → 59.30,18.00
2. Alternativt: Använd city name istället
   - "Stockholm" → Server hämtar default lat/lon
3. Terms of Service: Klargör att location används bara för weather

**Resultat:** Minimal privacy leakage, acceptable för use case

---

### Security Checklist

#### Central Server

- [ ] HTTPS only (TLS 1.3)
- [ ] API key stored hashed in database
- [ ] Rate limiting (100 req/min per user)
- [ ] Input validation (lat/lon ranges, etc.)
- [ ] SQL injection prevention (prepared statements)
- [ ] Password hashing (bcrypt, cost 12)
- [ ] JWT expiration (24h)
- [ ] CORS headers (restrict origins)

#### Edge Device

- [ ] API key stored encrypted in SQLite
- [ ] Database file permissions (chmod 600)
- [ ] Config file permissions (chmod 600)
- [ ] Validate API responses (check JSON schema)
- [ ] Timeout på HTTP requests (10s max)
- [ ] Verify HTTPS certificates
- [ ] No hardcoded secrets in code

---

## Sammanfattning

### Vad Ni Ska Bygga

**3 system:**

1. **Central Server** - Auth + Cache-proxy (Python/Node.js)
2. **Edge Device** - Config + Compute (C daemon)
3. **Web Platform** - UI (Next.js - optional)

### Hur De Pratar Med Varandra

```
Edge Device → Central Server:
  - POST /api/v1/auth/login (vid setup)
  - GET /api/v1/weather (varje request)
  - GET /api/v1/prices (varje request)

Central Server → Open-Meteo/Elpriset:
  - Endast vid cache MISS
  - 1 request per 15min (weather)
  - 1 request per dag (prices)
```

### Varför Denna Design Är Optimal

| Aspekt | Resultat |
|--------|----------|
| **Privacy** | User config aldrig på server ✅ |
| **Efficiency** | 1000 users = 1 API call ✅ |
| **Scalability** | Compute på edge = gratis ✅ |
| **Cost** | ~$20/månad (minimal server) ✅ |
| **Offline** | Optional lokal cache (future) ✅ |

---

## Nästa Steg

1. ✅ Läs denna guide (KLAR!)
2. ⏭️ Bestäm vem som bygger vad (backend vs edge)
3. ⏭️ Börja med Fas 1 (Central Server Foundation)
4. ⏭️ Parallellt: Fas 3 (Edge Device - Local Database)
5. ⏭️ Integrera när båda är klara

**Lycka till!** 🚀
