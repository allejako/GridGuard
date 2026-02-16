# GridGuard - Sammanfattning och Guide

*Dokument skapat: 2026-02-16*

---

## VAD AR PROJEKTET?

**GridGuard** (tidigare LEOP - Local Energy Optimization Platform) ar ett energioptimeringsystem som:

1. **Hamtar vaderdata** fran Open-Meteo API (solinstrålning, molnighet, temperatur)
2. **Hamtar elpriser** fran elprisetjustnu.se (spotpriser per 15-minuters intervall)
3. **Beraknar optimal energianvandning** - nar man ska anvanda el, ladda batteri eller salja overskott

**Kursmål:** Demonstrera systemprogrammering i C och C++ med trådar, synkronisering, IPC och minneshantering.

---

## VAR AR VI I KURSEN?

Enligt kursplaneringen:

| Vecka | Ämne | Status |
|-------|------|--------|
| 1-3 | Processer, trådar, synkronisering | KLART |
| 4-5 | IPC (pipes, sockets, delat minne) | KLART |
| **6** | **C++ grunderna (migration påbörjas)** | Pågående |
| **7** | **C++ klasser och objektdesign** | NU |
| **8** | **RAII och resursförvaltning** | Kommande |
| **9** | **STL-integration** | Kommande |
| 10 | Profilering och prestandaanalys | Kommande |
| 11 | Optimering och dokumentation | Kommande |
| 12 | **EXAMINATION** | Deadline |

---

## VAD HAR GRUPPEN BYGGT?

### Arkitekturöversikt

```
                    ┌─────────────────────┐
                    │      main.c         │  <- Entry point (34 rader)
                    │   Server_Initiate   │
                    └─────────┬───────────┘
                              │
        ┌─────────────────────┼─────────────────────┐
        │                     │                     │
        ▼                     ▼                     ▼
┌───────────────┐    ┌────────────────┐    ┌──────────────────┐
│  TCPServer    │    │  ThreadPool    │    │    Pipeline      │
│  (port 8080)  │    │  (20 workers)  │    │ (3 trådar)       │
│               │    │                │    │                  │
│ Accepterar    │    │ Hanterar       │    │ Fetch → Parse    │
│ klienter      │    │ klienters I/O  │    │    → Compute     │
└───────────────┘    └────────────────┘    └──────────────────┘
```

### Pipeline-arkitekturen (Producer-Consumer)

```
┌──────────────┐     ┌──────────────┐     ┌──────────────┐
│  FETCH-TRÅD  │────▶│  PARSE-TRÅD  │────▶│ COMPUTE-TRÅD │
│              │     │              │     │              │
│ Hämtar data  │     │ Parsar JSON  │     │ Beräknar     │
│ från API:er  │     │ till structs │     │ energiplan   │
│ (libcurl)    │     │ (cJSON)      │     │ (MOCK)       │
└──────────────┘     └──────────────┘     └──────────────┘
      │                    │                    │
      ▼                    ▼                    ▼
  fetchQueue          parseQueue          Svar till
  (mutex+cond)        (mutex+cond)        klient
```

### Mappstruktur

```
src/
├── server/           # Server-koordinering
│   ├── main.c        # Entry point
│   └── Server.h/c    # Orchestration
│
├── pipeline/         # Dataflödeskomponenter
│   ├── Fetcher.h/c   # HTTP API-hämtning (libcurl)
│   ├── Parser.h/c    # JSON-parsing (cJSON)
│   └── Compute.h/c   # Energiberäkningar (MOCK!)
│
├── threads/          # Tråd- och köhantering
│   ├── PipelineThreads.h/c  # 3 pipeline-trådar + queues
│   └── ThreadPool.h/c       # Worker-trådar för klienter
│
├── tcp/              # Nätverkskommunikation
│   ├── TCPServer.h/c # C-baserad server
│   └── TCPClient.hpp/cpp  # C++ klient (MINIMAL!)
│
├── data/             # Datastrukturer
│   ├── EnergyData.h/c      # Energiplan
│   ├── OpenMeteoData.h     # Väderdata
│   └── ElprisetData.h      # Spotprisdata
│
├── common/           # Gemensamma verktyg
│   ├── Logger.h/c          # Loggning
│   └── SignalHandler.h/c   # SIGINT/SIGTERM
│
├── api/              # API-endpoints
│   └── APIEndpoints.h/c    # URL-byggning
│
└── tests/            # Tester
    ├── test_api_fetch.c
    ├── test_logger.c
    └── test_pipeline.c
```

---

## VIKTIGA KONCEPT ATT FÖRSTÅ

### 1. Trådhantering (pthread)

Servern använder POSIX-trådar. Viktiga funktioner:
- `pthread_create()` - Skapa tråd
- `pthread_join()` - Vänta på tråd
- `pthread_mutex_lock/unlock()` - Låsa/låsa upp mutex
- `pthread_cond_wait/signal()` - Villkorsvariabler

**Se:** `src/threads/PipelineThreads.c` för producer-consumer implementation

### 2. Queue med Mutex och Condition Variables

```c
// Från PipelineThreads.c - Producer-consumer mönster
pthread_mutex_lock(&queue->mutex);
while (Queue_IsFull(queue)) {
    pthread_cond_wait(&queue->notFull, &queue->mutex);
}
Queue_Push(queue, item);
pthread_cond_signal(&queue->notEmpty);
pthread_mutex_unlock(&queue->mutex);
```

### 3. Dependency Injection (ingen global state)

Efter refaktorering 2026-02-12:
- `Server` äger `Pipeline`, `ThreadPool`, `TCPServer`
- `ThreadPool` får referens till `Pipeline` vid initiering
- Inga globala variabler

