# Hybrid Arkitektur - Konkret Implementation

**Baserat på Kursplanering_Arkitektur_Analys.md**

Detta dokument visar konkret kod för hur vi skulle migrera från nuvarande thread-baserade arkitektur till hybrid process+thread arkitektur för 100% kursbegrepp-täckning.

---

## Nuvarande arkitektur (förenklad)

```
HTTP Server Thread Pool
    ↓ (WorkRequest via Queue)
FetchWorker Thread
    ↓ (FetchResult via Queue)
ParseWorker Thread
    ↓ (ParseResult via Queue)
ComputeWorker Thread
    ↓ (WorkCompletion signal)
HTTP Response
```

**IPC:** Mutex-baserade Queue, SharedCache (shm_open), WorkCompletion (mutex+cond)

---

## Hybrid arkitektur (målbild)

```
Main Process (HTTP Server)
    ↓ (Anonymous pipe via stdin)
Fetch Process (fork+exec)
    ↓ (Named pipe/FIFO)
Parse Process (fork+exec)
    ↓ (Unix domain socket)
Compute Thread (i main process)
    ↓ (WorkCompletion - mutex+cond)
HTTP Response
```

**IPC-täckning:**
- ✅ Anonymous pipes (pipe + dup2)
- ✅ Named pipes (FIFO)
- ✅ Unix domain sockets
- ✅ Shared memory (shm_open + mmap)
- ✅ POSIX semaphores (sem_open)
- ✅ Mutex + condition variables

---

## 1. Main Process - HTTP Server med Fork/Exec

### Nuvarande: src/application/main.c

```c
// Nuvarande implementation (förenklad)
int main(int argc, char *argv[]) {
    GridGuard app;
    GridGuard_Initiate(&app);

    // Startar tre worker threads
    pthread_create(&app.fetchThread, NULL, FetchWorker_Run, &app);
    pthread_create(&app.parseThread, NULL, ParseWorker_Run, &app);
    pthread_create(&app.computeThread, NULL, ComputeWorker_Run, &app);

    // HTTP server loop
    HTTPServer_Run(&httpServer);

    // Cleanup
    GridGuard_Shutdown(&app);
}
```

### Hybrid: src/application/main_hybrid.c

