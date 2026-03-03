# Ändringslogg - 2026-03-03

## Watchdog.c delad upp i fyra moduler

`Watchdog.c` var 441 rader och innehöll fyra logiskt separata ansvarsområden blandade i en enda fil: signalhantering, heartbeat-pipe, restart-logik och huvudloopen. `CODE_QUALITY_ANALYSIS.md` pekade ut det som P0 (högsta prioritet) för kodkvalitet: "Monolitisk design — en fil gör för mycket". Filen har nu delats upp i tre nya moduler med egna `.c`/`.h`-filer, och `Watchdog.c` är halverad till 220 rader.

---

## Problemet: en fil med för många ansvar

Den gamla strukturen:

```
Watchdog.c (441 LOC)
├── Signal handling        (58 LOC)   — sigaction, signal_handler
├── Heartbeat pipe         (67 LOC)   — pipe(), poll(), read()
├── Status FIFO            (43 LOC)   — mkfifo(), named pipe för dashboard
├── Daemon spawning        (40 LOC)   — fork(), execl(), setenv()
├── Restart tracking       (45 LOC)   — RestartTracker struct + backoff
└── Main watchdog loop    (128 LOC)   — Watchdog_Run()
```

Problemet var inte att koden inte fungerade — den fungerade utmärkt. Problemet var att allt var sammankopplat via globala variabler: `heartbeat_pipe[2]`, `status_fd`, `watchdog_running`, `daemon_pid`. Att förstå `Watchdog_Run()` krävde att man hoppade upp och ner i filen för att hitta definitionerna av de globaler den använde. Att ändra heartbeat-logiken riskerade att påverka spawn-koden som använde samma pipe-array.

Det fanns också ett mer konkret problem: `RestartTracker` var en struct definierad lokalt i filen utan header, vilket gjorde den omöjlig att testa isolerat eller återanvända.

---

## Lösningen: tre fristående moduler med opaka pekare

```
src/infrastructure/processes/watchdog/
├── Watchdog.c          (220 LOC)  Spawn, status FIFO, main loop
├── Watchdog.h          (oförändrad)
├── WatchdogSignals.c   ( 38 LOC)  Signal handling
├── WatchdogSignals.h
├── Heartbeat.c         ( 89 LOC)  Heartbeat pipe management
├── Heartbeat.h
├── RestartPolicy.c     ( 78 LOC)  Restart tracking och backoff
└── RestartPolicy.h
```

Varje modul äger sin state privat och exponerar ett clean API. Inga globala arrays eller `int fd`-variabler som läcker ut.

### Heartbeat — opak pekare, äger pipe internt

Tidigare låg pipe-fd:arna i en global array `heartbeat_pipe[2]` som all kod läste och skrev direkt. Nu är de privata fält i en heap-allokerad struct:

```c
// Heartbeat.h
typedef struct Heartbeat Heartbeat;

Heartbeat *Heartbeat_Create(void);
void       Heartbeat_Destroy(Heartbeat *hb);
int        Heartbeat_GetWriteFd(const Heartbeat *hb);
int        Heartbeat_CloseWriteFd(Heartbeat *hb);
int        Heartbeat_CloseReadFd(Heartbeat *hb);
int        Heartbeat_Check(Heartbeat *hb, int timeout_sec);
// Returnerar: 1=heartbeat mottaget, 0=timeout, -1=fel
```

`Heartbeat_Create()` kör `pipe()` och returnerar en pekare. `Heartbeat_Destroy()` stänger öppna fd:ar och frigör minnet. Resten av koden behöver aldrig veta om fd-nummer eller pipe-implementation.

Separationen löste också en subtil fork-bugg: child-processen måste stänga read-fd:n innan exec (annars ärver daemonen den och poll-beteendet i watchdog blir oförutsägbart). Med den gamla globala arrayen var detta utspritt i spawning-koden. Nu finns det som `Heartbeat_CloseReadFd()` — ett explicit API-anrop med tydlig avsikt.

### RestartPolicy — ersätter RestartTracker

`RestartTracker` var en struct med fyra funktioner direkt i `Watchdog.c`. Konfigurationen (max restarts, window, backoff) var hårdkodad som defines och läste implicit av funktionerna.

Den nya `RestartPolicy` tar konfigurationen som argument till `_Create()`:

```c
RestartPolicy *RestartPolicy_Create(int max_restarts, int window_sec,
                                    int base_backoff_sec);
void           RestartPolicy_Destroy(RestartPolicy *rp);
int            RestartPolicy_CanRestart(RestartPolicy *rp);
void           RestartPolicy_RecordRestart(RestartPolicy *rp);
int            RestartPolicy_GetBackoffDelay(const RestartPolicy *rp);
int            RestartPolicy_GetCount(const RestartPolicy *rp);
int            RestartPolicy_GetMax(const RestartPolicy *rp);
```

`MAX_RESTARTS`, `RESTART_WINDOW_SEC` och `BASE_BACKOFF_SEC` är nu definierade i `RestartPolicy.h` och skickas in vid skapande. Det gör det möjligt att i framtiden testa modulen isolerat med kortare fönster (t.ex. `RestartPolicy_Create(3, 10, 1)` i ett test) utan att ändra produktionskoden.

### WatchdogSignals — isolerar signal handler

Signal handlers är bland de svåraste delarna av C-kod att resonera kring: de kan avbryta vilken kodrad som helst och har strikta regler om vad som får anropas inne i dem (`async-signal-safe`). Att ha dem utspridda i en stor fil ökade risken för att missa dessa begränsningar.

