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

// ── Palette ───────────────────────────────────────────────────────────────────
static const char* R  = "\033[0m";    // reset
static const char* B  = "\033[1m";    // bold
static const char* D  = "\033[2m";    // dim
static const char* W  = "\033[97m";   // bright white  — values
static const char* G  = "\033[90m";   // dark gray     — labels, borders, metadata
static const char* C  = "\033[96m";   // cyan          — accent, section headers
static const char* GN = "\033[92m";   // green         — BUY, savings, ok
static const char* YL = "\033[93m";   // yellow        — IDLE
static const char* RD = "\033[91m";   // red           — AVOID, errors

// ── Layout ────────────────────────────────────────────────────────────────────
static const int IW = 66;   // inner width between │ borders

// ── String utilities ──────────────────────────────────────────────────────────

static int visLen(const std::string& s) {
    int n = 0; bool esc = false;
    for (unsigned char c : s) {
        if (c == '\033') { esc = true;  continue; }
        if (esc)         { if (c == 'm') esc = false; continue; }
        if ((c & 0xC0) != 0x80) ++n;
    }
    return n;
}

static std::string padR(const std::string& s, int w) {
    std::string out; int len = 0; bool esc = false, had_color = false;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\033') { esc = true; had_color = true; }
        if (esc) { out += s[i]; if (s[i] == 'm') esc = false; continue; }
        if ((s[i] & 0xC0) != 0x80) { if (len >= w) break; ++len; }
        out += s[i];
    }
    if (had_color) out += R;
    if (len < w) out += std::string(static_cast<size_t>(w - len), ' ');
    return out;
}

// Repeat a UTF-8 string n times  (needed for multi-byte chars like ─)
static std::string rep(const std::string& s, int n) {
    std::string out; out.reserve(static_cast<size_t>(n) * s.size());
    for (int i = 0; i < n; ++i) out += s;
    return out;
}

static std::string c(const char* code, const std::string& text) {
    return std::string(code) + text + R;
}

// ── Border ────────────────────────────────────────────────────────────────────

static void top()   { std::cout << c(G, "╭" + rep("─", IW) + "╮") << "\n"; }
static void bot()   { std::cout << c(G, "╰" + rep("─", IW) + "╯") << "\n"; }
static void rule()  { std::cout << c(G, "├" + rep("─", IW) + "┤") << "\n"; }
static void blank() { std::cout << c(G, "│") << rep(" ", IW) << c(G, "│") << "\n"; }

// Print a bordered row. Content is left-aligned with `indent` spaces before it.
static void row(const std::string& content, int indent = 2) {
    int fill = IW - indent - visLen(content);
    if (fill < 0) fill = 0;
    std::cout << c(G, "│")
              << rep(" ", indent) << content << rep(" ", fill)
              << c(G, "│") << "\n";
}

// Print a row with left and right content — right is pushed to the border.
static void rowLR(const std::string& left, const std::string& right, int indent = 2) {
    int used  = indent + visLen(left) + visLen(right) + 2; // 2 trailing spaces
    int gap   = IW - used;
    if (gap < 1) gap = 1;
    std::cout << c(G, "│")
              << rep(" ", indent) << left << rep(" ", gap) << right << "  "
              << c(G, "│") << "\n";
}

// ── Table ─────────────────────────────────────────────────────────────────────
// Forecast columns: TIME(11) SIGNAL(6) PRICE(9) BAR(10) SOLAR(9) VSAVG(7)
//   = 52 visible + 5 spacers × 2 = 62. Row indent=2, trailing=2 → 2+62+2=66=IW ✓
static const std::vector<int> FC = {11, 6, 9, 10, 9, 7};

// Schedule columns: LOAD(10) START(11) DUR(4) COST(9) SAVING(9) STATUS(9)
//   = 52 visible + 5 × 2 = 62. trow adds indent(2) + trailing(2) → 66 = IW ✓
static const std::vector<int> SC = {10, 11, 4, 9, 9, 9};

static std::string trow(const std::vector<std::string>& cols,
                         const std::vector<int>& widths) {
    std::ostringstream out;
    for (size_t i = 0; i < cols.size(); ++i) {
        if (i > 0) out << "  ";
        out << padR(cols[i], widths[i]);
    }
    std::string content = out.str();
    int fill = IW - 2 - visLen(content) - 2;
    if (fill < 0) fill = 0;
    return c(G, "│") + "  " + content + rep(" ", fill + 2) + c(G, "│");
}

