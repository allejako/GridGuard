#!/usr/bin/env python3
"""Seed gridguard.db with demo user config and schedules.

When a JWT token is provided the script uses the live API, so LoadScheduler
runs the full forecast pipeline and computes real savings values.
Falls back to direct DB insertion (savings=0) when no token is given.

Usage:
  seed_client.py <db_path> [jwt_token]
"""

import sqlite3
import sys
import time
import json
import urllib.request
import urllib.error

BASE_URL = "http://localhost:8080"

db_path   = sys.argv[1] if len(sys.argv) > 1 else "gridguard.db"
jwt_token = sys.argv[2] if len(sys.argv) > 2 else None

# ── Schema ───────────────────────────────────────────────────────────────────

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

# ── API helpers ───────────────────────────────────────────────────────────────

def _request(method, path, payload, token):
    data = json.dumps(payload).encode()
    req = urllib.request.Request(
        f"{BASE_URL}{path}",
        data=data,
        method=method,
        headers={
            "Authorization":  f"Bearer {token}",
            "Content-Type":   "application/json",
        },
    )
    try:
        with urllib.request.urlopen(req, timeout=30) as resp:
            return json.loads(resp.read().decode())
    except urllib.error.HTTPError as e:
        body = e.read().decode()[:300]
        print(f"  ✗ {method} {path} → HTTP {e.code}: {body}")
        return None
    except Exception as e:
        print(f"  ✗ {method} {path} → {e}")
        return None

def api_put(path, payload, token):
    return _request("PUT", path, payload, token)

def api_post(path, payload, token):
    return _request("POST", path, payload, token)

# ── Live-API seed (real savings from LoadScheduler) ───────────────────────────

def seed_via_api(con, token, now):
    # 1. Push user config so POST /schedule can look it up.
    ok = api_put("/user/config", {
        "location":         "Linköping",
        "latitude":         58.4109,
        "longitude":        15.6216,
        "region":           "SE3",
        "solar_area_m2":    1500.0,
        "solar_efficiency": 0.20,
        "consumption_kwh":  45.0,
        "grid_fee_low":     0.25,
        "grid_fee_normal":  0.35,
        "grid_fee_high":    0.45,
    }, token)
    if ok is None:
        return False
    print("  ✓ User config pushed via API")

    deadline = now + 86400  # look within the next 24 hours

    # 2. Pending loads — POST /schedule runs the full pipeline and picks the
    #    optimal window, returning real cost and savings values.
    pending = [
        {"load_id": "ev_fleet_charger", "duration_minutes": 240, "power_kw": 50.0},
        {"load_id": "hvac_precool",     "duration_minutes": 120, "power_kw": 30.0},
    ]

    for load in pending:
        resp = api_post("/schedule", {**load, "deadline": deadline}, token)
        if resp is None:
            print(f"  ✗ Failed to schedule {load['load_id']} — aborting API seed")
            return False
        print(f"  ✓ {load['load_id']:20s}  "
              f"start={resp.get('scheduled_start', '?')}  "
              f"cost={resp.get('estimated_cost_sek', 0):.2f} kr  "
              f"savings={resp.get('savings_sek', 0):.2f} kr")

    return True


# ── Fallback: direct DB insert ────────────────────────────────────────────────

def seed_via_db(con, now):

    con.execute("""
INSERT OR REPLACE INTO user_configs
    (user_id, location, latitude, longitude, region,
     solar_area_m2, solar_efficiency, consumption_kwh,
     grid_fee_low, grid_fee_normal, grid_fee_high, updated_at)
VALUES ('SAAB_ARENA', 'Linköping', 58.4109, 15.6216, 'SE3',
        1500.0, 0.20, 45.0, 0.25, 0.35, 0.45, ?)
""", (now,))

    con.execute("""
INSERT OR REPLACE INTO schedules
    (schedule_id, user_id, load_id, scheduled_start, duration_minutes,
     power_kw, estimated_cost_sek, savings_sek, status, created_at)
VALUES
    ('demo_ev_001', 'SAAB_ARENA', 'ev_fleet_charger', ?, 240, 50.0, 0.0, 0.0, 'pending', ?),
    ('demo_dw_001', 'SAAB_ARENA', 'hvac_precool',     ?, 120, 30.0, 0.0, 0.0, 'pending', ?)
""", (
        now + 3600, now,
        now + 7200, now,
    ))
    con.commit()


# ── Main ──────────────────────────────────────────────────────────────────────

con = sqlite3.connect(db_path, timeout=10)
con.execute(CREATE_USER_CONFIGS)
con.execute(CREATE_SCHEDULES)

# Clear existing schedules so re-seeding is idempotent.
con.execute("DELETE FROM schedules")
con.commit()

now = int(time.time())

if jwt_token:
    print("Seeding via live API (real LoadScheduler savings)...")
    if not seed_via_api(con, jwt_token, now):
        print("  ⚠  API seed failed — falling back to static demo data (savings=0).")
        seed_via_db(con, now)
else:
    print("  ⚠  No JWT token — inserting static demo data (savings=0).")
    print("     Pass a JWT token to get real savings from the LoadScheduler.")
    seed_via_db(con, now)

con.close()
print(f"\n✓ {db_path} seeded")
print("  User:      SAAB_ARENA — Linköping, SE3, 1500 m² solar, 45 kWh/h base load")
print("  Schedules: ev_fleet_charger (240 min, 50 kW)  hvac_precool (120 min, 30 kW)")
