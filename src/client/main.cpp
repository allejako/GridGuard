#include "GridGuardClient.hpp"

#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <map>
#include <algorithm>
#include <numeric>
#include <cstdlib>
#include <ctime>

// ── Terminal colours ─────────────────────────────────────────────────────────
static const char* GREEN  = "\033[32m";
static const char* RED    = "\033[31m";
static const char* YELLOW = "\033[33m";
static const char* CYAN   = "\033[36m";
static const char* BOLD   = "\033[1m";
static const char* RESET  = "\033[0m";

// ── Helpers ──────────────────────────────────────────────────────────────────

static void printUsage(const char* prog) {
    std::cout << BOLD << "GridGuard CLI Client\n" << RESET
              << "\nUsage: " << prog << " [OPTIONS] COMMAND\n"
              << "\nOptions:\n"
              << "  --host HOST       Server host (default: localhost)\n"
              << "  --port PORT       Server port (default: 8080)\n"
              << "  --token TOKEN     JWT token (or set GRIDGUARD_TOKEN env var)\n"
              << "\nCommands:\n"
              << "  health                        Check server status\n"
              << "  forecast                      Show 96-hour energy forecast\n"
              << "  config get                    Show your stored configuration\n"
              << "  config set --lat LAT --lon LON --region SE3 [OPTIONS]\n"
              << "             [--location NAME] [--solar-area M2]\n"
              << "             [--solar-eff EFF] [--consumption KWH]\n"
              << "  schedule list                 List active schedules\n"
              << "  schedule add --load ID --duration MIN --power KW [--deadline UNIX]\n"
              << "  schedule delete ID            Cancel a schedule\n"
              << "\nExamples:\n"
              << "  " << prog << " --token $TOKEN health\n"
              << "  " << prog << " --token $TOKEN forecast\n"
              << "  " << prog << " --token $TOKEN config set --lat 59.33 --lon 18.07 --region SE3\n"
              << "  " << prog << " --token $TOKEN schedule add --load ev_charger --duration 480 --power 5.0\n";
}

// Format an ISO timestamp to a shorter "MM-DD HH:mm" form
static std::string shortTime(const std::string& iso) {
    if (iso.size() < 16) return iso;
    // "2026-03-03T14:00:00Z" → "03-03 14:00"
    return iso.substr(5, 5) + " " + iso.substr(11, 5);
}

// ── Display: forecast ────────────────────────────────────────────────────────

static void showForecast(const std::vector<gridguard::ForecastEntry>& entries,
                         const gridguard::ForecastSummary& summary) {
    if (entries.empty()) {
        std::cerr << RED << "No forecast data received.\n" << RESET;
        return;
    }

    // ── STL: count signals ───────────────────────────────────────────────────
    auto countSignal = [&](const std::string& sig) {
        return static_cast<int>(std::count_if(entries.begin(), entries.end(),
            [&](const gridguard::ForecastEntry& e){ return e.signal == sig; }));
    };

    // ── STL: find cheapest BUY hour ──────────────────────────────────────────
    auto cheapest = std::min_element(entries.begin(), entries.end(),
        [](const gridguard::ForecastEntry& a, const gridguard::ForecastEntry& b){
            return a.totalCostSekKwh < b.totalCostSekKwh;
        });

    // ── STL: total potential savings ─────────────────────────────────────────
    double totalSavings = std::accumulate(entries.begin(), entries.end(), 0.0,
        [](double sum, const gridguard::ForecastEntry& e){
            return sum + (e.savingsVsMedian > 0 ? e.savingsVsMedian : 0.0);
        });

    std::cout << "\n" << BOLD << "GridGuard Energy Forecast" << RESET << "\n";
    std::cout << "User: " << summary.userId << "  |  " << summary.location
              << " (" << summary.region << ")\n";
    std::cout << "─────────────────────────────────────────────────────────────\n";

    // Print table header
    std::cout << BOLD
              << std::left  << std::setw(12) << "Time"
              << std::left  << std::setw(8)  << "Signal"
              << std::right << std::setw(10) << "Price"
              << std::right << std::setw(12) << "TotalCost"
              << std::right << std::setw(10) << "Solar"
              << std::right << std::setw(10) << "Savings"
              << RESET << "\n";
    std::cout << "─────────────────────────────────────────────────────────────\n";

    // Show first 24 hours (one day)
    int shown = std::min(static_cast<int>(entries.size()), 24);
    for (int i = 0; i < shown; ++i) {
        const auto& e = entries[static_cast<size_t>(i)];

        const char* sigColor = RESET;
        if      (e.signal == "BUY")  sigColor = GREEN;
        else if (e.signal == "SELL") sigColor = CYAN;
        else                          sigColor = YELLOW;

        std::cout << std::left  << std::setw(12) << shortTime(e.time)
                  << sigColor
                  << std::left  << std::setw(8)  << e.signal
                  << RESET
                  << std::right << std::fixed << std::setprecision(3)
                  << std::setw(8)  << e.priceSekKwh     << " kr"
                  << std::setw(10) << e.totalCostSekKwh << " kr"
                  << std::setw(8)  << e.solarKwh        << " kWh"
                  << std::setw(8)  << e.savingsVsMedian << " kr"
                  << "\n";
    }

    if (static_cast<int>(entries.size()) > shown)
        std::cout << "  ... and " << (entries.size() - shown) << " more hours\n";

    std::cout << "─────────────────────────────────────────────────────────────\n";
    std::cout << BOLD << "Summary (96h):\n" << RESET;
    std::cout << "  BUY signals:    " << GREEN  << countSignal("BUY")  << RESET << "\n";
    std::cout << "  SELL signals:   " << CYAN   << countSignal("SELL") << RESET << "\n";
    std::cout << "  IDLE hours:     " << YELLOW << countSignal("IDLE") << RESET << "\n";
    std::cout << "  Grid import:    " << std::fixed << std::setprecision(2)
              << summary.gridImportKwh << " kWh\n";
    std::cout << "  Grid export:    " << summary.gridExportKwh << " kWh\n";
    std::cout << "  Total cost:     " << BOLD << summary.totalCostSek << " kr" << RESET << "\n";
    if (cheapest != entries.end())
        std::cout << "  Cheapest hour:  " << GREEN << shortTime(cheapest->time)
                  << " @ " << cheapest->totalCostSekKwh << " kr/kWh" << RESET << "\n";
    std::cout << "  Potential BUY savings vs median: "
              << GREEN << totalSavings << " kr" << RESET << "\n\n";
}

