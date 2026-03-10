#define _POSIX_C_SOURCE 200809L

#include "watchdog/IPC.h"
#include "sys/Logger.h"

#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

int IPC_Initiate(void)
{
    int errors = 0;

    if (mkfifo(REQUEST_FIFO_PATH, 0644) < 0 && errno != EEXIST)
    {
        LOG_WARNING("IPC: mkfifo(%s) failed: %s", REQUEST_FIFO_PATH, strerror(errno));
        errors++;
    }

    if (mkfifo(FETCH_TO_PARSE_FIFO_PATH, 0644) < 0 && errno != EEXIST)
    {
        LOG_WARNING("IPC: mkfifo(%s) failed: %s", FETCH_TO_PARSE_FIFO_PATH, strerror(errno));
        errors++;
    }

    unlink(PARSE_TO_COMPUTE_SOCK_PATH);

    return errors > 0 ? -1 : 0;
}

void IPC_Shutdown(void)
{
    unlink(REQUEST_FIFO_PATH);
    unlink(FETCH_TO_PARSE_FIFO_PATH);
    unlink(PARSE_TO_COMPUTE_SOCK_PATH);
}
