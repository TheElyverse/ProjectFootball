# Elyverse: Football — Implementation Plan

*Technische Architektur, Roadmap und Engineering-Plan*

> Unreal Engine 5 + C++
> Headless Simulation Core
>
> Status: Pre-Production Engineering Baseline

**Design-Leitmotiv**

*„Maximale Simulationstiefe bei minimaler Verwaltungsarbeit.“*

## Inhalt

| **1**  | Ziele und technische Leitplanken               |
| ------ | ---------------------------------------------- |
| **2**  | Systemarchitektur                              |
| **3**  | Repository & Build                             |
| **4**  | Domänenmodell und Datenhaltung                 |
| **5**  | Simulation Clock, Determinismus und Randomness |
| **6**  | Match Simulation Core                          |
| **7**  | Taktiksystem                                   |
| **8**  | Spielermodell                                  |
| **9**  | Training, Entwicklung und Fitness              |
| **10** | World Simulation & Club AI                     |
| **11** | Delegation & Application Layer                 |
| **12** | Unreal Integration und Match Presentation      |
| **13** | UI-Architektur                                 |
| **14** | Persistence und Savegames                      |
| **15** | Analytics, Telemetrie und Debugging            |
| **16** | Testing-Strategie                              |
| **17** | Performance und Skalierung                     |
| **18** | CI/CD und Engineering Practices                |
| **19** | Roadmap und Meilensteine                       |
| **20** | Backlog des ersten Prototyps                   |
| **21** | Risiken und Gegenmaßnahmen                     |
| **22** | Definition of Done / Exit Criteria             |

## 1. Ziele und technische Leitplanken

> **Primäres Engineering-Ziel**
>
> Eine deterministische, headless ausführbare Fußballsimulation, die unabhängig von Unreal getestet, gebenchmarkt und millionenfach wiederholt werden kann.

- Simulation Core möglichst frei von Unreal-Typen und UObject-Lifecycle.
- Datenorientierte, serialisierbare Zustände statt großer Objektgraphen.
- Determinismus: gleicher Initialzustand + Commands + Seed = gleicher Verlauf.
- Same systems for player and AI: keine parallele „KI-Abkürzungslogik“ für Kernworkflows.
- Unreal ist Adapter/Presentation Layer, nicht Source of Truth für Match- oder Weltlogik.
- Automatisierte Tests und Telemetrie sind Bestandteil des Designs, nicht Nacharbeit.

### 1.1 Vorläufiger Tech Stack

| **Bereich**      | **Entscheidung**                          | **Kommentar**                                                  |
| ---------------- | ----------------------------------------- | -------------------------------------------------------------- |
| Engine           | Unreal Engine 5 / C++                     | 3D-Präsentation, Animation, Audio, UI-Host.                    |
| Simulation Core  | Standard C++20/23                         | Ohne UE-Abhängigkeit; portabel und headless.                   |
| Build Core       | CMake + Ninja                             | Schnelle lokale Builds; eigenständig testbar.                  |
| Unreal Build     | Unreal Build Tool                         | Adapter-Module und Game Target.                                |
| Tests            | Catch2 oder GoogleTest                    | Entscheidung per Spike; beide geeignet.                        |
| Persistence      | SQLite                                    | Relationale, transaktionale Savegame-Basis.                    |
| Data Definitions | JSON/YAML → validierte interne Strukturen | Menschenlesbare Design-Daten; Schema-Validierung erforderlich. |
| Version Control  | Git + Git LFS                             | LFS nur für binäre Unreal-Assets.                              |
| CI               | GitHub Actions / vergleichbare Runner     | Core auf Linux/Windows; Unreal Build mindestens Windows.       |

## 2. Systemarchitektur

```text
World / Match Simulation Core
↑
Application Layer (Commands, Queries, Events, Responsibilities)
↑
Adapters
├─ CLI / Benchmarks
├─ Persistence
└─ Unreal Engine
├─ UI
├─ Match Presentation
├─ Animation / Audio
└─ Input
```

### 2.1 Module

