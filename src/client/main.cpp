#include "TokenManager.hpp"
#include "GridGuardClient.hpp"

#include <iostream>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <unistd.h>

// Terminal colors
namespace C {
    constexpr auto Reset  = "\033[0m";
    constexpr auto Bold   = "\033[1m";
    constexpr auto Dim    = "\033[2m";
    constexpr auto Cyan   = "\033[36m";
    constexpr auto Yellow = "\033[33m";
    constexpr auto Gray   = "\033[90m";
}

// Helpers
static std::string timeOf(const std::string& ts)
{
    auto p = ts.find('T');
    return (p != std::string::npos) ? ts.substr(p + 1, 5) : ts;
}

static std::string solarBar(double kwh, double maxKwh, int width = 8)
{
    std::string bar;
    int filled = (maxKwh > 0.01)
        ? std::clamp(static_cast<int>(std::round(kwh / maxKwh * width)), 0, width)
        : 0;
    for (int i = 0; i < filled; i++)        bar += "█";
    for (int i = filled; i < width; i++)    bar += "░";
    return bar;
}

// Logo
static void printLogo()
{
    std::cout << C::Bold <<
" ██████  ██████  ██ ██████   ██████  ██    ██  █████  ██████  ██████  \n"
"██       ██   ██ ██ ██   ██ ██       ██    ██ ██   ██ ██   ██ ██   ██ \n"
"██   ███ ██████  ██ ██   ██ ██   ███ ██    ██ ███████ ██████  ██   ██ \n"
"██    ██ ██   ██ ██ ██   ██ ██    ██ ██    ██ ██   ██ ██   ██ ██   ██ \n"
" ██████  ██   ██ ██ ██████   ██████   ██████  ██   ██ ██   ██ ██████  \n"
    << C::Reset << C::Gray
    << "  Energioptimering för solpaneler  ·  v1.0\n\n"
    << C::Reset;
}

// Commands

static int cmdLogin(const char* rawToken)
{
    try {
        TokenManager tm;
        tm.save(rawToken);
        std::cout << "  Token sparad i ~/.gridguard/token\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "  Fel: " << e.what() << "\n";
        return 1;
    }
}

static int cmdForecast()
{
    try {
        TokenManager tm;
        std::string  token = tm.load();

        GridGuardClient client("localhost", "8080");

        std::cout << C::Dim << "  Hämtar prognos..." << C::Reset << std::flush;
        ForecastResponse resp = client.forecast(token);
        std::cout << "\r                        \r";

        double maxSolar = 0;
        for (const auto& e : resp.entries)
            maxSolar = std::max(maxSolar, e.solarKwh);

        printLogo();
        std::cout << "  " << C::Bold << resp.location << C::Reset
                  << C::Gray << "  ·  " << C::Reset << resp.region << "\n";
        std::cout << C::Gray
                  << "  ─────────────────────────────────────────────────────────────\n"
                  << "  Tid     Signal    Pris kr/kWh    Sol\n"
                  << "  ─────────────────────────────────────────────────────────────\n"
                  << C::Reset;

        for (const auto& e : resp.entries) {
            std::ostringstream row;
            row << "  " << timeOf(e.time) << "   "
                << std::left << std::setw(6) << e.signal << "  "
                << std::fixed << std::setprecision(4) << e.priceSek << "         "
                << solarBar(e.solarKwh, maxSolar) << "  "
                << std::setprecision(2) << e.solarKwh << " kWh";

            if (e.signal == "SELL")
                std::cout << C::Yellow << row.str() << C::Reset << "\n";
            else if (e.signal == "BUY")
                std::cout << C::Cyan   << row.str() << C::Reset << "\n";
            else
                std::cout << C::Gray   << row.str() << C::Reset << "\n";
        }

        std::cout << C::Gray
                  << "  ─────────────────────────────────────────────────────────────\n"
                  << C::Reset;
        std::cout << std::fixed
                  << "  Import "  << C::Bold << std::setprecision(1) << resp.gridImportKwh << " kWh" << C::Reset
                  << C::Gray << "  ·  " << C::Reset
                  << "Export "   << C::Bold << std::setprecision(1) << resp.gridExportKwh << " kWh" << C::Reset
                  << C::Gray << "  ·  " << C::Reset
                  << "Kostnad "  << C::Bold << "~" << std::setprecision(2) << resp.totalCostSek << " kr" << C::Reset
                  << "\n\n";

        return 0;

    } catch (const std::exception& e) {
        std::cout << "\r                        \r";
        std::cerr << "\n  Fel: " << e.what() << "\n\n";
        return 1;
    }
}

static int cmdConfig()
{
    try {
        TokenManager tm;
        if (!tm.exists()) {
            std::cerr << "  Ingen token. Kör: gridguard login <token>\n";
            return 1;
        }
        std::string url = "http://localhost:8080/config#token=" + tm.load();

        // Try xdg-open (Linux) then open (macOS)
        int ret = system(("xdg-open '" + url + "' >/dev/null 2>&1 &").c_str());
        if (ret != 0)
            ret = system(("open '" + url + "' >/dev/null 2>&1 &").c_str());

        if (ret == 0)
            std::cout << "  Öppnade webbläsaren till konfigurationssidan.\n";
        else
            std::cout << "  Öppna manuellt: " << url << "\n";

        return 0;

    } catch (const std::exception& e) {
        std::cerr << "  Fel: " << e.what() << "\n";
        return 1;
    }
}

static void printUsage()
{
    printLogo();
    std::cout
        << "  Användning:\n\n"
        << "    gridguard login <token>    Spara JWT till ~/.gridguard/token\n"
        << "    gridguard forecast         Hämta och visa energiprognos\n"
        << "    gridguard config           Öppna konfiguration i webbläsaren\n"
        << "\n";
}

// Main entry point

int main(int argc, char* argv[])
{
    if (argc < 2) {
        printUsage();
        return 1;
    }

    std::string cmd = argv[1];

    if (cmd == "login") {
        if (argc < 3) {
            std::cerr << "  Användning: gridguard login <token>\n";
            return 1;
        }
        return cmdLogin(argv[2]);
    }
    if (cmd == "forecast") return cmdForecast();
    if (cmd == "config")   return cmdConfig();

    printUsage();
    return 1;
}