### 4. TCP Socket-programmering

Se `src/tcp/TCPServer.c`:
- `socket()` → `bind()` → `listen()` → `accept()`
- `getaddrinfo()` för IPv4/IPv6-stöd

---

## VAD BEHÖVER GÖRAS?

### STATUS - Vad som är KLART

| Komponent | Status | Kommentar |
|-----------|--------|-----------|
| Server-arkitektur (C) | KLART | main.c, Server.h/c |
| Pipeline med 3 trådar | KLART | Fetch/Parse/Compute-trådar |
| Queue-system | KLART | Mutex + condition variables |
| API-integration | KLART | Open-Meteo + elpriset.se |
| TCP-server | KLART | Accepterar klienter |
| Logger | KLART | Färgkodad loggning |
| Signalhantering | KLART | SIGINT/SIGTERM |
| Thread pool | KLART | 20 workers |
| Tester | DELVIS | 3 tester finns |

### STATUS - Vad som SAKNAS / BEHÖVER ARBETE

| Komponent | Status | Prioritet |
|-----------|--------|-----------|
| **Compute-modul** | MOCK | HÖG - Energiberäkningar är fake |
| **C++ klient** | MINIMAL | HÖG - Bara 11 rader |
| **RAII-klasser** | SAKNAS | HÖG - Kursens fokus vecka 8 |
| **STL-användning** | SAKNAS | HÖG - Kursens fokus vecka 9 |
| Profilering | SAKNAS | MEDIUM - Vecka 10 |
| Dokumentation | DELVIS | MEDIUM - Behöver kompletteras |
| Thread-safety i komponenter | DELVIS | LÅG - Påpekat i changelog |

---

## HUR DU KAN HJÄLPA GRUPPEN

### Alternativ 1: C++ Klient (REKOMMENDERAS)

Klienten (`src/tcp/TCPClient.hpp/cpp`) är nästan tom. Du kan:

1. **Skapa klasser för energidata**
   - `EnergyPlan` klass med konstruktor/destruktor
   - Använd `std::vector` för entries
   - Använd `std::string` för text

2. **Implementera RAII-wrappers**
   - `SocketRAII` - Stänger socket i destruktor
   - `FileRAII` - Hanterar filer

3. **Använd STL**
   - `std::unique_ptr` för ägande
   - `std::map` för cache
   - STL-algoritmer (sort, find, transform)

**Exempel - RAII Socket wrapper:**
```cpp
class SocketRAII {
private:
    int fd;
public:
    explicit SocketRAII(int socket_fd) : fd(socket_fd) {}
    ~SocketRAII() { if (fd >= 0) close(fd); }

    // Rule of Five
    SocketRAII(const SocketRAII&) = delete;
    SocketRAII& operator=(const SocketRAII&) = delete;
    SocketRAII(SocketRAII&& other) noexcept : fd(other.fd) { other.fd = -1; }
    SocketRAII& operator=(SocketRAII&& other) noexcept {
        if (this != &other) { close(fd); fd = other.fd; other.fd = -1; }
        return *this;
    }

    int get() const { return fd; }
};
```

### Alternativ 2: Compute-modulen

`src/pipeline/Compute.c` är en mock. Du kan:

1. Implementera riktiga energiberäkningar
2. Beräkna solcellsproduktion baserat på solinstrålning
3. Optimera energianvändning baserat på spotpriser

### Alternativ 3: Tester och dokumentation

- Skriva fler enhetstester
- Dokumentera API:er med Doxygen-kommentarer
- Skapa klassdiagram

---

## BUILD OCH TEST

### Kompilera

```bash
cd /home/alex/GridGuard
make              # Bygger allt
make debug        # Debug-build
make release      # Optimerad build
```

### Köra tester

```bash
make test         # Kör alla tester
make test-api     # Bara API-test
make test-pipeline # Bara pipeline-test
```

### Starta servern

```bash
./bin/server      # Startar på port 8080
```

### Minnesanalys

```bash
make valgrind-server  # Kolla minnesläckor
make helgrind         # Kolla thread-safety
```

---

## KURSMÅL ATT UPPFYLLA

Från kursplaneringen - dessa ska ni demonstrera:

**Kunskaper:**
1. Förklara processhantering, trådar, synkronisering, minne
2. Redogöra för IPC (pipes, sockets, delat minne)
3. Förklara skillnader mellan C och C++
4. Redogöra för RAII
5. Förklara STL (vector, string, unique_ptr)
6. Förklara profilering

**Färdigheter:**
7. Implementera flertrådade program med synkronisering
8. Använda IPC-lösningar
9. Implementera C++ med RAII och STL
10. Utföra profilering
11. Optimera kod baserat på mätdata
12. Dokumentera design och prestandaöverväganden

---

## SNABBSTART FÖR ATT KOMMA IGÅNG

1. **Läs genom koden**
   - Börja med `src/server/main.c` (34 rader)
   - Sedan `src/server/Server.c`
   - Sedan `src/threads/PipelineThreads.c`

2. **Bygg och kör**
   ```bash
   make && ./bin/server
   ```

3. **Kör testerna**
   ```bash
   make test
   ```

4. **Välj ett område att bidra till**
   - C++ klient med RAII/STL (rekommenderas)
   - Compute-modulen
   - Tester/dokumentation

5. **Fråga gruppen**
   - Vad behöver de mest hjälp med just nu?
   - Vilka filer jobbar de på?

---

## KONTAKTA MIG

Om du har frågor om koden eller vill ha hjälp att förstå specifika delar, fråga gärna! Lycka till!
