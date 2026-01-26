#define _POSIX_C_SOURCE 200809L

#include "Logger.h"
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <stdarg.h>

static FILE *logFile = NULL;
static LogLevel currentLevel = LOG_LEVEL_INFO;

static const char *levelStrings[] = {
    "DEBUG",
    "INFO",
    "WARNING",
    "ERROR",
    "FATAL"
};

static const char *levelColors[] = {
    "\033[36m", // DEBUG - Cyan
    "\033[32m", // INFO - Green
    "\033[33m", // WARNING - Yellow
    "\033[31m", // ERROR - Red
    "\033[35m"  // FATAL - Magenta
};
static const char *colorReset = "\033[0m";

int loggerInit(const char *logFilePath, LogLevel minLevel)
{
    currentLevel = minLevel;
    
    if (logFilePath != NULL) {
        logFile = fopen(logFilePath, "a");
        if (logFile == NULL) {
            fprintf(stderr, "Failed to open log file: %s\n", logFilePath);
            return -1;
        }
    }
    
    return 0;
}
void loggerShutdown(void)
{
    if (logFile != NULL) {
        fclose(logFile);
        logFile = NULL;
    }
}

void loggerSetLevel(LogLevel level)
{
    currentLevel = level;
}

LogLevel loggerGetLevel(void)
{
    return currentLevel;
}

void loggerLog(LogLevel level, const char *file, int line, const char *fmt, ...)
{
    if (level < currentLevel) {
        return;
    }
    
    // Get timestamp
    time_t now = time(NULL);
    struct tm *tmInfo = localtime(&now);
    char timestamp[20];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tmInfo);
    
    // Extract filename from path
    const char *filename = strrchr(file, '/');
    if (filename == NULL) {
        filename = strrchr(file, '\\');
    }
    filename = (filename != NULL) ? filename + 1 : file;
    
    // Format the message
    va_list args;
    
    // Print to stdout with colors
    va_start(args, fmt);
    printf("%s[%s] %s%-5s%s %s:%d: ", 
           colorReset, timestamp, 
           levelColors[level], levelStrings[level], colorReset,
           filename, line);
    vprintf(fmt, args);
    printf("\n");
    fflush(stdout);
    va_end(args);
    
    // Print to file without colors
    if (logFile != NULL) {
        va_start(args, fmt);
        fprintf(logFile, "[%s] %-5s %s:%d: ", 
                timestamp, levelStrings[level], filename, line);
        vfprintf(logFile, fmt, args);
        fprintf(logFile, "\n");
        fflush(logFile);
        va_end(args);
    }
}