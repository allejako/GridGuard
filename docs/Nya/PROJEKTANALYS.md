# GridGuard — Projektanalys
**Datum:** 2026-03-09
**Syfte:** Helhetsbedömning av projektet med fokus på kundupplevelse, styrkor och svagheter

---

## Vad är GridGuard?

GridGuard är en **lokal energioptimerings-plattform** (LEOP) skriven i C/C++. Systemet hjälper hushåll med solpaneler att fatta smarta beslut om när de ska:

- **KÖPA** el från nätet (billigaste timmarna)
- **SÄLJA** solöverskott till nätet (när det är lönsamt)
- **VÄNTA** (inga åtgärder behövs)

Systemet hämtar realtidsdata för väder (Open-Meteo) och spotpriser (Elpriset.se), kombinerar det med kundens solkonfiguration, och levererar ett 96-timmars schema — timme för timme.

**Kärntanken:** All känslig förbrukningsdata stannar lokalt på kundens enhet. Inget skickas till externa servrar.

---

## Arkitektur i korthet

```
Kund (CLI-klient)
       |
       | HTTP + JWT
       v
  [GridGuard-server]  <-- Watchdog håller koll & startar om vid krasch
       |
   Pipe/FIFO/Socket
       |
  [Fetcher]  -->  Hämtar väder & priser (cachas 15 min)
       |
  [Parser]   -->  Tolkar JSON-data
       |
  [Compute]  -->  Beräknar BUY/SELL/IDLE per timme
       |
   JSON-svar  -->  Tillbaka till kunden
```

- **Programspråk:** C (server, pipeline) + C++ (klientapp)
- **Databas:** SQLite (lokal, ingen molnberoende)
- **Autentisering:** JWT-tokens (HMAC-SHA256)
- **Port:** 8080 (localhost)

---

## Kursmålstäckning

### Kunskaper (1–6)

| # | Kursmål | Status | Var i koden |
|---|---------|--------|-------------|
| 1 | Förklara hur OS hanterar processer, trådar, synkronisering och minne | ✅ Täckt | `fork()`+`execl()` i GridGuard.c; `pthread_create` i WorkerPool; `pthread_mutex`+`pthread_cond` i Queue.c och Compute.c |
| 2 | Redogöra för IPC: pipes, sockets, delat minne | ✅ Täckt | Named pipe (FIFO) fetcher→parser; Unix domain socket parser→compute; POSIX shm + `sem_open` i SharedCache |
| 3 | Förklara skillnader mellan C och C++ | ✅ Täckt | C-server + C++-klient i samma repo; RAII/STL/namespaces demonstreras i klienten |
| 4 | Redogöra för C++-objektmodellen och RAII | ✅ Täckt | `std::unique_ptr<HttpClient>` med `make_unique`; `SocketGuard.hpp` (RAII-wrapper för fd); destruktorer |
| 5 | Förklara hur STL-komponenter hanterar resurser | ✅ Täckt | `std::vector`, `std::map`, `std::sort`, `std::min_element`, `std::accumulate`, `std::count_if` i client/main.cpp |
| 6 | Förklara hur profilering används för prestandaoptimering | ⚠️ Otillräckligt | Makefile har `make profile` (gprof) och `make valgrind-server` men **ingen profileringsrapport finns dokumenterad** |

### Färdigheter (7–12)

| # | Kursmål | Status | Var i koden |
|---|---------|--------|-------------|
| 7 | Implementera flertrådat C/C++ med effektiv synkronisering | ✅ Täckt | ThreadPool (20 workers), Queue (bounded, mutex+cond), WorkCompletion (cond-baserad barrier) |
| 8 | Använda IPC-lösningar för processkommunikation | ✅ Täckt | Anonym pipe + named FIFO + Unix socket + POSIX shared memory — alla fyra mekanismer demonstreras |
| 9 | Implementera C++-komponenter med RAII och STL | ✅ Täckt | SocketGuard, unique_ptr, move-semantik i GridGuardClient-konstruktor, lambdas som komparatorer |
| 10 | Utföra profilering, tolka resultat, identifiera flaskhalsar | ❌ Saknas | Ingen profileringsrapport, inga mätvärden, ingen dokumenterad flaskhals |
| 11 | Optimera kod baserat på mätdata och resursanalys | ❌ Saknas | Inga före/efter-mätningar. `qsort` i Compute.c är ett naturligt optimeringsobjekt men ej dokumenterat |
| 12 | Dokumentera design, minnesmodeller och prestandaöverväganden | ⚠️ Delvis | Många docs-filer men ingen samlad, examinationsklar arkitekturdokumentation med minnesmodell |

