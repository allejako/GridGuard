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
static const char* RED           = "\033[31m";
static const char* YELLOW        = "\033[33m";
static const char* BOLD          = "\033[1m";
static const char* RESET         = "\033[0m";
static const char* BRIGHT_GREEN  = "\033[92m";
static const char* BRIGHT_CYAN   = "\033[96m";
static const char* BRIGHT_YELLOW = "\033[93m";
static const char* BRIGHT_RED    = "\033[91m";
static const char* DIM           = "\033[2m";

// ── Box-drawing helpers ───────────────────────────────────────────────────────

// Inner content width for all panels
static const int W = 66;

static void boxTop(const std::string& label = "") {
    // ╔══ LABEL ══...══╗
    std::cout << "╔";
    if (label.empty()) {
        for (int i = 0; i < W; ++i) std::cout << "═";
    } else {
        std::string tag = "  " + label + "  ";
        int remaining = W - static_cast<int>(tag.size());
        int left = 2;
        int right = remaining - left;
        for (int i = 0; i < left;  ++i) std::cout << "═";
        std::cout << tag;
        for (int i = 0; i < right; ++i) std::cout << "═";
    }
    std::cout << "╗\n";
}

static void boxMid(const std::string& label = "") {
    // ╠══ LABEL ══...══╣  or  ╠══...══╣
    std::cout << "╠";
    if (label.empty()) {
        for (int i = 0; i < W; ++i) std::cout << "═";
    } else {
        std::string tag = "  " + label + "  ";
        int remaining = W - static_cast<int>(tag.size());
        int left = 2;
        int right = remaining - left;
        for (int i = 0; i < left;  ++i) std::cout << "═";
        std::cout << tag;
        for (int i = 0; i < right; ++i) std::cout << "═";
    }
    std::cout << "╣\n";
}

static void boxBot() {
    std::cout << "╚";
    for (int i = 0; i < W; ++i) std::cout << "═";
    std::cout << "╝\n";
}

// Pad a string to exactly `width` visible chars (truncating if needed)
static std::string pad(const std::string& s, int width) {
    if (static_cast<int>(s.size()) >= width)
        return s.substr(0, static_cast<size_t>(width));
    return s + std::string(static_cast<size_t>(width - static_cast<int>(s.size())), ' ');
}

// ── Price bar ─────────────────────────────────────────────────────────────────
// Returns an 8-char bar built from Unicode block characters.
static std::string priceBar(double price, double minP, double maxP, int width = 8) {
    double range = maxP - minP;
    int filled = (range > 0.0)
        ? static_cast<int>((price - minP) / range * width + 0.5)
        : 0;
    filled = std::max(0, std::min(filled, width));
    std::string bar;
    for (int i = 0; i < width; ++i)
        bar += (i < filled) ? "█" : "░";
    return bar;
}

// ── Helpers ───────────────────────────────────────────────────────────────────

static void printUsage(const char* prog) {
    boxTop("GRIDGUARD CLI");
    std::cout << "║  " << pad("Usage: " + std::string(prog) + " [OPTIONS] COMMAND", W - 2) << " ║\n";
    std::cout << "║  " << pad("", W - 2) << " ║\n";
    std::cout << "║  " << pad("Options:", W - 2) << " ║\n";
    std::cout << "║    " << pad("--host HOST    server host  (default: localhost)", W - 4) << " ║\n";
    std::cout << "║    " << pad("--port PORT    server port  (default: 8080)", W - 4) << " ║\n";
    std::cout << "║    " << pad("--token TOKEN  JWT token    (or GRIDGUARD_TOKEN env)", W - 4) << " ║\n";
    std::cout << "║  " << pad("", W - 2) << " ║\n";
    boxMid("COMMANDS");
    std::cout << "║    " << pad("health", W - 4) << " ║\n";
    std::cout << "║    " << pad("forecast", W - 4) << " ║\n";
    std::cout << "║    " << pad("config get", W - 4) << " ║\n";
    std::cout << "║    " << pad("config set --lat LAT --lon LON --region SE3", W - 4) << " ║\n";
    std::cout << "║    " << pad("           [--location NAME] [--solar-area M2]", W - 4) << " ║\n";
    std::cout << "║    " << pad("           [--solar-eff EFF] [--consumption KWH]", W - 4) << " ║\n";
    std::cout << "║    " << pad("schedule list", W - 4) << " ║\n";
    std::cout << "║    " << pad("schedule add --load ID --duration MIN --power KW", W - 4) << " ║\n";
    std::cout << "║    " << pad("schedule delete ID", W - 4) << " ║\n";
    boxBot();
}

