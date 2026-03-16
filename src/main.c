#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Path to the watchdog executable that runs the fetcher, parser, and server for GridGuard. 
// This main function simply starts the watchdog process with the provided arguments.

#define WATCHDOG_PATH "bin/GridGuard-watchdog" 

int main(int argc, char *argv[])
{
    char *args[argc + 1];
    args[0] = WATCHDOG_PATH;

    for (int i = 1; i < argc; i++)
        args[i] = argv[i];

    args[argc] = NULL;

    execv(WATCHDOG_PATH, args); 

    perror("GridGuard: Failed to start watchdog");
    return 1;
}
