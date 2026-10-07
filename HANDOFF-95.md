# Übergabe: #95 Luftduelle und Kopfbälle

> **Temporär:** Diese Datei wird vor dem Commit bzw. spätestens vor dem Merge wieder
> gelöscht (`git rm HANDOFF-95.md`). Sie gehört nicht ins Repo.

Branch: `feature/#95-aerial-duels`. Noch nichts committet.

## Stand

Implementierung, Tests und Docs sind geschrieben. **Bisher wurde nichts kompiliert und
kein Test ausgeführt**: Auf dem ursprünglichen Rechner (WSL) wird nicht gebaut. Der
Standard-Compiler dort, GCC 11, kennt `<expected>` nicht. Die geänderten Dateien sind mit
clang-format formatiert.

Design und Konfiguration stehen in `docs/aerial-duels.md`.

### Neu
- `libs/sim-match/include/aerialDuels.hpp`, `libs/sim-match/src/aerialDuels.cpp`:
  `AerialConfig`, Sprung (`jumpRise`, `timingSpread`, `reachAt`), `findAerialContact`,
  `findChallengers`, `resolveAerialDuel`/`duelUtility`, `holdChance`, Kopfball-Entscheidung
  (`headerOptions`, `decideHeader`) und Ausführung (`executeHeader`, `headerErrorFactor`).
- `tests/unit/sim-match/aerialDuelsTests.cpp` (in `tests/unit/CMakeLists.txt` eingetragen).
- `docs/aerial-duels.md`.

### Geändert
- `PlayerAttributes`: `jumping`, `heading`, `strength` (Standard 0,5), mit Validierung, Hash
  und Replay-JSON.
- `PlayerTacticalState::lastJump` (mit Hash): 0,6 s lang kein erneuter Sprung.
- Neues Event `AerialContest` (+ `AerialPlay`, `AerialContestant`), mit Hash, Name und
  Debug-Frame-JSON.
- `reception`: gemeinsame Suche `findFirstReach()` sowie `mayCompete()`, `hasHands()` und
  `BallReach::above`.
- `ballMovement.cpp`: `contestInTheAir()` und `playHeader()`. Die Einordnung eines
  angenommenen Balls läuft jetzt über `isPassed()` (die letzte Berührung ist der Schuss des
  letzten Passes) statt `isFromShot()`. Der Writer hat dafür neu `lastPass()`.
- `MatchConfig::aerial`, eine neue Überladung von `makeBallMovementSystem` mit
  `AerialConfig`.
- `unitOr` ist aus `shooting.cpp` nach `vec2.hpp` (sim-core) umgezogen.
- Replay-Schema 8 → 9, Core-Version 0.24.0 → 0.25.0, `tests/cli/smoke.cmake`.
- Docs: replay-format, debug-viewer, ball-movement, reception, shooting, match-events,
  match-state, CLAUDE.md.

## Nächste Schritte auf dem Build-Rechner

1. Bauen (`cmake --preset debug && cmake --build --preset debug`) und Kompilierfehler
   beheben. Da hier nie kompiliert wurde, sind welche wahrscheinlich.
2. `ctest --preset debug --output-on-failure`.
3. Erwartet rot, neu pinnen (Kommentare dabei fortschreiben):
   - `tests/unit/sim-match/matchStateHashTests.cpp`: Kickoff-Hash. Der Kommentar ist schon
     angepasst, der Wert noch nicht.
   - `tests/acceptance/m0AcceptanceTests.cpp`, `tests/acceptance/p1AcceptanceTests.cpp`:
     gepinnte Hashes.
   - Eventuell Golden Scenarios, Shot- und Goalkeeper-Szenarien sowie die Schwellen im
     Stil-Benchmark (`tests/benchmark/p2StyleBenchmark.cmake`): Ein unbedrängter Torwart
     zieht beim Fangen jetzt Zufallszahlen, und hohe Bälle nach Paraden oder Abfälschungen
     werden jetzt geköpft.
4. Die fünf Match-Tests in `aerialDuelsTests.cpp` (Kopfballtor, Klärung, Ball den keiner
   erreicht, zwei Torwarttests) beruhen auf von Hand nachgerechneten Flughöhen. Schlägt
   einer fehl, zuerst diese Spielräume prüfen:
   - Ballhöhe am ersten Kontakt 2,35 m gegen eine Reichweite von 2,2 m bzw. 2,6 m;
   - beim Torwart Kontakt aus 1,2 m Entfernung bei etwa 2,55 m gegen 2,4 m für den
     Angreifer bzw. 2,7 m für den Torwart.
5. `make ci` (Warnings as Errors, Format, clang-tidy).
6. **Diese Datei löschen**, dann committen. Die Commit-Nachricht endet mit `#95`, ohne
   Attribution-Trailer (siehe CLAUDE.md).
