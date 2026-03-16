#!/bin/bash

# GridGuard Demo Helper Commands
# Source this file: source demo_commands.sh

# Seed databases
seed_db() {
    echo "Seeding databases..."
    python3 scripts/seed_platform.py platform.db
    python3 scripts/seed_client.py gridguard.db
    echo "Done!"
}

# Generate JWT token
get_token() {
    export TOKEN=$(GRIDGUARD_JWT_SECRET=gridguard-test-secret python3 scripts/generate_jwt.py platform.db test_user)
    echo "Token set: ${TOKEN:0:20}..."
}

# API call aliases
alias health='curl -s http://localhost:8080/health | python3 -m json.tool'
alias metrics='curl -s http://localhost:8080/metrics | python3 -m json.tool'
alias forecast='curl -s -H "Authorization: Bearer $TOKEN" http://localhost:8080/forecast | python3 -m json.tool'
alias schedule='curl -s -H "Authorization: Bearer $TOKEN" http://localhost:8080/schedule | python3 -m json.tool'
alias config='curl -s -H "Authorization: Bearer $TOKEN" http://localhost:8080/config | python3 -m json.tool'

echo "Demo commands loaded!"
echo ""
echo "Available commands:"
echo "  seed_db      - Seed both databases"
echo "  get_token    - Generate and export JWT token"
echo ""
echo "Available aliases:"
echo "  health       - GET /health"
echo "  metrics      - GET /metrics"
echo "  forecast     - GET /forecast"
echo "  schedule     - GET /schedule"
echo "  config       - GET /config"
echo ""
echo "Usage:"
echo "  seed_db"
echo "  get_token"
echo "  forecast"
