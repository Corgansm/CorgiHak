# Resolver recording review

Reviewed the 107 `aa_1792533090` parts, 295 `aa_1792534184` parts,
and three `rs_1792532725` parts supplied from
`D:/SteamLibrary/steamapps/common/csgo legacy/resolver_logs/`.
No numbered parts were missing. The combined export is not counted a second time.
Reproduce the counts with `tests/analyze_resolver_logs.py`; detailed output is
`build/resolver-log-analysis.json`.

## Findings

- The anti-aim sessions contain 187,074 and 396,319 rows respectively.
- The resolver session contains 3,561 record rows and one fire/miss pair.
- All 2,936 records selecting a noncenter side with multiple matrices enabled
  report a body-yaw offset within two degrees of zero. This supports investigating
  candidate generation, but the logs do not expose every candidate's matrices.
- The single miss is labeled `spread`, with reported hitchance 48. It is not
  evidence that a different resolver side would have hit.
- The resolver session ends at Unix milliseconds 1792532785706; the earliest
  supplied anti-aim session starts at 1792533090682, 304,976 ms later. There is
  no overlapping session for shot-level comparison or accuracy measurement.

## Changes

The compiled x86 SDK layout placed minimum yaw at 0x338 and maximum yaw at
0x33C. The inspected client's velocity routine reads those limits at 0x334 and
0x338. Corrected trailing padding and added compile-time offset checks. This
prevents the resolver from reading the wrong fields as yaw limits.

Preserved the user's invalid-limit fallback and constrained reconstructed
walk/run and duck inputs to their normalized ranges. Regression cases cover
zero/invalid limits, asymmetric limits, distinct full/half hypotheses, wraparound,
and replay time/frame guards. Existing evidence and pattern tests are retained.

The animation reconstruction changes from the preceding audit independently
animate all four noncenter candidates, keep observed eye yaw as input, set the
candidate feet yaw before each update, and use each candidate's generated layers
for bone generation. They require live validation on the matching game build.

No player-name rules or thresholds fitted to a particular recorded player were
added. These logs cannot establish a universal hit rate or verify server hitboxes.

## Validation

Both `animation_replay.cpp` and `resolver_evidence.cpp` regression executables
passed. Release x86 built with toolset v145 and zero errors (193 warnings).
Whole-program optimization was disabled for this build after the compiler
reported C1001 / LNK1000 with it enabled; project settings were not changed.
Output: `build/Release/CSGO.dll`. No live-game accuracy test was performed.