```c
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>

#include "GridGuard.h"
#include "HTTPServer.h"
#include "ComputeWorker.h"
#include "Logger.h"

// Global process IDs for cleanup
static pid_t g_fetchPid = -1;
static pid_t g_parsePid = -1;
static int g_requestPipeFd = -1;  // Write end of anonymous pipe

// Signal handler for graceful shutdown
static void SignalHandler(int sig) {
    LOG_INFO("Main: Received signal %d, shutting down...", sig);

    // Kill child processes
    if (g_fetchPid > 0) {
        kill(g_fetchPid, SIGTERM);
    }
    if (g_parsePid > 0) {
        kill(g_parsePid, SIGTERM);
    }

    // Close pipe
    if (g_requestPipeFd >= 0) {
        close(g_requestPipeFd);
    }
}

int main(int argc, char *argv[]) {
    LOG_INFO("Main: Starting GridGuard Hybrid Architecture");

    // Setup signal handlers
    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);

    // Create shared memory caches (accessible by all processes)
    SharedCache weatherCache, priceCache;
    SharedCache_Create(&weatherCache, "/gridguard_weather", 900);
    SharedCache_Create(&priceCache, "/gridguard_prices", 900);

    // =================================================================
    // STEP 1: Create anonymous pipe for HTTP -> Fetch communication
    // (Vecka 4: pipe() + dup2())
    // =================================================================
    int requestPipe[2];
    if (pipe(requestPipe) != 0) {
        LOG_ERROR("Main: Failed to create request pipe");
        return 1;
    }

    LOG_INFO("Main: Created anonymous pipe (HTTP -> Fetch)");

    // =================================================================
    // STEP 2: Create named pipe (FIFO) for Fetch -> Parse communication
    // (Vecka 4: mkfifo())
    // =================================================================
    const char *fetchToParsefifo = "/tmp/gridguard_fetch_to_parse.fifo";
    unlink(fetchToParsefifo);  // Remove old FIFO if exists
    if (mkfifo(fetchToParsefifo, 0666) != 0) {
        LOG_ERROR("Main: Failed to create FIFO");
        return 1;
    }
    LOG_INFO("Main: Created FIFO at %s (Fetch -> Parse)", fetchToParsefifo);

    // =================================================================
    // STEP 3: Create Unix domain socket path for Parse -> Compute
    // (Vecka 5: Unix domain sockets - will be created by Parse process)
    // =================================================================
    const char *parseToComputeSocket = "/tmp/gridguard_parse_to_compute.sock";
    unlink(parseToComputeSocket);  // Remove old socket if exists
    LOG_INFO("Main: Will use Unix socket at %s (Parse -> Compute)", parseToComputeSocket);

    // =================================================================
    // STEP 4: Fork and exec Fetch process
    // (Vecka 1: fork() + exec() + wait())
    // =================================================================
    g_fetchPid = fork();
    if (g_fetchPid < 0) {
        LOG_ERROR("Main: Failed to fork Fetch process");
        return 1;
    }

    if (g_fetchPid == 0) {
        // CHILD PROCESS: Fetch
        LOG_INFO("Fetch Process: Starting (PID=%d)", getpid());

        // Close write end of request pipe
        close(requestPipe[1]);

        // Redirect stdin to read end of request pipe
        // (This demonstrates dup2() - Vecka 4)
        if (dup2(requestPipe[0], STDIN_FILENO) < 0) {
            LOG_ERROR("Fetch: Failed to dup2 stdin");
            exit(1);
        }
        close(requestPipe[0]);

        // Pass FIFO path as command-line argument
        execl("./bin/GridGuard-fetcher",
              "GridGuard-fetcher",
              fetchToParsefifo,  // Arg 1: FIFO path to write to
              NULL);

        // If exec fails
        LOG_ERROR("Fetch: exec failed");
        exit(1);
    }

    // PARENT: Close read end of request pipe
    close(requestPipe[0]);
    g_requestPipeFd = requestPipe[1];  // Save write end for HTTP handlers
    LOG_INFO("Main: Forked Fetch process (PID=%d)", g_fetchPid);

    // =================================================================
    // STEP 5: Fork and exec Parse process
    // (Vecka 1: fork() + exec())
    // =================================================================
    g_parsePid = fork();
    if (g_parsePid < 0) {
        LOG_ERROR("Main: Failed to fork Parse process");
        kill(g_fetchPid, SIGTERM);
        return 1;
    }

    if (g_parsePid == 0) {
        // CHILD PROCESS: Parse
        LOG_INFO("Parse Process: Starting (PID=%d)", getpid());

        // Pass FIFO path (read from) and socket path (write to) as arguments
        execl("./bin/GridGuard-parser",
              "GridGuard-parser",
              fetchToParsefifo,        // Arg 1: FIFO path to read from
              parseToComputeSocket,    // Arg 2: Unix socket path to create
              NULL);

        // If exec fails
        LOG_ERROR("Parse: exec failed");
        exit(1);
    }

    LOG_INFO("Main: Forked Parse process (PID=%d)", g_parsePid);

    // Give child processes time to set up
    sleep(1);

    // =================================================================
    // STEP 6: Start Compute worker thread in main process
    // (Vecka 2: pthread_create() - mixing processes and threads)
    // =================================================================
    pthread_t computeThread;
    ComputeThreadArgs computeArgs = {
        .socketPath = parseToComputeSocket,
        .isRunning = true
    };

    if (pthread_create(&computeThread, NULL, ComputeWorker_Run_Hybrid, &computeArgs) != 0) {
        LOG_ERROR("Main: Failed to create Compute thread");
        kill(g_fetchPid, SIGTERM);
        kill(g_parsePid, SIGTERM);
        return 1;
    }
    LOG_INFO("Main: Started Compute worker thread");

    // =================================================================
    // STEP 7: Run HTTP server in main process
    // =================================================================
    HTTPServer httpServer;
    HTTPServer_Initiate(&httpServer, 8080);

    // HTTP server will write WorkRequests to g_requestPipeFd
    // (see HTTP handler code below)
    httpServer.requestPipeFd = g_requestPipeFd;

    LOG_INFO("Main: Starting HTTP server on port 8080");
    HTTPServer_Run(&httpServer);  // Blocking

    // =================================================================
    // STEP 8: Cleanup - wait for child processes
    // (Vecka 1: waitpid())
    // =================================================================
    LOG_INFO("Main: Shutting down, waiting for child processes...");

    computeArgs.isRunning = false;
    pthread_join(computeThread, NULL);

    kill(g_fetchPid, SIGTERM);
    kill(g_parsePid, SIGTERM);

    int status;
    waitpid(g_fetchPid, &status, 0);
    LOG_INFO("Main: Fetch process exited with status %d", WEXITSTATUS(status));

    waitpid(g_parsePid, &status, 0);
    LOG_INFO("Main: Parse process exited with status %d", WEXITSTATUS(status));

    // Cleanup IPC resources
    close(g_requestPipeFd);
    unlink(fetchToParsefifo);
    unlink(parseToComputeSocket);

    SharedCache_Destroy(&weatherCache);
    SharedCache_Destroy(&priceCache);

    LOG_INFO("Main: Shutdown complete");
    return 0;
}
```

