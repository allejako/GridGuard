#define _POSIX_C_SOURCE 200809L

#include "server/GridGuard.h"
#include "compute/ComputeWorker.h"
#include "sys/CompletionRegistry.h"
#include "sys/Logger.h"
#include "domain/Config.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <limits.h>
#include <libgen.h>

// IPC paths - definierade här för att vara tillgängliga överallt
static const char *FIFO_PATH = "/tmp/gridguard_fetch_to_parse.fifo";
static const char *SOCKET_PATH = "/tmp/gridguard_parse_to_compute.sock";
static const char *NOTIFY_PATH = "/tmp/gridguard_parse_to_compute.fifo";

int GridGuard_Initiate(GridGuard *app)
{
    if (!app)
        return -1;

    memset(app, 0, sizeof(GridGuard));
    app->requestPipeFd = -1;
    app->computeWorker = NULL;

    strncpy(app->fifoPath, FIFO_PATH, sizeof(app->fifoPath) - 1);
    strncpy(app->socketPath, SOCKET_PATH, sizeof(app->socketPath) - 1);

    LOG_INFO("Initializing GR1DGU4RD application core...");

    // Resolve database path relative to binary location (works when running as daemon)
    const char *dbPath = getenv("GRIDGUARD_DB_PATH");
    char resolvedDbPath[PATH_MAX];

    if (!dbPath || dbPath[0] == '\0')
    {
        // Auto-resolve: bin/GridGuard-server -> gridguard.db
        char exe[PATH_MAX];
        ssize_t exeLen = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
        if (exeLen > 0)
        {
            exe[exeLen] = '\0';
            char exeCopy[PATH_MAX];
            strncpy(exeCopy, exe, sizeof(exeCopy) - 1);
            const char *binDir = dirname(exeCopy);
            snprintf(resolvedDbPath, sizeof(resolvedDbPath), "%s/../gridguard.db", binDir);
            dbPath = resolvedDbPath;
        }
        else
        {
            dbPath = DB_PATH;
        }
    }

    if (ClientDB_Initiate(&app->db, dbPath) != 0)
    {
        LOG_ERROR("GridGuard: Failed to initiate database");
        return -1;
    }

    // Initialize Compute service, används utav våran compute worker thread som kommunicerar med Parse-processen via Unix socket
    if (Compute_Initiate(&app->compute) != 0)
    {
        LOG_ERROR("GridGuard: Failed to initiate Compute");
        ClientDB_Shutdown(&app->db);
        return -1;
    }

    // Initialize shared memory caches för väder och elprisdata, används av både Fetch och Parse-processerna
    if (SharedCache_Initiate(&app->weatherCache, "/gridguard_weather", SHARED_CACHE_DEFAULT_TTL) != 0)
    {
        LOG_ERROR("GridGuard: Failed to initiate weather shared cache");
        Compute_Shutdown(&app->compute);
        ClientDB_Shutdown(&app->db);
        return -1;
    }

    if (SharedCache_Initiate(&app->priceCache, "/gridguard_price", SHARED_CACHE_DEFAULT_TTL) != 0)
    {
        LOG_ERROR("GridGuard: Failed to initiate price shared cache");
        SharedCache_Shutdown(&app->weatherCache);
        Compute_Shutdown(&app->compute);
        ClientDB_Shutdown(&app->db);
        return -1;
    }

    if (SharedCache_Initiate(&app->forecastCache, "/gridguard_forecast", SHARED_CACHE_DEFAULT_TTL) != 0)
    {
        LOG_ERROR("GridGuard: Failed to initiate forecast shared cache");
        SharedCache_Shutdown(&app->priceCache);
        SharedCache_Shutdown(&app->weatherCache);
        Compute_Shutdown(&app->compute);
        ClientDB_Shutdown(&app->db);
        return -1;
    }

    app->isRunning = true;
    app->lastDataUpdateCheck = 0;  // Initialize cache invalidation tracking
    pthread_mutex_init(&app->mutex, NULL);
    pthread_mutex_init(&app->updateCheckMutex, NULL);

    // Note: Watchdog creates all FIFOs before spawning processes
    LOG_INFO("Using FIFO created by Watchdog: %s", app->fifoPath);

    // Open request FIFO for writing (Watchdog created this already)
    // This will block until Fetcher process opens it for reading
    app->requestPipeFd = open("/tmp/gridguard_requests.fifo", O_WRONLY);
    if (app->requestPipeFd < 0)
    {
        LOG_ERROR("GridGuard: Failed to open request FIFO for writing");
        SharedCache_Shutdown(&app->forecastCache);
        SharedCache_Shutdown(&app->priceCache);
        SharedCache_Shutdown(&app->weatherCache);
        Compute_Shutdown(&app->compute);
        ClientDB_Shutdown(&app->db);
        return -1;
    }
    LOG_INFO("Opened request FIFO for writing");

    // Starta Compute worker thread (Unix socket client)
    ComputeWorker *computeWorker = calloc(1, sizeof(ComputeWorker));
    if (!computeWorker)
    {
        LOG_ERROR("Failed to allocate ComputeWorker");
        close(app->requestPipeFd);
        SharedCache_Shutdown(&app->forecastCache);
        SharedCache_Shutdown(&app->priceCache);
        SharedCache_Shutdown(&app->weatherCache);
        Compute_Shutdown(&app->compute);
        ClientDB_Shutdown(&app->db);
        return -1;
    }

    computeWorker->socketPath = app->socketPath; // Unix socket path för Parse → Compute
    computeWorker->notifyPath = NOTIFY_PATH; // Notify FIFO path för Parse → Compute (för att signalera när data är klar)
    computeWorker->compute = &app->compute; // Opaque pointer till Compute service
    computeWorker->isRunning = true; // Kontrollflagga för ComputeWorker

    // Save reference to worker so we can signal shutdown later
    app->computeWorker = computeWorker;

    if (pthread_create(&app->computeThread, NULL, ComputeWorker_Run, computeWorker) != 0)
    {
        LOG_ERROR("Failed to create Compute worker thread");
        app->computeWorker = NULL;
        free(computeWorker);
        close(app->requestPipeFd);
        SharedCache_Shutdown(&app->forecastCache);
        SharedCache_Shutdown(&app->priceCache);
        SharedCache_Shutdown(&app->weatherCache);
        Compute_Shutdown(&app->compute);
        ClientDB_Shutdown(&app->db);
        return -1;
    }

    LOG_INFO("Compute thread started");
    LOG_INFO("IPC resources ready");

    return 0;
}