### Sammanfattning

```
Täckta (klara):      1, 2, 3, 4, 5, 7, 8, 9   → 8 av 12
Delvis täckta:       6, 12                      → 2 av 12
Saknas helt:         10, 11                     → 2 av 12
```

> **Kritisk notering:** Kursmål 10 och 11 kopplas direkt till examineringskravet "profileringsrapport med före/efter-mätningar". Det är ett obligatoriskt moment som saknas helt. Kursmål 6 och 12 är halvdana — infrastrukturen finns men resultaten/dokumentet saknas.

---

## Obligatoriska leverabler — status

| Leverabel | Status | Notering |
|-----------|--------|----------|
| Komplett källkod i Git-repository | ✅ Finns | |
| Makefile för kompilering | ✅ Finns | |
| Fungerande server och klient | ✅ Finns | |
| Automatiserade tester (7 st) | ✅ Finns | JWT, HTTP, API, pipeline m.fl. |
| **README med installationsinstruktioner** | ❌ Saknas | Obligatoriskt — finns ingen README.md |
| **Profileringsrapport med före/efter** | ❌ Saknas | Kritiskt för kursmål 10–11 |
| Arkitekturdokumentation med diagram | ⚠️ Delvis | Många .md-filer men ingen samlad, aktuell version |
| API-dokumentation | ❌ Saknas | Ingen dokumentation av REST-endpoint-kontrakt |
| Individuell skriftlig reflektion | ❌ Saknas | Per student — personligt dokument |

---

## STYRKOR

### 1. Realistisk och korrekt energimodell
- Solcellsmodellen bygger på **IEC 61215 NOCT-standarden** — samma standard som industrin använder
- Tar hänsyn till temperaturpåverkan (heta sommardagar sänker verkningsgraden ~12–16%)
- Vindkylning kompenserar delvis för värmeförluster
- Alla avgifter inkluderas: spotpris + nättariff + energiskatt + moms
- Nettariffen varierar korrekt efter tid (låg 00–06, normal 07–16, hög 17–23)

### 2. Privacy by design
- Förbrukningsdata lämnar **aldrig** enheten
- Solpanelskonfiguration lagras lokalt i SQLite
- Endast anonymiserad plats/region skickas till väder-API
- JWT-autentisering separerar plattformsserver från energiserver

### 3. Robust och självläkande arkitektur
- **Watchdog-process** övervakar serverns hälsa och startar om den automatiskt vid krasch
- Exponentiell backoff vid upprepade krascher (undviker "crash loop")
- Delat minnes-cache (15 min TTL) — överlever processomsningar utan ny API-hämtning
- Timeouts på alla socket-operationer (30 sek) — hänger aldrig

### 4. Säker autentisering
- JWT valideras kryptografiskt via mbedTLS (HMAC-SHA256)
- Token-utgångsdatum kontrolleras
- Hemlig nyckel hämtas från miljövariabel (inte hårdkodad i källkod)

### 5. Ren databasdesign
- SQLite-schema med versionsmigreringar
- Prepared statements genomgående (SQL-injection inte möjlig)
- Transaktionsgarantier vid konfigurationsskrivning

### 6. Tydliga API-svar
- BUY/SELL/IDLE-signaler är enkla att förstå och agera på
- Varje timme inkluderar beräknad besparing (SEK) jämfört med mediants
- 96-timmars framförhållning ger kunden tid att planera

