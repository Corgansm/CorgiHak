# Resolver changes for aa_recorder_1

This is a recording-specific investigation note. The general runtime policy is
documented in `RESOLVER_RULES.md`; it applies to every opponent and does not read
this recording.

Source: `C:/Users/Corgan/Downloads/aa_recorder_1.csv` (read only).

The recording contains 11,368 tick rows, including 6,416 marked sent. The recorded
body-yaw offset has magnitude 57.4 in 4,891 rows and 28.2 in 1,857 rows. LBY minus
eye yaw is zero to the file's precision in 10,473 rows. The settings change
between static and jitter body yaw, with changing pitch and yaw settings.
These are local-client observations, not server hitbox ground truth. Neither the
local body-yaw pose nor the settings are inputs to the opponent resolver.

The implementation now:

- Keeps independent confidence for center, both full sides, and both half sides.
- Tests shot impact rays against all five available candidate matrices.
- Learns only from a unique hitgroup match or a discriminating miss. An overlapping
  headshot does not lock a side, and spread/occlusion/invalid records do not teach it.
- Immediately invalidates positive confidence when a discriminating miss contradicts it.
- Separates positive and negative jitter phase memory, and recognizes jitter only
  when the record's recent yaw samples form two compact, repeatedly visited bands.
- Uses the selected record's LBY, checks the standing animation activity, and does
  not invert animation evidence merely because the jitter phase is negative.
- Stores the exploration phase per shot so delayed feedback from two bullets
  cannot advance the same phase twice.

The existing five animation candidates and lag-compensation validity rules remain
in use. Half-delta candidates are approximate interpolated poses. The recording
cannot establish that a candidate matches the server's true pose, or measure the
new resolver's hit rate. Live shot logs and server-side ground-truth hitboxes would
be needed for that validation.

## Verification

`tests/resolver_evidence.cpp` exercises independent half-delta feedback, ambiguous
hits, misses across all poses, unavailable candidates, confidence invalidation,
delayed jitter, circular angle wrapping, random angles, single turns, and NaNs.

Build the standalone tests with MSVC C++20 and assertions enabled, then run the
executable. The solution uses `Release|x86` (mapped to project `Release|Win32`).
On this machine, the DLL is built with `/p:PlatformToolset=v145`; the checked-in
Release configuration still defaults to v143.