| **Modul**     | **Verantwortung**                                     | **Darf kennen**                |
| ------------- | ----------------------------------------------------- | ------------------------------ |
| sim-core      | IDs, Zeit, RNG, Math, Events, Basistypen              | STL                            |
| sim-player    | Capabilities, Match-/World-State, Development         | sim-core                       |
| sim-tactics   | Principles, Phases, Responsibilities, spatial targets | sim-core, sim-player contracts |
| sim-match     | Pitch, Ball, Perception, Decisions, Actions, rules    | sim-core/player/tactics        |
| sim-world     | Calendar, clubs, competitions, economy, careers       | sim-core/player                |
| sim-ai        | Club planning, coach decisions, staff behavior        | domain contracts               |
| sim-analytics | Events, metrics, explanations                         | read-only domain events        |
| app           | Commands, Queries, orchestration, permissions         | all domain modules             |
| persistence   | SQLite mapping, migrations, snapshots                 | app/domain DTOs                |
| ue-adapter    | State stream, input mapping, presentation DTOs        | app public API                 |

### 2.2 Dependency Rule

Abhängigkeiten zeigen nach innen. `sim-match` darf Unreal nicht kennen. Unreal erhält immutable Frames / View Models und sendet Commands zurück. Domain Events werden nicht direkt an Widgets gekoppelt.

## 3. Repository & Build

```text
project-football/
├─ CMakeLists.txt
├─ cmake/
├─ libs/
│ ├─ sim-core/
│ ├─ sim-player/
│ ├─ sim-tactics/
│ ├─ sim-match/
│ ├─ sim-world/
│ ├─ sim-ai/
│ └─ sim-analytics/
├─ apps/
│ ├─ sim-cli/
│ ├─ sim-benchmark/
│ ├─ sim-replay/
│ └─ unreal-game/
├─ data/
│ ├─ schemas/
│ ├─ tactics/
│ ├─ competitions/
│ └─ fixtures/
├─ tests/
│ ├─ unit/
│ ├─ integration/
│ ├─ tactical/
│ └─ statistical/
└─ tools/
```

- CMake Presets für Debug, RelWithDebInfo, Release und Sanitizer.
- Compiler-Warnings streng behandeln; optional `-Werror` in CI für Core.
- clang-format + clang-tidy mit versionierter Konfiguration.
- Pre-commit optional, CI verpflichtend.
- Third-party dependencies über CPM.cmake, vcpkg oder Conan; früh eine Strategie festlegen und locken.

## 4. Domänenmodell und Datenhaltung

### 4.1 IDs statt Pointer-Netze

```cpp
using PlayerId = StrongId<PlayerTag, uint32_t>;
using ClubId = StrongId<ClubTag, uint32_t>;
using MatchId = StrongId<MatchTag, uint64_t>;
```

Lang lebende Referenzen werden über stabile IDs abgebildet. Dadurch werden Serialisierung, Replay, Snapshotting und Tests einfacher.

### 4.2 Komponentenorientierter Player State

```text
PlayerId
├─ Identity
├─ PhysicalProfile
├─ TechnicalCapabilities
├─ CognitiveCapabilities
├─ Personality
├─ TacticalKnowledge
├─ CareerState
├─ ContractState
├─ FitnessState
└─ DevelopmentState
```

### 4.3 Commands / Events

| **Typ**       | **Beispiele**                                      | **Regel**                                  |
| ------------- | -------------------------------------------------- | ------------------------------------------ |
| Commands      | SetTacticalPrinciple, OfferContract, DelegateTask  | Intent; validiert gegen aktuellen Zustand. |
| Domain Events | PlayerJoinedClub, MatchFinished, TrainingCompleted | Unveränderliche Tatsachen.                 |
| Queries       | GetSquadView, GetActionQueue, GetMatchAnalysis     | Read-only; keine Seiteneffekte.            |

## 5. Simulation Clock, Determinismus und Randomness

### 5.1 Zeitebenen

