# Native-wake backoff test

Developer-only, opt-in standalone test for PR #55. Disabled by default.
The test injects a physical wake; this is **not** evidence of spontaneous KF2
wakes. The normal optimizer must detect it and decide when to sleep again.

## Run

1. Build the telemetry module and its matching portable app.
2. Open the app with KF2 closed. Verify that `SESSION_CONFIG_CAPTURED` and
   `GAMEPLAY_PROVIDER_PREPARED` were logged and the protected INI snapshot exists.
3. In the protected KFEngine.ini session section
   `[KF2OptimizerTelemetry.KF2OptimizerTelemetryProbe]`, temporarily set
   `bDebugNativeWakeTest=True`. Do not save this into a reusable game profile.
4. Start an offline match. Make some corpses, then stay at least 13 metres away
   from them. The test selects one already put to sleep by the real optimizer.
5. Allow up to four minutes. Close KF2 and the app afterward; verify that the
   original INIs were restored and the test switch is absent or false.

## Evidence

Find `KF2OPT_WAKE_TEST` in the active Launch.log. Correlate its full corpse ID
with ordinary `KF2OPT_CORPSE_DISTANCE` receipts. Six cycles must verify
2, 5, 10, 20, 30 and 30 seconds, including the cap. Each cycle requires an
effective wake, the correct per-actor cooldown, and an actual optimizer resleep
no earlier than the deadline (50 ms floating-point/timer allowance).
Sleep timing is captured at the committed action, not at the next test poll.
`sleep_source=distance` and `sleep_source=aging` distinguish the actual owner.
The existing `removal_reason=native_wake` is a detection category, not proof
of spontaneous engine activity: `wake_origin=injected_test` attributes a
test stimulus, while `wake_origin=unattributed` makes no causal claim.

- `passed`: all six cycles completed on the same corpse.
- `partial`, `aging_sleep_after_backoff`: aging committed a verified sleep after
  the cooldown. This confirms the completed cycle only. The test stops without
  injecting further wakes because aging does not enter the distance tracker;
  later backoff levels remain unverified.
- `failed`: incorrect backoff or an optimizer resleep before the deadline.
- `incomplete`: no suitable corpse, target lost, player too close, session ended,
  or another system changed the physics. This is **not a pass**.

The diagnostic never forces a sleep, changes quality, cancels aging, moves a
corpse, or touches a living Zed. If aging freezes the target, keep that result;
do not disable production safeguards to manufacture a passing gameplay test.
Check unrelated corpse IDs separately to establish that normal work continues.