`WatchdogSignals.c` samlar all signalregistrering och signal_handler på ett ställe. Handlersn behöver fortfarande nå `watchdog_running` och `daemon_pid` — det är oundvikligt, signal handlers måste sätta `volatile sig_atomic_t` globals. Dessa är definierade i `Watchdog.c` och deklarerade som `extern` i `WatchdogSignals.h`:

```c
extern volatile sig_atomic_t watchdog_running;
extern volatile pid_t        daemon_pid;
```

Det är inte perfekt (extern-koppling är en form av implicit beroende), men det är korrekt och tydligt dokumenterat. Alternativen — att lägga globals i headern eller att skicka pekare till signal handlers — är antingen sämre eller omöjliga i C.

---

## Varför opaka pekare och inte struct-by-value?

Den ursprungliga `RestartTracker` skickades som en lokal variabel och passades med pekare:

```c
RestartTracker tracker;
RestartTracker_Init(&tracker);
```

Det fungerar men exponerar implementationsdetaljerna (fältnamnen, storleken) i headern. Om man lägger till ett fält i structen måste alla kompilationenheter som inkluderar headern rekompileras.

Med opak pekare (`typedef struct Heartbeat Heartbeat`) syns bara pekaren i headern. Struct-layouten är en implementation-detalj i `.c`-filen. Det är samma mönster som `FILE*` i standardbiblioteket och `sqlite3*` i SQLite — välbeprövat för moduler som kan förändras.

---

## Vad som inte ändrades

Goto-statements i `Watchdog_Run()` (`goto check_waitpid`, `goto daemon_died`) är kvar. De är okonventionella men korrekt använda för att hoppa ut ur nästlade kontrollflöden i en loop — ett klassiskt goto-användningsfall som Linus Torvalds och Linux kernel-koden godkänner. Att ersätta dem med flaggor eller omstrukturering skulle göra loopen svårare att läsa, inte lättare.

Status FIFO-koden (`status_open`, `status_write`, `status_close`) stannade i `Watchdog.c`. Den är tätt kopplad till main loop-logiken (varje STOP/CRASH/RESTART-händelse skriver till FIFO:n) och är för liten för att motivera en egen fil.

---

## Påverkan på kompilering

Makefile uppdaterades med tre nya källfiler i `WATCHDOG_SRCS`:

```makefile
WATCHDOG_SRCS = $(WATCHDOG_DIR)/main.c \
                $(WATCHDOG_DIR)/Watchdog.c \
                $(WATCHDOG_DIR)/WatchdogSignals.c \
                $(WATCHDOG_DIR)/Heartbeat.c \
                $(WATCHDOG_DIR)/RestartPolicy.c \
                $(DAEMON_DIR)/PidFile.c \
                $(LOGGING_DIR)/Logger.c
```

Kompilering med `-Wall -Wextra -Werror` — inga varningar. Länkning utan ändringar i LDFLAGS.

---

## Verifiering

Innan ändringarna etablerades en baseline:

```
make clean && make all && make watchdog   → OK
make test-logger test-jwt test-http-*     → OK (12/12 PASS)
GET /health                               → 200 {"status":"ok"}
GET /forecast utan token                  → 401
GET /user/config                          → 200 med korrekt data
GET /forecast                             → 200, 96 entries, signaler: BUY/IDLE
```

Samma tester efter refaktorering — identiska resultat. Inget beteende ändrat.

---

## Filer som ändrats

**Tillagda:**
- `src/infrastructure/processes/watchdog/Heartbeat.h`
- `src/infrastructure/processes/watchdog/Heartbeat.c`
- `src/infrastructure/processes/watchdog/RestartPolicy.h`
- `src/infrastructure/processes/watchdog/RestartPolicy.c`
- `src/infrastructure/processes/watchdog/WatchdogSignals.h`
- `src/infrastructure/processes/watchdog/WatchdogSignals.c`

**Modifierade:**
- `src/infrastructure/processes/watchdog/Watchdog.c` — 441 → 220 LOC
- `Makefile` — tre nya källfiler i `WATCHDOG_SRCS`

---

## Möjliga framtida förbättringar

**Enhetstester för RestartPolicy.** Modulen är nu testbar isolerat. Ett test kan skapa en `RestartPolicy_Create(3, 5, 1)` och verifiera att `CanRestart()` returnerar 0 efter tre registrerade omstarter, att fönstret nollas efter 5 sekunder, och att backoff-sekvensen är 1→2→4 sekunder. Inget sådant test finns idag.

**Enhetstester för Heartbeat.** `Heartbeat_Check()` kan testas med ett mock-pipe: skriv en byte till write-fd och verifiera att Check returnerar 1, vänta på timeout och verifiera att Check returnerar 0.

**Goto-refaktorering.** `Watchdog_Run()` har två goto-statements som kan elimineras om loopen bryts ut till hjälpfunktioner (`handle_frozen_daemon()`, `handle_daemon_exit()`). Det är en separat refaktorering som inte gjordes nu för att undvika för stor förändring på en gång.

**StatusFIFO som egen modul.** Om status-rapporteringen utökas (fler event-typer, läsning av status utifrån, integration med systemd-notify) är det naturligt att extrahera `status_open/write/close` till `WatchdogStatus.c`. Idag är den för liten för att motivera det.
