#define _POSIX_C_SOURCE 200809L

#include "ClientHandler.h"
#include "PipelineOrchestrator.h"
#include "Logger.h"
#include <string.h>
#include <sys/socket.h>

void Client_HandleState(Client *client, struct Pipeline *pipeline)
{
    switch (client->state)
    {
    case CLIENT_CONNECTED:
    {
        // Send welcome message
        const char *welcome = "GridGuard LEOP Server\nCommands: forecast [location] [region]\nExample: forecast stockholm SE3\n\n> ";
        send(client->fd, welcome, strlen(welcome), 0);
        client->state = CLIENT_READY;

        // If buffer has data, process it immediately
        if (client->bufferLen > 0)
        {
            Client_HandleState(client, pipeline);  // Recursive call to handle the command
        }
        break;
    }

    case CLIENT_READY:
    {
        // Parse command from client
        char command[32] = {0};
        char location[64] = "stockholm";  // Default
        char region[16] = "SE3";          // Default

        sscanf(client->buffer, "%31s %63s %15s", command, location, region);

        if (strcmp(command, "forecast") == 0)
        {
            LOG_INFO("ClientHandler: Client FD %d requested forecast for %s/%s", client->fd, location, region);

            // Submit to pipeline
            PipelineRequest request = {
                .clientFd = client->fd
            };
            strncpy(request.location, location, sizeof(request.location) - 1);
            strncpy(request.region, region, sizeof(request.region) - 1);

            if (Pipeline_SubmitRequest((Pipeline *)pipeline, &request) == 0)
            {
                const char *processing = "Processing request...\n";
                send(client->fd, processing, strlen(processing), 0);
                client->state = CLIENT_PROCESSING;
            }
            else
            {
                const char *error = "ERROR: Pipeline queue full, try again later\n> ";
                send(client->fd, error, strlen(error), 0);
            }
        }
        else if (strcmp(command, "help") == 0 || strlen(client->buffer) == 0)
        {
            const char *help =
                "Available commands:\n"
                "  forecast [location] [region] - Get energy forecast\n"
                "  help                         - Show this help\n"
                "\n> ";
            send(client->fd, help, strlen(help), 0);
        }
        else
        {
            const char *unknown = "Unknown command. Type 'help' for available commands\n> ";
            send(client->fd, unknown, strlen(unknown), 0);
        }
        break;
    }

    case CLIENT_PROCESSING:
        // Client is waiting for pipeline response
        // Response will be sent directly from pipeline
        // Set back to READY state after a moment
        client->state = CLIENT_READY;
        break;

    default:
        break;
    }
}
