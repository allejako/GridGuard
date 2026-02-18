# GridGuard Edge Computing Architecture

**Datum:** 2026-02-18
**Filosofi:** Privacy-First, Edge Computing, Zero-Knowledge Server

---

## 🎯 Fundamental Idé

GridGuard använder en **edge computing** modell där:

1. **Central server** = Enbart user accounts (email + password)
2. **Edge devices** = Allt annat (config, data, beräkningar)

### Varför?

**Privacy:** Vi vill ALDRIG veta:
- Hur mycket solpaneler en user har
- Batteristorlek
- Exakt location
- Consumption patterns
- Historical data

**Scalability:**
- 1 user = samma server cost
- 1,000,000 users = samma server cost
- All computation händer på user's egen device

**Offline-First:**
- Device kan köra helt utan internet (efter initial setup)
- User äger sin data 100%

---

## 📐 System Översikt

```
┌──────────────────────────────────────────────────────────────┐
│  CENTRAL SERVER (PostgreSQL)                                 │
│  api.gridguard.se                                            │
│                                                               │
│  Lagrar:                                                      │
│    ✅ Email + password hash                                  │
│    ✅ User ID (UUID)                                         │
│    ✅ API key (för device auth)                              │
│    ✅ Subscription info                                      │
│                                                               │
│  Lagrar INTE:                                                 │
│    ❌ Solar panel config                                     │
│    ❌ Battery config                                         │
│    ❌ Location (lat/lon)                                     │
│    ❌ Energy data                                            │
│    ❌ Historical data                                        │
└──────────────────────────────────────────────────────────────┘
                            ▲
                            │ HTTPS/REST
                            │ Används ENDAST för:
                            │  - Register account
                            │  - Login
                            │  - Pair device
                            │
        ┌───────────────────┼───────────────────┐
        │                   │                   │
        ▼                   ▼                   ▼
┌──────────────┐    ┌──────────────┐    ┌──────────────┐
│ Raspberry Pi │    │ Docker/VPS   │    │ Laptop       │
│              │    │              │    │              │
│ LOCAL SQLite │    │ LOCAL SQLite │    │ LOCAL SQLite │
│              │    │              │    │              │
│ Innehåll:    │    │ Innehåll:    │    │ Innehåll:    │
│  - Config    │    │  - Config    │    │  - Config    │
│  - Cache     │    │  - Cache     │    │  - Cache     │
│  - History   │    │  - History   │    │  - History   │
└──────────────┘    └──────────────┘    └──────────────┘
       │                   │                   │
       │                   │                   │
       ▼                   ▼                   ▼
┌──────────────────────────────────────────────────────┐
│  EXTERNAL APIs (Public, Gratis)                      │
│                                                       │
│  Open-Meteo API     - Weather forecasts              │
│  Elpriset.se API    - Spot prices                    │
│                                                       │
│  ⚠️  Devices hämtar DIREKT från dessa!              │
│  ⚠️  ER central server är ALDRIG involverad!        │
└──────────────────────────────────────────────────────┘
```

---

## 🔄 Tre Faser

### Fas 1: Registration (En gång)

**User → Central Server**

```
User på web/CLI:
  1. Anger email + password
  2. Central server:
     - Skapar account
     - Genererar API key
     - Returnerar API key
  3. Device sparar API key lokalt
```

**Vad central server vet efter detta:**
- Email: john@example.com
- User ID: 123e4567-e89b-12d3-a456-426614174000
- API key: sk_live_abc123xyz...

**Vad central server INTE vet:**
- Ingenting om solar panels, battery, location, etc.

---

### Fas 2: Device Setup (En gång per device)

**Allt händer LOKALT - ingen server-kommunikation**

```
gridguard configure

Prompt:
  - Location? → Stockholm
  - Solar panels? → 20m², 18% efficiency
  - Battery? → 10kWh, 5kW charge rate
  - Consumption? → 15kWh/day average

Data sparas i:
  /var/lib/gridguard/gridguard.db (LOCAL SQLite)

⚠️ ALDRIG skickat till central server!
```

---

### Fas 3: Normal Operation (Daglig användning)

**100% Offline - ingen central server kontakt**

```
User: gridguard forecast

GridGuard daemon:
  1. Läser config från LOKAL SQLite:
     → Lat/lon: 59.33, 18.07
     → Solar: 20m², 0.18 efficiency
     → Battery: 10kWh

  2. Kollar LOKAL cache (hybrid L1+L2):
     → Weather för Stockholm?
     → Spot prices för SE3?

  3. Om cache MISS:
     → Hämtar från Open-Meteo API (DIREKT)
     → Hämtar från Elpriset.se API (DIREKT)
     → ⚠️ INTE från er central server!

  4. Beräknar energiplan LOKALT

  5. Sparar resultat LOKALT i SQLite

  6. Returnerar till user

✅ Central server kontaktas ALDRIG
✅ Complete privacy
✅ Fungerar offline
```

