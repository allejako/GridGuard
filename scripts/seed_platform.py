#!/usr/bin/env python3
"""Seed platform.db with demo users for authentication demonstration."""

import sqlite3
import sys
import time

db_path = sys.argv[1] if len(sys.argv) > 1 else "platform.db"

# Create platform database with users table
CREATE_TABLE = """
CREATE TABLE IF NOT EXISTS users (
    user_id TEXT PRIMARY KEY,
    email TEXT UNIQUE NOT NULL,
    plan_type TEXT NOT NULL,
    created_at INTEGER NOT NULL
)
"""

# Insert demo users with different subscription plans
INSERT_USERS = """
INSERT OR REPLACE INTO users (user_id, email, plan_type, created_at)
VALUES
    ('test_user', 'demo@gridguard.io', 'premium', ?),
    ('free_user', 'free@gridguard.io', 'free', ?),
    ('basic_user', 'basic@gridguard.io', 'basic', ?)
"""

con = sqlite3.connect(db_path)
con.execute(CREATE_TABLE)
now = int(time.time())
con.execute(INSERT_USERS, (now, now, now))
con.commit()
con.close()

print(f"✓ Platform DB seeded: {db_path}")
print("  Users: test_user (premium), free_user (free), basic_user (basic)")
