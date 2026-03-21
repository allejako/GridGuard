# Changelog 2026-03-21

**Branch:** `development`
**Status:** Pushad

---

## Sammanfattning

Bugfix i `make dev` — launcherbinären byggdes aldrig, vilket gjorde att servern aldrig startade. PID-displayen visade dessutom fel PIDs för alla fyra processer.

---

## Buggar

### `make dev` startade aldrig servern

`make dev` använde `$(MAIN_BIN)` (`bin/GridGuard`) för att starta systemet, men hade inte `$(MAIN_BIN)` som beroende. Binären byggdes alltså aldrig av det målet. `setsid` försökte exekvera en fil som inte existerade och misslyckades tyst (stderr omdirigerad till `/dev/null`).

Hälsocheck-loopen körde alla 20 försök utan att lyckas, men skrev " ready" ändå — vilket dolde att inget faktiskt körde. Testerna som följde misslyckades med "failed" eftersom port 8080 var stängd.

Åtgärdat genom att lägga till `$(MAIN_BIN)` som beroende i `dev`-målet:

```makefile
dev: server watchdog platform-objects $(MAIN_BIN)
```

---

### PID-displayen visade fel PIDs

Raden som skriver ut PIDs för watchdog, server, fetcher och parser använde `SERVER_PID` för både watchdog- och server-kolumnen, och `FETCHER_PID` för både fetcher- och parser-kolumnen. Alla fyra processer visades alltså med samma par av PIDs.

Åtgärdat — varje process läses nu ut korrekt:
- `WATCHDOG_PID` från `/tmp/gridguard.pid`
- `SERVER_PID` via `pgrep -f GridGuard-server`
- `FETCHER_PID` via `pgrep -f GridGuard-fetcher`
- `PARSER_PID` via `pgrep -f GridGuard-parser`

Fixen tillämpades i både `dev`- och `client`-målen.

---

## Ändrade filer

- `Makefile` — `$(MAIN_BIN)` som beroende i `dev`, PID-display fixad i `dev` och `client`