| **Kontext**      | **Zeiteinheit**                              |
| ---------------- | -------------------------------------------- |
| World Simulation | Minuten / Stunden / Tage                     |
| Training         | Sessions / Minuten                           |
| Match            | Fixed simulation ticks + scheduled decisions |
| Presentation     | beliebige Render-FPS, interpoliert           |

### 5.2 RNG-Strategie

- Seed pro Match / Simulation Session persistieren.
- Explizite RNG-Streams pro Domäne erwägen (Execution, injuries, generation), um Änderungen zu isolieren.
- Keine Nutzung globaler nicht-deterministischer Zufallsquellen im Core.
- Randomness modelliert Unsicherheit; sie ersetzt keine Ursache.

### 5.3 Replay Contract

> Replay = InitialSnapshot + OrderedCommands + Seed(s) + CoreVersion

Jeder reproduzierbare Bugreport soll auf dieses Format reduzierbar sein. Bei Änderungen am Core kann ein Replay-Kompatibilitätsflag notwendig werden.

## 6. Match Simulation Core

### 6.1 Iterationsstufen

| **Stufe** | **Inhalt**                             | **Exit**                                                  |
| --------- | -------------------------------------- | --------------------------------------------------------- |
| M0        | 2D pitch, 7v7, Ball, einfache Bewegung | Stabile Ticks, deterministische Replays.                  |
| M1        | Perception + passing + receiving       | Passketten und Interceptions entstehen plausibel.         |
| M2        | Off-ball movement + pitch control      | Support, Räume und Abstände reagieren auf Spielsituation. |
| M3        | Pressing + defensive coordination      | Pressing kann koordiniert gelingen oder scheitern.        |
| M4        | Shots + goalkeeper + simple duels      | Vollständige open-play possessions.                       |
| M5        | 11v11 + rules baseline                 | Plausible ganze Matches ohne Set Pieces.                  |
| M6        | Set Pieces, fouls, offside, aerial     | Regelvollständiger Basisfußball.                          |

### 6.2 State

```cpp
struct PlayerMatchState {
  Vec2 position;
  Vec2 velocity;
  float orientation;
  EnergyState energy;
  ActionState action;
  PerceptionState perception;
  TacticalRuntimeState tactical;
};
```

### 6.3 Frequenzen

Movement/Physics ca. 20–30 Hz, Ball 30–60 Hz, Perception 5–10 Hz, Decision Making 2–10 Hz adaptiv, Tactical Evaluation 1–5 Hz. Werte müssen profiliert und nicht dogmatisch behandelt werden.

### 6.4 Spatial Services

- Pitch coordinate system in Metern.
- Spatial index für Nearby Queries.
- Occupancy / pressure grid als Cache.
- Arrival-time / pitch-control service.
- Passing-lane geometry und interception windows.
- Semantic zones nur als Derived Data, nicht als harte Spielfeldlogik.

### 6.5 Perception Pipeline

1. Potentiell sichtbare Objekte per Spatial Query ermitteln.
2. Vision cone / peripheral vision / distance / orientation bewerten.
3. Attention und pressure berücksichtigen.
4. ObservedEntity mit estimatedPosition, velocity, confidence und lastSeen aktualisieren.
5. Vergessene / unsichere Beobachtungen graduell abbauen.

### 6.6 Decision Pipeline

> PerceptionState → CandidateGenerator → UtilityEstimator → ChoicePolicy → ActionPlanner → ExecutionModel

- CandidateGenerator reduziert den Suchraum aggressiv.
- Utility berücksichtigt Completion, Progression, TacticalFit, Risk und ReceiverAdvantage.
- ChoicePolicy nutzt z. B. temperaturgesteuerte Softmax statt immer `argmax`.
- Cognitive Fähigkeiten beeinflussen Schätzung und Auswahl; Personality beeinflusst Präferenzen.
- ExecutionModel nutzt Technik, Druck, Balance, Fuß, Müdigkeit und Ballzustand.

### 6.7 Ballmodell

