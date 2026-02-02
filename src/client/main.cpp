#include "TCPClient.hpp"
#include "HTTPClient.hpp"
#include "Logger.h"
#include "config.h"
#include <iostream>

int main()
{

    // TCPClient client;
    // client.connect(SERVER_HOST, SERVER_PORT);

    Logger_Initiate("logs/client.log", LOG_LEVEL_INFO);

    // Test av HTTPClient mot SMHI API - tas bort efter WeatherParser impl. 
    HTTPClient httpClient;
    int result = httpClient.get("opendata-download-metfcst.smhi.se", "80", "/api/category/pmp3g/version/2/geotype/point/lon/18.07/lat/59.33/data.json");

    if (result == 0 && httpClient.getStatusCode() == 200) {
        std::cout << "HTTPClient working: " << httpClient.getBodyLength() << " bytes received\n";
    }

    Logger_Shutdown();
    return 0;
}
