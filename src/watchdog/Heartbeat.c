#define _POSIX_C_SOURCE 200809L

#include "watchdog/Heartbeat.h"
#include "sys/Logger.h"

#include <unistd.h>
#include <poll.h>
#include <errno.h>
#include <string.h>

int Heartbeat_Initiate(Heartbeat *hb)
{
    if (!hb)
        return -1;

    int fds[2];
    if (pipe(fds) < 0)
    {
        LOG_ERROR("Heartbeat: pipe() failed: %s", strerror(errno));
        return -1;
    }

    hb->readFd  = fds[0];
    hb->writeFd = fds[1];

    LOG_INFO("Heartbeat pipe created (read=%d, write=%d)", hb->readFd, hb->writeFd);
    return 0;
}

void Heartbeat_Shutdown(Heartbeat *hb)
{
    if (!hb)
        return;
    if (hb->readFd >= 0)
        close(hb->readFd);
    if (hb->writeFd >= 0)
        close(hb->writeFd);
}

int Heartbeat_GetWriteFd(const Heartbeat *hb)
{
    return hb ? hb->writeFd : -1;
}

int Heartbeat_CloseWriteFd(Heartbeat *hb)
{
    if (!hb || hb->writeFd < 0)
        return 0;
    close(hb->writeFd);
    hb->writeFd = -1;
    return 0;
}

int Heartbeat_CloseReadFd(Heartbeat *hb)
{
    if (!hb || hb->readFd < 0)
        return 0;
    close(hb->readFd);
    hb->readFd = -1;
    return 0;
}

int Heartbeat_Check(Heartbeat *hb, int timeoutSec)
{
    if (!hb || hb->readFd < 0)
        return 1;

    struct pollfd pfd;
    pfd.fd     = hb->readFd;
    pfd.events = POLLIN;

    int ret = poll(&pfd, 1, timeoutSec * 1000);

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
    ssize_t n = read(hb->readFd, buf, sizeof(buf));
    if (n <= 0)
        return -1;

    return 1;
}
