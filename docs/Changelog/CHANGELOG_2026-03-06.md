# Ändringslogg - 2026-03-06

## IPC-structs konsoliderade och ComputeWorker städad upp

Tre IPC-structs var duplicerade i separata implementationsfiler utan någon gemensam definition. Det innebar att en ändring i en struct-definition på ett ställe tyst kunde bryta kommunikationen mellan processerna. Nu finns en enda kanonisk definition för varje struct, och `ComputeWorkerHybrid` är ersatt med `ComputeWorker` på rätt plats i kodbasen.

---

## Problemet: duplicerade struct-definitioner

`WorkRequest`, `FetchResult` och `ParseResult` var definierade lokalt i tre separata filer:

- `WorkRequest` i `GridGuard.h`
- `FetchResult` i `fetcher.c` **och** `ComputeWorkerHybrid.c`
- `ParseResult` i `parser.c` **och** `ComputeWorkerHybrid.c`

Det här är ett IPC-säkerhetsproblem. Kommunikationen mellan processerna sker via binär serialisering — `write(fd, &result, sizeof(result))` på ena sidan och `read(fd, &result, sizeof(result))` på den andra. Om struct-layouten skiljer sig åt ett enda byte mellan avsändare och mottagare läser processen fel data ur minnet utan att något fel rapporteras. Sådana buggar är svåra att hitta eftersom de kan orsaka subtila felberäkningar snarare än en tydlig krasch.

Konkret: `FetchResult` var definierad på två ställen oberoende av varandra. Om en fälttyp ändrades i `fetcher.c` men glömdes i `ComputeWorkerHybrid.c` — eller vice versa — hade parsern och compute-tråden läst bitvis fel data ur socketen. Kompilatorn hade inte gett något felmeddelande.

---

## Lösningen: `src/application/models/ipc/`

Tre kanoniska headers skapades, en per IPC-meddelande:

**`WorkRequest.h`** — skickas från HTTP-handler till Fetcher-processen via anonym pipe.
```c
typedef struct {
    char   userId[64];
    char   location[64];
    char   lat[16];
    char   lon[16];
    char   region[16];
    double solarAreaM2;
    double solarEfficiency;
    double consumptionKwh;
    double gridFee_low;
    double gridFee_normal;
    double gridFee_high;
} WorkRequest;
```

**`FetchResult.h`** — skickas från Fetcher till Parser via namngiven FIFO.
```c
typedef struct {
    char   userId[64];
    char   location[64];
    char   region[16];
    double solarAreaM2;
    double solarEfficiency;
    double consumptionKwh;
    double gridFee_low;
    double gridFee_normal;
    double gridFee_high;
    char   openMeteoJson[32768];
    char   priceJson[16384];
} FetchResult;
```

**`ParseResult.h`** — skickas från Parser till ComputeWorker-tråden via Unix domain socket.
```c
typedef struct {
    char         userId[64];
    char         location[64];
    char         region[16];
    double       solarAreaM2;
    double       solarEfficiency;
    double       consumptionKwh;
    double       gridFee_low;
    double       gridFee_normal;
    double       gridFee_high;
    ForecastData forecastData;
} ParseResult;
```

Varje fil innehåller en kommentar som dokumenterar vilket transportlager den tillhör. Alla tre processer — server, fetcher och parser — inkluderar nu samma headers. Det är inte längre möjligt att ha tysta layoutmismatchar: kompilatorn garanterar att `sizeof(struct)` är identiskt på båda sidor av varje IPC-kanal.

De lokala struct-definitionerna i `fetcher.c`, `parser.c` och `GridGuard.h` togs bort och ersattes med `#include`-direktiv till de nya kanoniska filerna. Totalt raderades ca 60 rader duplicerad kod.

---

## `ComputeWorkerHybrid` → `ComputeWorker`

`ComputeWorkerHybrid` låg i `src/application/workers/` och hade "Hybrid" i namnet som en kvarleva från ett tidigare arkitekturstadium där det var oklart om compute-steget skulle vara en tråd eller en process. Det är nu en ren tråd som läser från en Unix domain socket — inget hybrid-mönster kvarstår.

Filen är ersatt med `src/concurrency/threads/ComputeWorker.c/h`, som:

- Placeras i `src/concurrency/threads/` — rätt katalog för trådar enligt projektstrukturen.
- Byter namn på struct och entry point: `ComputeWorkerHybrid` → `ComputeWorker`, `ComputeWorkerHybrid_Run` → `ComputeWorker_Run`.
- Inkluderar `ParseResult.h` från det nya IPC-lagret istället för att definiera structen lokalt.

`GridGuard.c` uppdaterades att inkludera `ComputeWorker.h` och använda de nya namnen. `ComputeWorkerHybrid.c/h` togs bort.

---

## Makefile

- `APP_MODELS_IPC_DIR` lades till som include-sökväg så att kompilatorn hittar de nya IPC-headers.
- `ComputeWorkerHybrid.c` togs bort från `SERVER_SRCS_C`.

---

## Sammanfattning av ändrade filer

| Fil | Ändring |
|-----|---------|
| `src/application/models/ipc/WorkRequest.h` | Ny — kanonisk definition |
| `src/application/models/ipc/FetchResult.h` | Ny — kanonisk definition |
| `src/application/models/ipc/ParseResult.h` | Ny — kanonisk definition |
| `src/concurrency/threads/ComputeWorker.c` | Ny — ersätter ComputeWorkerHybrid.c |
| `src/concurrency/threads/ComputeWorker.h` | Ny — ersätter ComputeWorkerHybrid.h |
| `src/application/workers/ComputeWorkerHybrid.c` | Borttagen |
| `src/application/workers/ComputeWorkerHybrid.h` | Borttagen |
| `src/application/core/GridGuard.h` | Lokal WorkRequest-definition ersatt med `#include "WorkRequest.h"` |
| `src/application/core/GridGuard.c` | Uppdaterad att använda ComputeWorker |
| `src/infrastructure/processes/fetcher/fetcher.c` | Lokala struct-definitioner ersatta med `#include` |
| `src/infrastructure/processes/parser/parser.c` | Lokala struct-definitioner ersatta med `#include` |
| `Makefile` | IPC-katalog tillagd i INCLUDES, ComputeWorkerHybrid borttagen från sources |
