# Ändringslogg - 2026-03-02

## Övergång till multi-process-arkitektur + zombie-processer fixade

GridGuard började som en tråd-baserad pipeline: HTTP-trådar, en Fetch-tråd, en Parse-tråd och en Compute-tråd kommunicerande via Queue:er och mutex/cond-synkronisering. Det fungerade, men täckte bara kursvecka 2-3. Kursmålen kräver att vi demonstrerar `fork()`, `exec()`, `waitpid()`, anonyma pipes, namngivna pipes (FIFO), Unix domain sockets, och shared memory — alla IPC-mekanismer från kursvecka 1, 4 och 5. Det gick inte med en single-process-design.

Lösningen var att refaktorera till tre separata processer: GridGuard-server (HTTP + Compute), GridGuard-fetcher och GridGuard-parser. Varje process är en fristående binär med egen `main()`. De kommunicerar uteslutande via POSIX IPC — ingen delad kod, inget delat minne utom explicit via `shm_open()`. Det är mer komplext, men täcker alla kursmål.

Efter ombyggnationen fungerade inte processerna. De dukade upp som zombies direkt efter `fork()` och pipeline hängde sig. Problemet var att daemon-läget byter working directory till `/`, vilket gjorde att `execl("./bin/GridGuard-fetcher", ...)` letade i `/bin/` istället för projektets bin-katalog. Lösningen blev att använda `readlink("/proc/self/exe")` för att dynamiskt hitta binärernas sökväg relativt till den körande servern.

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

## Lösning: Hitta binärernas sökväg dynamiskt

Första lösningen var att hårdkoda absoluta paths i GridGuard.c:

```c
static const char *FETCHER_BIN = "/home/znees/github/GridGuard/bin/GridGuard-fetcher";
static const char *PARSER_BIN = "/home/znees/github/GridGuard/bin/GridGuard-parser";
```

Det fungerade, men krävde att man uppdaterar koden om projektet flyttas. Bättre lösning: använd `/proc/self/exe` för att hitta serverns egen sökväg, sedan räkna ut bin-katalogen relativt till den.

```c
char exe[PATH_MAX];
ssize_t exe_len = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
if (exe_len > 0) {
    exe[exe_len] = '\0';
    char exe_copy[PATH_MAX];
    strncpy(exe_copy, exe, sizeof(exe_copy) - 1);
    const char *bin_dir = dirname(exe_copy);
    snprintf(app->fetcherBin, sizeof(app->fetcherBin),
             "%s/GridGuard-fetcher", bin_dir);
    snprintf(app->parserBin, sizeof(app->parserBin),
             "%s/GridGuard-parser", bin_dir);
}
```

`/proc/self/exe` är en symbolisk länk till den körande binären. Om servern ligger i `/home/znees/github/GridGuard/bin/GridGuard-server` pekar länken dit, och `dirname()` extraherar `/home/znees/github/GridGuard/bin`. Då fungerar det oavsett var projektet ligger.

Fallback om `readlink()` misslyckas (andra OS än Linux): använd relativ path `bin/GridGuard-fetcher`. Det fungerar om servern körs från projektroten utan daemon-läge.

Nu kan projektet flyttas till vilken katalog som helst och `make dev` fungerar direkt.

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

## Varför gick vi från trådar till processer?

Första arkitekturen såg ut så här:

```
GridGuard-server (single process)
├── HTTP ThreadPool (20 workers)
├── Fetch thread (hämtar från API:er)
├── Parse thread (parsar JSON)
└── Compute thread (räknar energi-plan)
    └── Kommunikation: Queue + mutex/cond
```

Det fungerade. Men det täcker bara kursvecka 2-3 (pthreads och synkronisering). Kursmål 2 och 8 kräver att vi använder IPC för processkommunikation — inte trådar som delar samma minnesområde.

Kursplaneringen specificerar:

**Kursvecka 1:** fork(), exec(), waitpid() — skapa och hantera barnprocesser
**Kursvecka 4:** Anonymous pipes, named pipes (FIFO), dup2()
**Kursvecka 5:** Unix domain sockets, shared memory (shm_open, mmap), semaforer

En tråd-baserad design använder ingen av dessa. Trådar delar samma adressrymd och kommunicerar via gemensamt minne med mutex-skydd. Det är kursvecka 2-3, inte kursvecka 4-5.

### Vad gör multi-process-designen annorlunda?

Ny arkitektur:

```
GridGuard-server (main process)
├── HTTP ThreadPool
├── Compute thread
├── fork() → GridGuard-fetcher (egen process)
│   └── Läser WorkRequest från anonymous pipe (stdin)
│   └── Skriver FetchResult till named pipe (FIFO)
└── fork() → GridGuard-parser (egen process)
    └── Läser FetchResult från FIFO
    └── Lyssnar på Unix domain socket
    └── Skickar ParseResult när Compute ansluter
```

**IPC-flöde:**

1. HTTP-thread skapar WorkRequest → skriver till **anonymous pipe** (kursvecka 4)
2. Fetcher läser från stdin (pipe), hämtar data, skriver till **named FIFO** (kursvecka 4)
3. Parser läser från FIFO, parsar JSON, startar **Unix domain socket-server** (kursvecka 5)
4. Compute-thread ansluter till socket, läser ParseResult
5. Fetcher och Parser cacher API-svar i **POSIX shared memory** med **semaforer** (kursvecka 5)

Varje mekanism täcker ett specifikt kursmål. Fetcher och Parser är fristående binärer som startas med `fork()` och `exec()` (kursvecka 1). Main-processen reaper dem med `waitpid()` vid shutdown.

Det är betydligt mer komplext än den tråd-baserade designen. Men det är enda sättet att täcka alla IPC-kursmål.

---

## Nästa steg — C++-klient och examination

C++-klienten är sista delen av kursmålen (C++ STL, klasser, RAII). När den är klar täcker projektet:

- **Kursvecka 1-2:** fork, exec, waitpid, threading
- **Kursvecka 3:** mutex, condition variables, completion registry
- **Kursvecka 4:** anonymous pipes, named pipes (FIFO)
- **Kursvecka 5:** Unix domain sockets, shared memory, semaforer
- **Kursmål 9:** C++ STL, klasser, RAII i klienten

Projektet är redo för redovisning.

---

## Sökvägar, DB-seeding, load shifting och algoritm-översyn

Fyra separata problem åtgärdade: binärsökvägarna var hårdkodade till en annan maskin, databasen seedad aldrig vid uppstart, load shifting saknades helt, och BUY/SELL/IDLE-algoritmen gav signaler som inte stämde med vad kunden faktiskt betalar.

### Sökvägar löses nu vid runtime

Förra lösningen hårdkodade binärernas sökvägar till `/home/znees/github/GridGuard/bin/` direkt i `GridGuard.c`. Nu läser `GridGuard.c` sin egen sökväg ur `/proc/self/exe` via `readlink()` och bygger sökvägarna relativt den katalogen. Sökvägarna lagras i `GridGuard`-structen som `fetcherBin[4096]` och `parserBin[4096]`.

### DB-seeding och schemamigration

`make dev` seedade aldrig test_user-konfigurationen om databasen saknades. Ett Python-skript, `scripts/seed_db.py`, skapar nu databasen direkt med `INSERT OR IGNORE` innan servern startar. `Database_Initiate()` kör `ALTER TABLE ADD COLUMN` för saknade kolumner och ignorerar SQLite:s "duplicate column name"-fel — gamla databaser migreras automatiskt.

### Load shifting: /schedule-endpointen

`LoadScheduler` (`src/application/services/`) hittar det billigaste sammanhängande tidsfönstret för en given last via sliding window-algoritm med beräknade besparingar vs omedelbar start.

`ScheduleDB` (`src/infrastructure/database/`) lagrar planerade körningar i SQLite med soft delete.

Tre nya endpoints: `POST /schedule`, `GET /schedule`, `DELETE /schedule/<id>`.

### CompletionRegistry: stale entry ledde till timeout

`POST /schedule` utlöser ett andra forecast-anrop för samma `userId`. Det misslyckades med "Pipeline error or timeout" eftersom `CompletionRegistry` behöll gamla poster med skräp-pekare. Lösningen: `UnregisterCompletion()` anropas innan `WorkCompletion_Signal()` på alla utvägar ur compute-loopen.

### Ny BUY/SELL/IDLE-algoritm

**Tre-pass-struktur:**
- **Pass 1:** Beräknar totalkostnad (spot + nätavgift + skatt + moms) per kvartal
- **Pass 2:** Sorterar och deriverar trösklar — `buyThreshold` (P30) och `medianCost` (P50)
- **Pass 3:** Fattar beslut: surplus + positivt spotpris → SELL, surplus + negativt spotpris → IDLE, kostnad ≤ P30 → BUY, annars → IDLE

