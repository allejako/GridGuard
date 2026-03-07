#define _POSIX_C_SOURCE 200809L

#include "GridGuard.h"
#include "ComputeWorker.h"
#include "CompletionRegistry.h"
#include "Logger.h"
#include "Config.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <limits.h>
#include <libgen.h>

// IPC paths - definierade här för att vara tillgängliga överallt
static const char *FIFO_PATH = "/tmp/gridguard_fetch_to_parse.fifo";
static const char *SOCKET_PATH = "/tmp/gridguard_parse_to_compute.sock";

int GridGuard_Initiate(GridGuard *app)
{
    if (!app)
        return -1;

    memset(app, 0, sizeof(GridGuard));
    app->requestPipeFd = -1; 
    app->fetchPid = -1;
    app->parsePid = -1;

    strncpy(app->fifoPath, FIFO_PATH, sizeof(app->fifoPath) - 1);
    strncpy(app->socketPath, SOCKET_PATH, sizeof(app->socketPath) - 1);

    // Resolve binary paths relative to this executable so the paths are
    // correct even when running as a daemon (cwd becomes /).
    char exe[PATH_MAX];
    ssize_t exe_len = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (exe_len > 0)
    {
        exe[exe_len] = '\0';
        char exe_copy[PATH_MAX];
        strncpy(exe_copy, exe, sizeof(exe_copy) - 1);
        const char *bin_dir = dirname(exe_copy);
        snprintf(app->fetcherBin, sizeof(app->fetcherBin), "%s/GridGuard-fetcher", bin_dir);
        snprintf(app->parserBin,  sizeof(app->parserBin),  "%s/GridGuard-parser",  bin_dir);
    }
    else
    {
        strncpy(app->fetcherBin, "bin/GridGuard-fetcher", sizeof(app->fetcherBin) - 1);
        strncpy(app->parserBin,  "bin/GridGuard-parser",  sizeof(app->parserBin)  - 1);
    }

    LOG_INFO("Initializing GR1DGU4RD application core...");

    // Initialize gridguard database
    const char *dbPath = getenv("GRIDGUARD_DB_PATH");
    if (!dbPath || dbPath[0] == '\0')
        dbPath = DB_PATH;

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
    if (SharedCache_Create(&app->weatherCache, "/gridguard_weather", SHARED_CACHE_DEFAULT_TTL) != 0)
    {
        LOG_ERROR("GridGuard: Failed to create weather shared cache");
        Compute_Shutdown(&app->compute);
        ClientDB_Shutdown(&app->db);
        return -1;
    }

    if (SharedCache_Create(&app->priceCache, "/gridguard_price", SHARED_CACHE_DEFAULT_TTL) != 0)
    {
        LOG_ERROR("GridGuard: Failed to create price shared cache");
        SharedCache_Destroy(&app->weatherCache);
        Compute_Shutdown(&app->compute);
        ClientDB_Shutdown(&app->db);
        return -1;
    }

    app->isRunning = true;
    pthread_mutex_init(&app->mutex, NULL);

    // Skapa FIFO för Fetch → Parse kommunikation
    unlink(app->fifoPath); // Rensa gammal FIFO om den finns för att undvika EEXIST error
    if (mkfifo(app->fifoPath, 0666) != 0)
    {
        LOG_ERROR("GR1DGU4RD: Failed to create FIFO at %s", app->fifoPath);
        SharedCache_Destroy(&app->priceCache);
        SharedCache_Destroy(&app->weatherCache);
        Compute_Shutdown(&app->compute);
        ClientDB_Shutdown(&app->db);
        return -1;
    }
    LOG_INFO("FIFO created: %s", app->fifoPath);

    // Rensa gammal Unix socket
    unlink(app->socketPath);

    // Skapa anonymous pipe för HTTP → Fetch kommunikation
    int requestPipe[2];
    if (pipe(requestPipe) != 0)
    {
        LOG_ERROR("GridGuard: Failed to create request pipe");
        unlink(app->fifoPath);
        SharedCache_Destroy(&app->priceCache);
        SharedCache_Destroy(&app->weatherCache);
        Compute_Shutdown(&app->compute);
        ClientDB_Shutdown(&app->db);
        return -1;
    }
    LOG_INFO("Pipe created (HTTP → Fetch)");

    // Fork Fetch-process
    app->fetchPid = fork();
    if (app->fetchPid < 0)
    {
        LOG_ERROR("GridGuard: Failed to fork Fetch process");
        close(requestPipe[0]);
        close(requestPipe[1]);
        unlink(app->fifoPath);
        SharedCache_Destroy(&app->priceCache);
        SharedCache_Destroy(&app->weatherCache);
        Compute_Shutdown(&app->compute);
        ClientDB_Shutdown(&app->db);
        return -1;
    }

    if (app->fetchPid == 0)
    {
        // CHILD: Fetch process
        close(requestPipe[1]);

        // Redirect stdin till pipe read end
        if (dup2(requestPipe[0], STDIN_FILENO) < 0)
        {
            LOG_FATAL("Fetch: Failed to dup2 stdin");
            exit(EXIT_FAILURE);
        }
        close(requestPipe[0]);

        // Exec fetcher executable (use absolute path since daemon changes cwd to /)
        execl(app->fetcherBin, "GridGuard-fetcher", app->fifoPath, NULL);
        LOG_FATAL("Fetch: exec failed for %s", app->fetcherBin);
        exit(EXIT_FAILURE);
    }

    // PARENT: Stäng read end, spara write end
    close(requestPipe[0]);
    app->requestPipeFd = requestPipe[1];
    LOG_INFO("Fetch process started (PID %d)", app->fetchPid);

    // Ge fetch-processen tid att öppna FIFO write end
    sleep(1);

    // Fork Parse-process
    app->parsePid = fork();
    if (app->parsePid < 0)
    {
        LOG_ERROR("Failed to fork Parse process");
        kill(app->fetchPid, SIGTERM);
        waitpid(app->fetchPid, NULL, 0);
        close(app->requestPipeFd);
        unlink(app->fifoPath);
        SharedCache_Destroy(&app->priceCache);
        SharedCache_Destroy(&app->weatherCache);
        Compute_Shutdown(&app->compute);
        ClientDB_Shutdown(&app->db);
        return -1;
    }

    if (app->parsePid == 0)
    {
        // CHILD: Parse process (use absolute path since daemon changes cwd to /)
        execl(app->parserBin, "GridGuard-parser", app->fifoPath, app->socketPath, NULL);
        LOG_FATAL("Parse: exec failed for %s", app->parserBin);
        exit(EXIT_FAILURE);
    }

    LOG_INFO("Parse process started (PID %d)", app->parsePid);

    // Ge parse-processen tid att sätta upp Unix socket
    sleep(1);

    // Starta Compute worker thread (Unix socket client)
    ComputeWorker *computeWorker = calloc(1, sizeof(ComputeWorker));
    if (!computeWorker)
    {
        LOG_ERROR("Failed to allocate ComputeWorker");
        kill(app->fetchPid, SIGTERM);
        kill(app->parsePid, SIGTERM);
        waitpid(app->fetchPid, NULL, 0);
        waitpid(app->parsePid, NULL, 0);
        close(app->requestPipeFd);
        unlink(app->fifoPath);
        unlink(app->socketPath);
        SharedCache_Destroy(&app->priceCache);
        SharedCache_Destroy(&app->weatherCache);
        Compute_Shutdown(&app->compute);
        ClientDB_Shutdown(&app->db);
        return -1;
    }

    computeWorker->socketPath = app->socketPath;
    computeWorker->compute = &app->compute;
    computeWorker->isRunning = true;

    if (pthread_create(&app->computeThread, NULL, ComputeWorker_Run, computeWorker) != 0)
    {
        LOG_ERROR("Failed to create Compute worker thread");
        free(computeWorker);
        kill(app->fetchPid, SIGTERM);
        kill(app->parsePid, SIGTERM);
        waitpid(app->fetchPid, NULL, 0);
        waitpid(app->parsePid, NULL, 0);
        close(app->requestPipeFd);
        unlink(app->fifoPath);
        unlink(app->socketPath);
        SharedCache_Destroy(&app->priceCache);
        SharedCache_Destroy(&app->weatherCache);
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

    // Terminera child-processer
    if (app->fetchPid > 0)
    {
        LOG_INFO("Stopping Fetch (PID %d)", app->fetchPid);
        kill(app->fetchPid, SIGTERM);
    }

    if (app->parsePid > 0)
    {
        LOG_INFO("Stopping Parse (PID %d)", app->parsePid);
        kill(app->parsePid, SIGTERM);
    }

    // Vänta på child-processer
    if (app->fetchPid > 0)
    {
        int status;
        waitpid(app->fetchPid, &status, 0);
        LOG_INFO("Fetch exited (status %d)", WEXITSTATUS(status));
    }

    if (app->parsePid > 0)
    {
        int status;
        waitpid(app->parsePid, &status, 0);
        LOG_INFO("Parse exited (status %d)", WEXITSTATUS(status));
    }

    // Stäng pipe
    if (app->requestPipeFd >= 0)
    {
        close(app->requestPipeFd);
    }

    // Rensa IPC-resurser
    unlink(app->fifoPath);
    unlink(app->socketPath);

    // Cleanup komponenter
    SharedCache_Destroy(&app->priceCache);
    SharedCache_Destroy(&app->weatherCache);
    Compute_Shutdown(&app->compute);
    ClientDB_Shutdown(&app->db);

    pthread_mutex_destroy(&app->mutex);

    LOG_INFO("Shutdown complete");
}
