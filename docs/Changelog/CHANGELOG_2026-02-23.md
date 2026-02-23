# Ändringslogg - 2026-02-23

## HTTP-lager + JWT-autentisering + Enhetstester

Två kompletta fas-implementationer i samma session: HTTP-protokollager ovanpå TCP-servern (Fas 1) och JWT-autentisering med HMAC-SHA256 via OpenSSL (Fas 2). ThreadPool-arkitekturen refaktorerades till en work-queue-modell utan Scheduler. Tre nya enhetstester täcker det implementerade.

---

## Fas 1 — HTTP-lager

Servern kommunicerade tidigare via råtext (`forecast stockholm SE3`). För att bli ett professionellt REST-API och kunna hantera auth-headers behövdes HTTP/1.1-stöd direkt i TCP-lagret — utan extern lib, bara POSIX `read()`/`send()`.

`HTTPRequest` parsar inkommande förfrågningar och fyller method, path, authorization-header och body. `HTTPRequest_GetBearerToken` returnerar pekare direkt in i authorization-fältet, eller NULL om headern saknas eller har fel schema.

`HTTPResponse` bygger och skickar välformade HTTP/1.1-svar med korrekt `Content-Type: application/json` och `Content-Length`. Felresponser (`SendError`) bygger automatiskt ett JSON-fellobjekt av meddelandet.

### ThreadPool — work-queue-modell

Befintligt ThreadPool använde `select()`-multiplexering per worker — bra för stateful long-lived connections, men HTTP är stateless request-response. Lösningen är den klassiska Apache httpd-modellen: en delad work-queue och 20 workers som blockerar på `Queue_Pop()`. Scheduler togs bort — med en delad kö är kön i sig lastbalanseraren. Koden är bevarad i repot men exkluderad från kompilering.

`WorkCompletion` (döpt om från ResponseChannel) implementerar Linux-kernelns `struct completion`-mönster. HTTP-workern skapar en `WorkCompletion` på sin stack, lägger jobbet i kön och lägger sig att sova. ComputeWorker skriver resultatet och signalerar — HTTP-workern vaknar, skickar svaret och returnerar. `WorkCompletion` försvinner automatiskt när stacken rensas.

### Hur WorkCompletion fungerar

Föreställ dig att du lämnar in din bil på en verkstad. Du sätter dig i väntrummet och väntar tills mekanikern kommer ut och säger "klar!". Det är exakt vad som händer här.

HTTP-workern tar emot en request, lägger jobbet i kön och lägger sig att sova. Pipeline kör i bakgrunden — FetchWorker hämtar väder och spotpris, ParseWorker tolkar svaren, ComputeWorker räknar ut BUY/SELL/IDLE-planen. ComputeWorker skriver resultatet till WorkCompletion och signalerar. HTTP-workern vaknar, skickar svaret och är klar.

Varför inte skicka svaret direkt från ComputeWorker? Eftersom `send()` kräver att man vet vilken socket som tillhör vilket request — med 20 workers som hanterar requests parallellt blir det en röra. Med WorkCompletion äger varje HTTP-worker sin socket hela tiden, och ComputeWorker behöver inte veta något om sockets.

---

## Fas 2 — JWT-autentisering

Servern behöver identifiera vilken kund som ringer och verifiera att de är autentiserade. JWT (JSON Web Tokens) med HMAC-SHA256 är industristandard för stateless autentisering — servern delar en hemlig nyckel med plattformen och validerar tokens utan nätverksanrop eller databas.

`JWTValidator` i `src/infrastructure/auth/` validerar HS256-signerade JWT:er med OpenSSL. Valideringen kontrollerar att algoritmen är HS256, att signaturen stämmer (constant-time via `CRYPTO_memcmp`), att token inte har gått ut (`exp`), och extraherar `sub` som user-id. Den hemliga nyckeln läses alltid från miljövariabeln `GRIDGUARD_JWT_SECRET` — aldrig hårdkodad.

I `ClientHandler` är `/health` publik och alla andra endpoints kräver giltig JWT. En ogiltig eller saknad token ger alltid 401. `JWTClaims` med user-id skickas vidare till handlerfunktionerna och är redo att användas i Fas 3 för att slå upp användarkonfiguration i SQLite.