### 7. Modulär kodstruktur
- Varje process (server, fetcher, parser, compute) har ett tydligt ansvar
- Enkelt att byta ut en komponent utan att påverka de andra
- Testbar arkitektur med 7 automatiserade tester

---

## SVAGHETER

### Kritiska — påverkar korrekthet

#### 1. Tidsstämpel-bugg (parser.c)
- **Problem:** Parsern använder `mktime()` som tolkar tidsstämplar som lokal tid (UTC+1/+2)
- **Effekt:** Väder- och prisdata matchas fel — fel timme → fel rekommendation
- **Kundpåverkan:** Energiplanen stämmer inte med verkligheten; kunden köper el på fel timmar
- **Fix:** Byt `mktime()` mot `timegm()` i `parser.c`

---

### Allvarliga — påverkar kundupplevelse

#### 2. Platt förbrukningsprofil
- **Problem:** Systemet antar konstant förbrukning (t.ex. 0,5 kWh/timme dygnet runt)
- **Verklighet:** En familj förbrukar 3× mer 07–09 (frukost, dusch) än kl 03
- **Effekt:** SELL-signal kl 08 kan vara felaktig om hushållet faktiskt drar mer än solpanelerna ger
- **Fix:** Tidsbaserad förbrukningsprofil, eller integration med smart mätare

#### 3. Parser-flaskhals
- **Problem:** Parsern hanterar en request i taget (sekventiell pipeline)
- **Effekt:** 20 parallella trådar i server-ThreadPool, men alla väntar på parsern
- **Kundpåverkan:** Flera simultana användare = köbildning och hög latens
- **Fix:** Instansiera en parserprocess per request, eller asynkron kö

#### 4. Ingen push-notifiering
- **Problem:** Kunden måste aktivt hämta schema — systemet "knackar inte på"
- **Önskvärt:** Notis 30 min innan en BUY-timme börjar → kunden kan ladda EV eller starta tvättmaskinen i tid
- **Fix:** Webhook, MQTT, eller lokal cron-integration

#### 5. Ingen historik eller statistik
- **Problem:** Kunden ser bara framåt — inget om hur mycket de sparat senaste månaden
- **Önskvärt:** "Du sparade 142 kr förra månaden tack vare GridGuard"
- **Fix:** Logga genomförda rekommendationer och faktiska priser i databasen

---

### Dokumentation — påverkar onboarding och underhåll

#### 6. Ingen README
- **Problem:** Ingen fil som förklarar hur man installerar, bygger eller kör systemet
- **Effekt:** En ny användare eller tekniker vet inte var de ska börja
- **Fix:** Skapa `README.md` med: installation, konfiguration, uppstart, API-översikt

#### 7. Ingen API-dokumentation
- **Problem:** REST-endpoint-kontrakten är inte dokumenterade
- **Effekt:** Kan inte integrera med hemautomation (Home Assistant, etc.)
- **Fix:** Swagger/OpenAPI-spec eller minst en markdown-tabell med endpoints, body, respons

#### 8. Ingen inbyggd hjälp i klientappen
- **Problem:** CLI-klienten ger inte användaren tydlig vägledning
- **Önskvärt:** `gridguard --help`, tydliga felmeddelanden, prompter vid felaktig inmatning

#### 9. Dålig konfigurationsupplevelse
- **Problem:** Kunden måste konfigurera solpaneler via API-anrop med JSON-body
- **Önskvärt:** Guidad setup-wizard i klientappen: "Hur många kvm solpaneler har du?"
- **Fix:** Interaktiv inmatning i CLI-klienten vid första uppstarten

---

### Produktionsberedskap

#### 10. Ingen TLS/HTTPS
- **Problem:** All kommunikation sker i klartext (HTTP)
- **Risk:** JWT-token kan fångas upp på lokalt nätverk
- **Fix:** Lägg till TLS med t.ex. mbedTLS eller kör bakom nginx med certifikat

#### 11. Ingen rate limiting
- **Problem:** Inga begränsningar på antal requests per klient
- **Risk:** DoS mot localhost möjligt; externa API-kvoter kan tömmas
- **Fix:** Enkel token bucket per klient-IP

