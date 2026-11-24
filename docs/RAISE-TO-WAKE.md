# Raise to wake

A nudge or lift of the dial lights the screen when it is dark. Two tiers, from the request "only if it's picked up
and/or on battery":

- **On battery:** a light nudge fires (tilt the dial on the desk, give it a small shove).
- **On USB:** a deliberate nudge/pickup fires. USB is the tethered/desk mode, so ambient desk wobble must not light it.

Touch and PWR still wake it normally, on both. Raise to wake never *sleeps* it: the battery/USB idle timeouts in
`power.h` are unchanged.

## How it works

`buddy/raise_wake.h` (pure, host-tested in `tests/test_raise_wake.cpp`). `feed()` takes one accelerometer sample in
screen axes (the same stream `buddy/gaze.h` reads) and compares the gravity direction with a slowly-following
baseline, so the trigger is the deviation from where the dial was resting, not an absolute angle: it fires flat on a
desk, on the printed stand and in a hand alike.

- Baseline follow: only once the dial has been still ~300 ms, so a nudge leaves a large deviation and fires, then the
  baseline takes the new resting direction and it re-arms. A *held* tilt therefore settles; a fresh nudge still fires.
- Peak: `NUDGE_G` on battery, `PICK_G` on USB (about 6 deg vs 13 deg of fresh tilt). Two consecutive hot samples
  (`HOT_MIN`), so one noisy sample on a shared desk does not fire.
- Refractory: 600 ms, so one physical nudge is one wake, not a burst.
- Stale gap > 3 s (poller was off): relearn the resting direction instead of firing off a stale baseline.

All rates are per real time, so the detector behaves the same at the idle and fast sample rates below.

## Idle slow, fast on motion (not a flat 10 Hz)

The IMU poller used to stop whenever the screen went dark (`claude-buddy.yaml` `lvgl.on_pause`), so nothing was
sampling motion to wake from. It now keeps running while dark, but slowly:

- **Idle: `dark_poll_ms` (500 ms, 2 Hz).** Cheap; still sees a nudge or a deliberate lift.
- **On motion: `fast_poll_ms` (50 ms).** `feed()` reports `moving()` (direction-change speed past `MOVED_SPD`, held
  `MOVE_HOLD_MS`), the UI tick drops the rate to fast while true and back to idle after. So an idle desk is sampled at
  2 Hz, and only a hand on it pays the fast rate.
- A nudge shorter than the idle gap (tilt and back between two 2 Hz samples) is still caught: `jumped` (a single raw
  sample-to-sample jump past the threshold at a long gap) confirms on its own, so the coarse rate is not a blind spot.

`on_resume` puts the poller back to `frame_ms`. `on_usb` only moves the threshold; the poller runs on USB too.

Cost: while dark the IMU samples at 2 Hz at rest instead of not at all. That is the price of wake-from-motion; it was
previously zero.

The screen is dark and LVGL is paused, but the UI tick (`buddy-ui.yaml` interval) still runs, so a consumed raise
unpauses LVGL and fades the panel in exactly like a PWR/touch wake.

## Simulator

`n` fires a synthetic nudge (a ~220 ms tilt, then flat). Try it with the screen dark: it wakes on battery, and on USB
only if the tilt is large enough. Arrow keys are sustained tilts, so holding one is a different (settled) case. The
sim mimics the idle/fast rate so the dark path behaves like the dial.