---

## 2. Fetch Process - Läser från stdin (pipe), skriver till FIFO

### Ny fil: src/application/processes/fetcher_main.c

```c
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>

#include "Fetcher.h"
#include "SharedCache.h"
#include "APIEndpoints.h"
#include "Logger.h"

// Samma structs som tidigare, men kommuniceras via pipes
typedef struct {
    char userId[64];
    char location[64];
    char lat[16];
    char lon[16];
    char region[16];
    double solarAreaM2;
    double solarEfficiency;
    double consumptionKwh;
    int clientFd;  // Not used in this design, kept for compatibility
} WorkRequest;

typedef struct {
    char userId[64];
    char location[64];
    char region[16];
    double solarAreaM2;
    double solarEfficiency;
    double consumptionKwh;
    char openMeteoJson[32768];
    char priceJson[16384];
} FetchResult;

int main(int argc, char *argv[]) {
    if (argc < 2) {
        LOG_ERROR("Fetcher: Missing FIFO path argument");
        return 1;
    }

    const char *fifoPath = argv[1];
    LOG_INFO("Fetcher: Starting (PID=%d, FIFO=%s)", getpid(), fifoPath);

    // Initialize Fetcher service
    Fetcher fetcher;
    if (Fetcher_Initiate(&fetcher) != 0) {
        LOG_ERROR("Fetcher: Failed to initialize");
        return 1;
    }

    // Open shared memory caches (created by main process)
    SharedCache weatherCache, priceCache;
    SharedCache_Open(&weatherCache, "/gridguard_weather");
    SharedCache_Open(&priceCache, "/gridguard_prices");

    // =========================================================
    // STEP 1: Open FIFO for writing (Fetch -> Parse)
    // (Vecka 4: Named pipes - will block until Parse opens read end)
    // =========================================================
    int fifoFd = open(fifoPath, O_WRONLY);
    if (fifoFd < 0) {
        LOG_ERROR("Fetcher: Failed to open FIFO %s", fifoPath);
        return 1;
    }
    LOG_INFO("Fetcher: Opened FIFO for writing");

    // =========================================================
    // STEP 2: Main loop - read WorkRequests from stdin (anonymous pipe)
    // (Vecka 4: pipe communication)
    // =========================================================
    while (1) {
        WorkRequest request;

        // Read from stdin (redirected from anonymous pipe)
        ssize_t bytesRead = read(STDIN_FILENO, &request, sizeof(request));

        if (bytesRead == 0) {
            LOG_INFO("Fetcher: stdin closed, exiting");
            break;
        }

        if (bytesRead != sizeof(request)) {
            LOG_ERROR("Fetcher: Partial read from stdin (%zd bytes)", bytesRead);
            continue;
        }

        LOG_INFO("Fetcher: Got request for %s/%s", request.userId, request.region);

        // Prepare result
        FetchResult result = {0};
        strncpy(result.userId, request.userId, sizeof(result.userId) - 1);
        strncpy(result.location, request.location, sizeof(result.location) - 1);
        strncpy(result.region, request.region, sizeof(result.region) - 1);
        result.solarAreaM2 = request.solarAreaM2;
        result.solarEfficiency = request.solarEfficiency;
        result.consumptionKwh = request.consumptionKwh;

        // Fetch weather data (same logic as FetchWorker.c)
        char weatherKey[256];
        snprintf(weatherKey, sizeof(weatherKey), "openmeteo_%s_%s", request.lat, request.lon);

        if (SharedCache_Lookup(&weatherCache, weatherKey,
                              result.openMeteoJson, sizeof(result.openMeteoJson)) != 0) {
            // Cache miss - fetch from API
            char openMeteoUrl[512];
            BuildOpenMeteoApiUrl(openMeteoUrl, sizeof(openMeteoUrl), request.lat, request.lon);

            FetchResponse omResp;
            if (Fetcher_Fetch(&fetcher, openMeteoUrl, &omResp) == 0) {
                strncpy(result.openMeteoJson, omResp.data, sizeof(result.openMeteoJson) - 1);
                SharedCache_Store(&weatherCache, weatherKey, omResp.data);
                Fetcher_FreeResponse(&omResp);
                LOG_INFO("Fetcher: Fetched Open-Meteo data");
            }
        } else {
            LOG_INFO("Fetcher: Weather cache HIT");
        }

        // Fetch price data
        char priceKey[256];
        time_t now = time(NULL);
        struct tm today;
        localtime_r(&now, &today);
        snprintf(priceKey, sizeof(priceKey), "%s_%04d-%02d-%02d",
                 request.region, today.tm_year + 1900, today.tm_mon + 1, today.tm_mday);

        if (SharedCache_Lookup(&priceCache, priceKey,
                              result.priceJson, sizeof(result.priceJson)) != 0) {
            // Cache miss
            char priceUrl[256];
            BuildSpotPriceApiUrl(priceUrl, sizeof(priceUrl), request.region, NULL);

            FetchResponse priceResp;
            if (Fetcher_Fetch(&fetcher, priceUrl, &priceResp) == 0) {
                strncpy(result.priceJson, priceResp.data, sizeof(result.priceJson) - 1);
                SharedCache_Store(&priceCache, priceKey, priceResp.data);
                Fetcher_FreeResponse(&priceResp);
                LOG_INFO("Fetcher: Fetched price data");
            }
        } else {
            LOG_INFO("Fetcher: Price cache HIT");
        }

        // =========================================================
        // STEP 3: Write result to FIFO (Fetch -> Parse)
        // (Vecka 4: Named pipe write)
        // =========================================================
        ssize_t written = write(fifoFd, &result, sizeof(result));
        if (written != sizeof(result)) {
            LOG_ERROR("Fetcher: Failed to write to FIFO");
            break;
        }

        LOG_INFO("Fetcher: Wrote FetchResult to FIFO (%zd bytes)", written);
    }

    // Cleanup
    close(fifoFd);
    SharedCache_Close(&weatherCache);
    SharedCache_Close(&priceCache);
    Fetcher_Shutdown(&fetcher);

    LOG_INFO("Fetcher: Exiting");
    return 0;
}
```