Mindestens Position, 3D velocity, spin und contact state. Erste Version pragmatisch: Gravitation, Luft-/Bodenwiderstand, Bounce, Reibung. Deterministische Core-Physik priorisieren; Unreal darf visuell interpolieren, aber nicht das Ergebnis neu bestimmen.

## 7. Taktiksystem

### 7.1 Datenmodell

```text
Tactic
├─ BaseShape
├─ TeamPrinciples[]
├─ PhaseInstructions[Phase]
├─ PlayerResponsibilities[Slot]
└─ SetPiecePlans[]
```

Bekannte Rollen werden als Presets/Editor-Hilfen implementiert, die Responsibilities befüllen. Runtime arbeitet mit den atomaren Verantwortlichkeiten.

### 7.2 Desired Region

> PositionCost = TacticalTargetDistance + SpacingPenalty + PressureCost + OccupancyPenalty + TransitionRisk

Spieler optimieren nicht auf einen fixen Punkt, sondern auf eine lokale Kostenlandschaft. Das System muss hysteresis / smoothing besitzen, damit Spieler nicht zwischen ähnlichen Zielen oszillieren.

### 7.3 Tactical Familiarity

- FormationKnowledge
- ResponsibilityKnowledge
- TeamPrincipleKnowledge
- TeammateFamiliarity

## 8. Spielermodell

### 8.1 Interne Fähigkeiten vs. UI-Werte

Interne Fähigkeiten werden kontinuierlich (z. B. 0..1) gespeichert. UI-Werte oder Sterne sind Projektionen des Coach-/Scout-Wissens und können 1–20, Kategorien oder Range-Darstellungen verwenden.

### 8.2 Capability Families

| **Familie** | **Beispiele**                                      | **Matchwirkung**                        |
| ----------- | -------------------------------------------------- | --------------------------------------- |
| Physical    | acceleration, maxSpeed, agility, strength          | Bewegung, Duelle, Erreichbarkeit        |
| Technical   | firstTouch, shortPass, longPass, finishing         | Ausführungsfehler und mögliche Aktionen |
| Cognitive   | scanRate, awareness, anticipation, decisionQuality | Perception, Optionenschätzung, Reaktion |
| Personality | risk, composure, professionalism, creativity       | Choice bias, Entwicklung, Stabilität    |
| Tactical    | principle knowledge, role knowledge                | Positionierung, Koordination            |
| Habits      | preferred channels, body orientation, run types    | wiederkehrender individueller Stil      |

### 8.3 Observation Model

Scouting speichert Evidence statt sofort finaler Werte. Ein Assessment aggregiert Beobachtungen und erzeugt estimate + confidence + freshness. Eigene Trainer kennen Spieler besser, aber nicht perfekt.

### 8.4 Prozedurale Spielergenerierung

- M0 und die frühe Match Sandbox verwenden feste Spieler-Fixtures mit stabilen IDs; prozedurale Generierung liegt außerhalb ihres Scopes.
- P4 implementiert einen ersten Generator für Identität, Fähigkeitsprofile und latente Entwicklungsparameter auf Basis des Spielermodells. Profile berücksichtigen plausible Zusammenhänge statt ausschließlich unabhängiger Zufallswerte.
- P5 nutzt den Generator zum Aufbau der initialen Mini-League-Kader. Wiederkehrende Nachwuchsgenerationen mit nationalen und clubbezogenen Einflüssen werden bis P9 ausgebaut.
- Gleicher Seed, gleiche Eingaben und gleiche Generatorversion erzeugen dieselben Profile. Ein eigener RNG-Stream isoliert Generierung von der Matchsimulation.
- Jeder erzeugte Spieler erhält eine eindeutige, persistente PlayerId. Die ID identifiziert den Spieler; sie bestimmt nicht seine Eigenschaften. Savegames speichern die erzeugten Zustände und IDs, statt sie beim Laden neu zu generieren.
- Tests prüfen Reproduzierbarkeit, eindeutige IDs, gültige Wertebereiche und plausible Profilverteilungen.

## 9. Training, Entwicklung und Fitness

### 9.1 Training Session Contract

