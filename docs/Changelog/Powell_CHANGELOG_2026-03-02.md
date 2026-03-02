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