---

## 3. Parse Process - Läser från FIFO, skriver till Unix socket

### Ny fil: src/application/processes/parser_main.c

```c
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "Parser.h"
#include "Logger.h"
#include "OpenMeteoResponse.h"
#include "ElprisetResponse.h"
#include "ForecastData.h"

// Same as before
typedef struct {
    char userId[64];
    char location[64];
    char region[16];
    double solarAreaM2;
    double solarEfficiency;
    double consumptionKwh;
    char openMeteoJson[32768];
    char priceJson[16384];
} FetchResult;

typedef struct {
    char userId[64];
    char location[64];
    char region[16];
    double solarAreaM2;
    double solarEfficiency;
    double consumptionKwh;
    ForecastData forecastData;
} ParseResult;

int main(int argc, char *argv[]) {
    if (argc < 3) {
        LOG_ERROR("Parser: Missing arguments (FIFO path, socket path)");
        return 1;
    }

    const char *fifoPath = argv[1];
    const char *socketPath = argv[2];
    LOG_INFO("Parser: Starting (PID=%d, FIFO=%s, Socket=%s)", getpid(), fifoPath, socketPath);

    // Initialize Parser service
    Parser parser;
    if (Parser_Initiate(&parser) != 0) {
        LOG_ERROR("Parser: Failed to initialize");
        return 1;
    }

    // =========================================================
    // STEP 1: Open FIFO for reading (Fetch -> Parse)
    // (Vecka 4: Named pipe - will block until Fetch opens write end)
    // =========================================================
    int fifoFd = open(fifoPath, O_RDONLY);
    if (fifoFd < 0) {
        LOG_ERROR("Parser: Failed to open FIFO %s", fifoPath);
        return 1;
    }
    LOG_INFO("Parser: Opened FIFO for reading");

    // =========================================================
    // STEP 2: Create Unix domain socket server (Parse -> Compute)
    // (Vecka 5: socket(), bind(), listen())
    // =========================================================
    int serverSocket = socket(AF_UNIX, SOCK_STREAM, 0);
    if (serverSocket < 0) {
        LOG_ERROR("Parser: Failed to create Unix socket");
        return 1;
    }

    struct sockaddr_un serverAddr = {0};
    serverAddr.sun_family = AF_UNIX;
    strncpy(serverAddr.sun_path, socketPath, sizeof(serverAddr.sun_path) - 1);

    unlink(socketPath);  // Remove old socket
    if (bind(serverSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        LOG_ERROR("Parser: Failed to bind Unix socket");
        return 1;
    }

    if (listen(serverSocket, 5) < 0) {
        LOG_ERROR("Parser: Failed to listen on Unix socket");
        return 1;
    }

    LOG_INFO("Parser: Unix domain socket server listening at %s", socketPath);

    // =========================================================
    // STEP 3: Main loop - read from FIFO, parse, send to socket
    // =========================================================
    while (1) {
        FetchResult fetchResult;

        // Read from FIFO (Vecka 4: Named pipe read - blocks until data available)
        ssize_t bytesRead = read(fifoFd, &fetchResult, sizeof(fetchResult));

        if (bytesRead == 0) {
            LOG_INFO("Parser: FIFO closed, exiting");
            break;
        }

        if (bytesRead != sizeof(fetchResult)) {
            LOG_ERROR("Parser: Partial read from FIFO (%zd bytes)", bytesRead);
            continue;
        }

        LOG_INFO("Parser: Got FetchResult for %s/%s", fetchResult.userId, fetchResult.region);

        // Parse data (same logic as ParseWorker.c)
        OpenMeteoResponse omData = {0};
        ElprisetResponse elprisetData = {0};

        if (strlen(fetchResult.openMeteoJson) > 0) {
            Parser_ParseOpenMeteo(&parser, fetchResult.openMeteoJson, &omData);
        }

        if (strlen(fetchResult.priceJson) > 0) {
            Parser_ParseElpriset(&parser, fetchResult.priceJson, &elprisetData);
        }

        // Build ParseResult
        ParseResult parseResult = {0};
        strncpy(parseResult.userId, fetchResult.userId, sizeof(parseResult.userId) - 1);
        strncpy(parseResult.location, fetchResult.location, sizeof(parseResult.location) - 1);
        strncpy(parseResult.region, fetchResult.region, sizeof(parseResult.region) - 1);
        parseResult.solarAreaM2 = fetchResult.solarAreaM2;
        parseResult.solarEfficiency = fetchResult.solarEfficiency;
        parseResult.consumptionKwh = fetchResult.consumptionKwh;

        // Convert to ForecastData (same as ParseWorker.c)
        // ... (omitted for brevity - same logic as ParseWorker_Run)

        LOG_INFO("Parser: Parsed forecast data (%d entries)", parseResult.forecastData.count);

        // =========================================================
        // STEP 4: Send ParseResult to Compute via Unix socket
        // (Vecka 5: accept(), write() on Unix socket)
        // =========================================================

        // Accept connection from Compute thread (blocking)
        int clientSocket = accept(serverSocket, NULL, NULL);
        if (clientSocket < 0) {
            LOG_ERROR("Parser: Failed to accept connection");
            continue;
        }

        LOG_INFO("Parser: Accepted connection from Compute thread");

        // Send ParseResult to Compute
        ssize_t written = write(clientSocket, &parseResult, sizeof(parseResult));
        if (written != sizeof(parseResult)) {
            LOG_ERROR("Parser: Failed to write to Unix socket");
        } else {
            LOG_INFO("Parser: Sent ParseResult to Compute (%zd bytes)", written);
        }

        close(clientSocket);
    }

    // Cleanup
    close(fifoFd);
    close(serverSocket);
    unlink(socketPath);
    Parser_Shutdown(&parser);

    LOG_INFO("Parser: Exiting");
    return 0;
}
```

