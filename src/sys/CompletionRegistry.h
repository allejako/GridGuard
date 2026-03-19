#ifndef _COMPLETION_REGISTRY_H_
#define _COMPLETION_REGISTRY_H_

#include "sys/WorkCompletion.h"

// Maps userId -> WorkCompletion pointer.
// Thread-safe (internal mutex). Max 1024 concurrent entries.
// Lock level 1 - do not call Queue_Push while holding registry lock.

void RegisterCompletion(const char *userId, WorkCompletion *completion);
WorkCompletion *FindCompletionByUserId(const char *userId);
void UnregisterCompletion(const char *userId);

#endif // _COMPLETION_REGISTRY_H_
