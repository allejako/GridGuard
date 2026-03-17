#include "GridGuardClient.hpp"

#include <iostream>
#include <iomanip>
#include <sstream>
#include <vector>
#include <string>
#include <map>
#include <algorithm>
#include <numeric>
#include <cstdlib>
#include <ctime>
#include <thread>
#include <chrono>

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

// ── ANSI-safe terminal table rendering system ────────────────────────────────

// Pad string to exact visual width (right-aligned padding with spaces)
// Truncates if content exceeds width, preserving ANSI color codes
static std::string pad_right(const std::string& s, int width) {
    std::string result;
    int len = 0;
    bool in_ansi = false;
    bool needs_reset = false;

    for (size_t i = 0; i < s.size(); ++i) {
        // Detect ANSI escape sequences
        if (s[i] == '\033') {
            in_ansi = true;
            needs_reset = true;  // We started a color code
        }

        // Always include ANSI sequences (don't count towards width)
        if (in_ansi) {
            result += s[i];
            if (s[i] == 'm') in_ansi = false;
            continue;
        }

        // Count visible characters (UTF-8 safe)
        if ((s[i] & 0xC0) != 0x80) {
            if (len >= width) break;  // Truncate here
            ++len;
        }

        result += s[i];
    }

    // Close any open ANSI codes before padding
    if (needs_reset && !in_ansi) {
        result += "\033[0m";
    }

    // Pad if needed
    if (len < width)
        result += std::string(static_cast<size_t>(width - len), ' ');

    return result;
}

// Helper: wrap text with ANSI color code
static std::string colorize(const std::string& text, const char* color) {
    return std::string(color) + text + RESET;
}

// Render a table row with specified column widths
// Builds entire row content first, then pads to exactly W chars
static std::string render_row(const std::vector<std::string>& cols,
                               const std::vector<int>& widths) {
    std::ostringstream out;

    for (size_t i = 0; i < cols.size(); ++i) {
        if (i > 0) out << "  ";  // 2 spaces between columns
        out << pad_right(cols[i], widths[i]);
    }

    // Pad the entire row content to W chars, then wrap with ║
    std::string content = out.str();
    return "║ " + pad_right(content, W - 2) + " ║";
}

// Legacy compatibility: pad() maps to pad_right()
static std::string pad(const std::string& s, int width) {
    return pad_right(s, width);
}

// ── Table column widths ───────────────────────────────────────────────────────

// Forecast table columns:  TIME | SIGNAL | PRICE | LEVEL | SOLAR | DELTA
// Total: 2 (leading space) + 11 + 1 + 7 + 1 + 8 + 1 + 8 + 1 + 9 + 1 + 9 + 2 (trailing) = 68
// Content area: 11 + 7 + 8 + 8 + 9 + 9 + 5 spaces = 62 chars
// Adding ║ on each side = 64, but W=66 so content must be 66
// Adjusting: 2 + 11 + 2 + 7 + 2 + 8 + 2 + 8 + 2 + 9 + 2 + 9 = 64 (need 66)
// New calculation: TIME(11) + SIGNAL(7) + PRICE(8) + LEVEL(8) + SOLAR(9) + DELTA(9) = 52
// Plus 5 spacers (2 chars each except last) = 52 + 10 = 62
// Plus 4 leading/trailing = 66 ✓
static const std::vector<int> FORECAST_COL_WIDTHS = {11, 7, 8, 8, 9, 9};