// Format an ISO timestamp to a shorter "MM-DD HH:mm" form
static std::string shortTime(const std::string& iso) {
    if (iso.size() < 16) return iso;
    return iso.substr(5, 5) + " " + iso.substr(11, 5);
}

// ── Signal tag ────────────────────────────────────────────────────────────────

struct SigStyle { const char* color; std::string tag; };

static SigStyle sigStyle(const std::string& signal) {
    if (signal == "BUY")  return { BRIGHT_GREEN,  "[ BUY ]" };
    if (signal == "SELL") return { BRIGHT_CYAN,   "[SELL ]" };
    if (signal == "IDLE") return { BRIGHT_YELLOW, "[ IDLE]" };
    return                       { BRIGHT_RED,    "[AVOID]" };
}

// ── Print one forecast row ────────────────────────────────────────────────────

static void printForecastRow(const gridguard::ForecastEntry& e,
                              double minP, double maxP) {
    auto [col, tag] = sigStyle(e.signal);
    std::string bar = priceBar(e.totalCostSekKwh, minP, maxP);

    std::cout << "║  "
              << pad(shortTime(e.time), 11) << "  "
              << col << tag << RESET << "  "
              << std::right << std::fixed << std::setprecision(3)
              << std::setw(6) << e.priceSekKwh << " kr  "
              << col << bar << RESET << "  "
              << std::setw(6) << e.solarKwh << " kWh  "
              << std::setprecision(1)
              << std::right << std::setw(6) << e.savingsVsMedian << "% ║\n";
}

// ── Display: forecast ─────────────────────────────────────────────────────────

