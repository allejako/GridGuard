# GridGuard Konfigurationssystem — Design

## Översikt

GridGuard använder ett runtime-konfigurationssystem som stödjer INI-format config-filer med en flexibel fallback-kedja. Detta möjliggör enkel deployment i olika miljöer utan omkompilering.

## Arkitektur

### Komponenter

**ConfigParser** (`src/config/ConfigParser.c/h`)
- Lättviktig INI-filparser
- Stödjer sektioner, kommentarer och whitespace-trimning
- Inga externa beroenden
- Nycklar lagras som `section.key` internt

**RuntimeConfig** (`src/config/RuntimeConfig.c/h`)
- Trådsäker konfigurationsåtkomst via pthread_rwlock
- Singleton-mönster med global `g_config`-instans
- Stödjer hot-reload via SIGHUP-signal
- Tre-nivås fallback-kedja för maximal flexibilitet

### Fallback-kedja

Konfigurationsvärden löses upp i följande ordning:

1. **Runtime config-fil** (`config/gridguard.conf`)
2. **Miljövariabler** (t.ex. `GRIDGUARD_DB_PATH`)
3. **Compile-time defaults** (`src/domain/Config.h`)

Denna design säkerställer bakåtkompatibilitet samtidigt som den ger flexibilitet för olika deploymentscenarier.

## Konfigurationsfilformat

INI-stil konfiguration med sektioner:

```ini
# Kommentarer börjar med #
[section]
key=value
another_key=another_value

[other_section]
numeric_value=123
```

### Stödda sektioner

**[server]**
- `port` - TCP-lyssningsport (default: 8080)
- `host` - Bindadress (default: localhost)
- `log_level` - Loggningsnivå (default: INFO)

**[database]**
- `db_path` - Sökväg till gridguard.db (default: auto-upplöst)
- `platform_db_path` - Sökväg till platform.db (default: platform.db)

**[jwt]**
- `jwt_secret` - JWT-signeringsnyckel (obligatorisk, ingen default)

**[network]**
- `timeout` - HTTP-request timeout i sekunder (default: 30)
- `max_connections` - Maximalt antal samtidiga anslutningar (default: 100)
- `max_retries` - HTTP-återförsök vid fel (default: 3)

**[cache]**
- `weather_ttl` - Väder-cache TTL i sekunder (default: 900)
- `price_ttl` - Pris-cache TTL i sekunder (default: 3600)
- `forecast_ttl` - Prognos-cache TTL i sekunder (default: 1800)

## Användning

### Ladda konfiguration

Konfigurationen laddas automatiskt av Watchdog vid uppstart:

```c
// I watchdog/main.c
RuntimeConfig_Load("config/gridguard.conf");
```

Överskrid via kommandorad:
```bash
bin/GridGuard-watchdog --config /path/to/custom.conf
```

### Åtkomst till konfigurationsvärden

**Strängvärden:**
```c
const char *port = RuntimeConfig_Get("server.port", NULL, SERVER_PORT);
```

**Heltalsvärden:**
```c
int timeout = RuntimeConfig_GetInt("network.timeout", NULL, 30);
```

**Med miljövariabel-fallback:**
```c
const char *dbPath = RuntimeConfig_Get("database.db_path", "GRIDGUARD_DB_PATH", DB_PATH);
```

### Hot Reload (SIGHUP)

Konfigurationen kan laddas om utan omstart:

```bash
kill -SIGHUP $(cat /tmp/gridguard.pid)
```

Servern kontrollerar SIGHUP i sin mainloop och anropar `RuntimeConfig_Reload()` automatiskt.

## Trådsäkerhet

All config-åtkomst skyddas av `pthread_rwlock`:
- Flera trådar kan läsa config samtidigt
- Skriv-operationer (reload) blockerar tills alla läsare är klara
- Ingen prestandapåverkan under normal drift

## Integrationspunkter

### Watchdog (`src/watchdog/main.c`)
- Laddar config vid uppstart innan child-processer spawnas
- Skickar config via arv (barn använder samma globala config)

### Server (`src/server/GridGuard.c`, `src/server/Server.c`)
- Databassökvägar
- TCP-portbindning
- Cache TTL-värden

### Fetcher (`src/net/HTTPFetcher.c`)
- HTTP-timeout
- Retry-logik

### Framtida utökningar

Konfigurationssystemet är designat för att enkelt utökas:

1. Lägg till ny sektion/nyckel i `config/gridguard.conf`
2. Åtkomst via `RuntimeConfig_Get()` eller `RuntimeConfig_GetInt()`
3. Inga kodändringar behövs - fallback till defaults automatiskt

## Testning

Enhetstester: `tests/unit/test_config_parser_gtest.cpp`
- 13 testfall som täcker parsing, hämtning och edge cases
- Kör med: `./tests/test_config_parser_gtest`

## Prestandaöverväganden

- Config-fil parsas en gång vid uppstart
- Lagras i minnet (key-value par)
- Read-lock overhead: försumbar (<1µs)
- Reload-tid: <10ms för typiska config-filer

## Säkerhetsnoteringar

- JWT-hemlighet ska aldrig committas till versionshantering
- Använd miljövariabler eller säker config-hantering för produktion
- Config-filen bör ha begränsade rättigheter (0600 rekommenderas)

## Bakåtkompatibilitet

Fallback-kedjan säkerställer att befintliga deployments fortsätter fungera:
- System som använder miljövariabler: Ingen ändring behövs
- System som använder compile-time defaults: Ingen ändring behövs
- Nya deployments kan valfritt använda config-filer

## Exempelkonfiguration

Se `config/gridguard.conf` för ett komplett exempel med alla tillgängliga alternativ.
