#ifndef CLIENT_HANDLER_H
#define CLIENT_HANDLER_H

struct GridGuard;

// Handle a single HTTP connection end-to-end:
// parse request → route → process → send response → close(fd).
// Called by an HTTP worker thread. Blocks until response is sent.
void Client_HandleRequest(int fd, struct GridGuard *app);

#endif // CLIENT_HANDLER_H
