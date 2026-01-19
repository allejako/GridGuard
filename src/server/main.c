#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>

#include "../tcp/tcpserver.h"
#include "ClientHandler.h"

static volatile sig_atomic_t keep_running = 1;
static TCPServer server;  // Global, så signalhandler kan nå den


void handle_signal(int signum) 
{
    if (signum == SIGINT || signum == SIGTERM) 
    {
        printf("\nSignal %d received, shutting down server\n", signum);
        keep_running = 0;
        close(server.listen_fd); // Stoppa accept loopen
    }
}

void handle_sigchld(int signum)
{
    // Rensa avslutade barnprocesser
    int saved_errno = errno; // Bevara errno
    while (waitpid(-1, NULL, WNOHANG) > 0) {}
    errno = saved_errno;
}

int main()
{
    struct sigaction sa, sa_chld;

    // SIGINT/SIGTERM för att stänga servern
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    
    // SIGCHLD för att undvika zombies
    sa_chld.sa_handler = handle_sigchld;
    sigemptyset(&sa_chld.sa_mask);
    sa_chld.sa_flags = SA_RESTART; // restart av interrupted syscalls
    sigaction(SIGCHLD, &sa_chld, NULL);
  
    // Initiate TCP
    TCPServer_Initiate(&server, "8080");

    while (keep_running)
    {
        printf("Main: Listening...\n");
        int clientSocket = TCPServer_Accept(&server);

        // On TCP connection, fork() and continue process in child
        printf("Main: Forking...\n");
        pid_t pid = fork();

        if (pid < 0) // Error
        {
            perror("Fork failed\n");
            close(clientSocket);
            continue;
        }

        if (pid == 0) // Child process
        {
            printf("Child: Running child process %d\n", getpid());
            close(server.listen_fd); // Close server socket
            
            ClientHandler_HandleClient(clientSocket); // TODO, handle the actual process
            
            close(clientSocket); // Close when done
            printf("Child: clientSocket %d closed\n", getpid());
            exit(0);
        }
        else // Parent keeps looping, parent never leaves main.c
        {
            close(clientSocket); // Parent closes its copy of connection
            printf("Parent: clientSocket closed, child PID: %d, \n", pid);
        }
    }
    printf("Server shutting down..\n");
    close(server.listen_fd);

    // Vänta på eventuella återstående barnprocesser
    while (waitpid(-1, NULL, WNOHANG) > 0) {}

    printf("Server exited cleanly\n");
    return 0;
}

