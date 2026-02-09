#ifndef _CONFIG_H_
#define _CONFIG_H_

//  SERVER 
#define SERVER_PORT "8080"
#define SERVER_HOST "localhost"

// ============== WEATHER API =====================
#define WEATHER_API_BASE_URL    "https://api.open-meteo.com/v1/forecast"
#define WEATHER_LAT             "59.33"   // Stockholm
#define WEATHER_LON             "18.07"
#define WEATHER_TIMEZONE        "Europe/Stockholm"

// ============== SPOT PRICE API ==================
#define SPOTPRICE_API_BASE_URL  "https://www.elprisetjustnu.se/api/v1/prices"
#define SPOTPRICE_REGION        "SE3"  // SE1, SE2, SE3, SE4

// ============== THREAD POOL =====================
#define MAX_THREADS             20
#define MAX_CLIENTS_PER_THREAD  50
#define MAX_TOTAL_CLIENTS       (MAX_THREADS * MAX_CLIENTS_PER_THREAD)

// ============== CLIENT BUFFERS ==================
#define CLIENT_BUFFER_SIZE      1024

// ============== TIMEOUTS (seconds) ==============
#define SELECT_TIMEOUT_SEC      1
#define CLIENT_IDLE_TIMEOUT     300

// ============== HTTP CLIENT =====================
#define HTTP_TIMEOUT            30
#define HTTP_MAX_RETRIES        3
#define HTTP_BUFFER_SIZE        8192

#endif // _CONFIG_H_