---

## 4. Compute Thread - Unix socket client (i main process)

### Modifierad: src/application/workers/ComputeWorker_Hybrid.c

```c
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "ComputeWorker.h"
#include "Compute.h"
#include "WorkCompletion.h"
#include "Logger.h"

typedef struct {
    const char *socketPath;
    bool isRunning;
} ComputeThreadArgs;

typedef struct {
    char userId[64];
    char location[64];
    char region[16];
    double solarAreaM2;
    double solarEfficiency;
    double consumptionKwh;
    ForecastData forecastData;
} ParseResult;

void *ComputeWorker_Run_Hybrid(void *arg) {
    ComputeThreadArgs *args = (ComputeThreadArgs*)arg;
    LOG_INFO("ComputeWorker: Thread started (Unix socket client)");

    Compute compute;
    Compute_Initiate(&compute);

    while (args->isRunning) {
        // =========================================================
        // STEP 1: Connect to Parse process via Unix socket
        // (Vecka 5: socket(), connect() - client side)
        // =========================================================
        int clientSocket = socket(AF_UNIX, SOCK_STREAM, 0);
        if (clientSocket < 0) {
            LOG_ERROR("ComputeWorker: Failed to create socket");
            sleep(1);
            continue;
        }

        struct sockaddr_un serverAddr = {0};
        serverAddr.sun_family = AF_UNIX;
        strncpy(serverAddr.sun_path, args->socketPath, sizeof(serverAddr.sun_path) - 1);

        if (connect(clientSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
            LOG_ERROR("ComputeWorker: Failed to connect to Unix socket %s", args->socketPath);
            close(clientSocket);
            sleep(1);
            continue;
        }

        LOG_INFO("ComputeWorker: Connected to Parse process via Unix socket");

        // =========================================================
        // STEP 2: Read ParseResult from Unix socket
        // (Vecka 5: read() on connected Unix socket)
        // =========================================================
        ParseResult parseResult;
        ssize_t bytesRead = read(clientSocket, &parseResult, sizeof(parseResult));

        if (bytesRead == 0) {
            LOG_INFO("ComputeWorker: Socket closed");
            close(clientSocket);
            break;
        }

        if (bytesRead != sizeof(parseResult)) {
            LOG_ERROR("ComputeWorker: Partial read from socket (%zd bytes)", bytesRead);
            close(clientSocket);
            continue;
        }

        close(clientSocket);

        LOG_INFO("ComputeWorker: Got ParseResult for %s/%s",
                 parseResult.userId, parseResult.region);

        // =========================================================
        // STEP 3: Compute energy plan (same as before)
        // =========================================================
        EnergyData plan;
        if (Compute_GenerateEnergyPlan(&compute,
                                       &parseResult.forecastData,
                                       parseResult.solarAreaM2,
                                       parseResult.solarEfficiency,
                                       parseResult.consumptionKwh,
                                       &plan) != 0) {
            LOG_ERROR("ComputeWorker: Failed to generate energy plan");
            continue;
        }

        LOG_INFO("ComputeWorker: Generated energy plan (%d entries)", plan.count);

        // =========================================================
        // STEP 4: Signal completion back to HTTP thread
        // (Vecka 3: pthread_cond_signal - samma som innan)
        // =========================================================
        // NOTE: I hybrid-modellen behöver vi ha en mapping från
        // userId -> WorkCompletion, eftersom vi inte längre har
        // en direkt pekare i request-strukturen.
        // Detta kan lösas med en global hash-map skyddad av mutex.

        // For now, simplified - assume we have a way to get WorkCompletion
        // WorkCompletion *completion = FindCompletionByUserId(parseResult.userId);
        // if (completion) {
        //     char json[8192];
        //     SerializeEnergyPlan(&plan, json, sizeof(json));
        //     WorkCompletion_Signal(completion, json);
        // }
    }

    Compute_Shutdown(&compute);
    LOG_INFO("ComputeWorker: Thread exiting");
    return NULL;
}
```

