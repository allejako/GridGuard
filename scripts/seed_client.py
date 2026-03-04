#!/usr/bin/env python3
"""Seed gridguard.db (client database) with demo user config and schedules."""

import sqlite3
import sys
import time

db_path = sys.argv[1] if len(sys.argv) > 1 else "gridguard.db"

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

# Insert demo user config (Stockholm, 20m² solar panels, premium setup)
INSERT_CONFIG = """
INSERT OR REPLACE INTO user_configs
    (user_id, location, latitude, longitude, region,
     solar_area_m2, solar_efficiency, consumption_kwh,
     grid_fee_low, grid_fee_normal, grid_fee_high, updated_at)
VALUES
    ('test_user', 'Stockholm', 59.3293, 18.0686, 'SE3',
     20.0, 0.18, 1.5,
     0.25, 0.35, 0.45, ?)
"""

# Insert demo schedules (EV charger, dishwasher, washing machine)
INSERT_SCHEDULES = """
INSERT OR REPLACE INTO schedules
    (schedule_id, user_id, load_id, scheduled_start, duration_minutes,
     power_kw, estimated_cost_sek, savings_sek, status, created_at)
VALUES
    ('demo_ev_001', 'test_user', 'ev_charger',         ?, 240, 11.0, 32.50, 58.30, 'pending', ?),
    ('demo_dw_001', 'test_user', 'dishwasher',         ?, 120,  1.8,  2.10,  1.20, 'pending', ?),
    ('demo_wm_001', 'test_user', 'washing_machine',    ?, 90,   2.0,  1.80,  0.95, 'completed', ?)
"""

con = sqlite3.connect(db_path)
con.execute(CREATE_USER_CONFIGS)
con.execute(CREATE_SCHEDULES)

now = int(time.time())
con.execute(INSERT_CONFIG, (now,))

# Schedule times: EV tonight 23:00, dishwasher tomorrow 02:00, washing done yesterday
ev_start = now + 3600  # 1 hour from now
dw_start = now + 7200  # 2 hours from now
wm_start = now - 86400  # completed yesterday

con.execute(INSERT_SCHEDULES, (
    ev_start, now,
    dw_start, now,
    wm_start, wm_start
))

con.commit()
con.close()

print(f"✓ Client DB seeded: {db_path}")
print("  User: test_user (Stockholm, 20m² solar, SE3)")
print("  Schedules: 3 demo loads (EV charger, dishwasher, washing machine)")
