// TODO: Implement visual dashboard for GridGuard client
//
// DESIGN: Standalone C++ executable (NOT forked from server)
// - Demonstrates Kursmål 4, 5, 9 (C++-objektmodell, RAII, STL)
// - Runs in terminal, started by `make dev` after server
//
// Kursmål coverage:
// - Kursmål 4: RAII för resursförvaltning (SocketRAII klass)
// - Kursmål 5: STL-komponenter (std::vector, std::string)
// - Kursmål 9: C++-komponenter med RAII och grundläggande STL-användning
//
// Implementation requirements:
// 1. AUTHENTICATION:
//    - Hardcoded test JWT token for make dev (same as server expects)
//    - No CLI arguments needed for MVP
//
// 2. SERVER CONNECTION (RAII):
//    - SocketRAII class for automatic socket cleanup
//    - HTTP GET /forecast with Authorization: Bearer <token>
//    - Parse JSON response (reuse existing code or cJSON)
//
// 3. STL USAGE:
//    - std::string for HTTP request/response
//    - std::vector<ForecastEntry> for forecast data
//    - std::unique_ptr if needed for cJSON cleanup
//
// 4. VISUAL TERMINAL OUTPUT:
//    - ASCII boxes (╔═╗║╠╣╚╝─┼│)
//    - ANSI color codes (green for BUY, yellow for SELL, gray for IDLE)
//    - Show: Time, Signal, Price, Solar, Grid for each hour
//    - Display daily summary (grid import/export, cost)
//    - Display best charging window (e.g., "02:00-05:36 → Save 44 kr!")
//
// Estimated time: 3-4 hours
//
// Example output format:
// ╔════════════════════════════════════════════════════╗
// ║         GridGuard Energy Dashboard                 ║
// ║         User: test_user (Stockholm, SE3)           ║
// ╠════════════════════════════════════════════════════╣
// ║  Time       | Signal | Price    | Solar  | Grid    ║
// ║─────────────┼────────┼──────────┼────────┼──────── ║
// ║  00:00-01:00│  BUY   │ 0.88 kr  │ 0.0 kW │ +1.5 kW ║
// ║  ...                                               ║
// ╚════════════════════════════════════════════════════╝
//
// Run with: make dev (starts server, then this client)