// Schedule table columns:  LOAD | START | DUR | COST | SAVING | STATUS
// Must match forecast layout: sum(widths) + (5 × 2 spacers) = W - 2 = 64
// Therefore: sum(widths) = 52
// LOAD(16) + START(11) + DUR(4) + COST(8) + SAVING(8) + STATUS(5) = 52 ✓
static const std::vector<int> SCHEDULE_COL_WIDTHS = {16, 11, 4, 8, 8, 5};

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
    std::cout << "║    " << pad("forecast [--watch] [--interval SECONDS]", W - 4) << " ║\n";
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

    // Format numeric values with appropriate precision
    std::ostringstream price, solar, delta;
    price << std::fixed << std::setprecision(2) << e.priceSekKwh << " kr";

    // Smart formatting for solar: use 1 decimal if < 100, 0 decimals if >= 100
    solar << std::fixed;
    if (e.solarKwh >= 100.0) {
        solar << std::setprecision(0) << e.solarKwh << " kWh";  // Rounded, correct unit
    } else if (e.solarKwh >= 10.0) {
        solar << std::setprecision(1) << e.solarKwh << " kWh";
    } else {
        solar << std::setprecision(2) << e.solarKwh << " kWh";
    }

    delta << std::fixed << std::setprecision(1) << e.savingsVsMedian << "%";

    std::vector<std::string> cols = {
        shortTime(e.time),
        colorize(tag, col),
        price.str(),
        colorize(bar, col),
        solar.str(),
        delta.str()
    };

    std::cout << render_row(cols, FORECAST_COL_WIDTHS) << "\n";
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
        // Display current weather from summary
        std::ostringstream weather;
        weather << std::fixed << std::setprecision(1);
        weather << summary.currentTempC << "°C  ·  " << summary.currentWindMs << " m/s";

        std::string info = "  " + summary.userId
                         + "  ·  " + summary.location
                         + "  ·  " + summary.region
                         + "  ·  " + weather.str();
        std::cout << "║" << pad(info, W) << "║\n";
    }
    {
        std::string note = "  Intelligent signal windows (BUY/SELL/AVOID)";
        std::cout << "║" << DIM << pad(note, W) << RESET << "║\n";
    }
    boxMid("ACTION SIGNALS");

    // Column header using render_row for consistent alignment
    {
        std::vector<std::string> headers = {
            "TIME", "SIGNAL", "PRICE", "LEVEL", "SOLAR", "vs AVG"
        };
        std::cout << BOLD << render_row(headers, FORECAST_COL_WIDTHS) << RESET << "\n";
    }
    std::cout << "║" << DIM;
    for (int i = 0; i < W; ++i) std::cout << "─";
    std::cout << RESET << "║\n";

    // Show ALL signal windows (BUY/AVOID/SELL) - these are already filtered by server
    int shown = static_cast<int>(entries.size());
    for (int i = 0; i < shown; ++i)
        printForecastRow(entries[static_cast<size_t>(i)], minP, maxP);

    // Collect ALL BUY hours across full 96h, pick top 5 cheapest
    static const int MAX_BUY_SHOWN = 5;
    std::vector<const gridguard::ForecastEntry*> allBuy;
    for (const auto& e : entries)
        if (e.signal == "BUY")
            allBuy.push_back(&e);

    // STL: partial sort — cheapest first
    std::partial_sort(allBuy.begin(),
                      allBuy.begin() + std::min(MAX_BUY_SHOWN, static_cast<int>(allBuy.size())),
                      allBuy.end(),
                      [](const gridguard::ForecastEntry* a, const gridguard::ForecastEntry* b){
                          return a->totalCostSekKwh < b->totalCostSekKwh;
                      });
    allBuy.resize(std::min(MAX_BUY_SHOWN, static_cast<int>(allBuy.size())));

    // STL: restore chronological order for display
    std::sort(allBuy.begin(), allBuy.end(),
              [](const gridguard::ForecastEntry* a, const gridguard::ForecastEntry* b){
                  return a->time < b->time;
              });

    // No "more hours" message needed - we show all windows already

    // ── BUY windows panel ────────────────────────────────────────────────────
    if (!allBuy.empty()) {
        int totalBuy = countSignal("BUY");
        std::string label = "CHEAPEST " + std::to_string(static_cast<int>(allBuy.size()))
                          + " BUY WINDOWS";
        if (totalBuy > MAX_BUY_SHOWN)
            label += "  (of " + std::to_string(totalBuy) + " total)";
        boxMid(label);
        for (const auto* e : allBuy)
            printForecastRow(*e, minP, maxP);
    }

    // ── Summary panel ────────────────────────────────────────────────────────
    boxMid("SUMMARY");

    // Signal counts - build string and pad to W
    {
        std::ostringstream s;
        s << "  " << BRIGHT_GREEN << "BUY " << countSignal("BUY") << RESET << "  ·  "
          << BRIGHT_CYAN << "SELL " << countSignal("SELL") << RESET << "  ·  "
          << BRIGHT_YELLOW << "IDLE " << countSignal("IDLE") << RESET << "  ·  "
          << BRIGHT_RED << "AVOID " << countSignal("AVOID") << RESET;
        std::cout << "║" << pad(s.str(), W) << "║\n";
    }

    {
        std::ostringstream s;
        s << std::fixed << std::setprecision(2);
        s << "  Import: " << summary.gridImportKwh << " kWh"
          << "  ·  Export: " << summary.gridExportKwh << " kWh"
          << "  ·  Cost: " << summary.totalCostSek << " kr";
        std::cout << "║" << pad(s.str(), W) << "║\n";
    }

    if (cheapest != entries.end()) {
        std::ostringstream s;
        s << std::fixed << std::setprecision(3);
        s << "  " << BRIGHT_GREEN << "Best: " << shortTime(cheapest->time)
          << " @ " << cheapest->totalCostSekKwh << " kr/kWh"
          << std::setprecision(1) << "  ·  Avg BUY dev: " << avgDev << "%" << RESET;
        std::cout << "║" << pad(s.str(), W) << "║\n";
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

    // Column header using render_row for consistent alignment
    {
        std::vector<std::string> headers = {
            "LOAD", "START", "DUR", "COST", "SAVING", "STATUS"
        };
        std::cout << BOLD << render_row(headers, SCHEDULE_COL_WIDTHS) << RESET << "\n";
    }
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

        std::vector<std::string> cols = {
            s.loadId,
            shortTime(s.scheduledStart),
            dur.str(),
            cost.str(),
            colorize(saving.str(), BRIGHT_GREEN),
            colorize(s.status, stCol)
        };

        std::cout << render_row(cols, SCHEDULE_COL_WIDTHS) << "\n";
    }

    std::cout << "║" << DIM;
    for (int i = 0; i < W; ++i) std::cout << "─";
    std::cout << RESET << "║\n";

    std::ostringstream tot;
    tot << std::fixed << std::setprecision(2) << totalSavings;
    std::string savings_text = "  Total savings: " + tot.str() + " kr";
    std::cout << "║" << BOLD << BRIGHT_GREEN
              << pad(savings_text, W)
              << RESET << "║\n";
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

    // Show help only if explicitly requested
    if (std::find(rawArgs.begin(), rawArgs.end(), "--help") != rawArgs.end()
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

    // Default to forecast if no command given
    if (command.empty()) {
        command = "forecast";
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

        bool watch_mode = flags.find("--watch") != flags.end();
        int interval_sec = std::stoi(getArg(flags, "--interval", "60"));  // Default 60s (1 min)

        if (watch_mode) {
            // Use system clear for better compatibility
            system("clear");

            while (true) {
                // Clear screen using system clear command (more reliable than ANSI codes)
                system("clear");

                // Show refresh info at the top
                std::time_t now = std::time(nullptr);
                char time_buf[64];
                std::strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S",
                              std::localtime(&now));
                std::cout << DIM << "Last updated: " << time_buf
                          << "  ·  Refreshing every " << interval_sec << "s"
                          << "  ·  Press Ctrl+C to exit" << RESET << "\n\n";

                gridguard::ForecastSummary summary;
                auto entries = client.getForecast(summary);

                if (entries.empty()) {
                    std::cerr << RED << "Failed to get forecast.\n" << RESET;
                } else {
                    showForecast(entries, summary);

                    // Show schedules below forecast
                    std::cout << "\n";
                    auto schedules = client.getSchedules();
                    showSchedules(schedules);
                }

                std::cout << std::flush;
                std::this_thread::sleep_for(std::chrono::seconds(interval_sec));
            }
        } else {
            gridguard::ForecastSummary summary;
            auto entries = client.getForecast(summary);
            if (entries.empty()) {
                std::cerr << RED << "Failed to get forecast. Is your config set? "
                          << "Run: config set --lat ... --lon ... --region SE3\n" << RESET;
                return 1;
            }
            showForecast(entries, summary);
        }
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