static void showForecast(const std::vector<gridguard::ForecastEntry>& entries,
                         const gridguard::ForecastSummary& summary) {
    if (entries.empty()) {
        std::cerr << RED << "No forecast data received.\n" << RESET;
        return;
    }

    // STL: count signals
    auto countSignal = [&](const std::string& sig) {
        return static_cast<int>(std::count_if(entries.begin(), entries.end(),
            [&](const gridguard::ForecastEntry& e){ return e.signal == sig; }));
    };

    // STL: cheapest total-cost hour
    auto cheapest = std::min_element(entries.begin(), entries.end(),
        [](const gridguard::ForecastEntry& a, const gridguard::ForecastEntry& b){
            return a.totalCostSekKwh < b.totalCostSekKwh;
        });

    // STL: average price deviation for BUY hours
    double totalDev = std::accumulate(entries.begin(), entries.end(), 0.0,
        [](double s, const gridguard::ForecastEntry& e){
            return s + (e.savingsVsMedian < 0 ? e.savingsVsMedian : 0.0);
        });
    int buyCount = countSignal("BUY");
    double avgDev = buyCount > 0 ? totalDev / buyCount : 0.0;

    // STL: price range for bar normalisation
    double minP = std::min_element(entries.begin(), entries.end(),
        [](const gridguard::ForecastEntry& a, const gridguard::ForecastEntry& b){
            return a.totalCostSekKwh < b.totalCostSekKwh; })->totalCostSekKwh;
    double maxP = std::max_element(entries.begin(), entries.end(),
        [](const gridguard::ForecastEntry& a, const gridguard::ForecastEntry& b){
            return a.totalCostSekKwh < b.totalCostSekKwh; })->totalCostSekKwh;

    // ── Header ───────────────────────────────────────────────────────────────
    std::cout << "\n";
    boxTop("GRIDGUARD");
    {
        std::string info = "  " + summary.userId
                         + "  ·  " + summary.location
                         + "  ·  " + summary.region
                         + "  ·  " + std::to_string(summary.entries) + "h forecast";
        std::cout << "║" << pad(info, W) << "║\n";
    }
    boxMid("24h OVERVIEW");

    // Column header
    std::cout << "║  "
              << BOLD
              << pad("TIME",    11) << "  "
              << pad("SIGNAL",   7) << "  "
              << pad("kr/kWh",   8) << "   "
              << pad("LEVEL",    8) << "  "
              << pad("SOLAR",    9) << "   "
              << pad("Δ AVG",    6)
              << RESET << " ║\n";
    std::cout << "║" << DIM;
    for (int i = 0; i < W; ++i) std::cout << "─";
    std::cout << RESET << "║\n";

    // First 24 hours
    int shown = std::min(static_cast<int>(entries.size()), 24);
    for (int i = 0; i < shown; ++i)
        printForecastRow(entries[static_cast<size_t>(i)], minP, maxP);

    // Collect BUY hours outside first 24
    std::vector<const gridguard::ForecastEntry*> buyLater;
    for (size_t i = static_cast<size_t>(shown); i < entries.size(); ++i)
        if (entries[i].signal == "BUY")
            buyLater.push_back(&entries[i]);

    int remaining = static_cast<int>(entries.size()) - shown;
    if (remaining > 0) {
        std::string note = "  … " + std::to_string(remaining) + " more hours";
        if (buyLater.empty()) note += "  (no further BUY signals)";
        std::cout << "║" << DIM << pad(note, W) << RESET << "║\n";
    }

    // ── BUY windows panel ────────────────────────────────────────────────────
    if (!buyLater.empty()) {
        boxMid("BUY WINDOWS  " + std::to_string(static_cast<int>(buyLater.size())) + " hrs");
        for (const auto* e : buyLater)
            printForecastRow(*e, minP, maxP);
    }

    // ── Summary panel ────────────────────────────────────────────────────────
    boxMid("SUMMARY");

    // Signal counts — compute visible length to add correct padding before closing ║
    {
        int nBuy   = countSignal("BUY"),  nSell = countSignal("SELL");
        int nIdle  = countSignal("IDLE"), nAvoid = countSignal("AVOID");
        std::string vis = "  BUY " + std::to_string(nBuy)
                        + "  ·  SELL " + std::to_string(nSell)
                        + "  ·  IDLE " + std::to_string(nIdle)
                        + "  ·  AVOID " + std::to_string(nAvoid);
        int fill = W - static_cast<int>(vis.size());
        std::cout << "║  "
                  << BRIGHT_GREEN  << "BUY "   << nBuy   << RESET << "  ·  "
                  << BRIGHT_CYAN   << "SELL "  << nSell  << RESET << "  ·  "
                  << BRIGHT_YELLOW << "IDLE "  << nIdle  << RESET << "  ·  "
                  << BRIGHT_RED    << "AVOID " << nAvoid << RESET
                  << std::string(static_cast<size_t>(std::max(0, fill)), ' ') << "║\n";
    }

    {
        std::ostringstream s;
        s << std::fixed << std::setprecision(2);
        s << "Import: " << summary.gridImportKwh << " kWh"
          << "  ·  Export: " << summary.gridExportKwh << " kWh"
          << "  ·  Cost: " << summary.totalCostSek << " kr";
        std::cout << "║  " << pad(s.str(), W - 2) << " ║\n";
    }

    if (cheapest != entries.end()) {
        std::ostringstream s;
        s << std::fixed << std::setprecision(3);
        s << "Best: " << shortTime(cheapest->time)
          << " @ " << cheapest->totalCostSekKwh << " kr/kWh"
          << std::setprecision(1) << "  ·  Avg BUY dev: " << avgDev << "%";
        // Visible length known — print with color then pad
        int fill = W - 2 - static_cast<int>(s.str().size());
        std::cout << "║  " << BRIGHT_GREEN << s.str() << RESET
                  << std::string(static_cast<size_t>(std::max(0, fill)), ' ') << " ║\n";
    }

    boxBot();
    std::cout << "\n";
}

// ── Display: schedules ────────────────────────────────────────────────────────

