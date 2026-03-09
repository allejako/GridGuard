#ifndef HEARTBEAT_H
#define HEARTBEAT_H

#define HEARTBEAT_INTERVAL  5
#define HEARTBEAT_TIMEOUT   15

typedef struct Heartbeat Heartbeat;

// Skapar en ny heartbeat-pipe. Returnerar NULL vid fel.
Heartbeat *Heartbeat_Create(void);

// Frigör resurser och stänger öppna fd:ar.
void Heartbeat_Destroy(Heartbeat *hb);

// Returnerar write-fd:n (child-processen ärver denna).
int Heartbeat_GetWriteFd(const Heartbeat *hb);

// Stänger write-fd:n i parent efter fork(). Returnerar 0 vid framgång.
int Heartbeat_CloseWriteFd(Heartbeat *hb);

// Stänger read-fd:n i child-processen efter fork(). Returnerar 0 vid framgång.
int Heartbeat_CloseReadFd(Heartbeat *hb);

// Kontrollerar om ett heartbeat har anlänt inom timeout_sec sekunder.
// Returnerar: 1=heartbeat mottaget, 0=timeout, -1=fel
int Heartbeat_Check(Heartbeat *hb, int timeout_sec);

#endif // HEARTBEAT_H