int GridGuard_SubmitRequest(GridGuard *app, const WorkRequest *request, WorkCompletion *completion)
{
    if (!app || !request || !completion)
        return -1;

    // Registrera WorkCompletion så Compute-tråden kan hitta den via userId
    RegisterCompletion(request->userId, completion);

    // Skriv request till pipe (HTTP → Fetch)
    pthread_mutex_lock(&app->mutex);
    ssize_t written = write(app->requestPipeFd, request, sizeof(WorkRequest));
    pthread_mutex_unlock(&app->mutex);

    if (written != sizeof(WorkRequest))
    {
        LOG_ERROR(" Failed to write WorkRequest to pipe");
        UnregisterCompletion(request->userId);
        return -1;
    }

    LOG_DEBUG("Submitted request for %s to pipeline", request->userId);
    return 0;
}

void GridGuard_Shutdown(GridGuard *app)
{
    if (!app)
        return;

    LOG_INFO("Shutting down");

    app->isRunning = false;

    // Signal ComputeWorker thread to exit
    if (app->computeWorker)
    {
        ComputeWorker *worker = (ComputeWorker *)app->computeWorker;
        worker->isRunning = false;
        LOG_INFO("Signaled ComputeWorker to exit, waiting for thread...");

        // Wait for thread to finish (it will free itself)
        pthread_join(app->computeThread, NULL);
        LOG_INFO("ComputeWorker thread joined");
        app->computeWorker = NULL;
    }

    // Terminera child-processer
    // Watchdog owns Fetcher and Parser processes, so we don't kill them here
    LOG_INFO("Server shutting down (Watchdog manages other processes)");

    // Stäng pipe
    if (app->requestPipeFd >= 0)
    {
        close(app->requestPipeFd);
    }

    // Note: Watchdog owns and cleans up FIFOs and sockets

    // Shutdown komponenter
    SharedCache_Shutdown(&app->forecastCache);
    SharedCache_Shutdown(&app->priceCache);
    SharedCache_Shutdown(&app->weatherCache);
    Compute_Shutdown(&app->compute);
    ClientDB_Shutdown(&app->db);

    pthread_mutex_destroy(&app->updateCheckMutex);
    pthread_mutex_destroy(&app->mutex);

    LOG_INFO("Shutdown complete");
}
