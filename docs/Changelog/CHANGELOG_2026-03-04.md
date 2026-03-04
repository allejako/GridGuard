# Ändringslogg - 2026-03-04

## Säkerhetsförbättringar för API-anrop

Implementerat TLS-certifikatvalidering och input validation för att skydda mot MITM-attacker och korrupt/skadlig data från externa API:er.

---

## TLS-certifikatvalidering

HTTPClient validerar nu TLS-certifikat för alla HTTPS-anrop till externa API:er (Open-Meteo, Elpriset). Detta förhindrar man-in-the-middle-attacker där en angripare kan injicera falsk väderdata eller elpriser.

**Implementering:**

- HTTPClient laddar CA-certifikat från systemet vid initiering
- Stödjer flera Linux-distributioner:
  - Debian/Ubuntu: `/etc/ssl/certs/ca-certificates.crt`
  - RHEL/Fedora: `/etc/pki/tls/certs/ca-bundle.crt`
  - OpenSUSE: `/etc/ssl/ca-bundle.pem`
  - Alpine: `/etc/ssl/cert.pem`
- Fallback till `/etc/ssl/certs/` om enskild fil inte hittas
- mbedTLS authmode satt till `MBEDTLS_SSL_VERIFY_REQUIRED`
- CA-kedja konfigurerad med `mbedtls_ssl_conf_ca_chain()`

**Filer:**
- `src/network/client/HTTPClient.h` — tillagt `mbedtls_x509_crt cacert` till `HTTPClient`-struct
- `src/network/client/HTTPClient.c:162-233` — CA-certifikatsladdning i `HTTPClient_Initiate()`

---

## Input validation för API responses

Validerar och klampar alla värden från Open-Meteo och Elpriset för att skydda mot:

- **Non-finite values** (NaN, Inf) som kraschar beräkningar
- **Buffer overflow** i tidssträngar
- **Extremvärden** som orsakar integer overflow eller division by zero
- **Type confusion** från felaktiga JSON-typer

**Valideringsgränser:**

- Temperatur: -60°C till 60°C (realistiska jordtemperaturer)
- Luftfuktighet: 0-100%
- Molntäcke: 0-100%
- Vindhastighet: 0-150 m/s
- Solstrålning: 0-1500 W/m² (max solar irradiance ~1000-1200 W/m²)
- Spotpris SEK: 0-20 SEK/kWh (svensk el överstiger sällan detta)
- Spotpris EUR: 0-2 EUR/kWh
- Växelkurs: 5-20 SEK/EUR (typisk range)
- Tidssträngar: Endast alphanumeriska tecken, `-`, `:`, `T`, `+`

**Filer:**
- `src/application/api/APIParser.c:9-137` — valideringsfunktioner
- `src/application/api/APIParser.c:200-230` — validering applicerad på Open-Meteo
- `src/application/api/APIParser.c:281-315` — validering applicerad på Elpriset

**Beteende:**

- Vid non-finite värden: Loggar varning och använder säkert standardvärde
- Vid out-of-bounds: Loggar varning och klampar till gräns
- Vid ogiltiga tecken i tidssträngar: Ersätter med `_` och loggar position

---

## Status

Säkerhetsförbättringarna är implementerade och testade. Systemet fungerar normalt med TLS-certifikatvalidering och input validation aktiverad.
