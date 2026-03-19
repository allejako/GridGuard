#ifndef _IPC_PATHS_H_
#define _IPC_PATHS_H_

// IPC paths for GridGuard process communication
#define REQUEST_FIFO_PATH          "/tmp/gridguard_requests.fifo"
#define FETCH_TO_PARSE_FIFO_PATH   "/tmp/gridguard_fetch_to_parse.fifo"
#define PARSE_TO_COMPUTE_SOCK_PATH "/tmp/gridguard_parse_to_compute.sock"
#define PARSE_TO_COMPUTE_NOTIFY_PATH "/tmp/gridguard_parse_to_compute.fifo"

#endif // _IPC_PATHS_H_