// ── Signal ────────────────────────────────────────────────────────────────────

struct Sig { const char* color; const char* label; };  // label always 4 chars

static Sig sigOf(const std::string& s) {
    if (s == "BUY")  return { GN, "BUY " };
    if (s == "SELL") return { C,  "SELL" };
    if (s == "IDLE") return { YL, "IDLE" };
    return                  { RD, "SKIP" };
}

// "▸ BUY" — 6 visible chars
static std::string sigTag(const Sig& s) {
    return c(s.color, std::string("▸ ") + s.label);
}

// ── Price bar ─────────────────────────────────────────────────────────────────

static std::string pbar(double v, double lo, double hi, const Sig& s, int w = 10) {
    double range  = hi - lo;
    int    filled = (range > 0.0) ? static_cast<int>((v - lo) / range * w + 0.5) : 0;
    filled = std::max(0, std::min(filled, w));
    std::string out(s.color);
    for (int i = 0; i < w; ++i)
        out += (i < filled) ? "█" : (std::string(D) + "░" + R + s.color);
    return out + R;
}

// ── Formatting helpers ────────────────────────────────────────────────────────

static std::string shortTime(const std::string& iso) {
    if (iso.size() < 16) return iso;
    return iso.substr(5, 5) + " " + iso.substr(11, 5);
}

static std::string ff(double v, int prec, const std::string& unit = "") {
    std::ostringstream s;
    s << std::fixed << std::setprecision(prec) << v;
    if (!unit.empty()) s << " " << unit;
    return s.str();
}

// ── Single forecast row ───────────────────────────────────────────────────────

static void forecastRow(const gridguard::ForecastEntry& e,
                         double lo, double hi, bool highlight = false) {
    auto s = sigOf(e.signal);

    std::string timeStr = shortTime(e.time);
    if (highlight) timeStr = c(GN, timeStr);

    std::string solar;
    if      (e.solarKwh >= 100.0) solar = ff(e.solarKwh, 0, "kWh");
    else if (e.solarKwh >=  10.0) solar = ff(e.solarKwh, 1, "kWh");
    else                           solar = ff(e.solarKwh, 2, "kWh");

    double dev = e.savingsVsMedian;
    const char* devColor = dev < -5.0 ? GN : (dev > 10.0 ? RD : G);
    std::string devStr = (dev < 0 ? "" : "+") + ff(dev, 1) + "%";

    std::vector<std::string> cells = {
        c(G, timeStr),
        sigTag(s),
        c(W, ff(e.priceSekKwh, 2, "kr")),
        pbar(e.totalCostSekKwh, lo, hi, s),
        c(G, solar),
        c(devColor, devStr),
    };
    std::cout << trow(cells, FC) << "\n";
}

// ── Forecast view ─────────────────────────────────────────────────────────────

