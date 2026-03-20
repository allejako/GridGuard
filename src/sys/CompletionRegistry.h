#ifndef _COMPLETION_REGISTRY_H_
#define _COMPLETION_REGISTRY_H_

#include "sys/WorkCompletion.h"

// Maps userId -> WorkCompletion pointer.
// Thread-safe (internal mutex). Max 1024 concurrent entries.
// Lock level 1 - do not call Queue_Push while holding registry lock.

void CompletionRegistry_Initiate(void);
void CompletionRegistry_Shutdown(void);

void            CompletionRegistry_Register(const char *userId, WorkCompletion *completion);
WorkCompletion *CompletionRegistry_Find(const char *userId);
void            CompletionRegistry_Unregister(const char *userId);

#endif // _COMPLETION_REGISTRY_H_
