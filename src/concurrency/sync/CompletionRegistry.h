#ifndef _COMPLETION_REGISTRY_H
#define _COMPLETION_REGISTRY_H

#include "WorkCompletion.h"

// Maps userId -> WorkCompletion pointer so the compute thread can signal
// the right HTTP worker without needing a direct pointer in the work request.
// All operations are protected by an internal mutex.

void RegisterCompletion(const char *userId, WorkCompletion *completion);
WorkCompletion *FindCompletionByUserId(const char *userId);
void UnregisterCompletion(const char *userId);

#endif // _COMPLETION_REGISTRY_H
