#!/usr/bin/env python3
"""Seed gridguard.db (client database) with demo user config and schedules."""

import sqlite3
import sys
import time
import json
import urllib.request
from datetime import datetime, timedelta

db_path = sys.argv[1] if len(sys.argv) > 1 else "gridguard.db"
jwt_token = sys.argv[2] if len(sys.argv) > 2 else None

# Create user_configs table
CREATE_USER_CONFIGS = """
CREATE TABLE IF NOT EXISTS user_configs (
    user_id          TEXT PRIMARY KEY,
    location         TEXT,
    latitude         REAL NOT NULL,
    longitude        REAL NOT NULL,
    region           TEXT NOT NULL,
    solar_area_m2    REAL NOT NULL,
    solar_efficiency REAL NOT NULL,
    consumption_kwh  REAL NOT NULL DEFAULT 0.5,
    grid_fee_low     REAL NOT NULL DEFAULT 0.25,
    grid_fee_normal  REAL NOT NULL DEFAULT 0.35,
    grid_fee_high    REAL NOT NULL DEFAULT 0.45,
    updated_at       INTEGER NOT NULL
)
"""

# Create schedules table
CREATE_SCHEDULES = """
CREATE TABLE IF NOT EXISTS schedules (
    schedule_id        TEXT PRIMARY KEY,
    user_id            TEXT NOT NULL,
    load_id            TEXT NOT NULL,
    scheduled_start    INTEGER NOT NULL,
    duration_minutes   INTEGER NOT NULL,
    power_kw           REAL NOT NULL,
    estimated_cost_sek REAL NOT NULL,
    savings_sek        REAL NOT NULL DEFAULT 0.0,
    status             TEXT NOT NULL DEFAULT 'pending',
    created_at         INTEGER NOT NULL
)
"""

# Insert demo user config (SAAB ARENA, Linköping)
# Solar: 1500 m² monocrystalline panels on arena roof (10% of ~15 000 m² roof area)
# Consumption: ~45 kWh/h base load (standby — HVAC, lighting, facilities, no event)
INSERT_CONFIG = """
INSERT OR REPLACE INTO user_configs
    (user_id, location, latitude, longitude, region,
     solar_area_m2, solar_efficiency, consumption_kwh,
     grid_fee_low, grid_fee_normal, grid_fee_high, updated_at)
VALUES
    ('SAAB_ARENA', 'Linköping', 58.4109, 15.6216, 'SE3',
     1500.0, 0.20, 45.0,
     0.25, 0.35, 0.45, ?)
"""

# Insert demo schedules (EV charger, dishwasher, washing machine)
INSERT_SCHEDULES = """
INSERT OR REPLACE INTO schedules
    (schedule_id, user_id, load_id, scheduled_start, duration_minutes,
     power_kw, estimated_cost_sek, savings_sek, status, created_at)
VALUES
    ('demo_ev_001', 'SAAB_ARENA', 'ev_fleet_charger',    ?, 240, 50.0, 320.00, 580.00, 'pending', ?),
    ('demo_dw_001', 'SAAB_ARENA', 'hvac_precool',       ?, 120, 30.0,  85.00,  42.00, 'pending', ?),
    ('demo_wm_001', 'SAAB_ARENA', 'zamboni_charge',     ?, 90,  15.0,  40.00,  18.00, 'completed', ?)
"""

def fetch_forecast_signals(token):
    """Fetch forecast from server to find cheapest periods."""
    if not token:
        return []

    try:
        req = urllib.request.Request(
            "http://localhost:8080/forecast",
            headers={"Authorization": f"Bearer {token}"}
        )
        with urllib.request.urlopen(req, timeout=5) as response:
            data = json.loads(response.read().decode())

        # Extract ALL signals with their timestamps (not just BUY)
        periods = []
        for day in data.get("days", []):
            for signal in day.get("signals", []):
                start_time = signal.get("start", "")
                # Convert ISO timestamp to unix
                dt = datetime.fromisoformat(start_time.replace('Z', '+00:00'))
                periods.append({
                    "timestamp": int(dt.timestamp()),
                    "price": signal.get("total_cost_sek_kwh", 0),
                    "signal": signal.get("signal", ""),
                    "time_str": start_time
                })

        # Sort by price (cheapest first)
        periods.sort(key=lambda x: x["price"])
        return periods
    except Exception as e:
        print(f"  Warning: Could not fetch forecast ({e}), using default times")
        return []

