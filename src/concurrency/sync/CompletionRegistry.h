#ifndef _COMPLETION_REGISTRY_H
#define _COMPLETION_REGISTRY_H

#include "WorkCompletion.h"

// Global registry för att mappa userId → WorkCompletion-pekare.
// Används i hybrid-arkitekturen där ComputeWorker-tråden behöver
// hitta rätt WorkCompletion baserat på userId (som skickas via IPC),
// eftersom vi inte längre har direkta pekare i WorkRequest.
//
// Thread-safe: alla operationer skyddas av intern mutex.

void RegisterCompletion(const char *userId, WorkCompletion *completion);
WorkCompletion *FindCompletionByUserId(const char *userId);
void UnregisterCompletion(const char *userId);

#endif // _COMPLETION_REGISTRY_H