```text
TrainingSession {
  objectives[];
  intensity;
  participants[];
  coachAssignments[];
  tacticalContext;
}
→ TrainingResult { capabilityDeltas, familiarityDeltas, fatigue, injuryRisk }
```

### 9.2 Entwicklung

- Latente Entwicklungsparameter pro Capability/Family statt einzelne PA-Zahl.
- Alter, biologische Entwicklung, Professionalität, Coaching, Minuten, Challenge und Gesundheit kombinieren.
- Regression explizit modellieren; körperliche und kognitive Kurven dürfen unterschiedlich sein.
- Statistische Kalibrierung über viele simulierte Karrieren.

### 9.3 Fitness

Mindestens Energy, muscular fatigue, sprint capacity und mental fatigue. World-/Training-State und Match-State müssen sauber getrennt, aber gekoppelt sein.

## 10. World Simulation & Club AI

### 10.1 World Tick

World Simulation verarbeitet geplante Ereignisse, Kalender und periodische Systeme. Nicht jedes System läuft täglich. Verträge, Scouting, Entwicklung und Wirtschaft haben passende Frequenzen.

### 10.2 Club Planning

> AssessSquad → IdentifyNeeds → AllocateBudget → ExecuteRecruitment → Reassess

- Need Scores aus Stärke, Tiefe, Alter, Verträgen, taktischem Fit und Youth Pipeline.
- ClubStrategy und StaffPhilosophy modifizieren Prioritäten.
- Finanzmodell liefert harte Constraints.
- KI speichert Reason Codes, damit Transfers nachvollziehbar sind.

### 10.3 Simulation Resolution

Ein Scheduler entscheidet je Wettbewerb / Relevanz über Simulationslevel. Übergänge zwischen Levels benötigen konsistente statistische Outputs und dürfen keine offensichtlichen Exploits erzeugen.

## 11. Delegation & Application Layer

### 11.1 Responsibility Assignment

```text
ResponsibilityAssignment {
  taskType;
  owner: PersonId | User;
  policyId;
  escalationRules[];
}
```

### 11.2 Gemeinsame Workflows

Manuelle und delegierte Ausführung verwenden dieselben Commands. Beispiel Recruitment: der User sendet Commands über UI; ein SportingDirectorAgent sendet dieselben Commands auf Basis seiner Policy und Knowledge Base.

### 11.3 Action Queue

- Generated aus offenen Decisions / Escalations, nicht aus jeder Domain Message.
- Priorität, Deadline, Impact und empfohlenes Default-Verhalten speichern.
- „Advance“ blockiert nur bei konfigurierten kritischen Items.
- Dismiss / delegate / snooze nur, wenn fachlich sinnvoll.

## 12. Unreal Integration und Match Presentation

### 12.1 Adapter Contract

```cpp
struct MatchFrame {
  SimTime timestamp;
  BallFrame ball;
  std::array<PlayerFrame, 22> players;
  EventSlice events;
};
```

Unreal liest Frames und interpoliert. Animationen visualisieren Absicht und Ergebnis, dürfen aber nicht die Core-Kollisionen oder Passergebnisse überschreiben.

### 12.2 Presentation Pipeline

1. Core erzeugt Frames / Events.
2. Adapter übersetzt in UE-freundliche POD/Structs.
3. Presentation interpoliert Transformationsdaten.
4. Animation State Machine / Motion Matching wählt Darstellung.
5. Camera Director und Audio reagieren auf Domain Events.

### 12.3 Entwicklung vor 3D

Vor dem ersten hochwertigen 3D-Spieltag soll ein 2D-Debug-Viewer existieren. Er zeigt Spieler, Ball, Vision Cones, Pitch Control, Zielregionen, Pressing Links und Decision Scores.

## 13. UI-Architektur

UI konsumiert Query Models / View Models aus dem Application Layer. Widgets lesen nicht direkt aus veränderlichen Domain-Objekten. Dadurch bleiben Desktop-Workflows, Tests und spätere alternative Frontends beherrschbar.