con = sqlite3.connect(db_path)
con.execute(CREATE_USER_CONFIGS)
con.execute(CREATE_SCHEDULES)

now = int(time.time())
con.execute(INSERT_CONFIG, (now,))

# Try to get real forecast periods (cheapest times)
periods = fetch_forecast_signals(jwt_token)

# Check if we have any BUY signals (good times to schedule loads)
buy_periods = [p for p in periods if p.get("signal") == "BUY"]

# Default savings values
ev_savings = 580.00
dw_savings = 42.00
wm_savings = 18.00

if buy_periods and len(buy_periods) >= 2:
    # BEST CASE: We have BUY signals - use them!
    ev_start = buy_periods[0]["timestamp"]
    dw_start = buy_periods[1]["timestamp"]
    past_buys = [p for p in buy_periods if p["timestamp"] < now]
    wm_start = past_buys[0]["timestamp"] if past_buys else now - 86400

    print(f"  Using real BUY periods (optimal scheduling):")
    print(f"    EV charger scheduled at {datetime.fromtimestamp(ev_start).strftime('%Y-%m-%d %H:%M')} (BUY)")
    print(f"    Dishwasher scheduled at {datetime.fromtimestamp(dw_start).strftime('%Y-%m-%d %H:%M')} (BUY)")
    if past_buys:
        print(f"    Washing machine completed at {datetime.fromtimestamp(wm_start).strftime('%Y-%m-%d %H:%M')} (BUY)")
    else:
        print(f"    Washing machine completed at {datetime.fromtimestamp(wm_start).strftime('%Y-%m-%d %H:%M')} (fallback)")
elif periods and len(periods) >= 2:
    # FALLBACK: No BUY signals, but we have forecast data
    # Schedule in least-bad periods, but set savings to 0
    print(f"  ⚠ No BUY signals available - scheduling in least expensive periods")
    print(f"  ⚠ Savings set to 0 kr (no optimal periods found)")
    ev_start = periods[0]["timestamp"]
    dw_start = periods[1]["timestamp"]
    past_periods = [p for p in periods if p["timestamp"] < now]
    wm_start = past_periods[0]["timestamp"] if past_periods else now - 86400

    # Set savings to 0 when no BUY signals exist
    ev_savings = 0.00
    dw_savings = 0.00
    wm_savings = 0.00

    print(f"    EV charger at {datetime.fromtimestamp(ev_start).strftime('%Y-%m-%d %H:%M')} ({periods[0]['signal']})")
    print(f"    Dishwasher at {datetime.fromtimestamp(dw_start).strftime('%Y-%m-%d %H:%M')} ({periods[1]['signal']})")
else:
    # NO FORECAST DATA: use time offsets
    ev_start = now + 3600
    dw_start = now + 7200
    wm_start = now - 3600
    print("  Using fallback schedule times (no forecast data available)")

con.execute(f"""
INSERT OR REPLACE INTO schedules
    (schedule_id, user_id, load_id, scheduled_start, duration_minutes,
     power_kw, estimated_cost_sek, savings_sek, status, created_at)
VALUES
    ('demo_ev_001', 'SAAB_ARENA', 'ev_fleet_charger', ?, 240, 50.0, 320.00, {ev_savings}, 'pending', ?),
    ('demo_dw_001', 'SAAB_ARENA', 'hvac_precool',    ?, 120, 30.0,  85.00, {dw_savings}, 'pending', ?),
    ('demo_wm_001', 'SAAB_ARENA', 'zamboni_charge',  ?, 90,  15.0,  40.00, {wm_savings}, 'completed', ?)
""", (
    ev_start, now,
    dw_start, now,
    wm_start, wm_start
))

con.commit()
con.close()

print(f"✓ Client DB seeded: {db_path}")
print("  User: SAAB_ARENA (Linköping, 1500m² solar roof, SE3, 45 kWh/h base load)")
print("  Schedules: 3 arena loads (EV fleet charger, HVAC pre-cool, Zamboni charge)")