// ── Display: schedules ───────────────────────────────────────────────────────

static void showSchedules(const std::vector<gridguard::ScheduleEntry>& schedules) {
    if (schedules.empty()) {
        std::cout << YELLOW << "No active schedules.\n" << RESET;
        return;
    }

    // STL: sort by scheduled start time (lexicographic on ISO string)
    std::vector<gridguard::ScheduleEntry> sorted = schedules;
    std::sort(sorted.begin(), sorted.end(),
        [](const gridguard::ScheduleEntry& a, const gridguard::ScheduleEntry& b){
            return a.scheduledStart < b.scheduledStart;
        });

    // STL: total savings across all schedules
    double totalSavings = std::accumulate(sorted.begin(), sorted.end(), 0.0,
        [](double s, const gridguard::ScheduleEntry& e){ return s + e.savingsSek; });

    std::cout << "\n" << BOLD << "Active Schedules\n" << RESET;
    std::cout << "──────────────────────────────────────────────────────────────\n";
    std::cout << BOLD
              << std::left  << std::setw(14) << "Load"
              << std::left  << std::setw(13) << "Start"
              << std::right << std::setw(8)  << "Dur(m)"
              << std::right << std::setw(8)  << "kW"
              << std::right << std::setw(10) << "Cost"
              << std::right << std::setw(10) << "Saving"
              << std::left  << std::setw(10) << "  Status"
              << RESET << "\n";
    std::cout << "──────────────────────────────────────────────────────────────\n";

    for (const auto& s : sorted) {
        std::cout << std::left  << std::setw(14) << s.loadId
                  << std::left  << std::setw(13) << shortTime(s.scheduledStart)
                  << std::right << std::fixed << std::setprecision(1)
                  << std::setw(8)  << s.durationMinutes
                  << std::setw(8)  << s.powerKw
                  << std::setprecision(2)
                  << std::setw(8)  << s.estimatedCostSek << " kr"
                  << std::setw(8)  << s.savingsSek       << " kr"
                  << "  " << GREEN << s.status << RESET << "\n";
    }
    std::cout << "──────────────────────────────────────────────────────────────\n";
    std::cout << "  Total savings: " << GREEN << BOLD << totalSavings << " kr" << RESET << "\n\n";
}

// ── Argument parsing ─────────────────────────────────────────────────────────
//
// Parses "--key value" pairs into a std::map<string,string>.
// Positional (non-flag) arguments are stored under keys "cmd0", "cmd1", …

static std::map<std::string, std::string> parseArgs(const std::vector<std::string>& args) {
    std::map<std::string, std::string> flags;
    int positional = 0;
    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i].substr(0, 2) == "--") {
            // Flag with optional value
            if (i + 1 < args.size() && args[i + 1].substr(0, 2) != "--") {
                const std::string key = args[i];
                ++i;
                flags[key] = args[i];
            } else {
                flags[args[i]] = "";          // boolean flag (no value)
            }
        } else {
            flags["cmd" + std::to_string(positional++)] = args[i];
        }
    }
    return flags;
}

static std::string getArg(const std::map<std::string, std::string>& flags,
                           const std::string& flag,
                           const std::string& def = "") {
    auto it = flags.find(flag);
    return (it != flags.end()) ? it->second : def;
}