---

## 🗃️ Databas-strategi

### Central Server (PostgreSQL)

```sql
-- Minimal data - ENDAST autentisering
CREATE TABLE users (
    user_id UUID PRIMARY KEY,
    email VARCHAR(255) UNIQUE NOT NULL,
    password_hash VARCHAR(255) NOT NULL,
    api_key VARCHAR(64) UNIQUE NOT NULL,
    subscription_tier VARCHAR(20) DEFAULT 'free',
    created_at TIMESTAMP DEFAULT NOW()
);

-- Ingen user data! Inga configs!
```

### Edge Device (SQLite)

```sql
-- All user data - LOKAL ENDAST
CREATE TABLE user_config (
    -- Location (PRIVAT)
    location_name TEXT,
    latitude REAL,
    longitude REAL,
    region TEXT,

    -- Solar (PRIVAT)
    solar_panel_area_m2 REAL,
    solar_panel_efficiency REAL,

    -- Battery (PRIVAT)
    battery_capacity_kwh REAL,

    -- Consumption (PRIVAT)
    consumption_avg_daily_kwh REAL
);

-- Weather cache (från Open-Meteo)
CREATE TABLE weather_cache (
    location_key TEXT PRIMARY KEY,
    forecast_json TEXT,
    expires_at INTEGER
);

-- Price cache (från Elpriset.se)
CREATE TABLE price_cache (
    region TEXT,
    date TEXT,
    prices_json TEXT,
    PRIMARY KEY (region, date)
);

-- Energy plan history (PRIVAT)
CREATE TABLE energy_plan_history (
    generated_at INTEGER,
    plan_json TEXT
);
```

---

## 🔐 Security & Privacy

### Vad central server vet

```
✅ Email (för login)
✅ User ID (UUID)
✅ API key (för device auth)
✅ Subscription tier (free/pro)
✅ Created date
```

### Vad central server INTE vet

```
❌ Location (lat/lon)
❌ City/address
❌ Solar panel size
❌ Battery capacity
❌ Consumption patterns
❌ Energy production
❌ Historical data
❌ Forecast requests
```

### GDPR Compliance

**Right to be forgotten:**
1. User begär deletion via web
2. Central server raderar user record (email + password)
3. Device förlorar access (API key invalid)
4. User kan manuellt radera lokal SQLite fil

**Data portability:**
1. All data finns i lokal SQLite fil
2. User kan kopiera `gridguard.db`
3. User äger sin data 100%

---

## 📡 Data Sources

### Weather Data

```
Device → Open-Meteo API (DIREKT)
  - URL: https://api.open-meteo.com/v1/forecast
  - Parameters: latitude, longitude, hourly variables
  - Cost: Gratis
  - Rate limit: 10,000 requests/day
```

### Spot Price Data

```
Device → Elpriset.se API (DIREKT)
  - URL: https://www.elpriset.se/api/v1/prices
  - Parameters: year, month, region
  - Cost: Gratis
  - No rate limit
```

### ⚠️ Er Central Server

```
ALDRIG involverad i weather/price data!

Används ENDAST för:
  - POST /api/v1/auth/register
  - POST /api/v1/auth/login
  - POST /api/v1/devices/pair
```

---

## 🏗️ Implementation Komponenter

### 1. Central Server (Python/Node.js/Go)

```
API Endpoints:
  POST /api/v1/auth/register
    → Input: {email, password}
    → Output: {user_id, api_key, jwt}

  POST /api/v1/auth/login
    → Input: {email, password}
    → Output: {user_id, api_key, jwt}

  POST /api/v1/devices/pair
    → Input: {device_name}
    → Header: Authorization: Bearer {jwt}
    → Output: {device_id}

Database: PostgreSQL
  - users table
  - devices table
  - sessions table (optional)
```

### 2. Edge Device (C)

```
Komponenter:
  - AuthClient.c       → Talk to central server (auth only)
  - LocalDB.c          → Manage local SQLite
  - HybridCache.c      → L1 (shared memory) + L2 (SQLite) cache
  - Fetcher.c          → Fetch from Open-Meteo/Elpriset (DIREKT)
  - Compute.c          → Calculate energy plans (lokalt)
  - ConfigManager.c    → Manage user config (lokalt)

Database: SQLite (/var/lib/gridguard/gridguard.db)
  - user_config
  - weather_cache
  - price_cache
  - energy_plan_history
```