static void showForecast(const std::vector<gridguard::ForecastEntry>& entries,
                         const gridguard::ForecastSummary& summary) {
    if (entries.empty()) {
        std::cerr << c(RD, "no forecast data") << "\n";
        return;
    }

    auto count = [&](const std::string& sig) {
        return static_cast<int>(std::count_if(entries.begin(), entries.end(),
            [&](const gridguard::ForecastEntry& e){ return e.signal == sig; }));
    };

    auto lo_it = std::min_element(entries.begin(), entries.end(),
        [](const gridguard::ForecastEntry& a, const gridguard::ForecastEntry& b){
            return a.totalCostSekKwh < b.totalCostSekKwh; });
    auto hi_it = std::max_element(entries.begin(), entries.end(),
        [](const gridguard::ForecastEntry& a, const gridguard::ForecastEntry& b){
            return a.totalCostSekKwh < b.totalCostSekKwh; });
    double lo = lo_it->totalCostSekKwh;
    double hi = hi_it->totalCostSekKwh;

    // Best BUY windows, cheapest first, then chronological
    static const int MAX_BUY = 5;
    std::vector<const gridguard::ForecastEntry*> buyList;
    for (const auto& e : entries)
        if (e.signal == "BUY") buyList.push_back(&e);
    std::partial_sort(buyList.begin(),
                      buyList.begin() + std::min(MAX_BUY, (int)buyList.size()),
                      buyList.end(),
                      [](const gridguard::ForecastEntry* a, const gridguard::ForecastEntry* b){
                          return a->totalCostSekKwh < b->totalCostSekKwh; });
    buyList.resize(std::min(MAX_BUY, (int)buyList.size()));
    std::sort(buyList.begin(), buyList.end(),
              [](const gridguard::ForecastEntry* a, const gridguard::ForecastEntry* b){
                  return a->time < b->time; });

    int buyCount = count("BUY");
    double totalDev = std::accumulate(entries.begin(), entries.end(), 0.0,
        [](double s, const gridguard::ForecastEntry& e){
            return s + (e.savingsVsMedian < 0.0 ? e.savingsVsMedian : 0.0); });
    double avgDev = buyCount > 0 ? totalDev / buyCount : 0.0;

    // ─────────────────────────────────────────────────────────────────────────
    std::cout << "\n";
    top();
    blank();

    // Header: brand left, live weather right — location/user below
    {
        std::string solar = summary.currentSolarKw > 0.05
                          ? ff(summary.currentSolarKw, 2) + " kW"
                          : "—";
        std::string weather = c(W, ff(summary.currentTempC, 1) + "°C")
                            + c(G, "  ☀ ") + c(W, solar);
        rowLR(c(C, std::string(B) + "GridGuard" + R), weather);
    }
    {
        std::string meta = summary.userId
                         + "  " + summary.region
                         + "  " + summary.location;
        row(c(G, meta));
    }

    blank();
    rule();
    blank();

    // ── Signals ───────────────────────────────────────────────────────────────
    row(c(W, std::string(B) + "Signals" + R));
    blank();
    for (const auto& e : entries)
        forecastRow(e, lo, hi);

    // ── Best BUY windows ──────────────────────────────────────────────────────
    if (!buyList.empty()) {
        blank();
        rule();
        blank();
        std::string hdr = std::string(B) + "Best buy windows" + R;
        if (buyCount > MAX_BUY)
            hdr += c(G, "  " + std::to_string(buyCount) + " total");
        row(c(W, hdr));
        blank();
        for (const auto* e : buyList)
            forecastRow(*e, lo, hi, true);
    }

    // ── Summary ───────────────────────────────────────────────────────────────
    blank();
    rule();
    blank();

    // Signal dot summary:  ● 3 buy  ● 1 sell  ● 0 idle  ● 2 avoid
    {
        std::string line;
        auto dot = [&](const char* color, int n, const std::string& name) {
            if (!line.empty()) line += c(G, "   ");
            line += c(color, "●") + c(G, " " + std::to_string(n) + " " + name);
        };
        dot(GN, count("BUY"),   "buy");
        dot(C,  count("SELL"),  "sell");
        dot(RD, count("AVOID"), "avoid");
        row(line);
    }
    blank();

    // Energy flow
    {
        std::string line =
            c(G, "import  ") + c(W, ff(summary.gridImportKwh,  2) + " kWh") +
            c(G, "   export  ") + c(W, ff(summary.gridExportKwh, 2) + " kWh") +
            c(G, "   cost  ")  + c(W, ff(summary.totalCostSek,   2) + " kr");
        row(line);
    }

    // Best window
    if (lo_it != entries.end()) {
        blank();
        std::string line = c(GN, "▸ " + shortTime(lo_it->time)
                             + "  " + ff(lo_it->totalCostSekKwh, 3) + " kr/kWh");
        if (buyCount > 0)
            line += c(G, "   avg buy " + ff(avgDev, 1) + "% vs median");
        row(line);
    }

    blank();
    bot();
    std::cout << "\n";
}

// ── Schedules view ────────────────────────────────────────────────────────────

static void showSchedules(const std::vector<gridguard::ScheduleEntry>& schedules) {
    std::cout << "\n";
    top();
    blank();
    row(c(C, std::string(B) + "Schedules" + R));
    blank();

    if (schedules.empty()) {
        row(c(G, "No active schedules."));
        blank();
        bot();
        std::cout << "\n";
        return;
    }

    std::vector<gridguard::ScheduleEntry> sorted = schedules;
    std::sort(sorted.begin(), sorted.end(),
        [](const gridguard::ScheduleEntry& a, const gridguard::ScheduleEntry& b){
            return a.scheduledStart < b.scheduledStart; });

    double totalSavings = std::accumulate(sorted.begin(), sorted.end(), 0.0,
        [](double acc, const gridguard::ScheduleEntry& e){ return acc + e.savingsSek; });

    // Column headers
    {
        std::vector<std::string> hdr = {
            c(G, "Load"), c(G, "Start"), c(G, "Dur"),
            c(G, "Cost"), c(G, "Saving"), c(G, "Status")
        };
        std::cout << std::string(D) << trow(hdr, SC) << R << "\n";
    }
    blank();

    for (const auto& s : sorted) {
        const char* stColor = (s.status == "completed") ? GN
                            : (s.status == "pending")   ? YL : C;

        std::string loadId = s.loadId.size() > 10
                           ? s.loadId.substr(0, 9) + "…"
                           : s.loadId;

        std::vector<std::string> cells = {
            c(W, loadId),
            c(G, shortTime(s.scheduledStart)),
            c(G, std::to_string(s.durationMinutes) + "m"),
            c(G, ff(s.estimatedCostSek, 2) + " kr"),
            c(GN, ff(s.savingsSek,      2) + " kr"),
            c(stColor, s.status),
        };
        std::cout << trow(cells, SC) << "\n";
    }

    blank();
    row(c(G, "total savings  ") + c(GN, std::string(B) + ff(totalSavings, 2) + " kr" + R));
    blank();
    bot();
    std::cout << "\n";
}

