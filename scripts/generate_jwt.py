#!/usr/bin/env python3
"""Generate JWT for testing GridGuard"""

import jwt
import time
import sys

# Secret from environment
SECRET = "gridguard-test-secret"

# Create payload
payload = {
    "sub": "test_user",
    "exp": int(time.time()) + (365 * 24 * 60 * 60),  # 1 year from now
    "iat": int(time.time())
}

# Generate token
token = jwt.encode(payload, SECRET, algorithm="HS256")

print("JWT Token for test_user:")
print(token)
print("\nUse in request:")
print(f'curl -H "Authorization: Bearer {token}" http://localhost:8080/forecast')
