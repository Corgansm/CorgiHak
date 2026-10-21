# Client animation audit: first static pass

Input: `D:/SteamLibrary/steamapps/common/csgo legacy/csgo/bin/client.dll`

SHA-256: `183d42fac8a4ecc24dc02edf5f09122f37fe9e7533ca093a867718b25cfdeb03`

Size: 16,377,192 bytes. PE32 x86. Preferred image base: `0x10000000`.
Addresses below are preferred-base virtual addresses; subtract that base for RVAs.
ASLR can change live addresses. The DLL was read and disassembled, never executed.

## Confirmed binary observations

### Animation update

The project's animation-update pattern uniquely matches RVA `0x43E9E0`.
The routine accesses the player at state offset `0x60`, last-update time at `0x6C`,
and last-update frame at `0x70`. At `0x1043EA93–0x1043EAB6`, it independently checks
time and frame against globals and can return without updating. At `0x1043EABC–
0x1043EACF`, it stores a nonnegative elapsed interval at `0x74`.

The incoming XMM1 value becomes state eye yaw at `0x78`; the XMM2 path becomes
eye pitch at `0x7C`. This agrees with the intended register use of the existing
wrapper. The observed animation-layer iteration advances by `0x38` to `0x2D8`,
consistent with thirteen layers of the project's size.

### Velocity-stage yaw limits

Update directly calls `0x10443410` at `0x1043ED46`. This callee also contains the
project's uniquely matched `SetupVelocityReturn` at `0x10443459`.

At `0x10443984–0x10443A14`, the callee computes a yaw-limit modifier using state
offsets `0xF8`, `0x11C`, `0xA4`, and `0xFC`. Reading the referenced float constants
from the binary gives approximately -0.3, 0.2, 1.0, and 0.5:

```
walk = clamp(state[0xF8], 0, 1)
factor = (state[0x11C] * -0.3 - 0.2) * walk + 1
if state[0xA4] > 0:
    factor += state[0xA4] * clamp(state[0xFC], 0, 1) * (0.5 - factor)
upper = state[0x338] * factor
lower = state[0x334] * factor
```

The subsequent comparisons adjust feet yaw at `0x80`. This is not the same formula
as `ComputeMaxDesync`, which substitutes normalized movement speed for the
walk/run transition and assumes a fixed 58-degree limit. That helper is an
approximation; whether it materially changes shooting must be traced through
its consumers. The SDK's trailing offset comments differ from these offsets;
compiled `offsetof` values initially measured minimum at 0x338 and maximum at
0x33C. The follow-up corrected trailing padding so those members are at 0x334
and 0x338, with compile-time checks against this inspected build.

### Signature audit

The scanner found 95 literal CLIENT_DLL patterns in project C++ files: 90 unique,
2 missing, and 3 with multiple matches. This includes commented code. It scans
raw PE sections, not a running mapped image, and does not prove every binding.

| Pattern | Observation |
| --- | --- |
| ReturnToProcessInputIsBoneAvailableForRead | No match. Its active return-address comparison cannot recognize the intended caller if this resolves to zero. |
| DoResetLatchReturn | No match. The corresponding hook registration is commented out in the inspected source, so this alone is not an active miss explanation. |
| SetAbsOrigin | Matches RVAs `0x1EA790` and `0x8340E0`. The first accesses the entity vector at `+0xA0`; the second is different code. The signature should be tightened even though first-match selection appears consistent with the intended function here. |
| GetAbsOrigin hook pattern | Matches `0x1E7330` and `0x1E7340`. Both call the same transform helper, but return `this+0xA0` and `this+0xC4` respectively. The pattern does not distinguish those accessors. |
| WeaponSystem | Seven matches. They may reference the same global; target equivalence has not yet been checked. |

## Confirmed source behavior, with unresolved runtime impact

1. `UpdateSide` adds +120/-120 to eye yaw, but `InterpolateFromLastData` restores
   the network eye yaw before the last update. For a one-command record, that is
   the only update. Prior candidate state can still differ, so this does not prove
   every matrix is identical, but the injected yaw offset is not used on that tick.
2. Half-delta candidates average poses and selected layer values instead of running
   independent animation updates. No binary evidence establishes that this
   interpolation reproduces a valid intermediate server pose.
3. Reconstruction clears the same-frame guard and has a separate first-record
   time adjustment. Both guards are real in this binary; candidate replay should
   verify both for every update, especially after resets or duplicate times.

These findings support auditing candidate generation before adding more side
heuristics. None has yet been measured as the cause of a specific missed bullet.

## Evidence and next work

Reproducible scanner: `tests/inspect_client_binary.py`.
Raw output: `build/client-audit/client-signatures.json` and the `.asm` files in that
directory. The game DLL was not modified. Follow-up source changes and log
limitations are documented in `RESOLVER_LOG_FINDINGS.md`.

Next: verify compiled animstate layout, trace candidate-specific feet-yaw state
through the final update, identify missing caller signatures via control flow,
and compare bone-generation output with recorded runtime state. Client-side
disassembly cannot establish server-authoritative lag compensation or hitboxes;
the matching server binary or controlled server observations are still required.

This is a focused first pass, not a complete decompilation of client.dll.