### Hur JWT-validering fungerar 

En JWT-token är tre delar separerade med punkt: header, payload och signatur. Alla tre är bara base64-kodad text som vem som helst kan läsa — det är inte det som gör en JWT säker.

Det som gör den säker är signaturen. Plattformen skapar signaturen genom att köra `header.payload` genom HMAC-SHA256 med den hemliga nyckeln. Utan nyckeln kan man inte producera rätt signatur — och man kan inte heller ändra i payload utan att signaturen slutar stämma.

Servern kontrollerar token genom att räkna om signaturen själv med samma hemliga nyckel och jämföra resultatet med signaturen i token. Matchar de är token äkta. Stämmer de inte, eller har token gått ut, returneras 401.

Plattformen och C-servern delar aldrig tokens med varandra — de delar bara den hemliga nyckeln. Kunden bär token med sig. Det är det som menas med stateless autentisering.

---

## Enhetstester

39 tester, alla godkända med `-Wall -Wextra -Werror`.

`test_jwt_validator.c` (12 tester) testar JWTValidator i isolering: giltig token, utgången token, fel hemlig nyckel, saknad miljövariabel, fel algoritm (RS256), och diverse ogiltiga input. Makefile-targeten sätter `GRIDGUARD_JWT_SECRET` automatiskt.

`test_http_request.c` (15 tester) använder `socketpair(AF_UNIX, SOCK_STREAM)` för att simulera en riktig TCP-anslutning utan nätverk. Testar parsning av GET och POST, Authorization-header, GetBearerToken med Bearer-schema, Basic-schema och utan header.

`test_http_response.c` (12 tester) skickar svar till ett socketpar och läser tillbaka resultatet med `strstr`. Testar 200, 401, 404, 500 och att Content-Length finns i alla svar.

Kör alla tester med `make test`.

---

## Nästa steg — Fas 3: SQLite user configs

### Systemgränser

Det rör sig om tre helt separata system som aldrig kommunicerar direkt med varandra — bara via JWT-token som användaren bär med sig.

Plattformen (Next.js) hanterar kundregistrering, betalning och JWT-utfärdande. Den vet inte om C-servern existerar och kommunicerar aldrig med den. Användaren tar emot JWT från plattformen och sparar den lokalt. C-servern validerar JWT, slår upp user_config i SQLite och returnerar BUY/SELL/IDLE-plan.

Det enda plattformen och C-servern delar är `GRIDGUARD_JWT_SECRET` — en miljövariabel som sätts en gång vid driftsättning. Det är hela integrationen.

### user_config

Konfigurationen sätts av användaren via webbgränssnittet (se nedan). Plattformen gör det aldrig. C-servern lagrar bara en tabell med user_id, latitude, longitude, region, solpanelyta och verkningsgrad. Lösenord, e-post och betalningsstatus finns aldrig i C-serverns databas — det tillhör plattformen.

Fas 3 behöver `Database.h/c` för att öppna och stänga SQLite, `UserConfigDB.h/c` för att läsa och skriva user_config, samt endpoints `GET /user/config` och `PUT /user/config`.

### Konfigurationsgränssnitt — webbläsare

`gridguard config` öppnar inte ett terminalformulär — kommandot öppnar webbläsaren till `http://localhost:8080/config`, precis som en routers adminpanel. CLI:t läser JWT från `~/.gridguard/token` och bifogar den som ett hash-fragment i URL:en (`#token=...`). Hash-fragmentet skickas aldrig till servern — det stannar i webbläsaren och hanteras av JavaScript. Vanilla JS, inga ramverk.

---

## Arkitekturdiskussion — Cache-strategi för Fas 4

Befintlig `JsonCache` är in-memory och process-lokal. Den försvinner vid serverkrasch eller watchdog-restart och tvingar fram nya API-anrop mot SMHI/Open-Meteo/Elpriset vid varje omstart.

**Filbaserad cache** skriver JSON direkt till disk under `cache/` med en separat metadatafil för tidsstämpel. Skrivning sker atomiskt via `write tmp → rename()`. Fördelar: överlever restarter, inga nya beroenden, debuggbar med `cat`, täcker kursmålet för filhantering. Kan kombineras med befintlig JsonCache som L1 (in-memory) och filer som L2 (persistent).