Varje timme i svaret innehåller nu `savings_vs_median_sek_kwh`. `summary.total_cost_sek` visar faktisk konsumentkostnad med alla avgifter.

---
# Ändringslogg - 2026-03-02

## Sökvägar, DB-seeding, load shifting och algoritm-översyn

Fyra separata problem åtgärdade: binärsökvägarna var hårdkodade till en annan maskin, databasen seedad aldrig vid uppstart, load shifting saknades helt, och BUY/SELL/IDLE-algoritmen gav signaler som inte stämde med vad kunden faktiskt betalar.

---

## Sökvägar löses nu vid runtime

Förra lösningen hårdkodade binärernas sökvägar till `/home/znees/github/GridGuard/bin/` direkt i `GridGuard.c`. Det fungerade på znees maskin men inte någon annanstans.

Nu läser `GridGuard.c` sin egen sökväg ur `/proc/self/exe` via `readlink()` och bygger sökvägarna till `GridGuard-fetcher` och `GridGuard-parser` relativt den katalogen:

```c
char exe[PATH_MAX];
readlink("/proc/self/exe", exe, sizeof(exe) - 1);
const char *bin_dir = dirname(exe_copy);
snprintf(app->fetcherBin, sizeof(app->fetcherBin), "%s/GridGuard-fetcher", bin_dir);
```

Daemon-processen anropar `chdir("/")` som en del av POSIX-mönstret, så relativa sökvägar fungerar inte. Men en absolut sökväg byggd från serverns faktiska plats på disk fungerar oavsett working directory. Sökvägarna lagras i `GridGuard`-structen som `fetcherBin[4096]` och `parserBin[4096]`.

---

## DB-seeding och schemamigration

`make dev` seedade aldrig test_user-konfigurationen om databasen saknades, vilket resulterade i `{"error": "Database error"}` vid första forecast-anropet.

Försöket att seeda via `curl` efter serverstart fungerar inte — curl returnerar exit 0 även när HTTP-svaret är ett fel, och servern kan hinna svara med 401 om JWT-hemligheten inte är satt rätt. Istället skapar ett Python-skript, `scripts/seed_db.py`, databasen direkt med `INSERT OR IGNORE` innan servern ens startar.

```bash
python3 scripts/seed_db.py "$(CURDIR)/gridguard.db"
```

Skriptet använder Pythons inbyggda `sqlite3`-modul (sqlite3 CLI-verktyget var inte installerat) och kör samma `CREATE TABLE IF NOT EXISTS` och `ALTER TABLE ADD COLUMN`-migrationer som servern gör. `INSERT OR IGNORE` gör att befintlig konfiguration lämnas orörd.

**Schemamigration:** Det gamla `gridguard.db` saknade kolumnerna `grid_fee_low`, `grid_fee_normal` och `grid_fee_high`. `CREATE TABLE IF NOT EXISTS` är en no-op på befintliga tabeller, så kolumnerna aldrig lades till vid uppstart.

`Database_Initiate()` kör nu `ALTER TABLE ADD COLUMN` för varje kolumn och ignorerar `"duplicate column name"`-felet som SQLite returnerar om kolumnen redan finns. Det gör att gamla databaser migreras automatiskt utan att nya databaser påverkas.

---

## Load shifting: /schedule-endpointen

`docs/KRITISKA_FYND_OCH_VAGEN_FRAMAT.md` pekade ut att systemet saknade ett sätt att faktiskt hjälpa kunden flytta förbrukning — forecast-signalerna sa "köp nu" men det fanns ingen mekanism att agera på det.

Tre nya delar:

**`LoadScheduler`** (`src/application/services/`) hittar det billigaste sammanhängande tidsfönstret för en given last. Den tar ett energiprisschema (timmar med totalkostnad), lastens effekt i kW, hur lång tid den tar och en deadline. Sliding window-algoritmen itererar alla möjliga starttider och väljer fönstret med lägst total kostnad. Den beräknar även vad samma last kostar om den startar omedelbart, vilket ger ett konkret besparingstal.

**`ScheduleDB`** (`src/infrastructure/database/`) lagrar planerade körningar i `schedules`-tabellen i SQLite. Soft delete — "cancelled" är ett statusvärde, inte en DELETE-SQL. Tre funktioner: `Insert`, `GetByUser` och `Delete`.

**Tre nya endpoints i `ClientHandler.c`:**

