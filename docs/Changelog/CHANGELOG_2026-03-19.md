# Changelog 2026-03-19

**Branch:** `development`
**Status:** Pushad

---

## Sammanfattning

Produktionsstädning inför demo. Flera buggar som låg och myrade under ytan åtgärdades — mest påtagligt att config-filen aldrig nådde barnprocesserna och att spotpriscachen löpte ut var 15:e minut istället för var 12:e timme. Dessutom fixades DST-hantering, timezone-inkonsekvens, och tomorrow-prisets retry-logik.

---

## Buggar

### Spotpriscachen löpte ut för snabbt

`priceTTL` var satt till 900 sekunder — samma som väder-cachen. Spotpriser från Elprisetjustnu publiceras en gång om dagen och ändras inte under dagen, men cachen tvingade Fetcher att göra om API-anropet var 15:e minut. Det resulterade i ~384 onödiga anrop per dag mot ett API som inte hade ny data att ge.

Ändrat till 43200 sekunder (12 h), vilket matchar hur ofta datan faktiskt förändras.

---

### Tomorrow-priser slutade hämtas om 14:30-fönstret missades

Fetcher hämtade tomorrow-priser en gång per dag och satte `lastPriceFetchDate` oavsett om anropet lyckades. Om Elprisetjustnu inte hade publicerat priserna ännu (de kommer någon gång 13:00–14:00 CET) satte systemet datumet och försökte inte igen förrän nästa dag — vilket innebar att tomorrow-data kunde saknas hela kvällen.

Nu håller Fetcher reda på `tomorrowPricesFetched` separat. Så länge den är `false` och klockan är efter 13:00 görs ett nytt försök var femte minut tills priserna väl kommit. Inget timeout — systemet ger inte upp.

---

### DST-bugg vid beräkning av morgondagens datum

Både `Fetcher.c` och `APIEndpoints.c` beräknade morgondagens datum med `now + 86400`. Det fungerar alla dagar utom en: natten då vi ställer klockan framåt. Den natten är bara 23 timmar lång, och `+86400` landar på samma kalenderdatum.

Ersatt med `tm_mday += 1` följt av `mktime()` som normaliserar dag/månad/år och hanterar DST korrekt.

---

### Timezone-inkonsekvens mellan bakgrundsflöde och on-demand-path

Bakgrundsflödet (periodisk hämtning) använde korrekt Stockholm-tid för att bestämma rätt datum vid API-anropen mot Elprisetjustnu. On-demand-pathen (när en request triggar en hämtning) använde serverns lokala tid — vilket är UTC på en Raspberry Pi. Runt midnatt kunde det ge fel datum och ett 404 från Elprisetjustnu.

Båda flödena använder nu `get_stockholm_date()` konsekvent.

---

### Config-filen nådde aldrig barnprocesserna

Watchdog läste `config/gridguard.conf` vid uppstart. Däremot spawnas Fetcher, Parser och Server som separata processer via `fork()` + `execv()` — `execv()` ersätter processens hela minne, inklusive den inlästa konfigurationen. Barnprocesserna startade med oinitierad `RuntimeConfig` och föll tillbaka på hårdkodade defaults. Config-filens värden för port, cache-TTL, timeout och databasväg hade noll effekt på systemet.

Lösning: Watchdog skriver config-sökvägen till `GRIDGUARD_CONFIG_PATH` innan den spawnar. Miljövariabler överlever `fork()`, och varje barnprocess anropar `RuntimeConfig_Load(getenv("GRIDGUARD_CONFIG_PATH"))` direkt när den startar.

---

### config/gridguard.conf saknades i repot

Filen (och hela `config/`-mappen) togs bort i commit `82db02b` under en filstrukturomorganisering. `RuntimeConfig` lades till senare i `73e1f09` med antagandet att filen redan fanns. Systemet loggade en varning och körde vidare på defaults, men det var oklart för en ny installation att filen överhuvudtaget behövde skapas.

`config/gridguard.conf.example` är nu committad och dokumenterar alla tillgängliga nycklar. `config/gridguard.conf` är git-ignorerad (innehåller lokala inställningar).

---

### ASAN/UBSAN länkfel med GCC 15

`make test-gtest` kraschade med `cannot find -lasan` och `cannot find -lubsan`. GCC 15 levererar `libasan.so.9` men länkaren letade efter den äldre varianten.

Sanitizers är nu ett opt-in CMake-alternativ som är avstängt som default:

```bash
cmake -DENABLE_SANITIZERS=ON ..
```

`make test-gtest` fungerar igen utan ändringar i installationen.

---

## Dokumentation

### docs/CONFIG_DESIGN.md

Dokumentet beskrev ett system som inte existerade — bland annat en `[jwt]`-sektion i config-filen, nycklar som `server.host`, `server.log_level` och `database.platform_db_path`, och ett påstående om att barnprocesser ärver config via minnet. Allt var fel.

Dokumentet är omskrivet från grunden på svenska med ett Mermaid-diagram som visar hur config-sökvägen faktiskt flödar från Watchdog till barnprocesserna.

### README.md

Konfigurationsavsnittet visade en `[jwt]`-sektion som aldrig läses av koden (JWT-hemligheten hanteras enbart via miljövariabel). Avsnittet är korrigerat och innehåller nu en korrekt nyckelreferenstabell med rätt defaults.

---

## Ändrade filer

- `src/fetcher/Fetcher.c` — priceTTL, tomorrow-retry, DST-fix, Stockholm TZ
- `src/fetcher/main.c` — RuntimeConfig_Load vid uppstart
- `src/parser/main.c` — RuntimeConfig_Load vid uppstart
- `src/server/main.c` — RuntimeConfig_Load vid uppstart
- `src/server/GridGuard.c` — priceTTL default 3600 → 43200
- `src/watchdog/main.c` — setenv(GRIDGUARD_CONFIG_PATH)
- `src/api/APIEndpoints.c` — get_stockholm_date(), DST-fix för tomorrow-URL
- `CMakeLists.txt` — ENABLE_SANITIZERS option, default OFF
- `config/gridguard.conf.example` — ny fil
- `config/gridguard.conf` — återskapad (git-ignorerad)
- `docs/CONFIG_DESIGN.md` — omskriven
- `README.md` — konfigurationsavsnitt korrigerat
