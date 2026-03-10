#ifndef IPC_H
#define IPC_H

// IPC paths for process communication
#define REQUEST_FIFO_PATH          "/tmp/gridguard_requests.fifo"
#define FETCH_TO_PARSE_FIFO_PATH   "/tmp/gridguard_fetch_to_parse.fifo"
#define PARSE_TO_COMPUTE_SOCK_PATH "/tmp/gridguard_parse_to_compute.sock"

// Create FIFOs and clean up stale socket paths
int IPC_Initiate(void);
void IPC_Shutdown(void);

#endif