---

## 5. HTTP Handler - Skriver WorkRequest till pipe

### Modifierad: src/application/network/HTTPServer.c (fragment)

```c
// HTTP request handler (körs i ThreadPool thread)
void HandleForecastRequest(HTTPRequest *req, HTTPResponse *resp, void *userData) {
    HTTPServer *server = (HTTPServer*)userData;

    // Parse request, validate JWT, get user config, etc.
    // ... (samma som innan)

    // Build WorkRequest
    WorkRequest request = {
        .clientFd = -1,  // Not used in hybrid design
        // ... populate fields ...
    };

    strncpy(request.userId, userId, sizeof(request.userId) - 1);
    strncpy(request.location, location, sizeof(request.location) - 1);
    // ... etc ...

    // =========================================================
    // NEW: Create WorkCompletion channel for this request
    // (Vecka 3: mutex + condition variable)
    // =========================================================
    WorkCompletion completion;
    WorkCompletion_Init(&completion);

    // Store completion channel in global map (userId -> WorkCompletion*)
    // so Compute thread can signal it later
    RegisterCompletion(userId, &completion);

    // =========================================================
    // NEW: Write WorkRequest to anonymous pipe (HTTP -> Fetch)
    // (Vecka 4: write() to pipe)
    // =========================================================
    pthread_mutex_lock(&server->pipeMutex);
    ssize_t written = write(server->requestPipeFd, &request, sizeof(request));
    pthread_mutex_unlock(&server->pipeMutex);

    if (written != sizeof(request)) {
        LOG_ERROR("HTTP: Failed to write to request pipe");
        SendErrorResponse(resp, 500, "Internal error");
        return;
    }

    LOG_INFO("HTTP: Wrote WorkRequest to pipe for %s", userId);

    // =========================================================
    // Wait for Compute thread to signal completion
    // (Vecka 3: pthread_cond_wait - samma som innan)
    // =========================================================
    if (WorkCompletion_Wait(&completion, 30000) != 0) {
        LOG_ERROR("HTTP: Timeout waiting for result");
        SendErrorResponse(resp, 504, "Request timeout");
        UnregisterCompletion(userId);
        return;
    }

    // Get result from WorkCompletion
    const char *jsonResult = WorkCompletion_GetResult(&completion);

    // Send HTTP response
    resp->statusCode = 200;
    resp->body = strdup(jsonResult);
    resp->bodyLen = strlen(jsonResult);

    // Cleanup
    UnregisterCompletion(userId);
    WorkCompletion_Destroy(&completion);
}
```

---

## 6. Makefile - Bygga tre separata executables

### Modifierad: Makefile

