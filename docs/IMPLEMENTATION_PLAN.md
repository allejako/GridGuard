# GridGuard — Implementationsplan
**Datum:** 2026-02-23
**Deadline:** Kursvecka 12
**Kurs:** Systemprogrammering och introduktion till C++

---

## Innehåll

1. [Systemöversikt](#systemöversikt)
2. [Ansvarsfördelning](#ansvarsfördelning)
3. [Vad som är klart](#vad-som-är-klart)
4. [Implementationsordning](#implementationsordning)
5. [Steg-för-steg per komponent](#steg-för-steg-per-komponent)
6. [Kursmålskoppling](#kursmålskoppling)

---

## Systemöversikt

GridGuard är ett energioptimeringssystem med tre separata delar:

```
PLATTFORM (web, extern)
  Kund registrerar sig och betalar
  Utfärdar JWT-token till kunden
        │
        │ JWT
        ▼
C++ CLI-KLIENT  ──── HTTP + JWT ────▶  GRIDGUARD SERVER (C)
  Identifierar sig                       Validerar JWT
  Skickar förfrågningar                  Hämtar user config (plats + solpaneler)
  Visar rekommendation                   Beräknar BUY / SELL / IDLE per timme
        ▲                                       │
        │                               Extern API (SMHI, Elpriset)
        └──────────── Svar ─────────────────────┘
```

### Systemets output — BUY / SELL / IDLE

GridGuard beräknar **inte** batteristyrning. Systemet ger kunden en rekommendation varje kvart:

| Signal | Betydelse |
|--------|-----------|
| **BUY** | Spotpriset är lågt — köp el från nätet nu |
| **SELL** | Solproduktionen är hög och/eller priset är högt — sälj tillbaka till nätet |
| **IDLE** | Inget speciellt att göra — normalt läge |

Kunden agerar sedan på dessa signaler med sin egen utrustning.

### Viktiga avgränsningar

- **Plattformen** (registrering, betalning, JWT-utfärdande) är ett separat system — **inte C**.
- **GridGuard C-servern** validerar JWT men utfärdar det aldrig.
- **JWT används** för att identifiera användaren och ge åtkomst till systemet.
- **ESP32/klienter** lagrar JWT och skickar med det vid varje förfrågan.

---

---

## Vad som är klart

- ✅ TCP-server med ThreadPool (20 workers)
- ✅ GridGuard-pipeline: FetchWorker → ParseWorker → ComputeWorker
- ✅ Hämtning från SMHI, Open-Meteo, Elprisetjustnu.se
- ✅ JSON-cache på fetch-nivå (JsonCache)
- ✅ Daemon + Watchdog med PID-fil och heartbeat
- ✅ Logger, SignalHandler
- ✅ Grundläggande TCP-klient i C++

---

## Implementationsordning

Ordningen är baserad på kursmålsprioritering och beroenden.

```
Fas 1: HTTP-lager          ← Allt annat bygger på detta
Fas 2: JWT-validering      ← Kräver HTTP-lager
Fas 3: SQLite user configs ← Kräver JWT (för user_id)
Fas 4: Shared Memory Cache ← Alex, parallellt med Fas 2-3
Fas 5: C++ CLI-klient      ← Kevin, kräver Fas 1-2
Fas 6: Profilering         ← Kursmål 10-11, kräver fungerande system
Fas 7: Dokumentation       ← Kursmål 12, löpande
```

---

## Steg-för-steg per komponent

---

### Fas 1 — HTTP-lager över TCP

**Kursmål:** 8 (IPC/sockets)
**Ansvarig:** Alla
**Filer:** `src/server/ClientHandler.c`, `src/network/server/TCPServer.c`

**Vad det innebär:**
Nuvarande server förstår bara råtext (`forecast stockholm SE3`).
Vi lägger till HTTP-protokollparsning direkt i ClientHandler — ingen extern lib.

**Steg:**

1. Skapa `src/network/http/HTTPRequest.h` och `HTTPRequest.c`
   - Struct för method, path, headers, body
   - Funktion: parsa rå TCP-data till HTTPRequest

2. Skapa `src/network/http/HTTPResponse.h` och `HTTPResponse.c`
   - Byggfunktioner för `200 OK`, `401 Unauthorized`, `404 Not Found`
   - Hjälpfunktion för JSON-svar med rätt Content-Type-header

3. Skapa `src/network/http/HTTPRouter.c`
   - Routar paths till rätt handler:
     - `GET /weather`   → WeatherHandler
     - `GET /spotprice` → SpotPriceHandler
     - `GET /forecast`  → ForecastHandler
     - `GET /health`    → HealthHandler (ingen auth krävs)

4. Uppdatera `ClientHandler.c`
   - Ersätt textparsning med HTTPRequest-parsning
   - Skicka inkommande data till HTTPRouter

**Endpoints:**

| Method | Path | Auth | Beskrivning |
|--------|------|------|-------------|
| GET | `/health` | Nej | Serverstatus |
| GET | `/weather` | JWT | Väderdata för användarens plats |
| GET | `/spotprice` | JWT | Spotpriser för användarens region |
| GET | `/forecast` | JWT | BUY/SELL/IDLE per timme |

---

### Fas 2 — JWT-validering

**Kursmål:** 8 (systemnära protokoll)
**Ansvarig:** Alla
**Filer:** `src/infrastructure/auth/JWTValidator.h`, `JWTValidator.c`

**Vad det innebär:**
C-servern behöver bara **validera** JWT — inte utfärda det.
Plattformen delar en hemlig nyckel med servern via miljövariabel.

**Steg:**

1. Förstå JWT-strukturen
   - JWT = `base64(header).base64(payload).base64(signature)`
   - Payload innehåller: `sub` (user_id), `exp` (expiry), `iat` (issued at)

2. Skapa `JWTValidator.c`
   - Funktion: extrahera JWT från `Authorization: Bearer <token>` header
   - Funktion: base64url-avkoda header + payload
   - Funktion: verifiera HMAC-SHA256-signaturen med OpenSSL (finns redan på systemet)
   - Funktion: kontrollera att `exp` inte passerat
   - Funktion: extrahera `sub` (user_id) ur payload

3. Skapa Middleware-funktion i HTTPRouter
   - Anropas innan varje skyddad endpoint
   - Returnerar `401 Unauthorized` om JWT saknas eller är ogiltigt
   - Vid giltigt JWT: lägg `user_id` i request-kontexten

4. Konfigurera hemlig nyckel
   - Läses från miljövariabel `GRIDGUARD_JWT_SECRET`
   - Aldrig hårdkodad i källkod

---

### Fas 3 — SQLite för user configs

**Kursmål:** 8, 11 (persistens, optimering)
**Ansvarig:** Alex (synk med cache-arbetet)
**Filer:** `src/infrastructure/database/`

**Vad det innebär:**
När JWT är validerat och vi har `user_id` behöver vi hämta användarens konfiguration.
Systemet behöver bara veta **var** kunden är och **hur stora solpaneler** de har —
inget om batteri eller förbrukning (det hanterar kunden själv).

**Steg:**

1. Designa databasschemat
   - Tabell `users`: `user_id`, `api_key`, `active`, `created_at`
   - Tabell `user_configs`: `user_id`, `latitude`, `longitude`, `region`,
     `solar_area_m2`, `solar_efficiency`, `solar_orientation_degrees`, `solar_tilt_degrees`

2. Skapa `Database.c/h` — enkel SQLite-wrapper
   - Öppna/stänga databas
   - Köra prepared statements
   - Felhantering

3. Skapa `UserConfigDB.c/h`
   - Funktion: hämta user config för ett givet `user_id`
   - Funktion: spara/uppdatera user config

4. Integrera i ComputeWorker
   - Ersätt hårdkodade lat/lon/region och SolarConfig
     med data från SQLite baserat på `user_id` från JWT
   - Ta bort BatteryConfig och ConsumptionProfile ur pipeline

5. Skapa testdata
   - SQL-skript: `scripts/seed_db.sql` med 2-3 testanvändare

---

### Fas 4 — Shared Memory Cache (L1) + SQLite Cache (L2)

**Kursmål:** 8 (delat minne, POSIX IPC)
**Ansvarig:** Alex
**Filer:** `src/infrastructure/cache/`

**Vad det innebär:**
Väderdata och spotpriser delas mellan alla användare i samma område.
Vi bygger en tvånivå-cache: snabb shared memory (L1) + persistent SQLite (L2).

**Steg:**

1. Designa cache-strukturen
   - Nyckel: `"59.33,18.07"` för väder, `"SE3,2026-02-23"` för pris
   - Värde: rå JSON-sträng
   - TTL: väder 15 min, spotpris 1 dag

2. Implementera SharedCache (L1 — Shared Memory)
   - Använd `shm_open()` + `mmap()` för att skapa delat minnessegment
   - Process-shared mutex med `pthread_mutexattr_setpshared()`
   - Max 100 entries, LRU-eviction när fullt
   - Automatisk expire-check vid läsning

3. Implementera SQLiteCache (L2 — Persistent)
   - Tabell `weather_cache`: `location_key`, `json`, `fetched_at`, `expires_at`
   - Tabell `price_cache`: `region`, `date`, `json`, `fetched_at`
   - Prepared statements för alla operationer

4. Implementera HybridCache (L1 + L2 kombinerat)
   - Läsordning: kolla L1 → kolla L2 (promota till L1) → API-fetch (spara i båda)
   - Skriv alltid till både L1 och L2
   - Statistik: hits/misses per nivå

5. Integrera i FetchWorker
   - Ersätt nuvarande JsonCache med HybridCache
   - Logga cache hit-rate per request

6. Skriv enhetstester
   - test_shared_cache.c: SET/GET/expire/LRU
   - test_hybrid_cache.c: L1-hit, L2-hit, promotion, miss

---

### Fas 5 — C++ CLI-klient

**Kursmål:** 9 (C++, RAII, STL)
**Ansvarig:** Kevin
**Filer:** `src/client/`

**Vad det innebär:**
En terminalbaserad klient som autentiserar med JWT och hämtar
energiplan, väderdata och spotpriser från GridGuard-servern.

**Steg:**

1. Planera kommandostrukturen
   ```
   gridguard login             — autentisera med JWT-token
   gridguard logout            — ta bort sparad token
   gridguard help              — visa tillgängliga kommandon
   gridguard forecast          — hämta energiplan (BUY/SELL/IDLE per timme)
   gridguard weather           — väderdata
   gridguard spotprice         — spotpriser
   gridguard config set <key> <value>  — ändra config
   gridguard status            — serverstatus
   ```

2. Implementera `TokenManager` (RAII)
   - Läser JWT från `~/.gridguard/token`
   - Skriver JWT till fil vid inloggning
   - RAII-destruktor säkerställer att filen stängs korrekt
   - Använd `std::string` för token-hantering

3. Implementera `HTTPClient` i C++
   - Bygger ovanpå befintlig `TCPClient.cpp`
   - Lägger automatiskt till `Authorization: Bearer <token>` header
   - Parsar HTTP-svar och extraherar JSON-body
   - Använd `std::vector<std::string>` för headers

4. Implementera `ResponseFormatter`
   - Formaterar JSON-svar till läsbar terminaloutput
   - ASCII-tabell för spotpriser
   - Enkel ascii-graf för energiplan
   - Använd `std::map` för JSON-nyckelvärden

5. Implementera `ConfigManager`
   - Läser/skriver `~/.gridguard/config.json`
   - Validerar värden (panelarea > 0, etc.)
   - RAII-hantering av konfigurationsfil

6. Sätt ihop `main.cpp`
   - Parsning av CLI-argument med `std::vector<std::string> args`
   - **Auth-gate**: kolla om `~/.gridguard/token` finns och är giltig
     - Om inte inloggad: visa inloggningsvy oavsett vilket kommando som angavs
     - Om inloggad: routa till rätt kommando
   - Routing till rätt kommando
   - Felhantering med exceptions (try/catch)

   **Flöde:**
   ```
   gridguard <valfritt kommando>
        │
        ▼
   Är token sparad och giltig?
        │
      Nej ──▶  Visa inloggningsvy
               "GridGuard — logga in"
               "Klistra in din JWT-token: "
               [användaren skriver token]
               Validera + spara → fortsätt till kommandot
        │
       Ja
        │
        ▼
   Kör kommandot (forecast / weather / spotprice / ...)
   ```

   - `help` och `login` är alltid tillgängliga utan inloggning
   - `logout` är alltid tillgänglig utan inloggning

---

### Fas 6 — Profilering och optimering

**Kursmål:** 10, 11
**Ansvarig:** Alla
**Verktyg:** gprof, Valgrind/Massif

**Steg:**

1. Bygg med profileringsflags
   - `make profile` (redan konfigurerat i Makefile)

2. Kör mot realistisk belastning
   - Simulera 10-50 samtida klienter
   - Mät responstid med/utan cache-träff

3. Analysera med gprof
   - Identifiera tidskrävande funktioner
   - Dokumentera vilka funktioner som dominerar CPU-tid

4. Analysera minneanvändning med Valgrind/Massif
   - Verifiera inga läckor i workers
   - Kontrollera shared memory-hantering

5. Benchmarka cache-nivåerna
   - Mät L1 (shared memory) vs L2 (SQLite) vs API-fetch latens
   - Beräkna cache hit-rate vid realistisk last
   - Dokumentera resultaten med siffror

6. Optimera baserat på mätdata
   - Justera TTL-värden om hit-rate är låg
   - Identifiera flaskhalsar i pipeline (fetch/parse/compute)
   - Dokumentera varje optimering och dess effekt

---

### Fas 7 — Dokumentation

**Kursmål:** 12
**Ansvarig:** Alla
**Deadline:** Kursvecka 11 (vecka innan inlämning)

**Ska dokumenteras:**

1. **Systemdesign**
   - Arkitekturdiagram (uppdatera befintliga i `docs/images/`)
   - Komponentbeskrivning: vad gör varje modul
   - Dataflöde från klientrequest till svar

2. **Minnesmodell**
   - Processens minneslayout (stack/heap/BSS)
   - Shared memory-segmentets struktur
   - SQLite-filens relation till processen

3. **Trådmodell**
   - Vilka trådar finns (ThreadPool + workers)
   - Vilka mutexar skyddar vad
   - Dataflöde mellan trådar via köer

4. **Prestandaöverväganden**
   - Cache-strategi och motivering av TTL-värden
   - Profileringsresultat med faktiska mätvärden
   - Optimeringar gjorda och deras effekt

5. **Säkerhetsöverväganden**
   - JWT-validering och vad den skyddar mot
   - Varför servern är localhost-bound som default
   - Vad som saknas för fullständig produktion (TLS, rate limiting)

---

## Kursmålskoppling

| Kursmål | Beskrivning | Var i projektet |
|---------|-------------|-----------------|
| **7** | Flertådat C/C++ med synkronisering | ThreadPool, workers, Queue — **klart** |
| **8** | IPC: pipes, sockets, delat minne | Daemon-pipes ✅, HTTP/TCP-sockets (Fas 1-2), Shared memory (Fas 4) |
| **9** | C++ RAII + STL | CLI-klient (Fas 5) |
| **10** | Profilering med gprof/valgrind | Fas 6 |
| **11** | Optimera baserat på mätdata | Fas 6 (cache-tuning, pipeline) |
| **12** | Dokumentera design + minnesmodell | Fas 7 |

---

## Vad som medvetet utelämnats

Följande är **inte** en del av kursprojektet:

- **Next.js / webbfrontend** — Kursen är systemprogrammering i C/C++
- **JWT-utfärdande** — Hanteras av extern plattform
- **bcrypt / libjwt** — Onödiga externa libs; HMAC-SHA256 via OpenSSL räcker
- **libmicrohttpd** — HTTP implementeras direkt över TCP
- **VPS-deployment, nginx, Cloudflare** — DevOps, inte systemkursmål
- **10 000+ users i skala** — Inte mätbart i kursprojektet

---

*Detta dokument är den primära implementationsreferensen för kursprojektets slutfas.*
