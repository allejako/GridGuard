# Ändringslogg - 2026-02-18

## Multi-Source Weather System + Models Refactoring

Idag implementerade vi multi-source weather (SMHI + OpenMeteo) och refaktorerade models-strukturen för tydligare separation mellan API responses, business logic och konfiguration.

---

## Multi-Source Weather

**Varför:** SMHI har bäst svensk väderdata men saknar solar irradiance i forecast-API:et. OpenMeteo har solar radiation som är kritisk för solpanelsberäkningar. SMHI STRÅNG har solar men bara historisk data, inte prognoser.

**Lösning:** Hämta från båda och kombinera:
- SMHI: temperature, humidity, windSpeed, cloudCover
- OpenMeteo: solarIrradiance (shortwave_radiation)

**Dataflow:**
```
FetchWorker: SMHI + OpenMeteo + Elpriset.se (parallellt)
     ↓
ParseWorker: SMHI + OpenMeteo → Weather
             Weather + SpotPrice → Forecast
     ↓
ComputeWorker: Forecast → Energy optimization
```

**Timestamp matching:** 60 sekunder tolerans (bara för clock skew). Viktigt för framtida 15-minuters uppdateringar.

**Nya filer:**
- `apis/SMHIResponse.h` - SMHI API struktur (parameters array)
- `domain/Weather.h` - Kombinerad väderdata (SMHI + OpenMeteo)
- `domain/SpotPrice.h` - Unified pricing model
- `services/Parser.c` - Parser_ParseSMHI() för komplex SMHI JSON
- `tests/integration/test_multi_source_weather.c` - Integration test

**Uppdaterade:**
- APIEndpoints: BuildSMHIApiUrl() och BuildOpenMeteoApiUrl()
- FetchWorker: Hämtar från SMHI, OpenMeteo, Elpriset
- ParseWorker: Tre-stegs kombination (SMHI+OpenMeteo→Weather, Elpriset→SpotPrice, Weather+SpotPrice→Forecast)

---

## Models Directory Refactoring

**Problem:** Alla models låg platt i en mapp. Svårt att se skillnad mellan API responses, business logic och config.

**Ny struktur:**
```
models/
├── apis/        # External API responses
│   ├── SMHIResponse.h
│   ├── OpenMeteoResponse.h
│   └── ElprisetResponse.h
│
├── domain/      # Business models (API-agnostic)
│   ├── Weather.h
│   ├── Forecast.h
│   ├── Energy.h/c
│   └── SpotPrice.h
│
└── config/      # System configuration
    ├── Solar.h
    ├── Battery.h
    └── Consumption.h
```

**Fördelar:**
- Tydlig separation: externa API:er vs intern logic
- Lätt att lägga till fler eller byta ut API-källor (t.ex. Nordpool istället för Elpriset)
- SpotPrice kan fyllas från Elpriset, Nordpool, Tibber - samma domain model
- Config extraherad från Energy.h till egna filer
- Enklare att mocka i tester

**Dataflow efter refactoring:**
```
External APIs → API models → Domain models → Optimization
SMHI         → SMHIResponse ──┐
OpenMeteo    → OpenMeteoResponse ──> Weather ──┐
                                               ├──> Forecast ──> Energy
Elpriset.se  → ElprisetResponse ──> SpotPrice ─┘
```

**SpotPrice domain model:**
Skapade unified pricing model som normaliserar alla pricing sources till samma format:
- Timestamp, pricePerKwh, currency, region, source
- ConvertToSpotPrice() tar ElprisetResponse + region → SpotPrice
- Region hämtas dynamiskt från FetchResult (inte hårdkodad)
- Lätt att lägga till Nordpool, Tibber, ENTSO-E senare

**Files renamed:**
- SMHIData.h → apis/SMHIResponse.h
- OpenMeteoData.h → apis/OpenMeteoResponse.h
- ElprisetData.h → apis/ElprisetResponse.h
- WeatherData.h → domain/Weather.h
- ForecastData.h → domain/Forecast.h
- EnergyData.h/c → domain/Energy.h/c

**Config extraction:**
SolarConfig, BatteryConfig, ConsumptionProfile extraherades från Energy.h till:
- config/Solar.h
- config/Battery.h
- config/Consumption.h

**Code cleanup:**
Tog bort AI-genererade block-kommentarer från alla model-filer för konsekvent kodstil.

---

## Tekniska detaljer

**SMHI API:**
- Endpoint: lon/lat order (inte lat/lon)
- Response: parameters array per timme med name/unit/value
- Octas (0-8) konverteras till procent (0-100)
- ws redan i m/s (ingen konvertering)

**ParseWorker flow:**
1. Parse SMHI → SMHIResponse
2. Parse OpenMeteo → OpenMeteoResponse
3. Combine SMHI + OpenMeteo → Weather
4. Parse Elpriset → ElprisetResponse
5. Convert Elpriset + region → SpotPrice
6. Combine Weather + SpotPrice → Forecast

**Makefile updates:**
- Include paths för apis/, domain/, config/
- Build directories för nya strukturen
- Source wildcards pekar på domain/ istället för models/

---

## Verifiering

- Build: `make clean && make` - kompilerar rent
- Tests: `make test-weather` - alla tester passar
- Integration test verifierar SMHI fetch/parse, OpenMeteo fetch/parse, timestamp matching, data kombination
- Systemet hanterar night-time testing (solar = 0 är korrekt)

---

## Sammanfattning

Byggde multi-source weather system (SMHI + OpenMeteo) och refaktorerade models till tydlig 3-lagers arkitektur (apis/domain/config). SpotPrice domain model gör det lätt att utöka med fler pricing sources. Systemet är nu redo för energioptimering baserat på svensk väderdata och solar irradiance.
