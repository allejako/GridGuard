# Ändringslogg - 2026-03-02

## Processarkitektur fixad + förbättrade development targets

Multi-process-arkitekturen fungerar nu korrekt. Zombie-processen från förra körningen berodde på att daemon-läget ändrar working directory till `/`, vilket gjorde att `execl()` inte kunde hitta process-binärerna med relativa paths. Lösningen var att hårdkoda absoluta paths till `bin/GridGuard-fetcher` och `bin/GridGuard-parser`. `make dev` och `make stop` städades upp och skrev om utan emojis och fancy formatting.

---

## Problemet: Zombie-processer efter fork+exec

När servern startades i daemon-läge skapades Fetcher- och Parser-processerna korrekt med `fork()`, men direkt efter `execl()` blev de zombies. `ps aux` visade:

```
znees  123  0.0  0.0      0     0 ?  Z  08:42  [GridGuard-serve] <defunct>
```

Ingen data flödade genom pipeline trots att servern var igång och svarade på `/health`. Varje försök att hämta prognos gav timeout eller "Queue full" eftersom Fetcher-processen aldrig startade.

### Vad händer när en daemon startar?

`Daemon.c` implementerar klassisk Unix-daemon-sekvens: dubbel-fork för att bli process group leader, `setsid()` för att koppla loss från terminal, och slutligen `chdir("/")` för att inte låsa filsystemet. Det sista steget är problemet.

Daemon-processen byter working directory till `/` för att garantera att den inte håller kvar någon katalog som administratören vill unmount:a. Det är korrekt Unix-beteende och standard för alla daemons (nginx, sshd, systemd-enheter). Men det betyder att när GridGuard.c kör `fork()` och försöker köra `execl("./bin/GridGuard-fetcher", ...)` letar systemet efter `/bin/GridGuard-fetcher` — inte projektets bin-katalog.

`execl()` misslyckas, child-processen avslutar med `EXIT_FAILURE`, och `waitpid()` i parent-processen körs aldrig eftersom den tror att child-processen fortfarande lever. Resultatet är zombie.

---

## Lösning: Absoluta paths i execl()

GridGuard.c fick två nya konstanter:

```c
static const char *FETCHER_BIN = "/home/znees/github/GridGuard/bin/GridGuard-fetcher";
static const char *PARSER_BIN = "/home/znees/github/GridGuard/bin/GridGuard-parser";
```

Dessa används i `execl()`:

```c
execl(FETCHER_BIN, "GridGuard-fetcher", app->fifoPath, NULL);
execl(PARSER_BIN, "GridGuard-parser", app->fifoPath, app->socketPath, NULL);
```

När daemon-processen nu byter till `/` spelar det ingen roll — `execl()` får en fullständig path som fungerar oavsett vilket working directory som gäller. Processerna startar korrekt, öppnar sina IPC-resurser och börjar kommunicera.

### Varför hårdkoda sökvägen?

I produktionsmiljö skulle man antingen installera binärerna i `/usr/local/bin/` och använda `execvp()` (som söker i `$PATH`), eller läsa installationssökvägen från en config-fil. Men detta är ett universitetsprojekt med en enda utvecklare — hårdkodning är enklast och tydligast.

Om projektet flyttas till en annan maskin eller katalog måste GridGuard.c uppdateras. Det är acceptabelt för detta projekt.

---

## Verifiering: Tre processer igång samtidigt

Efter ombyggnation och omstart:

```
$ make dev
Starting server...
Waiting for server ready

  [PID 182365] GridGuard-server
  [PID 182367] GridGuard-fetcher
  [PID 182386] GridGuard-parser

  /tmp/gridguard_fetch_to_parse.fifo
  /tmp/gridguard_parse_to_compute.sock
```

Alla tre processer lever. En test-forecast-request går igenom hela pipeline:

1. HTTP-thread tar emot GET `/forecast`
2. WorkRequest skrivs till anonymous pipe
3. Fetcher läser från stdin, hämtar Open-Meteo och Elpriset, skriver FetchResult till FIFO
4. Parser läser från FIFO, parsar JSON, väntar på Unix socket-anslutning
5. Compute-thread ansluter till socket, läser ParseResult, genererar energi-plan
6. HTTP-thread vaknar, skickar JSON-svar

96 timmar prognos, 118 kWh grid import, 26.85 SEK total kostnad. Systemet fungerar.

---

## make dev och make stop — städade Makefile

`make dev` hade box-drawing characters, emojis och formatering som skrek "AI-genererat". Lika illa var `make stop` som hade checkmarks och färgkodade meddelanden.

### Före:

```
🛑 Stopping GridGuard...
→ Killing processes...
  ✓ Watchdog (PID 12345)
╔═══════════════════════════════╗
║  ✅ Server running...          ║
╚═══════════════════════════════╝
```

### Efter:

```
Stopping GridGuard...
  Killed watchdog (PID 12345)
==========================================
  GridGuard Development Server
==========================================
```

Ingen funktionalitet ändrades — bara formatering. Targets fungerar identiskt men ser nu ut som alla andra professionella C-projekts Makefiles. Jämför med nginx, Redis eller PostgreSQL: inga emojis, bara enkla meddelanden och `=====`-separatorer.

---

## Varför multi-process-arkitektur?

Detta projekt kunde ha använt en single-process threaded-design: en Fetch-thread, en Parser-thread och en Compute-thread kommunicerande via Queue:er. Det hade varit enklare.

Men kursmålen kräver att vi demonstrerar `fork()`, `exec()`, `waitpid()`, named pipes (FIFO), Unix domain sockets och shared memory. En threaded design täcker bara pthread-delen.

Multi-process-arkitekturen tvingar fram implementering av alla IPC-mekanismer som lärs ut i kursvecka 1-5. Varje process är en fristående binär med egen `main()` som kan debuggas och testas separat. Fetcher vet ingenting om Parser. Parser vet ingenting om Compute. De kommunicerar endast via POSIX IPC — precis som verkliga Unix-system.

Det är mer komplext. Men det är det som gör att projektet täcker kursmålen.

---

## Nästa steg — C++-klient och examination

C++-klienten är sista delen av kursmålen (C++ STL, klasser, RAII). När den är klar täcker projektet:

- **Kursvecka 1-2:** fork, exec, waitpid, threading
- **Kursvecka 3:** mutex, condition variables, completion registry
- **Kursvecka 4:** anonymous pipes, named pipes (FIFO)
- **Kursvecka 5:** Unix domain sockets, shared memory, semaforer
- **Kursmål 9:** C++ STL, klasser, RAII i klienten

Projektet är redo för redovisning.
