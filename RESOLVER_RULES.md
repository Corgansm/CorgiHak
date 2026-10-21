# General resolver rules

The same policy applies to every player. No player names, Steam IDs, recorded
angles, command numbers, or anti-aim product names select a special strategy.
Identity only separates learned observations between opponents.

1. **Validate the record before learning.** Exclude unreliable, shifted,
   extrapolated, expired, or incomplete records from shot feedback. Use the
   timestamps, angles, and LBY attached to the selected record.
2. **Separate movement states.** Store evidence independently for standing,
   moving, crouching, crouch-moving, airborne, and airborne-crouched states.
   Keep opposite jitter phases separate inside those contexts. Attribute delayed
   shot feedback to its original context, not the opponent's current state.
3. **Compare candidates rather than assume a side.** Consider center, both full
   sides, and both half sides. Unavailable candidates cannot win. Low-delta poses
   have their own confidence; they do not inherit a full side's successes.
4. **Require repeatable patterns.** Classify jitter from compact, repeatedly
   visited yaw bands across recent records in the same movement context. A turn,
   random angles, insufficient history, and ambiguous opposite angles are not
   proof of jitter. Do not predict tomorrow's side for yesterday's record.
5. **Reconcile evidence.** A distinct animation match or a distinctly learned
   candidate may guide selection. If fresh animation and learned history disagree,
   use the animation candidate provisionally and mark the choice uncertain.
   Historical confidence must not silently override changed behavior.
6. **Learn only from discriminating outcomes.** A unique matching hitgroup can
   reward a candidate. A reliable miss through the chosen pose can invalidate it
   only when other poses were outside the path. Overlapping hits, all-pose misses,
   spread, and occlusion do not prove orientation. Keep confidence bounded.
7. **Adapt.** Contradictory misses immediately remove positive confidence. Existing
   aging unlocks stale evidence. Explore alternatives when no candidate has a
   clear lead; a fixed left/right rule is never considered proof.
8. **Treat uncertainty as uncertainty.** With Head Safety enabled, an unknown
   choice requests intersection with at least two full-side candidates. Conflicting
   evidence requests all three full sides and prefers a body point. A confident
   choice permits one; airborne records still require at least two. These are
   candidate-consistency checks, not guarantees about the actual server pose.
9. **Keep user controls.** Resolver enablement, Head Safety, hitbox selection,
   damage thresholds, and explicit script overrides retain their existing roles.
   Body preference does not permanently prohibit headshots.

## Validation and limits

`tests/resolver_evidence.cpp` tests the policy independently of game state, including
125 orientation/amplitude combinations, movement contexts, conflicting evidence,
missing candidates, ambiguous hits, and adaptation after misses.

The supplied CSV is an example, not training labels or a runtime dependency.
Server-authoritative hitboxes are absent, so neither it nor these unit tests can
prove a live hit rate. Five approximate poses do not represent every possible
server animation. Random hidden state can remain unidentifiable from client
observations; the policy must preserve that uncertainty.