| **UI-Baustein** | **Technische Anforderung**                                |
| --------------- | --------------------------------------------------------- |
| Command Palette | globaler indexierter Search/Action Service                |
| Tabs / History  | persistierbarer Navigation State                          |
| Custom Tables   | Spalten-Metadaten, Filter, Sortierung, gespeicherte Views |
| Action Queue    | Decision DTOs mit Priority/Deadline/Actions               |
| Analytics       | immutable analysis snapshots / timeseries                 |
| Delegation      | shared workflow state + policy editor                     |

Für die konkrete UE-UI-Technologie sollte ein früher Spike UMG/CommonUI gegen Slate-Lastigkeit evaluieren. PC-Dichte, Tastaturfokus und Tabellenperformance sind Auswahlkriterien.

## 14. Persistence und Savegames

### 14.1 SQLite Schema

- Normalisierte Stammdaten für stabile IDs und Beziehungen.
- JSON/BLOB nur dort, wo Versionierung und Zugriffsmuster es rechtfertigen.
- Savegame-Metadaten: schemaVersion, coreVersion, createdAt, gameTime, seed state.
- Transaktionen für atomare Speichervorgänge.
- Snapshots für große Runtime-Zustände + Event-/Command-Log optional für Diagnose.

### 14.2 Migrationen

Jede persistente Schemaänderung erhält eine vorwärtsgerichtete Migration und einen Test mit repräsentativen Savegames. Savegame-Kompatibilität wird als Produktfeature behandelt, nicht als spätere Aufräumarbeit.

## 15. Analytics, Telemetrie und Debugging

### 15.1 Match Telemetry

- Goals / shots / xG
- Possession / pass completion / pass length
- Progressive actions
- Turnovers
- PPDA / pressures
- Dribbles / duels
- Territory / pitch control
- Decision and execution errors

### 15.2 Explainability Events

Für relevante Aktionen werden Reason Codes gespeichert: was war sichtbar, welche Kandidaten wurden bewertet, welche Utility-Komponenten dominierten und ob Fehler eher aus Perception, Decision oder Execution entstanden.

### 15.3 Replay Debugger

```text
t=2067.4 Player 8
visible: P2, P4, P7, P11
candidates:
pass P7 0.81
carry 0.74
pass P2 0.62
chosen: pass P7
executionError: +1.3m lateral
```

## 16. Testing-Strategie

| **Ebene**   | **Ziel**               | **Beispiele**                                              |
| ----------- | ---------------------- | ---------------------------------------------------------- |
| Unit        | Lokale Regeln          | arrivalTime, vision checks, utility components, contracts  |
| Property    | Invarianten            | keine NaNs, Ballzustand gültig, IDs stabil, Energiegrenzen |
| Integration | Module zusammen        | Pass → receive → possession event; transfer workflow       |
| Replay      | Determinismus          | identischer Hash nach jedem N-ten Tick                     |
| Tactical    | Verhaltensunterschiede | Possession vs Counter vs Pressing                          |
| Statistical | Verteilungen           | Tore, Passquoten, Karten, Entwicklung über 100k+ Läufe     |
| Performance | Budget                 | Simulationszeit pro Match / Weltwoche                      |
| Savegame    | Kompatibilität         | Migrationen und round-trip serialization                   |

### 16.1 Golden Scenarios

Kleine deterministische Szenarien werden versioniert: 3v2 Umschalten, isolierter Flügel, Pressingfalle an Seitenlinie, Steckpass hinter Linie, GK Sweep. Sie dienen als Regressionstests für taktisches Verhalten.

### 16.2 Statistical Guardrails

Statistische Tests verwenden Bandbreiten statt exakte Werte. Änderungen außerhalb definierter Konfidenzbereiche erzeugen CI-Warnung oder Fail, abhängig von Stabilität des Systems.

## 17. Performance und Skalierung

### 17.1 Budgets