#### 12. Solkonfiguration — begränsad
- **Problem:** Panelvinkel och orientering lagras men används **inte** i beräkningsmodellen
- **Effekt:** En 45°-lutad panel söderut ger ~30% mer energi än en platt — men systemet ignorerar detta
- **Fix:** Inkludera lutningsvinkel och azimut i solmodellen

---

## Sammanfattande bedömning

| Område | Betyg | Kommentar |
|--------|-------|-----------|
| Energimodellens korrekthet | 8/10 | Solida fysikberäkningar, men konsumtionsprofil är förenklad |
| Arkitektur & robusthet | 9/10 | Watchdog, cache, timeouts — väl genomtänkt |
| Säkerhet | 7/10 | Bra JWT, men saknar TLS och rate limiting |
| Kundvänlighet | 4/10 | Svår att installera, konfigurera och förstå utan dokumentation |
| Dokumentation | 3/10 | Många interna docs men inget kundriktat material |
| Skalbarhet | 5/10 | Parser-flaskhals begränsar vid fler användare |
| Produktionsberedskap | 6/10 | Bra för hemmabruk, behöver arbete för SaaS |

**Helhetsbetyg: 6/10**

Projektet är **tekniskt imponerande** och löser ett genuint problem korrekt. Grunden är stark. Men för att kunden ska kunna använda det på egen hand krävs:

1. En tydlig README med installationsguide
2. En setup-wizard i klienten
3. Fix av tidsstämpel-buggen (annars fel rekommendationer)
4. Push-notifieringar (annars tappar kunden värdet)
5. Historik och sparade besparingar (för att motivera kunden att fortsätta använda det)

---

## Prioriterad åtgärdslista

### Röd — blockerande för godkänt (kursmålskritiska)

- [ ] **Profileringsrapport** — Kör `make profile`, kör servern under last, analysera med `gprof`, dokumentera flaskhals och optimering med före/efter-mätningar → `docs/PROFILING_REPORT.md` *(Kursmål 10, 11)*
- [ ] **Fixa `mktime()` → `timegm()`** i `parser.c:30` — kritisk korrekthetsbug som ger fel energiplan *(affärskritisk)*
- [ ] **Skriv `README.md`** — installation, beroenden, `make`, `make dev`, `make test`, API-endpoints, konfiguration *(obligatoriskt leveranskrav)*
- [ ] **Minnesmodell och arkitekturdokumentation** — ett samlat dokument med IPC-flödesdiagram och minnesmodell *(Kursmål 12)*

### Orange — bör göras (leverans- och strukturkvalitet)

- [ ] **API-dokumentation** — dokumentera alla REST-endpoints i `docs/API.md` med request/response-schema och felkoder *(leveranskrav)*
- [ ] **Fixa logisk bugg** — identiska `if/else`-grenar i `parser.c:235` ger ingen fallback när spotpriser saknas
- [ ] **Flytta `platform/` ur server-binären** — JWT-utfärdningskod ska inte länkas in i servern; förstärker arkitekturpoängen *(arkitektur)*
- [ ] **Slå ihop `src/database/` och `src/infrastructure/database/`** — eliminera dubbletten *(struktur)*
- [ ] **Lägg till `--help`** och tydliga felmeddelanden i CLI-klienten *(kundupplevelse)*

### Gul — förbättringar (om tid finns)

- [ ] Tidsbaserad förbrukningsprofil (ersätt konstant kWh med typisk dygnsprofil)
- [ ] Aktivera lutningsvinkel/azimut i solmodellen
- [ ] Push-notifieringar (MQTT eller webhook) när BUY-timme närmar sig
- [ ] Historikloggning av utförda rekommendationer och faktiska besparingar
- [ ] TLS/HTTPS-stöd
- [ ] Rate limiting
- [ ] Interaktiv setup-wizard vid första körning
- [ ] Uppdatera `.gitignore` — lägg till `*.db`, `bin/`, `build/`, `logs/*.log`, `gmon.out`
