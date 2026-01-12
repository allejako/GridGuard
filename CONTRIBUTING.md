# Bidragsriktlinjer för LEOP-projektet

## Innehållsförteckning
1. [Allmänna principer](#allmänna-principer)
2. [Roller och ansvar](#roller-och-ansvar)
3. [Workflow](#workflow)
4. [Git-konventioner](#git-konventioner)
5. [Kodstandarder](#kodstandarder)
6. [Code Review](#code-review)
7. [Testning](#testning)
8. [Kommunikation](#kommunikation)

---

## Allmänna principer

### Värderingar
- **Samarbete:** Vi hjälper varandra att lyckas
- **Transparens:** Vi kommunicerar öppet om framsteg och problem
- **Kvalitet:** Vi prioriterar hållbar kod framför snabba lösningar
- **Lärande:** Vi ser misstag som möjligheter att utvecklas
- **Respekt:** Vi respekterar varandras tid och bidrag

### Förväntningar
- Delta aktivt i dagliga standups
- Håll deadlines och kommunicera i god tid om förseningar
- Granska andras kod konstruktivt
- Dokumentera dina beslut och kod
- Hjälp till när teammedlemmar behöver stöd

---

## Roller och ansvar

### Roller (exempel - anpassa efter ert team)

| Roll | Ansvar | Person |
|------|--------|--------|
| **Product Owner** | Prioriterar backlog, beslutar om scope | [Namn] |
| **Tech Lead** | Arkitekturbeslut, code reviews | [Namn] |
| **Scrum Master** | Faciliterar möten, tar bort blockeringar | [Namn] |
| **Developer** | Implementation, testning, dokumentation | Alla |

**OBS:** Rollerna är flexibla och kan rotera!

---

## Workflow

### Sprintstruktur

#### Måndag - Boiler Room
- **09:00-13:00:** Implementationsarbete i grupp
- **Före:** Sprint planning (30 min)
- **Efter:** Sprint review (30 min)

#### Tisdag - Checkpoint
- **[Tid]:** Demo av framsteg via Zoom
- Förbered vad ni ska visa
- Identifiera blockeringar

#### Onsdag & Torsdag - Workshops
- **[Tid]:** Genomgångar och code-alongs via Zoom
- Följ med och ställ frågor
- Anteckna viktiga koncept

#### Fredag - Eget arbete
- Individuellt eller i mindre grupper
- Slutför påbörjade tasks
- Förbered för nästa vecka

### Daily Standup (15 min)
Varje arbetsdag, samma tid:
1. Vad gjorde jag igår?
2. Vad ska jag göra idag?
3. Finns det några blockeringar?

---

## Git-konventioner

### Branch-strategi

```
main (production-ready code)
  │
  └─── develop (integration branch)
         │
         ├─── feature/add-weather-api
         ├─── feature/implement-cache
         ├─── bugfix/fix-memory-leak
         └─── refactor/improve-logging
```

### Branch-namngivning

**Format:** `<type>/<beskrivning>`

**Typer:**
- `feature/` - Ny funktionalitet
- `bugfix/` - Buggfixar
- `refactor/` - Kodförbättringar utan funktionsändring
- `docs/` - Dokumentationsändringar
- `test/` - Testning
- `hotfix/` - Kritiska fixar till main

**Exempel:**
```bash
git checkout -b feature/unix-socket-server
git checkout -b bugfix/fix-race-condition
git checkout -b docs/update-api-documentation
```

### Commit-meddelanden

Följ **Conventional Commits**:

```
<type>(<scope>): <subject>

<body>

<footer>
```

**Typer:**
- `feat` - Ny funktionalitet
- `fix` - Buggfix
- `docs` - Dokumentation
- `style` - Formatering (inte påverkar funktionalitet)
- `refactor` - Kod refactoring
- `test` - Lägga till tester
- `chore` - Build, dependencies, etc.

**Exempel:**
```bash
feat(server): add Unix domain socket support

Implemented server-side Unix socket listener that accepts
multiple client connections. Added connection pool for
managing active clients.

Closes #23
```

```bash
fix(cache): resolve race condition in cache access

Added mutex locking around cache read/write operations
to prevent race conditions in multi-threaded environment.

Fixes #45
```

```bash
docs(api): update client protocol documentation

Added examples for all client commands and error responses.
```

### Commit-riktlinjer
- **Atomära commits:** En logisk förändring per commit
- **Beskrivande:** Förklara *vad* och *varför*, inte *hur*
- **Imperativ form:** "Add feature" inte "Added feature"
- **Max 50 tecken** i subject line
- **Referera issues:** Använd "Closes #X" eller "Fixes #X"

---

## Kodstandarder

### C-kod

#### Namngivning
```c
// Funktioner: snake_case
int calculate_solar_production(double irradiance);

// Variabler: snake_case
double solar_irradiance = 0.0;

// Konstanter: UPPER_SNAKE_CASE
#define MAX_BUFFER_SIZE 1024

// Structs: PascalCase
typedef struct {
    int id;
    char name[64];
} WeatherStation;

// Enums: PascalCase för typ, UPPER_CASE för värden
typedef enum {
    STATUS_OK,
    STATUS_ERROR,
    STATUS_TIMEOUT
} StatusCode;
```

#### Formatering
```c
// Indentation: 4 spaces (ingen tabs)
void example_function(int param) {
    if (param > 0) {
        // kod här
    } else {
        // kod här
    }
}

// Klammerparenteser på samma rad för if/for/while
if (condition) {
    // ...
}

// Klammerparenteser på ny rad för funktioner
void function_name(void)
{
    // ...
}

// Mellanslag omkring operatorer
int sum = a + b;
if (x == y) { }

// Pointer-asterisk vid typen
char* string;
int* array;
```

#### Best practices
```c
// Använd const där möjligt
const char* get_config_path(void);

// Initiera alltid variabler
int count = 0;
char* buffer = NULL;

// Felhantering
if (result < 0) {
    log_error("Operation failed: %s", strerror(errno));
    return -1;
}

// Frigör alltid allokerat minne
char* data = malloc(size);
if (data == NULL) {
    return -1;
}
// ... använd data ...
free(data);
```

### C++-kod

#### Namngivning
```cpp
// Klasser: PascalCase
class EnergyOptimizer {
    // ...
};

// Metoder och funktioner: camelCase eller snake_case (välj en!)
void calculateProduction();  // camelCase
void calculate_production(); // snake_case (konsekvent med C)

// Medlemsvariabler: m_prefix + camelCase
class Cache {
private:
    int m_size;
    std::vector<Data> m_entries;
};

// Konstanter: kPrefix + PascalCase
constexpr int kMaxConnections = 100;
```

#### Modern C++ idiom
```cpp
// Använd auto där typen är uppenbar
auto value = calculate_something();
auto it = container.begin();

// Range-based for loops
for (const auto& item : collection) {
    // ...
}

// Smart pointers istället för raw pointers
std::unique_ptr<Cache> cache = std::make_unique<Cache>();
std::shared_ptr<Data> shared_data = std::make_shared<Data>();

// RAII för resurser
{
    std::lock_guard<std::mutex> lock(mutex);
    // kritisk sektion
} // mutex låses upp automatiskt

// Använd STL-algoritmer
std::sort(vec.begin(), vec.end());
auto it = std::find_if(vec.begin(), vec.end(), [](int x) { return x > 10; });
```

#### Klassdefinition
```cpp
class WeatherCache {
public:
    // Konstruktor
    explicit WeatherCache(int capacity);
    
    // Destruktor
    ~WeatherCache();
    
    // Deleted copy constructor (om inte kopiering är meningsfullt)
    WeatherCache(const WeatherCache&) = delete;
    WeatherCache& operator=(const WeatherCache&) = delete;
    
    // Move constructor
    WeatherCache(WeatherCache&& other) noexcept;
    WeatherCache& operator=(WeatherCache&& other) noexcept;
    
    // Publika metoder
    void insert(const WeatherData& data);
    std::optional<WeatherData> get(time_t timestamp) const;

private:
    // Privata medlemmar
    std::map<time_t, WeatherData> m_cache;
    std::mutex m_mutex;
    int m_capacity;
};
```

### Kommentarer

```c
/**
 * Brief description of function.
 * 
 * Detailed explanation if needed.
 * 
 * @param param1 Description of parameter
 * @param param2 Description of parameter
 * @return Description of return value
 */
int function_name(int param1, const char* param2);

// Inline kommentar för komplexa logiska steg
if (complex_condition) {
    // Förklara varför detta behövs
    do_something();
}

// TODO: Beskrivning av framtida arbete
// FIXME: Beskrivning av känt problem
// NOTE: Viktig information
```

### Kompilering

```bash
# C kod
gcc -Wall -Wextra -Werror -std=c11 -pthread -g -o output source.c

# C++ kod
g++ -Wall -Wextra -Werror -std=c++17 -pthread -g -o output source.cpp

# Optimerad build
gcc -Wall -Wextra -O2 -DNDEBUG -o output source.c
```

---

## Code Review

### Innan du begär review

**Checklista:**
- [ ] Koden kompilerar utan warnings
- [ ] Alla tester passerar
- [ ] Valgrind visar inga minnesläckor
- [ ] Kod följer kodstandarder
- [ ] Commit-meddelanden är beskrivande
- [ ] Dokumentation är uppdaterad

### Review-process

1. **Skapa Pull Request**
   ```bash
   git push origin feature/my-feature
   # Gå till GitHub/GitLab och skapa PR
   ```

2. **Fyll i PR-template**
   ```markdown
   ## Beskrivning
   Kort beskrivning av ändringarna
   
   ## Typ av ändring
   - [ ] Ny feature
   - [ ] Buggfix
   - [ ] Refactoring
   - [ ] Dokumentation
   
   ## Checklist
   - [ ] Koden kompilerar
   - [ ] Tester passerar
   - [ ] Dokumentation uppdaterad
   
   ## Screenshots (om relevant)
   ```

3. **Review-guidelines**
   - Minst 1 godkänd review innan merge
   - Fokusera på logik, säkerhet, prestanda
   - Var konstruktiv, inte kritisk
   - Föreslå förbättringar, inte bara peka ut fel

4. **Exempel på bra feedback**
   ```
   ✅ "Kan vi lägga till felhantering här för NULL-pekare?"
   ✅ "Bra lösning! Har du övervägt att cacha detta resultat?"
   ✅ "Denna funktion är lite lång, kanske dela upp i mindre delar?"
   
   ❌ "Detta är fel."
   ❌ "Varför gjorde du så här?"
   ```

### Merge-strategi

- **feature → develop:** Squash merge (rena commit history)
- **develop → main:** Merge commit (bevara historik)
- Ta bort feature-branch efter merge

---

## Testning

### Testtyper

1. **Enhetstester**
   - Testa individuella funktioner
   - Mock externa beroenden
   - Snabba att köra

2. **Integrationstester**
   - Testa komponenter tillsammans
   - End-to-end flöden
   - IPC-kommunikation

3. **Minnestester**
   ```bash
   valgrind --leak-check=full --show-leak-kinds=all ./program
   ```

4. **Thread-safety tester**
   ```bash
   valgrind --tool=helgrind ./program
   valgrind --tool=drd ./program
   ```

### Test-coverage

Sträva efter:
- **Kritiska komponenter:** >80% coverage
- **Övriga komponenter:** >60% coverage

```bash
# Generera coverage report
gcc --coverage -o program program.c
./program
gcov program.c
```

---

## Kommunikation

### Kanaler

| Kanal | Användning | Svarstid |
|-------|------------|----------|
| **Discord/Slack** | Daglig kommunikation, snabba frågor | <30 min |
| **Email** | Formell kommunikation, dokumentation | <24h |
| **GitHub Issues** | Buggar, feature requests | Enligt prioritet |
| **Pull Requests** | Code review, diskussioner | <24h |
| **Zoom** | Möten, par-programmering | Schemalagt |

### Möten

**Daily Standup (15 min)**
- Samma tid varje dag
- Håll det kort och fokuserat
- Djupdykning tas offline

**Sprint Planning (30 min)**
- Måndag före Boiler Room
- Prioritera tasks för veckan
- Fördela ansvar

**Sprint Review (30 min)**
- Måndag efter Boiler Room
- Visa vad som gjorts
- Diskutera vad som gick bra/dåligt

**Retrospektiv (30 min)**
- Var 2:a vecka
- Vad kan vi förbättra?
- Action items för nästa sprint

### Frågor och hjälp

**När du behöver hjälp:**
1. Försök lösa problemet själv (max 30 min)
2. Googla och läs dokumentation
3. Fråga i team-chatten
4. Boka par-programmering session

**När du hjälper andra:**
- Var tålmodig och uppmuntrande
- Förklara konceptet, inte bara lösningen
- Peka på resurser för vidare lärande

---

## Konflikthantering

### Vid oenighet
1. Diskutera respektfullt
2. Förklara dina resonemang
3. Lyssna på andras perspektiv
4. Beslut tas av Tech Lead om konsensus inte nås
5. När beslut är taget, stöd det gemensamt

### Vid problem i teamet
1. Prata direkt med berörd person först
2. Involvera Scrum Master om det inte löser sig
3. Involvera lärare/handledare som sista utväg

---

## Onboarding för nya medlemmar

1. Läs igenom README.md
2. Sätt upp utvecklingsmiljö
3. Gå igenom ARCHITECTURE.md
4. Välj en "good first issue"
5. Gör din första PR med hjälp av mentor

---

## Resurser

### Verktyg
- **Git:** https://git-scm.com/doc
- **Valgrind:** https://valgrind.org/docs/manual/
- **GDB:** https://sourceware.org/gdb/documentation/
- **Doxygen:** https://www.doxygen.nl/manual/

### C/C++ Standarder
- **C11 Standard:** ISO/IEC 9899:2011
- **C++17 Standard:** ISO/IEC 14882:2017
- **Linux man pages:** `man 2 fork`, `man 3 pthread_create`, etc.

### Best Practices
- **Clean Code** - Robert C. Martin
- **The Pragmatic Programmer** - Hunt & Thomas
- **C++ Core Guidelines:** https://isocpp.github.io/CppCoreGuidelines/

---

**Dokument uppdaterat:** [DATUM]  
**Ansvarig:** [TEAM]