| **Ziel**             | **Initiales Budget**                                          |
| -------------------- | ------------------------------------------------------------- |
| Headless 11v11 Match | deutlich schneller als Echtzeit; Zielwert nach M2 benchmarken |
| Viewer Match         | stabile Simulation unabhängig von Render-FPS                  |
| World Day            | unter interaktiver Wartezeit für Mini League                  |
| Batch Benchmark      | tausende Matches parallelisierbar                             |

### 17.2 Optimierungsreihenfolge

1. Profiler und belastbare Benchmarks erstellen.
2. Algorithmische Komplexität und Spatial Queries optimieren.
3. Caches / data-oriented layouts nutzen.
4. Adaptive Frequenzen und Simulation Level einsetzen.
5. Parallelisierung erst bei klaren Ownership-Grenzen einführen.
6. SIMD / Spezialoptimierungen zuletzt.

## 18. CI/CD und Engineering Practices

- Pull Requests: Core build + unit/integration/replay tests + formatting/lint.
- Nightly: statistical suite, sanitizer builds, long simulations, save migration corpus.
- Unreal: automatisierter editorless build / cook für definierte Targets, sobald praktikabel.
- Build- und Testartefakte versionieren: benchmark summary, statistical report, replay failures.
- Conventional commits oder klarer Change-Log-Prozess; ADRs für wichtige Architekturentscheidungen.
- Binary assets über Git LFS; keine generierten Build-Ordner im Repository.
- Reproduzierbare Toolchain via dokumentierten Versionen / Setup-Skripten / Container für Core-CI.

### 18.1 Quality Gates

| **Gate**          | **Minimum**                                                                      |
| ----------------- | -------------------------------------------------------------------------------- |
| Merge             | Build grün, relevante Tests grün, keine neuen Lint-Fehler                        |
| Milestone         | Replay deterministisch, Performancebudget dokumentiert, Telemetrie plausibel     |
| Release Candidate | Save migration corpus grün, Long-run simulations stabil, Unreal smoke tests grün |

## 19. Roadmap und Meilensteine

| **Meilenstein**            | **Ergebnis**                                                                   | **Abhängigkeit** |
|----------------------------|--------------------------------------------------------------------------------|------------------|
| P0 – Foundation            | Repo, CMake, CI, strong IDs, clock, RNG, events, CLI                           | keine            |
| P1 – 7v7 Sandbox           | Movement, ball, perception, passing, 2D debug viewer                           | P0               |
| P2 – Tactical Sandbox      | off-ball, pitch control, pressing, 3 tactical identities                       | P1               |
| P3 – 11v11 Baseline        | shots, GK, duels, rules baseline, full matches                                 | P2               |
| P4 – Player Layer          | capabilities, observation, fitness, development skeleton, player generator     | P3               |
| P5 – Mini League           | 4 clubs with generated squads, calendar, training, contracts, simple transfers | P4               |
| P6 – Delegation            | staff agents, policies, action queue, shared workflows                         | P5               |
| P7 – Club AI               | squad planning, recruitment, coach adaptation                                  | P6               |
| P8 – Unreal Vertical Slice | 3D presentation, core UI workflows, one polished matchday                      | P3–P7            |
| P9 – Pre-Alpha World       | mehr Clubs/Ligen, youth generation, simulation levels, savegame hardening      | P8               |

### 19.1 Reihenfolge ist bewusst risk-driven

Die 3D-Produktion wird nach hinten verschoben, weil Matchverhalten, Delegation und Welt-AI die schwersten Produktannahmen sind. Ein hübscher Spieltag darf keine schlechte Simulation kaschieren.

## 20. Backlog des ersten Prototyps