```makefile
CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -D_POSIX_C_SOURCE=200809L -Iinclude
LDFLAGS = -lpthread -lcurl -lcjson -lmbedtls -lmbedcrypto -lmbedx509 -lrt

# Main executables
BIN_DIR = bin
MAIN = $(BIN_DIR)/GridGuard-server
FETCHER = $(BIN_DIR)/GridGuard-fetcher
PARSER = $(BIN_DIR)/GridGuard-parser

# Source files
MAIN_SRC = src/application/main_hybrid.c \
           src/application/workers/ComputeWorker_Hybrid.c \
           src/application/network/HTTPServer.c \
           src/application/services/Compute.c \
           src/infrastructure/SharedCache.c \
           src/infrastructure/WorkCompletion.c \
           # ... (other common sources)

FETCHER_SRC = src/application/processes/fetcher_main.c \
              src/application/services/Fetcher.c \
              src/application/services/HTTPClient.c \
              src/infrastructure/SharedCache.c \
              # ... (fetcher dependencies)

PARSER_SRC = src/application/processes/parser_main.c \
             src/application/services/Parser.c \
             # ... (parser dependencies)

# Build targets
all: $(MAIN) $(FETCHER) $(PARSER)

$(MAIN): $(MAIN_SRC)
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(FETCHER): $(FETCHER_SRC)
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $^ -lcurl -lmbedtls -lmbedcrypto -lmbedx509 -lrt

$(PARSER): $(PARSER_SRC)
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $^ -lcjson

clean:
	rm -rf $(BIN_DIR)
	rm -f /tmp/gridguard_*.fifo /tmp/gridguard*.sock

test-hybrid: all
	@echo "Testing hybrid architecture..."
	./$(MAIN)

.PHONY: all clean test-hybrid
```

---

## 7. WorkCompletion Global Registry (nytt)

### Ny fil: src/infrastructure/CompletionRegistry.c

```c
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include "CompletionRegistry.h"
#include "WorkCompletion.h"

#define MAX_COMPLETIONS 1024

typedef struct {
    char userId[64];
    WorkCompletion *completion;
    bool active;
} CompletionEntry;

static CompletionEntry g_registry[MAX_COMPLETIONS];
static pthread_mutex_t g_registryMutex = PTHREAD_MUTEX_INITIALIZER;

void RegisterCompletion(const char *userId, WorkCompletion *completion) {
    pthread_mutex_lock(&g_registryMutex);

    for (int i = 0; i < MAX_COMPLETIONS; i++) {
        if (!g_registry[i].active) {
            strncpy(g_registry[i].userId, userId, sizeof(g_registry[i].userId) - 1);
            g_registry[i].completion = completion;
            g_registry[i].active = true;
            pthread_mutex_unlock(&g_registryMutex);
            return;
        }
    }

    pthread_mutex_unlock(&g_registryMutex);
    // Error: registry full
}

WorkCompletion *FindCompletionByUserId(const char *userId) {
    pthread_mutex_lock(&g_registryMutex);

    for (int i = 0; i < MAX_COMPLETIONS; i++) {
        if (g_registry[i].active && strcmp(g_registry[i].userId, userId) == 0) {
            WorkCompletion *completion = g_registry[i].completion;
            pthread_mutex_unlock(&g_registryMutex);
            return completion;
        }
    }

    pthread_mutex_unlock(&g_registryMutex);
    return NULL;
}

void UnregisterCompletion(const char *userId) {
    pthread_mutex_lock(&g_registryMutex);

    for (int i = 0; i < MAX_COMPLETIONS; i++) {
        if (g_registry[i].active && strcmp(g_registry[i].userId, userId) == 0) {
            g_registry[i].active = false;
            break;
        }
    }

    pthread_mutex_unlock(&g_registryMutex);
}
```

---

## Sammanfattning av ändringar

### Nya filer att skapa

1. `src/application/main_hybrid.c` - Main process med fork/exec
2. `src/application/processes/fetcher_main.c` - Fetch process
3. `src/application/processes/parser_main.c` - Parse process
4. `src/application/workers/ComputeWorker_Hybrid.c` - Compute thread (Unix socket client)
5. `src/infrastructure/CompletionRegistry.c` - Global WorkCompletion registry

### Modifieringar

1. `Makefile` - Bygga tre separata executables
2. `src/application/network/HTTPServer.c` - Skriv till pipe istället för Queue

### Behålls oförändrat

1. `src/infrastructure/SharedCache.c` - Shared memory cache
2. `src/infrastructure/WorkCompletion.c` - Mutex + condition variables
3. `src/application/services/Fetcher.c` - HTTP fetching logic
4. `src/application/services/Parser.c` - JSON parsing logic
5. `src/application/services/Compute.c` - Energy plan computation

---

## IPC-täckning i Hybrid-arkitekturen