**Shared memory + SQLite** (ursprunglig plan) ger snabbare access och delning mellan processer, men kräver process-shared mutex, explicit cleanup vid krasch, och är väsentligt mer komplex att implementera korrekt. För GridGuards TTL:er på 15 min och 1 dag tillför hastighetsvinsten inget praktiskt värde — ett request tar ändå sekunder i pipeline.

| | Filbaserad | Shared Mem + SQLite | Nuvarande |
|---|---|---|---|
| Överlever omstart | Ja | Ja | Nej |
| Implementationskomplexitet | Låg | Hög | Låg |
| Debuggbarhet | Enkel | Svår | Svår |
| Extra beroenden | Inga | libsqlite3 | Inga |
| Kursmål | Filhantering | POSIX IPC, mmap | Mutex/trådar |

Rekommendation: filbaserad cache som L2 under befintlig JsonCache. Löser kärnproblemet med minimal ny kod och noll nya beroenden. `shm_open`/`mmap` kan läggas till som frivillig L0-nivå om kursmålet kräver det.

---

## Arkitekturbeslut — Enhetens onboarding via Wi-Fi AP

Slutprodukten är en fysisk enhet som installeras hemma hos kunden. Konfigurationsflödet måste fungera för en användare som aldrig öppnat en terminal.

Enheten startar ett eget Wi-Fi-nät med SSID **"gridguard"** när den saknar konfiguration. Kunden ansluter till det nätet och webbläsaren öppnas automatiskt via captive portal-detection — precis som när man ansluter till ett hotell-Wi-Fi eller en ny router i setup-läge. Kunden fyller i JWT, hemmanätets SSID och lösenord, koordinater och solpaneldata. Enheten sparar allt, stänger ner AP:n och kopplar upp mot hemmanätverket.

Samma `config.html` fungerar för båda ingångarna: onboarding via captive portal (`http://192.168.4.1/config`) och omkonfiguration via CLI (`http://localhost:8080/config#token=...`). JavaScript avgör vilket flöde som visas baserat på om ett JWT-token finns i URL-hash eller inte.

Den tekniska implementationen sker i systemd och Linux-verktyg utanför C-koden: `hostapd` för Wi-Fi AP, `dnsmasq` för DHCP och DNS-redirect, `wpa_supplicant` för anslutning till hemmanätverket. C-servern behöver bara servera `config.html` och ta emot konfigurationen via `PUT /user/config`.

---

## Vad teamet kan börja med nu

Nedan är de delar som återstår för att nå en färdig produkt, mappade mot kursmålen i projektinlämningen (kursmål 7–12). SQLite och extern libs-hantering är redan tilldelat.

**Filbaserad cache (Fas 4)** — kursmål 7, 8, 12

Ersätt nuvarande in-memory `JsonCache` med en lösning som överlever omstarter. JSON-data skrivs till `cache/`-katalogen med atomisk `rename()`. Befintlig mutex i JsonCache hanterar trådsäkerheten. Täcker kursmål 8 via POSIX filhantering (`open`, `read`, `write`, `stat`) och kursmål 7 via mutex-skyddad läsning/skrivning från flera trådar. Kan startas direkt utan att vänta på SQLite.

**C++ CLI-klient (Fas 5)** — kursmål 3, 4, 5, 9

Den enda C++-delen i projektet och det som täcker kursmål 9 (RAII + STL). `TokenManager` är en RAII-klass som öppnar och stänger tokenfilen säkert med destruktor. `ForecastClient` skickar HTTP-requests med `std::string` för headers och body. `ResponseFormatter` skriver ut BUY/SELL/IDLE-planen med `std::vector` och `std::map`. Utan CLI:n saknas kursmål 9 helt i projektet.

**Shared memory som L0-cache (Fas 4, komplettering)** — kursmål 8

`shm_open`, `mmap` och POSIX-semaforer är de enda IPC-mekanismerna i kursen som inte täcks av det befintliga projektet. Kan läggas till som ett snabbt minneslager ovanpå filcachen utan att riva upp annan kod. Frivilligt men stärker kursmål 8 avsevärt och är det enda stället i projektet dessa systemanrop passar naturligt in.

