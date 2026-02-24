# Ändringslogg - 2026-02-25

## C++ CLI-klient, SMHI borttaget, Makefile-flöde och Compute-fix

C++-klienten är implementerad och kan kommunicera med servern end-to-end. SMHI är borttaget som väderkälla — Open-Meteo täcker nu allt (temperatur, luftfuktighet, vind, molntäcke, solstrålning). Makefile har ett stabilt dev-flöde med `make dev` och `make stop`. En bugg i kostnadsberäkningen är åtgärdad.

---

## C++ CLI-klienten

Klienten är skriven i C++17 och lever i `src/client/`. Den kompileras till `bin/GridGuard-client` och har tre kommandon:

**`gridguard login <token>`** sparar JWT-token till `~/.gridguard/token`. Katalogen skapas automatiskt om den inte finns. Token läses automatiskt vid nästa forecast-körning — du behöver bara logga in en gång.

**`gridguard forecast`** öppnar en TCP-anslutning mot servern på `localhost:8080`, skickar `GET /forecast` med JWT som Bearer-token och visar resultatet som en färgkodad tabell i terminalen. BUY-signaler visas i cyan, SELL i gult, IDLE i grått. En ASCII-stapel visar relativ solproduktion per timme. Längst ned visas aggregerad import, export och beräknad kostnad.

**`gridguard config`** öppnar webbläsaren till konfigurationssidan via `xdg-open` (Linux) eller `open` (macOS). Token levereras som URL-fragment (`#token=...`) och når aldrig servern — JavaScript på konfigurationssidan läser den lokalt.

Klienten är uppdelad i tre delar:

- `TokenManager.hpp` — header-only klass som läser och skriver token-filen. Kastar `std::runtime_error` med svenska felmeddelanden om filen saknas eller är tom.
- `GridGuardClient.hpp/cpp` — hanterar HTTP-kommunikationen mot servern via POSIX-sockets och parsar JSON-svaret med cJSON (samma lib som servern använder).
- `main.cpp` — kommandoparser, ASCII-logo och terminalformattering.

---

## Hur du testar klienten

Alternativ 1 — allt-i-ett:

```bash
make dev GRIDGUARD_JWT_SECRET=gridguard-test-secret
```

Startar server via watchdog, loggar in automatiskt med dev-token och kör forecast. Servern lever kvar i bakgrunden efteråt. Stoppa med `make stop`.

Alternativ 2 — manuellt (två terminaler):

Terminal 1 — starta servern:
```bash
make run-watchdog GRIDGUARD_JWT_SECRET=gridguard-test-secret
```

Terminal 2 — logga in och hämta prognos:
```bash
./bin/GridGuard-client login "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJzdWIiOiJ0ZXN0X3VzZXIiLCJleHAiOjE4OTM0NTYwMDB9.d33GazykNsOCuOyy545_484DACV1vEd3owJr-dvL-1c"
./bin/GridGuard-client forecast
```

Testtoken är signerad med `gridguard-test-secret` och giltig till 2030. `sub` är `test_user` — E2E-testskriptet seedar den användaren i databasen, men du kan också sätta config manuellt:

```bash
curl -X PUT http://localhost:8080/user/config \
  -H "Authorization: Bearer <token>" \
  -H "Content-Type: application/json" \
  -d '{"latitude":57.70,"longitude":11.97,"region":"SE3","solar_area_m2":20.0,"solar_efficiency":0.18}'
```

---

## SMHI borttaget — Open-Meteo som enda väderkälla

SMHI returnerar en 10-dagarsprognos men spotpriser finns bara för idag och imorgon, vilket gör de längre posterna meningslösa. Open-Meteo ger redan alla fält som pipelinen behöver: temperatur, luftfuktighet, vind, molntäcke i procent och solstrålning i W/m².

All SMHI-kod är borttagen ur kodbasen: `SMHIResponse.h` är raderad, `Parser_ParseSMHI()` är borttagen ur `Parser.c/h`, `BuildSMHIApiUrl()` är borttagen ur `APIEndpoints.c/h` och `SMHI_API_BASE_URL` är borttagen ur `Config.h`. Integrationstestet `test_multi_source_weather.c` är ersatt av `test_openmeteo_parser.c` som testar Open-Meteo och Elpriset.

`ParseWorker` bygger nu `WeatherData` direkt från Open-Meteo via `WeatherFromOpenMeteo()` — ingen timestamp-matchning mot en andra källa behövs, och molntäcke är redan i procent.

**Modulärt mönster för nya API-källor** är dokumenterat direkt i `APIEndpoints.h` och `Parser.h`:
1. Ny `<Source>Response.h` i `src/application/models/apis/`
2. `Build<Source>ApiUrl()` i `APIEndpoints.h/c`
3. `Parser_Parse<Source>()` i `Parser.h/c`
4. Mappningsfunktion i `ParseWorker.c`

En bieffekt: `FetchResult`-structen tappade `smhiJson[1000000]` — en megabyte per request i kön är borta.

---

## Makefile: make dev och make stop

`make dev GRIDGUARD_JWT_SECRET=<nyckel>` gör följande i ett svep:

1. Dödar eventuella gamla server- och watchdog-processer via PID-filer
2. Bygger server, client och watchdog om nödvändigt
3. Startar watchdog i en isolerad session (`setsid`) med rätt DB-sökväg
4. Väntar tills `/health` svarar
5. Loggar in automatiskt med dev-token
6. Kör forecast och visar resultatet
7. Lämnar servern igång i bakgrunden

`make stop` dödar watchdog och server via samma PID-filer.

**Varför PID-filer och inte `pkill -f`?** `pkill -f "GridGuard-watchdog"` söker i hela kommandoraden inklusive argumenten till `pkill`-kommandot självt. Det resulterade i att make-skalet dödade sig självt (SIGKILL/SIGTERM tillbaka till make-processen). PID-filer är deterministiska och dödar exakt rätt process.

**Varför `setsid`?** Utan det hamnar watchdog-processen i samma processgrupp som make-skalet. En SIGTERM till watchdog propagerades tillbaka och terminerade make. `setsid` skapar en ny session, isolerat från make.

**Varför `GRIDGUARD_DB_PATH`?** Daemonen anropar `chdir("/")` som del av POSIX daemon-mönstret. Det gör att en relativ sökväg som `"gridguard.db"` löser upp till `/gridguard.db` — utan skrivbehörighet. `GridGuard.c` läser nu `GRIDGUARD_DB_PATH` från miljön med fallback till `DB_PATH` i `Config.h`. `make dev` och `make run-watchdog` sätter variabeln till `$(CURDIR)/gridguard.db`.

---

## Buggfix: Compute — IDLE-timmar bidrar nu till kostnad

Tidigare räknades kostnad och nätimport bara för timmar med `ACTION_BUY_FROM_GRID`. IDLE-timmar (när priset är högt och systemet inte aktivt köper extra) konsumerar ändå el från nätet vid negativt netto (förbrukning > produktion), men det registrerades inte.

Effekten var att sammanfattningsraden visade `Kostnad ~0.00 kr` trots att IDLE-timmar med priser runt 1.10 kr/kWh drog el. Nu räknas nätimport och kostnad för alla timmar där `netKwh < 0`, oavsett signal. BUY och IDLE skiljer sig i intent (kör extra laster vs basförbrukning) men inte i hur nätet behandlar dem.

BUY 0.0000-timmarna är fortfarande korrekta — Elprisets API publicerar morgondagens priser runt kl 13, så timmar utan känt pris visas med 0.00 kr.

---

## Förbättringsförslag — CLI-klienten och `src/client/`

### Kodstruktur

`src/client/` är idag en platt mapp med fyra filer. Det fungerar nu men skalas inte bra:

- `TokenManager.hpp`, `GridGuardClient.hpp/cpp` och `main.cpp` blandar ansvarsområden i samma nivå — HTTP-lager, auth, UI och kommandon sitter bredvid varandra utan separation.
- `main.cpp` gör för mycket: den parsar kommandon, ritar logo, formaterar tabellen och hanterar fel. Det bör brytas ut.

En naturlig uppdelning vore `commands/` (ett fil per kommando), `http/` (GridGuardClient), `auth/` (TokenManager) och `ui/` (logo, solarBar, tabellformattering). Det gör det enkelt att lägga till kommandon utan att röra i varandra.

### Hårdkodad serveradress

`GridGuardClient("localhost", "8080")` är hårdkodad i `cmdForecast()`. En verklig enhet kanske inte svarar på localhost — särskilt inte under onboarding via Wi-Fi AP (192.168.4.1). Det bör läsas från en config-fil (`~/.gridguard/config`) eller en miljövariabel `GRIDGUARD_HOST`.

### Ingen `--version`

`gridguard --version` och `gridguard --help` saknas. Små saker men viktiga för en CLI som ska paketeras och distribueras.

### `system()` i cmdConfig

`system("xdg-open ...")` är funktionellt men spawnear ett onödigt shell-lager. Bättre att använda `execvp` direkt. Det är också säkrare om token-värdet skulle innehålla specialtecken.

### Buffert för Open-Meteo kan vara för liten

`openMeteoJson[8192]` i `FetchResult` rymmer dagens 4-dagars prognos, men om `forecast_days` ökas i `BuildOpenMeteoApiUrl` (t.ex. till 7) kan svaret överstiga bufferten och trunkeras utan felmeddelande. Bufferten bör antingen ökas eller kompletteras med längdvalidering.

### Inga tester för C++-koden

`TokenManager` och `GridGuardClient` saknar enhetstester. `TokenManager` är enkel att testa isolerat (mock-fil, kontrollera att rätt exception kastas). `GridGuardClient`-parsningen kan testas mot hårdkodad JSON utan en server igång.

### Felkoder är inkonsekventa

Klienten returnerar exit-kod 1 på alla fel. En verktygsanvändare som kör `gridguard forecast` i ett skript kan inte skilja på "server nere" (nätfel) och "ogiltig token" (auth-fel) utan att läsa stderr. Separata exit-koder gör klienten skriptbar.
