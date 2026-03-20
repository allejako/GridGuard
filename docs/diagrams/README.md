# GridGuard — C4 Diagram

PlantUML-diagram med C4-modellen för systemarkitektur.

## VS Code Extension

Installera följande extension i VS Code:

| Extension | ID | Beskrivning |
|---|---|---|
| PlantUML | `jebbs.plantuml` | Renderar `.puml`-filer med live preview |

## Krav i WSL

Installera Graphviz beroende på din Linux-distribution:

**Ubuntu / Debian:**
```bash
sudo apt install graphviz
```

**Fedora:**
```bash
sudo dnf install graphviz
```

Verifiera installationen:

```bash
dot -V
```

> Java krävs också av PlantUML men är oftast redan installerat. Om du får ett Java-relaterat fel: `sudo apt install default-jdk` (Ubuntu) eller `sudo dnf install java-21-openjdk` (Fedora).

## Visa ett diagram

1. Öppna valfri `.puml`-fil
2. Tryck `Alt+D` för live preview

## Diagramöversikt

| Fil | Typ | Innehåll |
|---|---|---|
| `01_system_context.puml` | C4 Level 1 | GridGuard i relation till externa system och användare |
| `02_containers.puml` | C4 Level 2 | Alla processer, trådar och IPC-kanaler |
| `03_components_server.puml` | C4 Level 3 | Komponenter inuti GridGuard-server |
| `04_sequence_pipeline.puml` | Sekvens | End-to-end pipeline vid cache miss |
| `05_sequence_jwt_auth.puml` | Sekvens | JWT-flödet från plattform till enhet |
