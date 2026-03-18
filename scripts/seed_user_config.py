#!/usr/bin/env python3
"""Seed gridguard.db with just user config (before server starts)."""

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

# Create schedules table (will be populated later)
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

con = sqlite3.connect(db_path)
con.execute(CREATE_USER_CONFIGS)
con.execute(CREATE_SCHEDULES)

now = int(time.time())
con.execute(INSERT_CONFIG, (now,))

con.commit()
con.close()

print(f"✓ User config seeded: {db_path}")
print("  User: SAAB_ARENA (Linköping, 1500m² solar roof, SE3, 45 kWh/h base load)")
print("  Schedules will be created after forecast is available...")
