# GridGuard — API-dokumentation

Servern lyssnar på port `8080`. Alla svar är JSON om inget annat anges.

---

## Autentisering

Skyddade endpoints kräver ett JWT-token i Authorization-headern:

```
Authorization: Bearer <token>
```

Token genereras med:
```bash
export TOKEN=$(python3 scripts/generate_jwt.py platform.db <username>)
```

**Felsvar vid saknat eller ogiltigt token:**
```
HTTP 401 Unauthorized
{"error":"Unauthorized"}
```

---

## Publika endpoints

### GET /

Välkomstsida med API-översikt (HTML).

---

### GET /health

Hälsokontroll — returnerar alltid 200 om servern är uppe.

**Svar:**
```json
{"status":"ok","service":"GridGuard"}
```

---

### GET /metrics

Processtatistik från Watchdog via POSIX shared memory. Visar PID, uptime och senaste heartbeat för varje process.

**Svar:**
```json
{
  "service": "GridGuard",
  "timestamp": 1741954800,
  "watchdog": {
    "uptime_seconds": 3600,
    "restart_count": 0,
    "max_restarts": 5,
    "restart_window_seconds": 300
  },
  "fetcher": {
    "pid": 12341,
    "uptime_seconds": 3598,
    "last_heartbeat_seconds_ago": 1
  },
  "parser": {
    "pid": 12342,
    "uptime_seconds": 3599,
    "last_heartbeat_seconds_ago": 1
  },
  "server": {
    "pid": 12343,
    "uptime_seconds": 3597,
    "last_heartbeat_seconds_ago": 2
  }
}
```

**Felfall:** Om Watchdog inte är igång (shared memory saknas):
```json
{"error":"Watchdog metrics not available"}
```

---

## Autentiserade endpoints

### GET /forecast

Hämtar en 96-timmars energiprognos med BUY/SELL/AVOID/IDLE-rekommendationer per 15-minutersintervall. Kräver att `/user/config` är satt.

Returnerar cachat svar om data är färsk. Vid cache miss körs hela pipeline (Fetch → Parse → Compute) synkront — kan ta upp till 30 sekunder vid första anropet.

**Svar:**
```json
{
  "quarters": [
    {
      "time": "2026-03-17T06:00:00Z",
      "spot_price_sek_kwh": 0.42,
      "grid_fee_sek_kwh": 0.35,
      "total_cost_sek_kwh": 0.77,
      "production_kwh": 0.12,
      "recommendation": "BUY",
      "cloud_cover": 20.5,
      "temperature": 4.2,
      "solar_irradiance": 180.0
    }
  ]
}
```

**Fält i varje kvartal:**

| Fält | Typ | Beskrivning |
|---|---|---|
| `time` | string (ISO 8601 UTC) | Kvartalets starttid |
| `spot_price_sek_kwh` | number | Elpris från elnätet (SEK/kWh) |
| `grid_fee_sek_kwh` | number | Nätavgift (SEK/kWh) |
| `total_cost_sek_kwh` | number | Totalkostnad inkl. nätavgift |
| `production_kwh` | number | Förväntad solcellsproduktion (kWh) |
| `recommendation` | string | `BUY`, `SELL`, `AVOID` eller `IDLE` |
| `cloud_cover` | number | Molntäckning (%) |
| `temperature` | number | Temperatur (°C) |
| `solar_irradiance` | number | Solinstrålning (W/m²) |

**Rekommendationslogik:**
- `BUY` — Lågt pris, fördelaktigt att köpa el från nätet
- `SELL` — Hög solproduktion och högt pris, sälj överskott
- `AVOID` — Högt pris och låg produktion, minimera förbrukning
- `IDLE` — Normalt pris, ingen specifik åtgärd rekommenderas (filtreras bort i JSON-output)

**Felfall:**

| HTTP | Felmeddelande | Orsak |
|---|---|---|
| 400 | `User config not set. Use PUT /user/config first.` | Ingen konfiguration sparad |
| 500 | `Pipeline error or timeout` | Fetch/Parse-processen svarade inte inom 30s |
| 500 | `Queue full, try again later` | Intern kö full |

---

### GET /user/config

Hämtar sparad användarkonfiguration.