// ── Usage ─────────────────────────────────────────────────────────────────────

static void printUsage(const char* prog) {
    std::cout << "\n";
    top();
    blank();
    row(c(C, std::string(B) + "GridGuard" + R) + c(G, "  energy management CLI"));
    blank();
    rule();
    blank();
    row(c(G, std::string(prog) + " [--host HOST] [--port PORT] [--token TOKEN] COMMAND"));
    blank();
    row(c(G, "Options"));
    blank();
    row(c(G, "--host   HOST   server hostname  ") + c(W, "default: localhost"));
    row(c(G, "--port   PORT   server port      ") + c(W, "default: 8080"));
    row(c(G, "--token  TOKEN  JWT token        ") + c(W, "or $GRIDGUARD_TOKEN"));
    blank();
    row(c(G, "Commands"));
    blank();
    row(c(C, "health"));
    row(c(G, "  check server reachability"));
    blank();
    row(c(C, "forecast") + c(G, "  [--watch]  [--interval SECS]"));
    row(c(G, "  energy signals and 48h price forecast"));
    blank();
    row(c(C, "config get"));
    row(c(C, "config set") + c(G, "  --lat LAT  --lon LON  --region SE3"));
    row(c(G, "  [--location NAME]  [--solar-area M2]  [--solar-eff EFF]"));
    blank();
    row(c(C, "schedule list"));
    row(c(C, "schedule add") + c(G, "  --load ID  --duration MIN  --power KW"));
    row(c(C, "schedule delete") + c(G, "  ID"));
    blank();
    bot();
    std::cout << "\n";
}

// ── Argument parsing ──────────────────────────────────────────────────────────

