# Changelog – 2026-02-20

Buggfixar för daemon- och watchdog-implementationen. Alla fem felen
identifierades genom statisk kodgranskning och praktiska tester.

---

## Fix 1 — Watchdog spårade fel process-ID (kritisk)

**Filer:** `src/infrastructure/daemon/Daemon.c`

### Varför det inte fungerade

`Daemon_Init()` utförde alltid den klassiska Unix double-fork oavsett
hur processen startades. När watchdog:en startade daemonen via `fork()`
+ `execl()` fick den ett direkt barn med ett känt PID. Det PID:et
övervakades sedan med `waitpid()`.

Problemet: det direkta barnet är *inte* den färdiga daemonen. Double-fork
fungerar så här:

```
watchdog
  └─ fork() → barn A   (execl → GridGuard-server -d)
                └─ fork() → barn B   (_exit(0))  ← watchdog's waitpid träffar detta!
                             └─ fork() → barnbarnsbarn C   ← den faktiska daemonen
```

Barn A exitade omedelbart med `_exit(0)`. Watchdog:en tolkade det som
"daemonen avslutades rent" och stängde av sig. Den faktiska daemonen
(barnbarnsbarn C, PID 14245) fortsatte köra som en föräldralös process
utan någon watchdog-övervakning.

### Varför det fungerar nu

När `GRIDGUARD_HEARTBEAT_FD` är satt i environment (dvs. processen
startades av watchdog:en) hoppas double-fork:en över. Processen stannar
som ett direkt barn av watchdog:en och `waitpid()` spårar rätt PID.

Watchdog:en hanterar redan processövervakning, omstarter och
signalforwardering — double-fork:en behövs bara vid fristående start
(utan watchdog).

```c
int under_watchdog = (getenv("GRIDGUARD_HEARTBEAT_FD") != NULL);
if (!under_watchdog)
{
    // double-fork som tidigare
}
// I/O-redirect, PID-fil och SIGPIPE-ignore körs alltid
```

---

## Fix 2 — Hjärtslags-timeout var 2 sekunder, inte 15 (kritisk)

**Filer:** `src/infrastructure/watchdog/Watchdog.c`

### Varför det inte fungerade

Watchdog:en anropade `Watchdog_CheckHeartbeat(MONITOR_POLL_SEC)` där
`MONITOR_POLL_SEC = 2`. Den inbyggda `select()`-timeoutten på 2 sekunder
användes direkt som dödsgräns. Om inga data anlände på 2 sekunder
ansågs daemonen fryst och fick SIGTERM.

Daemonen skriver hjärtslag var 5:e sekund (`HEARTBEAT_INTERVAL_SEC = 5`).
Det är matematiskt omöjligt att hålla sig under 2 sekunders tystnad.
Resultatet var att watchdog:en dödade daemonen direkt efter varje
`select()`-cykel som råkade infalla mellan två hjärtslag — vilket i
princip alltid.

`HEARTBEAT_TIMEOUT = 15` var bara ett tal som loggades i
varningsmeddelandet. Det påverkade inte när daemonen faktiskt dödades.

### Varför det fungerar nu

En `last_heartbeat`-tidsstämpel spåras i huvudloopen. Vid varje
poll-cykel uppdateras den om ett hjärtslag togs emot. Daemonen anses
fryst först när `difftime(now, last_heartbeat) >= HEARTBEAT_TIMEOUT`
(15 sekunder faktisk tystnad).

```c
time_t last_heartbeat = time(NULL);

// I loopen:
int hb = Watchdog_CheckHeartbeat(MONITOR_POLL_SEC);  // poll-intervall: 2s
if (hb == 1)
{
    last_heartbeat = time(NULL);   // uppdatera vid faktiskt hjärtslag
}
else if (hb == 0)
{
    double elapsed = difftime(time(NULL), last_heartbeat);
    if (elapsed < HEARTBEAT_TIMEOUT)
        goto check_waitpid;         // ännu inom toleransen, fortsätt polla
    // elapsed >= 15s → döda daemonen
}
```

`last_heartbeat` återställs även till `time(NULL)` efter varje omstart
av daemonen, så att en nyrespawnad daemon får sina 15 sekunder på sig
att starta upp och börja skicka hjärtslag.

---

## Fix 3 — Logger initierades efter Daemon_Init (silent log loss)

**Filer:** `src/server/main.c`

### Varför det inte fungerade

`Logger_Initiate()` anropades *efter* `Daemon_Init()`. Steg 5 i
`Daemon_Init()` stänger och omdirigerar stdout/stderr till `/dev/null`
innan `PidFile_Write()` anropas i steg 6.

`PidFile_Write()` anropar `LOG_INFO` och `LOG_ERROR`. `Logger_Log()`
skriver alltid till stdout via `printf()` — men vid det laget pekar
stdout på `/dev/null`. Och `logFile` är `NULL` (Logger ej initierad),
så fil-loggningen hoppas också. Loggmeddelanden från PID-filshanteringen
försvann tyst utan att hamna varken på terminal eller i loggfil.

Om `PidFile_Write()` misslyckades (t.ex. om `/tmp` saknade skrivrättigheter)
syntes felet ingenstans.

