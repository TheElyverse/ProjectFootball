# Tactic file format

Tactics are data: tactic files under `data/tactics/` describe them, so a
tactical identity can be tuned without recompiling. The `sim-tactics` library
reads and writes them (`tacticJson.hpp`); the model they describe is in
[tactics.md](tactics.md).

## Version 1

```json
{
  "format": "elyverse-tactic",
  "version": 1,
  "name": "reference",
  "description": "A neutral 1-2-1-3 for tests and hand-built fixtures.",
  "slots": [
    { "position": { "depth": 0.04, "width": 0.5 }, "role": "goalkeeper" },
    {
      "position": { "depth": 0.2, "width": 0.3 },
      "responsibilities": [
        { "responsibility": "holdDefensiveLine", "weight": 1.0 },
        { "responsibility": "markOpponent", "weight": 0.6 }
      ]
    }
  ],
  "principles": {
    "pressingLine": 0.6,
    "pressingTriggers": ["backPass", "receiverFacingOwnGoal"],
    "positioning": {
      "targetDistance": 1.0,
      "spacing": 1.0,
      "pressure": 0.5,
      "occupancy": 0.5,
      "transitionRisk": 0.5
    }
  },
  "phases": {
    "buildUp": {
      "lineHeight": 0.35,
      "blockLength": 0.45,
      "blockWidth": 0.85,
      "ballShift": 0.4,
      "pressingIntensity": 0.3,
      "passingRisk": 0.5,
      "runFrequency": 0.3
    }
  }
}
```

The example is shortened: a file has seven slots and an entry for each of the
seven phases.

| Field              | Type   | Meaning                                                     |
|--------------------|--------|-------------------------------------------------------------|
| `format`           | string | always `"elyverse-tactic"`                                   |
| `version`          | integer| the format version, 1                                        |
| `name`             | string | the tactic's name; a file is named after it (`<name>.json`)  |
| `description`      | string | optional; what the tactic is for and which parameters express it |
| `slots`            | array  | seven slots, in line-up order                                |
| `slots[].position` | object | `depth` and `width`, fractions of the pitch                  |
| `slots[].role`     | string | a role preset, expanded into its responsibilities on load    |
| `slots[].responsibilities` | array | `{ "responsibility", "weight" }` entries          |
| `principles`       | object | `pressingLine`, `pressingTriggers`, `positioning` weights    |
| `phases`           | object | one instruction per phase, keyed by phase name               |

Every field is required except `description`. A slot has either `role` or
`responsibilities`, not both: a preset plus extra duties would hide what the
slot really holds. Names are spelled as in [tactics.md](tactics.md): phases
(`buildUp`, `progression`, ...), responsibilities (`provideWidth`, ...), role
presets (`winger`, ...) and pressing triggers (`backPass`, ...). Values and
their ranges are the model's.

## Loading

`loadTactic(path)` and `parseTacticJson(text, source)` produce the same
validated `Tactic` that `Tactic::create()` builds in code and apply all of its
rules. They reject:

| Code                  | Cause                                                          |
|-----------------------|----------------------------------------------------------------|
| `kIoError`            | the file cannot be read                                        |
| `kMalformed`          | not JSON, another `format`, a missing, mistyped or unknown field, an unknown name |
| `kUnsupportedVersion` | a `version` other than 1                                        |
| `kInvalidTactic`      | the tactic breaks a rule of the model; every broken rule is listed |

Every message names the file and the offending field:

```text
data/tactics/pressing.json: phases.pressing.lineHeight: 1.3 must lie in [0, 1]
```

Unknown fields are errors rather than ignored, so a misspelled key cannot
silently fall back to nothing.

Loading is deterministic: the same file always yields an equal tactic.
`contentHash()` (`tacticHash.hpp`) hashes the loaded model, not the text, so
whitespace, key order or a role written out as its responsibilities do not
change it; replays record it to name the tactic a team played.
`toTacticJson()` writes a tactic back, with responsibilities written out, and
reading that text yields an equal tactic.

## Versioning

`version` counts up by one when a reader written against this document would
misread a new file: a field removed, renamed, or changed in type or meaning.
Readers accept exactly the version they know and reject every other one with
an error naming it. A new optional field with a documented default does not
need a new version; a new required one does.

| Version | Change          |
|---------|-----------------|
| 1       | Initial format. |

## Files

| File                          | Tactic                                               |
|-------------------------------|------------------------------------------------------|
| `data/tactics/reference.json` | the neutral reference tactic of `referenceTacticSpec()` |
| `data/tactics/possession.json` | keeps the ball, counterpresses ([tactical identities](tactical-identities.md)) |
| `data/tactics/counter.json`   | deep block, vertical after a regain                  |
| `data/tactics/pressing.json`  | high line, presses on every trigger                  |

`tests/unit/sim-tactics/tacticFilesTests.cpp` loads every file in
`data/tactics/`, checks that each is named after its tactic, and that
`reference.json` equals the reference tactic built in code.
