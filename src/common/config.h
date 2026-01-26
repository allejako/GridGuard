#ifndef _CONFIG_H_
#define _CONFIG_H_

// ============== SERVER ==============
#define SERVER_PORT "8080"

// ============== THREAD POOL ==============
#define MAX_THREADS             20
#define MAX_CLIENTS_PER_THREAD  50
#define MAX_TOTAL_CLIENTS       (MAX_THREADS * MAX_CLIENTS_PER_THREAD)

// ============== CLIENT BUFFERS ==============
#define CLIENT_BUFFER_SIZE      1024

// ============== TIMEOUTS (seconds) ==============
#define SELECT_TIMEOUT_SEC      1
#define CLIENT_IDLE_TIMEOUT     300  // 5 minutes

// ============== LOGGING ==============
#define LOG_LEVEL_DEBUG         0
#define LOG_LEVEL_INFO          1
#define LOG_LEVEL_WARN          2
#define LOG_LEVEL_ERROR         3
#define LOG_LEVEL               LOG_LEVEL_INFO

#endif // _CONFIG_H_
