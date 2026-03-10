#define _POSIX_C_SOURCE 200809L

#include "watchdog/Status.h"
#include "sys/Logger.h"

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>

struct Status {
    char *fifo_path;
    int fd;
};

Status *Status_Initiate(const char *fifo_path)
{
    if (!fifo_path)
        return NULL;

    Status *status = malloc(sizeof(Status));
    if (!status)
        return NULL;

    status->fifo_path = strdup(fifo_path);
    if (!status->fifo_path)
    {
        free(status);
        return NULL;
    }

    if (mkfifo(fifo_path, 0600) < 0 && errno != EEXIST)
    {
        LOG_WARNING("Status: mkfifo failed: %s", strerror(errno));
        free(status->fifo_path);
        free(status);
        return NULL;
    }

    status->fd = open(fifo_path, O_RDWR | O_NONBLOCK);
    if (status->fd < 0)
    {
        LOG_WARNING("Status: could not open status fifo: %s", strerror(errno));
        free(status->fifo_path);
        free(status);
        return NULL;
    }

    LOG_INFO("Status FIFO opened: %s", fifo_path);
    return status;
}

void Status_Write(Status *status, const char *fmt, ...)
{
    if (!status || status->fd < 0)
        return;

    char buf[128];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (n > 0)
        write(status->fd, buf, (size_t)n);
}

void Status_Shutdown(Status *status)
{
    if (!status)
        return;

    if (status->fd >= 0)
    {
        close(status->fd);
        status->fd = -1;
    }

    if (status->fifo_path)
    {
        unlink(status->fifo_path);
        free(status->fifo_path);
    }

    free(status);
}