**Svar:**
```json
{
  "location": "Linköping",
  "latitude": 58.4108,
  "longitude": 15.6214,
  "region": "SE3",
  "solar_area_m2": 20.0,
  "solar_efficiency": 0.18,
  "consumption_kwh": 1.5,
  "panel_tilt_deg": 30.0,
  "panel_azimuth_deg": 180.0,
  "updated_at": 1741900000
}
```

**Felfall:**

| HTTP | Felmeddelande | Orsak |
|---|---|---|
| 404 | `No config found` | Ingen konfiguration har sparats för användaren |

---

### PUT /user/config

Sparar eller uppdaterar användarkonfiguration. Alla tidigare lagrade prognoser ogiltigförklaras (ny cache-nyckel genereras).

**Request body:**
```json
{
  "latitude": 58.4108,
  "longitude": 15.6214,
  "region": "SE3",
  "location": "Linköping",
  "solar_area_m2": 20.0,
  "solar_efficiency": 0.18,
  "consumption_kwh": 1.5,
  "grid_fee_low": 0.25,
  "grid_fee_normal": 0.35,
  "grid_fee_high": 0.45,
  "panel_tilt_deg": 30.0,
  "panel_azimuth_deg": 180.0
}
```

**Obligatoriska fält:** `latitude`, `longitude`, `region`

**Fältbeskrivningar:**

| Fält | Typ | Obligatorisk | Validering | Beskrivning |
|---|---|---|---|---|
| `latitude` | number | Ja | -90 .. 90 | Koordinat (WGS84) |
| `longitude` | number | Ja | -180 .. 180 | Koordinat (WGS84) |
| `region` | string | Ja | — | Elområde: `SE1`–`SE4` |
| `location` | string | Nej | — | Fritext (för visning) |
| `solar_area_m2` | number | Nej | 0 .. 10000 | Solpanelyta (m²) |
| `solar_efficiency` | number | Nej | 0.0 .. 1.0 | Verkningsgrad (ex. 0.18 = 18%) |
| `consumption_kwh` | number | Nej | 0 .. 1000 | Genomsnittlig last (kWh/h) |
| `grid_fee_low` | number | Nej | 0 .. 10 | Nätavgift lågtrafik (SEK/kWh) |
| `grid_fee_normal` | number | Nej | 0 .. 10 | Nätavgift normaltrafik (SEK/kWh) |
| `grid_fee_high` | number | Nej | 0 .. 10 | Nätavgift högtrafik (SEK/kWh) |
| `panel_tilt_deg` | number | Nej | 0 .. 90 | Panellutning i grader (0°=horisontell, 90°=vertikal); default 30.0 |
| `panel_azimuth_deg` | number | Nej | 0 .. 359 | Panelazimut (kompass): 0°=N, 90°=Ö, 180°=S, 270°=V; default 180.0 |

**Svar (200 OK):**
```json
{"status":"ok"}
```

**Felfall:**

| HTTP | Felmeddelande | Orsak |
|---|---|---|
| 400 | `Missing required fields: latitude, longitude, region` | Obligatoriska fält saknas |
| 400 | `Invalid coordinates: latitude must be -90..90, longitude -180..180` | Ogiltiga koordinater |
| 400 | `Invalid solar_efficiency: must be 0.0..1.0` | Verkningsgrad utanför intervall |
| 400 | `Invalid grid fees: must be 0..10 kr/kWh` | Nätavgift utanför intervall |
| 400 | `Invalid panel_tilt_deg: must be 0..90` | Lutning utanför intervall |
| 400 | `Invalid panel_azimuth_deg: must be 0..359` | Azimut utanför intervall |
| 400 | `Invalid JSON` | Malformad request body |

---

### POST /schedule

Schemalägger en shiftable last (t.ex. elbilsladdning, tvättmaskin) till det billigaste tillgängliga tidsfönstret inom prognoshorisonten.

Kräver att `/user/config` är satt. Kör intern forecast om ingen cache finns.

**Request body:**
```json
{
  "load_id": "ev_charger",
  "duration_minutes": 480,
  "power_kw": 3.5,
  "deadline": 1741986000
}
```

