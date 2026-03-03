#!/bin/bash
#
# GridGuard Demo Script
# Demonstrates the full platform/client separation architecture
#

set -e  # Exit on error

echo "═══════════════════════════════════════════════════════════════════"
echo "  GRIDGUARD ARCHITECTURE DEMO"
echo "  Privacy-First Energy Optimization Platform"
echo "═══════════════════════════════════════════════════════════════════"
echo

# Colors for output
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Step 1: Platform DB (Authentication)
echo -e "${BLUE}[1/6] Creating Platform DB (auth layer)${NC}"
echo "      Location: platform.db"
python3 scripts/seed_platform.py platform.db
echo

# Step 2: Client DB (User data - stays local!)
echo -e "${BLUE}[2/6] Creating Client DB (user's device)${NC}"
echo "      Location: gridguard.db"
python3 scripts/seed_client.py gridguard.db
echo

# Step 3: Generate JWT from Platform DB
echo -e "${BLUE}[3/6] Generating JWT token from Platform DB${NC}"
echo "      User: test_user (premium plan)"
export GRIDGUARD_JWT_SECRET="demo_secret_key_change_in_production_2024"
JWT_TOKEN=$(python3 scripts/generate_jwt.py platform.db test_user 2>/dev/null)
echo "      Token generated ✓"
echo

# Step 4: Show the separation
echo -e "${YELLOW}[4/6] Database Separation (Privacy-First Design)${NC}"
echo
echo "  Platform DB (platform.db):"
echo "  ├── users table"
echo "  └── Contains: user_id, email, plan_type"
echo "  └── Used for: JWT authentication only"
echo
echo "  Client DB (gridguard.db):"
echo "  ├── user_configs table"
echo "  ├── schedules table"
echo "  └── Contains: energy data, solar config, schedules"
echo "  └── Stays on: User's device (NEVER sent to platform)"
echo

# Step 5: Decode JWT to show claims
echo -e "${BLUE}[5/6] JWT Claims (from Platform DB)${NC}"
echo "      Decoding token..."
JWT_PAYLOAD=$(echo $JWT_TOKEN | cut -d'.' -f2)
# Pad base64 if needed
case $((${#JWT_PAYLOAD} % 4)) in
    2) JWT_PAYLOAD="${JWT_PAYLOAD}==" ;;
    3) JWT_PAYLOAD="${JWT_PAYLOAD}=" ;;
esac
echo "$JWT_PAYLOAD" | base64 -d 2>/dev/null | python3 -m json.tool || echo "      (binary data)"
echo

# Step 6: Summary
echo -e "${GREEN}[6/6] Demo Setup Complete!${NC}"
echo
echo "  Platform DB:  platform.db"
echo "  Client DB:    gridguard.db"
echo "  JWT Token:    Exported as GRIDGUARD_JWT_TOKEN"
echo
echo "═══════════════════════════════════════════════════════════════════"
echo "  ARCHITECTURE SUMMARY"
echo "═══════════════════════════════════════════════════════════════════"
echo
echo "  Platform Server (Your infrastructure):"
echo "    • Handles authentication only"
echo "    • Issues JWT tokens"
echo "    • NO access to energy data"
echo
echo "  Client Application (User's device):"
echo "    • Stores ALL energy data locally"
echo "    • Performs ALL computations locally"
echo "    • Validates JWT for API access"
echo
echo "  Privacy guarantee:"
echo "    └─> Energy consumption data NEVER leaves user's device!"
echo
echo "═══════════════════════════════════════════════════════════════════"
echo
echo "Next steps:"
echo "  export GRIDGUARD_JWT_TOKEN=\"$JWT_TOKEN\""
echo "  make dev    # Start the demo server"
echo
