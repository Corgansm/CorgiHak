# Resolver validation

## Automated regression

From a Visual Studio Developer Command Prompt, compile `tests/resolver_evidence.cpp`
with `/EHsc /std:c++17` and run the resulting executable. Assertions must remain
enabled (do not define `NDEBUG`). The test covers playback ties, invalid rates,
unique versus overlapping hitgroup evidence, unreliable outcomes, spread or
obstruction rays, bounded penalties, and recovery after contradictory evidence.

Build the game project with the installed toolset:

```text
MSBuild havoc-sdk.sln /t:Build /p:Configuration=Release /p:Platform=x86 /p:PlatformToolset=v145
```

The project still specifies v143; this command overrides it without retargeting
the saved project.

## Required in-game validation (not performed by the automated test)

Use a controlled local server with access to authoritative hitboxes. For each
case, record simulation tick, chosen side, reconstructed head position, actual
server head position at the shot's rewind tick, hitgroup, and result. Compare
position/orientation error and hit rate with the previous build at the same
latency, weapon, distance, and movement inputs.

- Static left/right desync and low-delta variants.
- Two-state jitter and delayed jitter, including yaw wrapping at +/-180 degrees.
- Walking, running, crouching, jumping, and landing transitions.
- Separate double-tap outcomes and delayed event delivery. Ambiguous event
  associations must not change confidence.
- Teammate damage and grenade damage while a bullet is pending: neither should
  be credited to that bullet.
- Defensive rollback: no shifted animation enters clean history, and only
  previously valid records can be targeted. If none remain, the resolver waits.
- Respawn and player-slot reuse: previous-life evidence must not be applied.

The resolver currently compares the three existing independently simulated
animation candidates. Playback thresholds are heuristics, and these tests do not
establish that any candidate exactly matches the server. Additional low-delta
candidates should be introduced with measured animation evidence, not by rotating
a skeleton and treating it as a server-authoritative pose.