| **ID** | **Area**   | **Story**                                          | **Milestone** |
| ------ | ---------- | -------------------------------------------------- | ------------- |
| PF-001 | Foundation | CMake workspace + core library + CLI executable    | P0            |
| PF-002 | Foundation | StrongId, SimTime, deterministic RNG               | P0            |
| PF-003 | Match      | Pitch + 7v7 entities + fixed tick                  | P1            |
| PF-004 | Match      | Ball state + simple pass trajectory                | P1            |
| PF-005 | Spatial    | Nearby index + arrivalTime                         | P1            |
| PF-006 | Perception | Vision cone + observed entities + confidence decay | P1            |
| PF-007 | Decision   | Pass candidate generation + utility + softmax      | P1            |
| PF-008 | Viewer     | 2D debug renderer / overlay                        | P1            |
| PF-009 | Tactics    | Base shape + responsibilities                      | P2            |
| PF-010 | Tactics    | Desired-region cost function                       | P2            |
| PF-011 | Spatial    | Occupancy / pitch-control approximation            | P2            |
| PF-012 | Defense    | Press / cover / lane block actions                 | P2            |
| PF-013 | Telemetry  | Event stream + match summary                       | P2            |
| PF-014 | Tests      | Three-style tactical benchmark                     | P2            |
| PF-015 | Replay     | Snapshot + commands + seed replay file             | P2            |

### 20.1 Prototype Exit

> **Prototype accepted when**
>
> 7v7/11v11 simulations are deterministic, three tactical identities produce measurably different behavior, and a replay/debugger can explain at least pass, movement and pressing decisions.

## 21. Risiken und Gegenmaßnahmen

| **Risiko**                                               | **Impact** | **Gegenmaßnahme**                                                                               |
| -------------------------------------------------------- | ---------- | ----------------------------------------------------------------------------------------------- |
| Emergentes Verhalten bleibt „matschig“                   | hoch       | Golden scenarios + explainability + staged capabilities; nicht zu viele Parameter gleichzeitig. |
| Performance zu teuer für Welt-Simulation                 | hoch       | Adaptive frequencies, simulation levels, profiling ab P1, headless benchmark CI.                |
| Determinismus bricht durch Floating Point / Parallelität | hoch       | Single-thread deterministic baseline, strict math policy, replay hash tests.                    |
| Taktik-UI wird zu komplex                                | mittel     | Atomare intern, Presets und progressive disclosure extern; frühe UX-Prototypen.                 |
| KI-Transfers wirken trotz System unplausibel             | hoch       | Reason codes, squad plan assertions, scenario tests, market constraints.                        |
| Savegames werden bei Änderungen unwartbar                | mittel     | Schema versioning + migrations + save corpus ab Mini League.                                    |
| Unreal-Integration zieht Domain in UObjects              | mittel     | harte Adaptergrenze, keine UE includes im Core, Architekturtests.                               |
| Zu früher Content-/3D-Fokus                              | hoch       | Milestone-Gates; Presentation erst nach Simulation-Proofs.                                      |

## 22. Definition of Done / Exit Criteria

### 22.1 P0 Foundation

- Core baut auf mindestens Windows + Linux in CI.
- Deterministischer RNG-Test vorhanden.
- CLI kann eine leere Simulation starten und Replay-Metadaten schreiben.
- Format/Lint/Test lokal und CI dokumentiert.

### 22.2 Tactical Prototype

- Mindestens drei Spielstile statistisch unterscheidbar.
- Replay produziert identische Tick-/Event-Hashes.
- Debug-Viewer zeigt Wahrnehmung, Kandidaten und Zielregionen.
- Keine systematischen NaNs / invalid states in Long Runs.

### 22.3 Mini League

- Vier Clubs spielen eine Saison vollständig durch.
- Training und Entwicklung verändern Spielerzustände plausibel.
- Transfers nutzen Squad Needs und Budgets.
- Save/Load round-trip + Migrationstest vorhanden.

### 22.4 Management Vertical Slice

- Recruitment kann vollständig manuell und vollständig delegiert durch denselben Workflow laufen.
- Policies und Eskalationen funktionieren.
- Action Queue enthält ausschließlich echte Entscheidungen.
- KI-Clubs nutzen dieselben Kernregeln.

### 22.5 Unreal Vertical Slice

- Unreal konsumiert Simulation Frames; Core bleibt ohne UE-Abhängigkeit.
- Ein kompletter Matchday ist 3D sichtbar.
- Taktikänderungen während des Spiels werden als Commands an den Core gesendet.
- Performance- und Debug-Metriken sind eingebaut.
