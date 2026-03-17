#!/usr/bin/env python3
"""Seed gridguard.db with test_user config if not already present."""

import sqlite3
import sys
import time

db_path = sys.argv[1] if len(sys.argv) > 1 else "gridguard.db"

CREATE_TABLE = """
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

MIGRATE = [
    "ALTER TABLE user_configs ADD COLUMN grid_fee_low    REAL NOT NULL DEFAULT 0.25",
    "ALTER TABLE user_configs ADD COLUMN grid_fee_normal REAL NOT NULL DEFAULT 0.35",
    "ALTER TABLE user_configs ADD COLUMN grid_fee_high   REAL NOT NULL DEFAULT 0.45",
]

INSERT = """
INSERT OR IGNORE INTO user_configs
    (user_id, location, latitude, longitude, region,
     solar_area_m2, solar_efficiency, consumption_kwh,
     grid_fee_low, grid_fee_normal, grid_fee_high, updated_at)
VALUES
    ('SAAB_ARENA', 'Linköping', 58.4109, 15.6216, 'SE3',
     20.0, 0.18, 1.5,
     0.25, 0.35, 0.45, ?)
"""

con = sqlite3.connect(db_path)
con.execute(CREATE_TABLE)
for sql in MIGRATE:
    try:
        con.execute(sql)
    except sqlite3.OperationalError as e:
        if "duplicate column name" not in str(e):
            raise
con.execute(INSERT, (int(time.time()),))
con.commit()
con.close()

print("OK: SAAB_ARENA configured")
