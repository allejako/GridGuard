#define _POSIX_C_SOURCE 200809L

#include "sys/PidFile.h"
#include "sys/Logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>

int PidFile_Write(const char *path)
{
    FILE *fp = fopen(path, "w");
    if (!fp)
    {
        LOG_ERROR("PidFile: Failed to open %s for writing", path);
        return -1;
    }

    fprintf(fp, "%d\n", getpid());
    fclose(fp);

    LOG_INFO("PidFile: Wrote PID %d to %s", getpid(), path);
    return 0;
}

pid_t PidFile_Read(const char *path)
{
    FILE *fp = fopen(path, "r");
    if (!fp)
    {
        return -1;
    }

    pid_t pid;
    if (fscanf(fp, "%d", &pid) != 1)
    {
        fclose(fp);
        LOG_ERROR("PidFile: Failed to read PID from %s", path);
        return -1;
    }

    fclose(fp);
    return pid;
}

int PidFile_Remove(const char *path)
{
    if (unlink(path) != 0)
    {
        if (errno != ENOENT)
        {
            LOG_ERROR("PidFile: Failed to remove %s", path);
            return -1;
        }
    }

    LOG_INFO("PidFile: Removed %s", path);
    return 0;
}

int PidFile_IsRunning(const char *path)
{
    pid_t pid = PidFile_Read(path);
    if (pid <= 0)
    {
        return 0;
    }

    // kill with signal 0 checks if process exists without sending a signal
    if (kill(pid, 0) == 0)
    {
        return 1;
    }

    // ESRCH means process doesn't exist, anything else means it exists but we can't signal it
    if (errno == ESRCH)
    {
        return 0;
    }

    return 1;
}
