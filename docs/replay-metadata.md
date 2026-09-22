# Replay metadata

`sim-cli` writes a replay metadata file on every successful run. `--replay-out`
selects the path; the default is `replay_metadata.json` in the working directory.
The file is written before the terminal interface opens, so a run rejected by
argument or terminal validation leaves no file behind.

The current file holds metadata only. It is not a complete replayable match
state: there is no initial snapshot and no command log yet. See
[implementation plan](implementation-plan.md) section 5.3 for the replay contract
this file will grow into.

## Schema version 1

```json
{
  "schemaVersion": 1,
  "coreVersion": "0.1.0",
  "createdAt": "2026-09-22T06:32:29Z",
  "seed": "18446744073709551615",
  "gameTime": 0
}
```

| Field | JSON type | Meaning |
| --- | --- | --- |
| `schemaVersion` | number | Version of this schema. Currently `1`. |
| `coreVersion` | string | The `sim-core` version that produced the file. |
| `createdAt` | string | Creation time in UTC, formatted `%Y-%m-%dT%H:%M:%SZ`. |
| `seed` | string | Unsigned 64-bit master seed, in decimal. |
| `gameTime` | number | Simulation tick at the time of writing. |

## The seed is a string on purpose

`seed` is an unsigned 64-bit value serialized as a decimal JSON string, for
example `"18446744073709551615"` for `UINT64_MAX`. The quotes are not cosmetic.
JSON numbers carry no integer guarantee, and many parsers decode them as IEEE 754
doubles, which represent integers exactly only up to 2^53. A larger seed would
come back silently rounded, and a rounded seed is a different simulation.

Consumers must preserve the string or parse it as an unsigned 64-bit integer,
never through a floating-point type. Producers must keep the quotes: removing
them looks like a cleanup and invalidates every recorded replay.

`tests/cli/smoke.cmake` guards this. It asserts that the JSON type of `seed` is a
string, and feeds `UINT64_MAX` back through the CLI to verify the round trip.

## Versioning

`schemaVersion` describes the format of this file. It is a single integer, not a
semver string, and it counts up by one.

Raise it when a reader written against this document would misread a new file:

- a field is removed or renamed,
- a field changes its JSON type,
- a field changes its meaning, unit, or encoding.

Leave it unchanged otherwise. Adding a field is not a breaking change, because
consumers ignore fields they do not know. Neither are key order, indentation, or
line endings: they are not part of the contract, and no consumer may depend on
them.

Consumers read `schemaVersion` first and reject a version they do not know, with
an error naming the version they found. Guessing at an unfamiliar layout is worse
than refusing it, because a misread seed yields a run that looks valid and
reproduces nothing.

Producers write exactly one version, the current one. There is no negotiation and
no writing of older layouts. A change that raises the version updates this
document, the writer in `apps/sim-cli/src/main.cpp`, and the assertions in
`tests/cli/smoke.cmake` in the same commit, and records here whether files of the
previous version stay readable, and by what.

`schemaVersion` and `coreVersion` are independent. The first describes the shape
of the file, the second the simulation that produced it. A change to simulation
behavior -- a different random mapping, a different update order -- invalidates
recorded replays without changing the file format, and surfaces as a new
`coreVersion` value rather than a new `schemaVersion`.

| Version | Change |
| --- | --- |
| 1 | Initial schema. |