| IPC-mekanism | Var används | Kursvecka |
|-------------|-------------|-----------|
| **fork()** | Main process → Fetch/Parse | Vecka 1 |
| **exec()** | Starta Fetch/Parse executables | Vecka 1 |
| **wait()/waitpid()** | Main väntar på child processes | Vecka 1 |
| **pthread_create()** | Compute thread i main process | Vecka 2 |
| **pthread_join()** | Main väntar på Compute thread | Vecka 2 |
| **pthread_mutex_t** | WorkCompletion, CompletionRegistry | Vecka 3 |
| **pthread_cond_t** | WorkCompletion signaling | Vecka 3 |
| **pipe()** | HTTP → Fetch (anonymous pipe) | Vecka 4 |
| **dup2()** | Redirect Fetch stdin till pipe | Vecka 4 |
| **mkfifo()** | Fetch → Parse (named pipe) | Vecka 4 |
| **socket(AF_UNIX)** | Parse → Compute (Unix socket) | Vecka 5 |
| **bind/listen/accept** | Parse socket server | Vecka 5 |
| **connect()** | Compute socket client | Vecka 5 |
| **shm_open()/mmap()** | SharedCache (weather + price) | Vecka 5 |
| **sem_open()/sem_wait()** | SharedCache synkronisering | Vecka 5 |

**Täckning: 100% av alla IPC-koncept i kursen!**

---

## Testning

### Test 1: Verifiera att alla processer startar

```bash
make clean
make all
./bin/GridGuard-server &

# Verifiera processer
ps aux | grep GridGuard
# Ska visa:
# - GridGuard-server (main)
# - GridGuard-fetcher (child)
# - GridGuard-parser (child)

# Verifiera IPC-resurser
ls -l /tmp/gridguard*
# Ska visa:
# - /tmp/gridguard_fetch_to_parse.fifo (FIFO)
# - /tmp/gridguard_parse_to_compute.sock (Unix socket)

ls -l /dev/shm/gridguard*
# Ska visa:
# - /dev/shm/gridguard_weather (shared memory)
# - /dev/shm/gridguard_prices (shared memory)
```

### Test 2: End-to-end request

```bash
curl -X POST http://localhost:8080/api/forecast \
  -H "Authorization: Bearer YOUR_JWT" \
  -H "Content-Type: application/json" \
  -d '{
    "location": "Stockholm",
    "lat": "59.3300",
    "lon": "18.0700",
    "region": "SE3"
  }'
```

**Förväntad log-sekvens:**
```
[HTTP] Wrote WorkRequest to pipe for user123
[Fetcher] Got request for user123/SE3
[Fetcher] Wrote FetchResult to FIFO
[Parser] Got FetchResult for user123/SE3
[Parser] Sent ParseResult to Compute
[ComputeWorker] Got ParseResult for user123/SE3
[ComputeWorker] Generated energy plan (48 entries)
[HTTP] Received result for user123
```

---

## Debugging tips

### Strace för att verifiera IPC

```bash
# Trace pipe communication
strace -e trace=read,write,pipe,dup2 ./bin/GridGuard-server

# Trace FIFO
strace -e trace=open,read,write ./bin/GridGuard-fetcher

# Trace Unix socket
strace -e trace=socket,bind,listen,accept,connect ./bin/GridGuard-parser
```

### Lsof för open file descriptors

```bash
# Visa alla file descriptors för GridGuard-processer
lsof -c GridGuard
```

### Valgrind för memory leaks

```bash
valgrind --leak-check=full --track-fds=yes ./bin/GridGuard-server
```

---

## Migration Plan

### Fas 1: Proof of Concept (1 vecka)

1. Implementera `fetcher_main.c` med pipe stdin input
2. Testa: HTTP → pipe → Fetch (isolerat)

### Fas 2: FIFO (1 vecka)

1. Implementera `parser_main.c` med FIFO input
2. Testa: Fetch → FIFO → Parse (isolerat)

### Fas 3: Unix Socket (1 vecka)

1. Implementera Parse socket server
2. Implementera Compute socket client
3. Testa: Parse → Unix socket → Compute

### Fas 4: Integration (1 vecka)

1. Implementera `main_hybrid.c` med fork/exec
2. Implementera CompletionRegistry
3. End-to-end test
4. Performance benchmarking vs nuvarande

### Fas 5: Dokumentation (3 dagar)

1. Uppdatera README med arkitekturdiagram
2. Skriva kursrapport med IPC-motivation
3. Benchmarking-resultat

---

## Slutsats

Hybrid-arkitekturen maximerar kursbegrepp-täckning (100% vs 56%) men kräver:

- **+5 nya filer** (~600 rader kod totalt)
- **Refactoring** av HTTP handler
- **Testning** av alla IPC-gränser
- **Debugging** av multi-process system

**Trade-off:** Mer implementation-arbete, men mycket starkare examination-demonstration.

**Rekommendation:**
- Om ni har tid (2-3 veckor): Implementera hybrid
- Om ni är tight på tid: Behåll nuvarande, dokumentera design-valet
