#ifndef _COMPUTE_WORKER_H_
#define _COMPUTE_WORKER_H_

// Compute worker thread function (pthread-compatible) expects GridGuard* as argument
void *ComputeWorker_Run(void *arg);

#endif // _COMPUTE_WORKER_H_