**Profilering och optimering (Fas 6)** — kursmål 10, 11

Kör `gprof` på servern under last och `valgrind --tool=massif` för minnesprofilering. Identifiera vilka delar av pipeline som tar mest tid — troligen HTTP-parsning och JSON-tolkning. Dokumentera mätresultaten och gör minst en mätbar förändring baserat på dem. Utan detta saknas kursmål 10 och 11 helt.

**Doxygen-kommentarer** — kursmål 12

Sätt på `/** */`-kommentarer på de viktigaste publika funktionerna: `JWT_Validate`, `HTTPRequest_Parse`, `WorkCompletion_Wait`, `Queue_Push`/`Queue_Pop`. Generera HTML-dokumentation med `doxygen`. Kursmål 12 kräver strukturerad dokumentation av design och minnesmodeller — changelogs räcker inte ensamma.

**Daemon och watchdog** — kursmål 1, 2, 8

Befintlig daemon och watchdog-kod använder `fork()` och processer. Om kommunikationen mellan watchdog och server sker via en anonym pipe (`pipe()` + `fork()`) täcks kursmål 8 (pipes) och kursmål 1 (processer, `fork`, `waitpid`). Kolla om nuvarande implementation redan gör detta — annars är det ett enkelt tillägg.

---

## Fas 3 — Bort med libcurl och OpenSSL, in med HTTPClient och mbedTLS

### Bakgrund

Systemet ska i slutändan köras på en inbyggd enhet — troligen ett ESP32 eller liknande mikrokontroller. Varken libcurl eller OpenSSL finns tillgängliga på embedded-plattformar av den storleken. Det är bättre att lösa det nu än att behöva skriva om allt närmre slutet av projektet.

### HTTPClient — egen HTTPS-klient utan libcurl

`Fetcher.c` använde tidigare libcurl för att hämta data från SMHI, Open-Meteo och Elpriset. Curl är ett stort externt beroende som kräver dynamisk länkning och inte finns på embedded.

Den egna `HTTPClient` i `src/network/client/` gör exakt det projektet behöver: en blockerande HTTPS GET med konfigurerbar timeout, retry-logik och en enkel response-struct. Under huven är det POSIX-sockets med `getaddrinfo` + `connect`, med send/receive-timeouts satta via `setsockopt`. Inget mer.

Interfacet följer projektets Initiate/Shutdown-mönster. `Fetcher` äger ett `HTTPClient`-objekt och initierar det i `Fetcher_Initiate`. Allt som anropade `Fetcher_Fetch` behöver inte ändras alls.

### mbedTLS — bort med OpenSSL

OpenSSL är installerat på Linux men saknas på ESP32 och de flesta embedded-miljöer. mbedTLS är skrivet specifikt för inbyggda system — liten kodbas, inga externa beroenden, officiellt stöd i ESP-IDF.

Två ställen i projektet använde OpenSSL:

`JWTValidator.c` använde `EVP_DecodeBlock` för base64url-avkodning, `HMAC(EVP_sha256, ...)` för signaturberäkning och `CRYPTO_memcmp` för konstant-tids-jämförelse. Dessa är ersatta med `mbedtls_base64_decode`, `mbedtls_md_hmac` och `mbedtls_ct_memcmp`. Logiken är identisk — bara biblioteksanropen byttes ut.

`HTTPClient.c` använde `SSL_CTX`/`SSL` för TLS-handskakning, läsning och skrivning. Det är ersatt med `mbedtls_ssl_config`, `mbedtls_ssl_context`, `mbedtls_entropy_context` och `mbedtls_ctr_drbg_context`. Entropy och DRBG initieras en gång i `HTTPClient_Initiate` och återanvänds vid varje request. TLS-kontexten skapas och förstörs per anrop. TCP-lagret med `getaddrinfo`/`connect` och `setsockopt`-timeouts är oförändrat.

Makefile länkar nu med `-lmbedtls -lmbedx509 -lmbedcrypto` istället för `-lssl -lcrypto`.

### Vad som krävs på Linux för att bygga

```
sudo dnf install mbedtls-devel
```

På Ubuntu/Debian:

```
sudo apt install libmbedtls-dev
```

På ESP32 ingår mbedTLS i ESP-IDF — inga extra steg behövs.
