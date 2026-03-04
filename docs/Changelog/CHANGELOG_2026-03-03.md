# Ändringslogg - 2026-03-03

## Platform-lager och demo-scripts

Platform-lagret är implementerat för att demonstrera separationen mellan auth-server och klient-enhet. Fyra demo-scripts seedar båda databaserna och visar arkitekturen. Projektet är klart för redovisning.

---

## Platform-lagret

Ett minimalt auth-lager för JWT-utfärdning. Tanken är att visa att autentisering sker från en separat databas som aldrig ser användarens energy-data.

**PlatformDB** — användartabell med user_id, email och plan_type (free/basic/premium). Lever i `src/platform/database/`.

**JWTIssuer** — genererar HS256-signerade tokens med 24h TTL. Inkluderar userId (sub), email och plan i JWT-claims. Lever i `src/platform/auth/`.

Det är allt som behövs för demon — en users-tabell och en funktion som genererar tokens.

---

## Varför två databaser?

Projektet demonstrerar privacy-first-arkitektur för IoT-enheter. Energy-data lever lokalt på kundens enhet och lämnar aldrig systemet. Platform-servern är bara där för autentisering och prenumerationshantering.

**platform.db** innehåller:
- users-tabell: user_id, email, plan_type
- Används för: JWT-utfärdning

**gridguard.db** innehåller:
- user_configs: solpanelsstorlek, plats, elkonsumtion, grid-avgifter
- schedules: schemalagda laster, kostnadsberäkningar
- Används för: all energioptimering och beräkningar

Platform-servern ser aldrig gridguard.db. JWT-token innehåller bara userId, email och subscription-nivå — ingen energidata.

---

## Demo-scripts

Fyra nya scripts i `scripts/`:

**seed_platform.py** — seedar platform.db med tre testanvändare (test_user/premium, free_user/free, basic_user/basic).

**seed_client.py** — seedar gridguard.db med test_user-config (Stockholm, 20m² solpaneler, 1.5 kWh timförbrukning, SE3-region) och tre demo-schedules (elbilsladdning, diskmaskin, tvättmaskin).

**generate_jwt.py** — läser en användare från platform.db och genererar JWT-token med GridGuards JWTIssuer. Scriptet kompilerar en liten C-wrapper som anropar JWTIssuer direkt — samma kod som plattformen skulle använda i produktion.

**demo.sh** — master-script som kör allt i rätt ordning och visar arkitekturseparationen visuellt. Skapar båda databaserna, genererar token, decodar JWT-payload för att visa claims, och summerar privacy-garantin.

Alla scripts är Python 3 och använder standardbibliotek. generate_jwt behöver gcc och GridGuards build-artefakter.

---

## JWT-generering utan Logger-brus

generate_jwt.py hade problem med Logger som skrev till stdout samtidigt som token printades:

```
[2026-03-03 18:10:10] INFO  JWTIssuer.c:125: JWTIssuer: Created token for user=test_user plan=premium
eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9...
```

Token kunde inte användas direkt i curl eller exporteras. Lösningen var att redirecta stdout under JWT-generering:

C-wrappern sparar originell stdout med `dup(STDOUT_FILENO)`, redirectar till /dev/null med `dup2(fileno(devnull), STDOUT_FILENO)`, kör JWTIssuer (Logger skriver nu till /dev/null), återställer stdout, och printar bara token. Clean output utan att röra Logger-koden.

---

## Database-mappar omstrukturerade

Client-databas-filerna flyttades från `src/client/database/` till `src/database/` direkt. Det finns ingen anledning att ha en hel `src/client/`-struktur när det bara är databasfiler.

**Flyttade filer:**
- ClientDB.c/h — initiering av client-databasen
- UserConfigDB.c/h — hanterar user_configs-tabellen
- ScheduleDB.c/h — hanterar schedules-tabellen

**Makefile uppdaterades:** `CLIENT_DB_DIR` pekar nu direkt på `src/database`, build-katalogen skapas som `build/database`.

Separation mellan platform och client DB är fortfarande tydlig:
- `src/platform/database/PlatformDB.c` — platform-serverns auth-data
- `src/database/ClientDB.c` — enhetens lokala data

---

## Makefile dev-target

`make dev` kör nu:

1. `python3 scripts/seed_platform.py platform.db`
2. `python3 scripts/seed_client.py gridguard.db`
3. `python3 scripts/generate_jwt.py platform.db test_user` för att generera dev-token
4. Startar watchdog med rätt miljövariabler
5. Kör test-requests mot servern

Seedningen är idempotent — INSERT OR REPLACE används så att `make dev` kan köras om och om igen.

---

## Status

Platform-lagret är klart för demo. JWT-tokens genereras från platform.db, servern validerar dem, och all energy-data lever i gridguard.db lokalt. Demo-scriptet visar separationen tydligt.
