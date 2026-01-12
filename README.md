# LEOP - Local Energy Optimization Platform

## Projektöversikt

LEOP (Local Energy Optimization Platform) är ett systemnära mjukvaruprojekt för prognostisering och optimering av solenergi. Systemet kombinerar väderdata med spotprisdata från elnätet för att beräkna optimala tider för elförbrukning, batteriladdning och försäljning av överskottsproduktion.

**Kurs:** Systemprogrammering och introduktion till C++  
**Institution:** CHAS Academy  
**Projektperiod:** Vecka 1-12

## Produktnamn

[LÄGG TILL ERT UNIKA PRODUKTNAMN HÄR]

## Systemöversikt

LEOP är ett lokalt körbart system utan molnberoende som kan användas i:
- Smarta hem
- Energioptimeringssystem
- Embedded-sammanhang

### Kärnfunktionalitet

- **Väderdata:** Hämtar solinstrålning, molnighet och temperatur
- **Energiprognos:** Beräknar förväntad solcellsproduktion per 15 minuter
- **Spotprisdata:** Integrerar elpriser från nätet
- **Optimering:** Genererar tidsbaserad energiplan (24-72 timmar)
- **Lagring:** Lokal cache med TTL för prognosdata

## Teknisk Stack

- **Server:** C (multi-threaded)
- **Klient:** C++ med STL och RAII
- **IPC:** Unix domain sockets, pipes, shared memory
- **Versionshantering:** Git
- **Build:** Makefile
- **Testning:** [TBD]

## Projektstruktur

```
leop/
├── src/
│   ├── server/          # C-baserad server
│   ├── client/          # C++-baserad klient
│   ├── common/          # Gemensam kod
│   └── tests/           # Tester
├── docs/                # Dokumentation
├── config/              # Konfigurationsfiler
├── scripts/             # Build och deploy scripts
├── Makefile
└── README.md
```

## Installation

### Förutsättningar

- Linux (Ubuntu 24.04 rekommenderas)
- GCC/G++ (version 11 eller senare)
- Make
- Git
- Valgrind (för minnesanalys)
- GDB (för debugging)

### Kompilering

```bash
# Klona repository
git clone [REPOSITORY_URL]
cd leop

# Kompilera projektet
make

# Kör tester
make test

# Rensa byggfiler
make clean
```

## Användning

### Starta servern

```bash
./bin/leop-server --port 8080 --config config/server.conf
```

### Kör klienten

```bash
./bin/leop-client --command forecast
./bin/leop-client --command spotprice
./bin/leop-client --command energyplan
```

## Utveckling

### Kodstandard

- Följ konventioner i `CONTRIBUTING.md`
- Använd `-Wall -Wextra -pthread` flaggor vid kompilering
- Kör Valgrind för minnesläckagedetektering
- Dokumentera alla publika funktioner

### Branching-strategi

- `main` - Stabil produktionskod
- `develop` - Utvecklingsbranch
- `feature/*` - Feature branches
- `bugfix/*` - Bugfix branches

### Commit-meddelanden

Följ Conventional Commits:
```
feat: Lägg till väderdata-hämtning
fix: Rätta minnesläcka i cache-modul
docs: Uppdatera API-dokumentation
```

## Tidsplan

| Vecka | Fokusområde |
|-------|-------------|
| 1 | Projektplanering och arkitekturdesign |
| 2 | Serverarkitektur med processhantering |
| 3 | Multi-threaded pipeline |
| 4 | IPC med pipes |
| 5 | Unix sockets och delat minne |
| 6 | C++-migrering |
| 7 | C++ klasser och objektdesign |
| 8 | RAII och resursförvaltning |
| 9 | STL-integration |
| 10 | Profilering och prestandaanalys |
| 11 | Optimering och dokumentation |
| 12 | Examination och presentation |

## Team

| Namn | Roll | Ansvarsområde |
|------|------|---------------|
| [Namn] | [Roll] | [Område] |
| [Namn] | [Roll] | [Område] |
| [Namn] | [Roll] | [Område] |

## Dokumentation

- [Arkitektur](docs/ARCHITECTURE.md)
- [API-dokumentation](docs/API.md)
- [Profileringsrapport](docs/PROFILING.md)
- [Backlog](BACKLOG.md)
- [Bidragsriktlinjer](CONTRIBUTING.md)

## Licens

Detta projekt är utvecklat som en del av utbildning på CHAS Academy.

## Kontakt

Vid frågor kontakta teamet via [KONTAKTINFORMATION]

---

**Senast uppdaterad:** [DATUM]
