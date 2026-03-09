#define _POSIX_C_SOURCE 200809L

#include "watchdog/Heartbeat.h"
#include "sys/Logger.h"

#include <stdlib.h>
#include <unistd.h>
#include <poll.h>
#include <errno.h>
#include <string.h>

struct Heartbeat
{
    int read_fd;
    int write_fd;
};

Heartbeat *Heartbeat_Create(void)
{
    int fds[2];
    if (pipe(fds) < 0)
    {
        LOG_ERROR("Heartbeat: pipe() failed: %s", strerror(errno));
        return NULL;
    }

    Heartbeat *hb = malloc(sizeof(Heartbeat));
    if (!hb)
    {
        close(fds[0]);
        close(fds[1]);
        return NULL;
    }

    hb->read_fd  = fds[0];
    hb->write_fd = fds[1];

    LOG_INFO("Heartbeat pipe created (read=%d, write=%d)", hb->read_fd, hb->write_fd);
    return hb;
}

void Heartbeat_Destroy(Heartbeat *hb)
{
    if (!hb)
        return;
    if (hb->read_fd >= 0)
        close(hb->read_fd);
    if (hb->write_fd >= 0)
        close(hb->write_fd);
    free(hb);
}

int Heartbeat_GetWriteFd(const Heartbeat *hb)
{
    return hb ? hb->write_fd : -1;
}

int Heartbeat_CloseWriteFd(Heartbeat *hb)
{
    if (!hb || hb->write_fd < 0)
        return 0;
    close(hb->write_fd);
    hb->write_fd = -1;
    return 0;
}

int Heartbeat_CloseReadFd(Heartbeat *hb)
{
    if (!hb || hb->read_fd < 0)
        return 0;
    close(hb->read_fd);
    hb->read_fd = -1;
    return 0;
}

int Heartbeat_Check(Heartbeat *hb, int timeout_sec)
{
    if (!hb || hb->read_fd < 0)
        return 1;

    struct pollfd pfd;
    pfd.fd     = hb->read_fd;
    pfd.events = POLLIN;

    int ret = poll(&pfd, 1, timeout_sec * 1000);

    if (ret < 0)
    {
        if (errno == EINTR)
            return 1;
        LOG_ERROR("Heartbeat: poll() failed: %s", strerror(errno));
        return -1;
    }

    if (ret == 0)
        return 0;

    char buf[64];
    ssize_t n = read(hb->read_fd, buf, sizeof(buf));
    if (n <= 0)
        return -1;

    return 1;
}