---

## 🚀 Deployment Scenarios

### Scenario 1: Raspberry Pi (Recommended)

```bash
# Install
sudo apt-get install gridguard

# Setup
sudo gridguard setup
  → Register account (talks to central server)
  → Configure device (LOCAL only)

# Run
sudo systemctl enable gridguard
sudo systemctl start gridguard

# Data location
/var/lib/gridguard/gridguard.db  (LOCAL SQLite)
/var/log/gridguard/              (Logs)
```

### Scenario 2: Docker

```bash
docker run -d \
  --name gridguard \
  -v gridguard_data:/var/lib/gridguard \
  -p 8080:8080 \
  gridguard:latest

# Data är persistent i volume 'gridguard_data'
```

### Scenario 3: VPS/Cloud

```bash
# Same as Raspberry Pi
# Data finns på VPS disk
# User kan ta backup av SQLite fil
```

---

## 💡 Varför Denna Arkitektur?

### 1. Privacy

**Traditionell arkitektur (BAD):**
```
User → Central Server (har ALL data) → Compute → Return
  ❌ Server kan se allt
  ❌ Data breach = all user data läcker
  ❌ Måste lita på server operator
```

**Er arkitektur (GOOD):**
```
User → Local Device (har ALL data) → Compute → Return
  ✅ Server kan INTE se user data
  ✅ Data breach = bara email/password (ingen user data)
  ✅ Zero-knowledge design
```

### 2. Scalability

**Traditionell:**
- 1 user = X compute + Y storage cost
- 1,000,000 users = 1,000,000X compute + 1,000,000Y storage cost

**Er arkitektur:**
- 1 user = minimal auth cost
- 1,000,000 users = samma minimal auth cost
- Compute händer på user's device (gratis för er!)

### 3. Offline Capability

**Traditionell:**
- Kräver internet för varje request
- Server down = ingen kan använda

**Er arkitektur:**
- Efter setup: fungerar helt offline
- Lokal cache för weather/prices
- Server down = users påverkas inte

### 4. Cost

**Traditionell:**
- Stor databas
- Många CPU-cores
- Backup infrastructure
- $1000s/månad

**Er arkitektur:**
- Liten PostgreSQL (bara auth)
- Minimal compute (bara auth endpoints)
- Ingen user data backup
- $10-50/månad

---

## 🎓 Kursmål Coverage

### Vecka 5: IPC & Shared Memory

✅ **Hybrid cache** använder:
- Shared memory (L1 cache)
- SQLite (L2 cache)
- Process-shared mutex

### Vecka 6-9: C++

✅ **CLI client** kan byggas i C++:
- RAII för resource management
- STL för data structures

---

## 📊 Jämförelse: Centralized vs Edge

| Aspekt | Centralized | Edge (Er arkitektur) |
|--------|-------------|----------------------|
| **Privacy** | Server ser allt | Zero-knowledge |
| **GDPR** | Komplex compliance | Enkelt (data ägs av user) |
| **Cost** | Hög (skalar med users) | Låg (konstant) |
| **Scalability** | Begränsad | Obegränsad |
| **Offline** | ❌ Kräver server | ✅ Fungerar offline |
| **Data ownership** | Server äger | User äger |
| **Performance** | Network latency | Lokal (instant) |

---

## ✅ Sammanfattning

### Fundamental Tanke

1. **Central server** = Bara email + password (minimal trust)
2. **Edge device** = All user data + computation (zero-knowledge)
3. **External APIs** = Weather/prices hämtas direkt (open-source data)

### Data Flow

```
Registration:  User → Central Server → API key → Store locally
Configuration: User → Local SQLite (ALDRIG till server)
Forecast:      Local SQLite → External APIs → Compute locally → Return
               ⚠️ Central server ALDRIG involverad!
```

### Privacy Guarantee

**Central server kan ALDRIG:**
- Se var du bor
- Veta hur mycket el du producerar
- Veta dina consumption patterns
- Läsa din historical data

**Du äger din data 100%** ✅

---

## 🚧 Nästa Steg

1. ✅ Förstå fundamental tanke (detta dokument)
2. ⏭️ Implementera hybrid cache (HYBRID_CACHE_IMPLEMENTATION.md)
3. ⏭️ Implementera AuthClient (talk to central server)
4. ⏭️ Implementera LocalDB (manage local SQLite)
5. ⏭️ Implementera Setup CLI (device setup)

**Frågor?**