| Fält | Typ | Obligatorisk | Validering | Beskrivning |
|---|---|---|---|---|
| `load_id` | string | Ja | — | Identifierare för lasten |
| `duration_minutes` | number | Ja | 1 .. 1440 | Hur länge lasten behöver köra |
| `power_kw` | number | Ja | > 0, ≤ 1000 | Effektuttag (kW) |
| `deadline` | number | Nej | Unix timestamp | Senast lasten måste vara klar |

**Svar (200 OK):**
```json
{
  "schedule_id": "user123_1741954800",
  "load_id": "ev_charger",
  "scheduled_start": "2026-03-17T22:00:00Z",
  "duration_minutes": 480,
  "power_kw": 3.5,
  "estimated_cost_sek": 14.70,
  "savings_sek": 8.40,
  "status": "pending"
}
```

**Felfall:**

| HTTP | Felmeddelande | Orsak |
|---|---|---|
| 400 | `Missing required fields: load_id, duration_minutes, power_kw` | Obligatoriska fält saknas |
| 400 | `Invalid duration_minutes: must be 1..1440` | Ogiltig varaktighet |
| 400 | `Invalid power_kw: must be > 0 and <= 1000` | Ogiltig effekt |
| 400 | `No valid window found within forecast and deadline` | Ingen plats inom prognos/deadline |
| 400 | `User config not set. Use PUT /user/config first.` | Konfiguration saknas |
| 500 | `Pipeline error or timeout` | Forecast-pipeline misslyckades |

---

### GET /schedule

Listar alla schemalagda laster för den inloggade användaren.

**Svar:**
```json
{
  "schedules": [
    {
      "schedule_id": "user123_1741954800",
      "load_id": "ev_charger",
      "scheduled_start": "2026-03-17T22:00:00Z",
      "duration_minutes": 480,
      "power_kw": 3.5,
      "estimated_cost_sek": 14.70,
      "savings_sek": 8.40,
      "status": "pending"
    }
  ]
}
```

Tom lista returneras som `{"schedules":[]}`.

---

### DELETE /schedule/:id

Avbokar ett schema. Användare kan bara ta bort sina egna scheman.

**Exempel:**
```bash
curl -X DELETE http://localhost:8080/schedule/user123_1741954800 \
  -H "Authorization: Bearer $TOKEN"
```

**Svar (200 OK):**
```json
{"status":"cancelled"}
```

**Felfall:**

| HTTP | Felmeddelande | Orsak |
|---|---|---|
| 404 | `Schedule not found or not owned by user` | ID finns ej eller tillhör annan användare |

---

## Felkoder — sammanfattning

| HTTP-kod | Innebörd |
|---|---|
| 200 | OK |
| 400 | Felaktig request (validering misslyckades, saknade fält) |
| 401 | Saknat eller ogiltigt JWT-token |
| 404 | Resursen finns inte |
| 500 | Internt serverfel (pipeline, databas, minne) |

Alla felmeddelanden har formatet:
```json
{"error":"<meddelande>"}
```

---

## Exempel — komplett flöde

```bash
# 1. Generera token
export TOKEN=$(python3 scripts/generate_jwt.py platform.db test_user)

# 2. Sätt konfiguration
curl -X PUT http://localhost:8080/user/config \
  -H "Authorization: Bearer $TOKEN" \
  -H "Content-Type: application/json" \
  -d '{
    "latitude": 58.4108,
    "longitude": 15.6214,
    "region": "SE3",
    "location": "Linköping",
    "solar_area_m2": 20.0,
    "solar_efficiency": 0.18,
    "consumption_kwh": 1.5,
    "grid_fee_low": 0.25,
    "grid_fee_normal": 0.35,
    "grid_fee_high": 0.45
  }'

# 3. Hämta prognos
curl -H "Authorization: Bearer $TOKEN" http://localhost:8080/forecast

# 4. Schemalägg elbilsladdning
curl -X POST http://localhost:8080/schedule \
  -H "Authorization: Bearer $TOKEN" \
  -H "Content-Type: application/json" \
  -d '{"load_id":"ev_charger","duration_minutes":480,"power_kw":3.5}'

# 5. Lista scheman
curl -H "Authorization: Bearer $TOKEN" http://localhost:8080/schedule

# 6. Avboka
curl -X DELETE http://localhost:8080/schedule/<schedule_id> \
  -H "Authorization: Bearer $TOKEN"
```