// ── main ─────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    std::vector<std::string> rawArgs(argv + 1, argv + argc);

    if (rawArgs.empty() || std::find(rawArgs.begin(), rawArgs.end(), "--help") != rawArgs.end()
                        || std::find(rawArgs.begin(), rawArgs.end(), "-h")     != rawArgs.end()) {
        printUsage(argv[0]);
        return 0;
    }

    // Parse all flags into a map for O(log n) lookup
    auto flags = parseArgs(rawArgs);

    // Options
    std::string host  = getArg(flags, "--host",  "localhost");
    int         port  = std::stoi(getArg(flags, "--port", "8080"));

    // Token from --token flag or environment variable
    std::string token = getArg(flags, "--token");
    if (token.empty()) {
        const char* env = std::getenv("GRIDGUARD_TOKEN");
        if (env) token = env;
    }

    // Command and optional subcommand come from positional slots
    std::string command    = getArg(flags, "cmd0");
    std::string subcommand = getArg(flags, "cmd1");

    if (command.empty()) {
        std::cerr << "Error: no command given.\n";
        printUsage(argv[0]);
        return 1;
    }

    gridguard::GridGuardClient client(host, port, token);

    // ── health ───────────────────────────────────────────────────────────────
    if (command == "health") {
        if (client.checkHealth()) {
            std::cout << GREEN << "Server is UP" << RESET
                      << " — " << host << ":" << port << "\n";
        } else {
            std::cerr << RED << "Server is DOWN or unreachable.\n" << RESET;
            return 1;
        }
    }

    // ── forecast ─────────────────────────────────────────────────────────────
    else if (command == "forecast") {
        if (token.empty()) {
            std::cerr << "Error: --token required for forecast.\n";
            return 1;
        }
        gridguard::ForecastSummary summary;
        auto entries = client.getForecast(summary);
        if (entries.empty()) {
            std::cerr << RED << "Failed to get forecast. Is your config set? "
                      << "Run: config set --lat ... --lon ... --region SE3\n" << RESET;
            return 1;
        }
        showForecast(entries, summary);
    }

    // ── config ───────────────────────────────────────────────────────────────
    else if (command == "config") {
        if (token.empty()) {
            std::cerr << "Error: --token required for config.\n";
            return 1;
        }
        if (subcommand == "get") {
            std::string cfg = client.getUserConfig();
            if (cfg.empty()) {
                std::cerr << RED << "No config found. Use: config set\n" << RESET;
                return 1;
            }
            std::cout << cfg << "\n";
        }
        else if (subcommand == "set") {
            std::string latStr  = getArg(flags, "--lat");
            std::string lonStr  = getArg(flags, "--lon");
            std::string region  = getArg(flags, "--region", "SE3");
            std::string loc     = getArg(flags, "--location", "");
            double area   = std::stod(getArg(flags, "--solar-area",  "0.0"));
            double eff    = std::stod(getArg(flags, "--solar-eff",   "0.0"));
            double load   = std::stod(getArg(flags, "--consumption", "0.5"));

            if (latStr.empty() || lonStr.empty()) {
                std::cerr << "Error: --lat and --lon are required.\n";
                return 1;
            }
            double lat = std::stod(latStr);
            double lon = std::stod(lonStr);

            if (client.setUserConfig(lat, lon, region, loc, area, eff, load)) {
                std::cout << GREEN << "Config saved.\n" << RESET;
            } else {
                std::cerr << RED << "Failed to save config.\n" << RESET;
                return 1;
            }
        }
        else {
            std::cerr << "Unknown config subcommand: " << subcommand << "\n";
            return 1;
        }
    }

    // ── schedule ─────────────────────────────────────────────────────────────
    else if (command == "schedule") {
        if (token.empty()) {
            std::cerr << "Error: --token required for schedule.\n";
            return 1;
        }
        if (subcommand == "list") {
            showSchedules(client.getSchedules());
        }
        else if (subcommand == "add") {
            std::string loadId   = getArg(flags, "--load");
            int         duration = std::stoi(getArg(flags, "--duration", "0"));
            double      power    = std::stod(getArg(flags, "--power",    "0"));
            long        deadline = std::stol(getArg(flags, "--deadline", "0"));

            if (loadId.empty() || duration <= 0 || power <= 0.0) {
                std::cerr << "Error: --load, --duration and --power are required.\n";
                return 1;
            }
            if (!client.createSchedule(loadId, duration, power, deadline)) {
                return 1;
            }
        }
        else if (subcommand == "delete") {
            // schedule delete <id> — id is positional slot cmd2
            std::string scheduleId = getArg(flags, "cmd2");
            if (scheduleId.empty()) {
                std::cerr << "Error: schedule delete <SCHEDULE_ID>\n";
                return 1;
            }
            if (client.deleteSchedule(scheduleId)) {
                std::cout << GREEN << "Schedule " << scheduleId << " cancelled.\n" << RESET;
            } else {
                std::cerr << RED << "Failed to delete schedule.\n" << RESET;
                return 1;
            }
        }
        else {
            std::cerr << "Unknown schedule subcommand: " << subcommand << "\n";
            return 1;
        }
    }

    else {
        std::cerr << "Unknown command: " << command << "\n";
        printUsage(argv[0]);
        return 1;
    }

    return 0;
}