### Varför det fungerar nu

`Logger_Initiate()` anropas *innan* `Daemon_Init()`. Logg-sökvägen
är redan upplöst till en absolut sökväg innan anropet, vilket innebär
att efterföljande `chdir("/")` i `Daemon_Init()` inte påverkar
den öppna filhandtaget. Fil-loggningen är aktiv när `PidFile_Write()`
körs, och eventuella fel syns i loggfilen.

Felhanteringen uppdaterades också: om `Daemon_Init()` misslyckas
används nu `LOG_FATAL()` istället för `fprintf(stderr, ...)`, eftersom
stderr redan kan vara stängd.

```c
// Initiera logger INNAN daemonisering
if (Logger_Initiate(log_path, LOG_LEVEL_DEBUG) != 0)
{
    fprintf(stderr, "Failed to initialize logger\n");
    return EXIT_FAILURE;
}

if (daemonize)
{
    if (Daemon_Init() != 0)
    {
        LOG_FATAL("Failed to daemonize");  // nu faktiskt loggat
        Logger_Shutdown();
        return EXIT_FAILURE;
    }
}
```

---

## Fix 4 — select() med fd_set-begränsning (FD_SETSIZE)

**Filer:** `src/infrastructure/watchdog/Watchdog.c`

### Varför det inte fungerade

`Watchdog_CheckHeartbeat()` använde `select()` med en `fd_set`. `fd_set`
är en bitmask med en hårdkodad övre gräns på `FD_SETSIZE` (1024 på
Linux). Anropet `FD_SET(fd, &set)` skriver bit nummer `fd` i masken
— om `fd >= 1024` skrivs utanför buffertens gränser, vilket är
undefined behavior och potentiellt ett minnesspill.

En watchdog-process som ärvt många öppna filbeskrivare från sin
förälder (t.ex. från en shell med öppna filer, loggningssystem eller
testramverk) kan lätt nå fd-nummer i den regionen.

### Varför det fungerar nu

`select()` + `fd_set` ersattes med `poll()`. `poll()` tar en pekare
till en array av `struct pollfd` och har ingen övre gräns på
fd-numret. Funktionen är i övrigt identisk till beteende.

```c
// Innan: begränsat till fd < 1024
fd_set readfds;
FD_ZERO(&readfds);
FD_SET(heartbeat_pipe[0], &readfds);
select(heartbeat_pipe[0] + 1, &readfds, NULL, NULL, &tv);

// Nu: inga fd-begränsningar
struct pollfd pfd = { .fd = heartbeat_pipe[0], .events = POLLIN };
poll(&pfd, 1, timeout_sec * 1000);
```

---

## Fix 5 — Fryst daemon med ren exit blockerade omstart (logikfel)

**Filer:** `src/infrastructure/watchdog/Watchdog.c`

### Varför det inte fungerade

När watchdog:en klassificerade daemonen som fryst (timeout på hjärtslag)
skickades SIGTERM. Om daemonen svarade på SIGTERM och avslutade med
exit-kod 0 nåddes `daemon_died:`-etiketten med `WIFEXITED(status) == 1`
och `WEXITSTATUS(status) == 0`. Den grenen loggade "Daemon exited
cleanly (code 0), shutting down" och returnerade 0 — watchdog:en
avslutade *utan att starta om daemonen*.

Det är alltså en motstridighet: watchdog:en hade just konstaterat att
daemonen var fryst, men behandlade dess svar på SIGTERM som ett
planerat, rent avslut och stängde av sig själv.

### Varför det fungerar nu

En boolesk flagga `killed_for_timeout` sätts till `1` precis innan
SIGTERM skickas till en fryst daemon. I `daemon_died:`-hanteraren
kontrolleras flaggan: exit-kod 0 tolkas bara som "rent avslut" (och
triggar watchdog-shutdown) om flaggan *inte* är satt.

```c
int killed_for_timeout = 0;

// Vid heartbeat-timeout:
killed_for_timeout = 1;
kill(daemon_pid, SIGTERM);
// ...

// I daemon_died:-hanteraren:
if (exit_code == 0 && !killed_for_timeout)
{
    LOG_INFO("Watchdog: Daemon exited cleanly (code 0), shutting down");
    return 0;   // planerad nedstängning
}
// killed_for_timeout == 1: fall igenom till omstartslogiken
LOG_WARNING("Watchdog: Frozen daemon exited cleanly after SIGTERM, restarting");
```

Flaggan återställs till `0` efter varje lyckad omstart.

---

## Testresultat

Alla fem scenarion verifierades med automatiserade tester efter fixarna:

| Test | Scenario |
|------|----------|
| 1 | Watchdog startar daemon, PID-fil skapas, båda processer lever |
| 2 | Hjärtslag flödar var 5:e sekund, ingen falsk-positiv frozen-detektion |
| 3 | SIGTERM till watchdog → graceful shutdown, PID-fil borttagen |
| 4 | SIGKILL på daemon → watchdog omstartar med 2s exponentiellt backoff |
| 5 | 5 krascher i följd → backoff 2→4→8→16→32s → watchdog ger upp och loggar fatalt fel |

Resultat: **13/13 PASS**