- `POST /schedule` — tar `load_id`, `duration_minutes`, `power_kw` och `deadline`. Kör hela forecast-pipelinen för kunden, konverterar svaret till ett `SchedulerEntry[]`-array och anropar `LoadScheduler_FindWindow()`. Sparar det optimala fönstret i databasen och returnerar JSON med `scheduled_start`, `estimated_cost_sek` och `savings_sek`.

- `GET /schedule` — listar alla aktiva scheman för inloggad användare.

- `DELETE /schedule/<id>` — avbokar ett schema (sätter status till `cancelled`).

`make dev` kör ett exempelanrop automatiskt — en elbil (216 minuter, 11 kW) med deadline imorgon 07:00 UTC.

---

## CompletionRegistry: stale entry ledde till timeout

`POST /schedule` löser implicit ett andra forecast-anrop för samma `userId`. Det misslyckades alltid med "Pipeline error or timeout".

`CompletionRegistry` är en global array som mappar `userId → WorkCompletion*`. `RegisterCompletion` lägger alltid in en ny post. `FindCompletionByUserId` söker linjärt och returnerar första träff. Problemet: när compute-tråden signalerade och HTTP-tråden väckte var `WorkCompletion`-objektet redan frigjort på stacken. Nästa request för samma `userId` hittade slot 0 med det gamla `userId` kvar — pointervärdena var skräp.

Lösningen är att anropa `UnregisterCompletion()` innan `WorkCompletion_Signal()`, på alla tre utvägar ur compute-loopen (success, compute-fel, serialiseringsfel). Det tar bort posten ur registret medan signalen skickas, så nästa request börjar med ett rent tillstånd.

---

## Compute: ny BUY/SELL/IDLE-algoritm

Den gamla algoritmen hade tre problem:

**BUY-tröskeln var meningslös.** `avgTotalCost × 0.80` ger noll BUY-signaler när priserna är jämna och trettio BUY-signaler när priserna spretar. Kunden visste aldrig hur många "billiga timmar" de kunde förvänta sig.

**SELL vid negativt spotpris.** Systemet exporterade alltid solöverskott. Men vid negativa spotpriser (allt vanligare vid hög vindkraftsproduktion) betalar man i praktiken för att mata in på nätet. Det är bättre att konsumera egenkrafts och hålla IDLE.

**Totalkostnad i sammanfattningen använde spotpriset.** `summary.total_cost_sek` visade ungefär 40% av vad kunden faktiskt betalar när nätavgift, energiskatt och moms inkluderas.

### Ny struktur: tre passes

**Pass 1** beräknar totalkostnad (spot + nätavgift + skatt + moms) för varje giltig timme och lagrar värdena i `entryCosts[]`. De samlas även i `sortedCosts[]` för sortering.

**Pass 2** kör `qsort` och deriverar två trösklar:
- `buyThreshold` — 30:e percentilen av totalkostnad. Alltid de billigaste ~30% av timmarna i prognosen, oavsett hur priserna spretar den dagen.
- `medianCost` — 50:e percentilen. Används som referenspunkt för beräkning av per-timme-besparing.

**Pass 3** fattar besluten:
```
solöverskott och positivt spotpris → SELL
solöverskott och negativt spotpris → IDLE
totalkostnad ≤ 30:e percentilen   → BUY
annars                             → IDLE
```

Varje timme i forecast-svaret innehåller nu `savings_vs_median_sek_kwh` — hur mycket billigare (eller dyrare) timmen är jämfört med medianen. Positivt tal för BUY-timmar, negativt för dyra IDLE-timmar. En kund som ser BUY 03:00 med `savings_vs_median_sek_kwh: 0.42` och behöver ladda 10 kWh vet att det sparar ~4.20 kr jämfört med att vänta.

`summary.total_cost_sek` visar nu faktisk konsumentkostnad med alla avgifter.

---

## Kvarstående begränsningar

Förbrukningsprofilen är fortfarande ett konstant `consumptionKwh` per timme. Verkliga hushåll har morgon- och kvällstoppar som är 2–3× baslasten. En SELL-signal kl 07:00 kan i praktiken vara nettoimport om hushållet frukostlagar. Det kräver att kunden antingen anger en timbaserad förbrukningsprofil eller att systemet lär sig mönstret från historik.

`BUY_PERCENTILE = 0.30` är vald utifrån att ett typiskt hushåll har 6–8 flexibla timmar per dygn. Det är inte kalibrerat mot faktiska beteendedata.