static void showSchedules(const std::vector<gridguard::ScheduleEntry>& schedules) {
    std::cout << "\n";
    if (schedules.empty()) {
        boxTop("SCHEDULES");
        std::cout << "║  " << YELLOW << pad("No active schedules.", W - 2) << RESET << " ║\n";
        boxBot();
        std::cout << "\n";
        return;
    }

    // STL: sort by start time (lexicographic on ISO string)
    std::vector<gridguard::ScheduleEntry> sorted = schedules;
    std::sort(sorted.begin(), sorted.end(),
        [](const gridguard::ScheduleEntry& a, const gridguard::ScheduleEntry& b){
            return a.scheduledStart < b.scheduledStart;
        });

    // STL: total savings
    double totalSavings = std::accumulate(sorted.begin(), sorted.end(), 0.0,
        [](double s, const gridguard::ScheduleEntry& e){ return s + e.savingsSek; });

    boxTop("SCHEDULES");

    // Column header — widths: load=13, start=11, dur=4, cost=8, saving=8, status=8  → total=66
    std::cout << "║  "
              << BOLD
              << pad("LOAD",   13) << "  "
              << pad("START",  11) << "  "
              << pad("DUR",     4) << "  "
              << pad("COST",    8) << "  "
              << pad("SAVING",  8) << "  "
              << pad("STATUS",  8)
              << RESET << " ║\n";
    std::cout << "║" << DIM;
    for (int i = 0; i < W; ++i) std::cout << "─";
    std::cout << RESET << "║\n";

    for (const auto& s : sorted) {
        const char* stCol = (s.status == "completed") ? BRIGHT_GREEN
                          : (s.status == "pending")   ? BRIGHT_YELLOW
                          :                             BRIGHT_CYAN;

        std::ostringstream cost, saving, dur;
        cost   << std::fixed << std::setprecision(2) << s.estimatedCostSek << " kr";
        saving << std::fixed << std::setprecision(2) << s.savingsSek       << " kr";
        dur    << s.durationMinutes << "m";

        std::cout << "║  "
                  << pad(s.loadId,                   13) << "  "
                  << pad(shortTime(s.scheduledStart), 11) << "  "
                  << pad(dur.str(),                    4) << "  "
                  << std::right << std::setw(8) << cost.str()   << "  "
                  << BRIGHT_GREEN << std::setw(8) << saving.str() << RESET << "  "
                  << stCol << pad(s.status, 8) << RESET
                  << " ║\n";
    }

    std::cout << "║" << DIM;
    for (int i = 0; i < W; ++i) std::cout << "─";
    std::cout << RESET << "║\n";

    std::ostringstream tot;
    tot << std::fixed << std::setprecision(2) << totalSavings;
    std::cout << "║  " << BOLD << BRIGHT_GREEN
              << pad("Total savings: " + tot.str() + " kr", W - 2)
              << RESET << " ║\n";
    boxBot();
    std::cout << "\n";
}

// ── Argument parsing ──────────────────────────────────────────────────────────

static std::map<std::string, std::string> parseArgs(const std::vector<std::string>& args) {
    std::map<std::string, std::string> flags;
    int positional = 0;
    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i].substr(0, 2) == "--") {
            if (i + 1 < args.size() && args[i + 1].substr(0, 2) != "--") {
                const std::string key = args[i];
                ++i;
                flags[key] = args[i];
            } else {
                flags[args[i]] = "";
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


// ── main ──────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    std::vector<std::string> rawArgs(argv + 1, argv + argc);

    if (rawArgs.empty() || std::find(rawArgs.begin(), rawArgs.end(), "--help") != rawArgs.end()
                        || std::find(rawArgs.begin(), rawArgs.end(), "-h")     != rawArgs.end()) {
        printUsage(argv[0]);
        return 0;
    }

    auto flags = parseArgs(rawArgs);

    std::string host  = getArg(flags, "--host",  "localhost");
    int         port  = std::stoi(getArg(flags, "--port", "8080"));

    std::string token = getArg(flags, "--token");
    if (token.empty()) {
        const char* env = std::getenv("GRIDGUARD_TOKEN");
        if (env) token = env;
    }

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
        boxTop("GRIDGUARD");
        if (client.checkHealth()) {
            std::cout << "║  " << BRIGHT_GREEN << BOLD
                      << pad("SERVER UP  ·  " + host + ":" + std::to_string(port), W - 2)
                      << RESET << " ║\n";
        } else {
            std::cout << "║  " << BRIGHT_RED << BOLD
                      << pad("SERVER DOWN  ·  " + host + ":" + std::to_string(port), W - 2)
                      << RESET << " ║\n";
        }
        boxBot();
        if (!client.checkHealth()) return 1;
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
            std::string latStr = getArg(flags, "--lat");
            std::string lonStr = getArg(flags, "--lon");
            std::string region = getArg(flags, "--region", "SE3");
            std::string loc    = getArg(flags, "--location", "");
            double area = std::stod(getArg(flags, "--solar-area",  "0.0"));
            double eff  = std::stod(getArg(flags, "--solar-eff",   "0.0"));
            double load = std::stod(getArg(flags, "--consumption", "0.5"));

            if (latStr.empty() || lonStr.empty()) {
                std::cerr << "Error: --lat and --lon are required.\n";
                return 1;
            }
            if (client.setUserConfig(std::stod(latStr), std::stod(lonStr),
                                     region, loc, area, eff, load)) {
                std::cout << BRIGHT_GREEN << "Config saved.\n" << RESET;
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
            if (!client.createSchedule(loadId, duration, power, deadline))
                return 1;
        }
        else if (subcommand == "delete") {
            std::string scheduleId = getArg(flags, "cmd2");
            if (scheduleId.empty()) {
                std::cerr << "Error: schedule delete <SCHEDULE_ID>\n";
                return 1;
            }
            if (client.deleteSchedule(scheduleId)) {
                std::cout << BRIGHT_GREEN << "Schedule " << scheduleId << " cancelled.\n" << RESET;
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