static std::map<std::string, std::string> parseArgs(const std::vector<std::string>& args) {
    std::map<std::string, std::string> flags;
    int positional = 0;
    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i].substr(0, 2) == "--") {
            if (i + 1 < args.size() && args[i + 1].substr(0, 2) != "--") {
                const std::string key = args[i]; ++i;
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
                           const std::string& flag, const std::string& def = "") {
    auto it = flags.find(flag);
    return it != flags.end() ? it->second : def;
}

// ── main ──────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    std::vector<std::string> rawArgs(argv + 1, argv + argc);

    if (std::find(rawArgs.begin(), rawArgs.end(), "--help") != rawArgs.end()
     || std::find(rawArgs.begin(), rawArgs.end(), "-h")     != rawArgs.end()) {
        printUsage(argv[0]);
        return 0;
    }

    auto flags = parseArgs(rawArgs);

    std::string host  = getArg(flags, "--host", "localhost");
    int         port  = std::stoi(getArg(flags, "--port", "8080"));

    std::string token = getArg(flags, "--token");
    if (token.empty()) {
        const char* env = std::getenv("GRIDGUARD_TOKEN");
        if (env) token = env;
    }

    std::string command    = getArg(flags, "cmd0");
    std::string subcommand = getArg(flags, "cmd1");
    if (command.empty()) command = "forecast";

    gridguard::GridGuardClient client(host, port, token);

    // ── health ───────────────────────────────────────────────────────────────
    if (command == "health") {
        bool up = client.checkHealth();
        std::cout << "\n";
        top();
        blank();
        if (up)
            row(c(GN, "●  online") + c(G, "   " + host + ":" + std::to_string(port)));
        else
            row(c(RD, "●  offline") + c(G, "   " + host + ":" + std::to_string(port)));
        blank();
        bot();
        std::cout << "\n";
        return up ? 0 : 1;
    }

    // ── forecast ─────────────────────────────────────────────────────────────
    else if (command == "forecast") {
        if (token.empty()) {
            std::cerr << c(RD, "error: --token required") << "\n";
            return 1;
        }

        bool watch_mode   = flags.find("--watch") != flags.end();
        int  interval_sec = std::stoi(getArg(flags, "--interval", "60"));

        if (watch_mode) {
            // Each spinner frame is 3 UTF-8 bytes
            static const char* spin = "⠋⠙⠹⠸⠼⠴⠦⠧⠇⠏";
            int tick = 0;
            while (true) {
                system("clear");

                std::time_t now = std::time(nullptr);
                char tbuf[16];
                std::strftime(tbuf, sizeof(tbuf), "%H:%M:%S", std::localtime(&now));

                int fi = (tick % 10) * 3;
                std::string frame(spin + fi, spin + fi + 3);
                std::cout << c(G, std::string(D) + frame + "  " + tbuf
                                  + "  refresh every " + std::to_string(interval_sec) + "s"
                                  + "  Ctrl+C to quit" + R) << "\n";

                gridguard::ForecastSummary summary;
                auto entries = client.getForecast(summary);

                if (entries.empty()) {
                    std::cerr << c(RD, "failed to fetch forecast") << "\n";
                } else {
                    showForecast(entries, summary);
                    showSchedules(client.getSchedules());
                }

                std::cout << std::flush;
                ++tick;
                std::this_thread::sleep_for(std::chrono::seconds(interval_sec));
            }
        } else {
            gridguard::ForecastSummary summary;
            auto entries = client.getForecast(summary);
            if (entries.empty()) {
                std::cerr << c(RD, "error: no forecast — run: config set --lat ... --lon ... --region SE3") << "\n";
                return 1;
            }
            showForecast(entries, summary);
        }
    }

    // ── config ───────────────────────────────────────────────────────────────
    else if (command == "config") {
        if (token.empty()) {
            std::cerr << c(RD, "error: --token required") << "\n";
            return 1;
        }
        if (subcommand == "get") {
            std::string cfg = client.getUserConfig();
            if (cfg.empty()) {
                std::cerr << c(RD, "no config found — run: config set") << "\n";
                return 1;
            }
            std::cout << cfg << "\n";
        }
        else if (subcommand == "set") {
            std::string latStr = getArg(flags, "--lat");
            std::string lonStr = getArg(flags, "--lon");
            std::string region = getArg(flags, "--region",      "SE3");
            std::string loc    = getArg(flags, "--location",    "");
            double area = std::stod(getArg(flags, "--solar-area",  "0.0"));
            double eff  = std::stod(getArg(flags, "--solar-eff",   "0.0"));
            double load = std::stod(getArg(flags, "--consumption", "0.5"));

            if (latStr.empty() || lonStr.empty()) {
                std::cerr << c(RD, "error: --lat and --lon are required") << "\n";
                return 1;
            }
            if (client.setUserConfig(std::stod(latStr), std::stod(lonStr),
                                     region, loc, area, eff, load))
                std::cout << c(GN, "✓ config saved") << "\n";
            else {
                std::cerr << c(RD, "error: failed to save config") << "\n";
                return 1;
            }
        }
        else {
            std::cerr << c(RD, "unknown subcommand: " + subcommand) << "\n";
            return 1;
        }
    }

    // ── schedule ─────────────────────────────────────────────────────────────
    else if (command == "schedule") {
        if (token.empty()) {
            std::cerr << c(RD, "error: --token required") << "\n";
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
                std::cerr << c(RD, "error: --load, --duration and --power required") << "\n";
                return 1;
            }
            if (client.createSchedule(loadId, duration, power, deadline))
                std::cout << c(GN, "✓ schedule created") << "\n";
            else
                return 1;
        }
        else if (subcommand == "delete") {
            std::string scheduleId = getArg(flags, "cmd2");
            if (scheduleId.empty()) {
                std::cerr << c(RD, "error: schedule delete <ID>") << "\n";
                return 1;
            }
            if (client.deleteSchedule(scheduleId))
                std::cout << c(GN, "✓ schedule " + scheduleId + " cancelled") << "\n";
            else
                return 1;
        }
        else {
            std::cerr << c(RD, "unknown subcommand: " + subcommand) << "\n";
            return 1;
        }
    }

    else {
        std::cerr << c(RD, "unknown command: " + command) << "\n";
        printUsage(argv[0]);
        return 1;
    }

    return 0;
}
