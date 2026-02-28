# Ändringslogg - 2026-02-28

## SharedCache — POSIX delat minne ersätter JsonCache

`JsonCache` är borttagen ur kodbasen och ersatt av `SharedCache` i `src/infrastructure/cache/`. Bytet löser ett grundläggande problem: den gamla cachen levde i serverprocessens heap och dog med processen. Den nya lever i POSIX delat minne och överlever omstarter.

---

## Varför JsonCache inte räckte

`JsonCache` var en in-process-cache skyddad av en `pthread_mutex`. Det fungerade för trådsäkerheten — alla 20 worker-trådar delade samma mutex och kom åt samma entries utan dataraces. Men minnet ägdes av serverprocessens heap.

Det medför två problem:

**Cachat data försvinner vid omstart.** Watchdog-daemonen är designad för att starta om servern automatiskt om den kraschar. Varje omstart innebar att alla tre externa API:er (Open-Meteo, Elpriset) behövde anropas på nytt — även om svaren var färska och TTL:n fortfarande gällde. Spotpriser uppdateras en gång om dagen och väderprognoser ändras sällan inom 15 minuter. Det är onödiga nätverksanrop mot externa tjänster.

**Inget IPC.** En `pthread_mutex` fungerar bara inom en process. Om watchdogen eller ett framtida monitoringverktyg vill läsa cachestatus direkt är det omöjligt — de ser inte heap-minnet i en annan process. Det täcker inte kursmålet om IPC (delat minne).

---

## Vad SharedCache gör annorlunda

`SharedCache` använder POSIX shared memory (`shm_open` + `mmap`) och ett namngivet POSIX-semafor (`sem_open`) för synkronisering.

**Minnet lever i kerneln, inte i heapen.** `shm_open("/gridguard_cache")` skapar ett objekt i `/dev/shm` som är oberoende av vilken process som skapade det. Servern mappar in det med `mmap(MAP_SHARED)` — när servern startar om mappar den in samma segment igen och hittar datan intakt.

**Magic-nummer för initiering.** Regionen innehåller ett `magic`-fält (`0xCA5EC0DE`). `SharedCache_Create` kontrollerar det under en kortvarig `flock`-lås: om magic matchar är segmentet redan initierat och befintlig data bevaras. Om det inte matchar är det en nystart och regionen nollsätts. Det gör att en omstart inte raderar färsk cache utan bara återansluter till den.

**Namngivet semafor i stället för pthread_mutex.** `sem_open` skapar ett namngivet semafor som är synligt för alla processer som känner till namnet. Det gör att watchdog eller externa verktyg kan nå samma lås som servern använder — rätt IPC-primitiv för cross-process-scenariot.

**Invarianter:**

| Egenskap | JsonCache | SharedCache |
|---|---|---|
| Lagring | Processens heap | `/dev/shm` (kernel) |
| Synkronisering | `pthread_mutex` (in-process) | Namngivet POSIX-semafor (cross-process) |
| Överlever omstart | Nej | Ja |
| Tillgänglig för andra processer | Nej | Ja |
| Max entries | 64 | 16 |
| IPC-primitiv | Nej | Ja (`shm_open`, `mmap`, `sem_open`) |

Antalet entries minskades från 64 till 16. Med Open-Meteo och Elpriset som enda externa källor, och nycklar på formen `openmeteo:57.70:11.97` och `elpriset:SE3:2026-02-28`, behöver cachen aldrig hålla mer än ett tiotal entries samtidigt. 64 var en överdimensionering från den tid då SMHI och fler källor var aktiva.

---

## Hur initialiseringen fungerar

```
shm_open(O_CREAT | O_RDWR)
  └── flock(LOCK_EX)           # Serialiserar om flera processer startar samtidigt
       ├── fstat → ftruncate   # Storleksätter segmentet om det är nytt
       ├── mmap(MAP_SHARED)    # Mappar in i adressrymden
       ├── magic == 0xCA5EC0DE?
       │    ├── Ja → återanslut, behåll data
       │    └── Nej → memset till noll, sätt magic och TTL
       └── flock(LOCK_UN)
sem_open(O_CREAT, initial=1)   # Binärt semafor, mutex-liknande
```

`flock` används bara under initieringen för att säkra att bara en process sätter upp regionen. All löpande läsning och skrivning skyddas sedan av semaforen.

---

## Påverkan på kursmålen

Den tidigare implementationen av IPC-målet begränsades till Watchdog-pipan (en enkelriktad FIFO). `SharedCache` lägger till:

- `shm_open` / `shm_unlink` — namngivna shared memory-objekt
- `mmap(MAP_SHARED)` — process-oberoende minnesmappning
- `sem_open` / `sem_wait` / `sem_post` / `sem_unlink` — namngivna POSIX-semaforer

Det täcker kursmålet om IPC (delat minne) konkret och med produktionskod som faktiskt används av servern.

---

## Filer som ändrats

- **Borttagna:** `src/application/services/JsonCache.h`, `src/application/services/JsonCache.c`
- **Tillagda:** `src/infrastructure/cache/SharedCache.h`, `src/infrastructure/cache/SharedCache.c`
- **Uppdaterade:** `src/server/Server.c` — initierar `SharedCache` vid start, förstör vid shutdown. `src/application/services/FetchWorker.c` — anropar `SharedCache_Lookup` och `SharedCache_Store` i stället för JsonCache-motsvarigheterna.
