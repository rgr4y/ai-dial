// Host check for buddy/raise_wake.h: a nudge fires on battery, only a pickup on USB; the baseline settles so a held
// tilt does not re-fire; feed() reports moving() so the caller can raise the poll rate.
//   c++ -std=c++17 tests/test_raise_wake.cpp -o /tmp/test_raise_wake && /tmp/test_raise_wake
#include <cassert>
#include <cmath>
#include <cstdio>
#include "../buddy/raise_wake.h"

static uint32_t t = 0;
// Feed a horizontal tilt (deg) for n samples at `step` ms; gz keeps the magnitude at 1 g.
static void feed(float deg, int n, bool usb = false, uint32_t step = 50) {
  float s = std::sin(deg * 3.14159265f / 180);
  for (int i = 0; i < n; i++, t += step)
    raise_wake::feed(s, 0, std::sqrt(std::fmax(0.f, 1 - s * s)), usb, t);
}
// Step from `a` to `b` deg over `ms`, sampled at `step` (a fast, realistic nudge ramp).
static void ramp(float a, float b, uint32_t ms, uint32_t step = 20, bool usb = false) {
  int n = (int) (ms / step) + 1;
  for (int i = 0; i <= n; i++, t += step) {
    float deg = a + (b - a) * (float) i / (float) n;
    float s = std::sin(deg * 3.14159265f / 180);
    raise_wake::feed(s, 0, std::sqrt(std::fmax(0.f, 1 - s * s)), usb, t);
  }
}

int main() {
  // Battery: a light nudge fires once, when fed at the fast (confirming) rate.
  raise_wake::reset();
  feed(0, 40);
  assert(!raise_wake::take_raise());
  ramp(0, 20, 200);
  assert(raise_wake::take_raise());
  assert(!raise_wake::take_raise());  // consumed once

  // Held at that tilt: the baseline takes it, no repeat while it sits there; a fresh nudge still fires.
  feed(20, 60);
  assert(!raise_wake::take_raise());
  ramp(20, 45, 200);
  assert(raise_wake::take_raise());

  // A nudge detected even from the SLOW idle poll: the coarse samples move it and moving() lights up, so the caller
  // would switch to the fast rate; here we just check the coarse-then-fast path still fires and reports moving().
  raise_wake::reset();
  feed(0, 20, false, 500);  // 2 Hz idle
  assert(!raise_wake::moving());
  feed(20, 1, false, 500);  // one coarse sample of the nudge: displaced
  assert(raise_wake::moving());
  feed(20, 3, false, 50);   // escalated to fast: confirms and fires
  assert(raise_wake::take_raise());

  // A flick shorter than the idle gap: tilt and back between two 500 ms samples. The displacement d is back near
  // zero by the next sample, but the raw jump confirms alone.
  raise_wake::reset();
  feed(0, 20, false, 500);
  feed(20, 1, false, 500);   // the flick lands on this sample: d is large, jump large
  assert(raise_wake::take_raise());
  feed(0, 1, false, 500);    // back flat; no second fire
  assert(!raise_wake::take_raise());

  // moving() holds for MOVE_HOLD_MS after the last motion, then clears (feed the same value: no fresh motion).
  raise_wake::reset();
  feed(0, 20, false, 500);
  feed(20, 1, false, 500);  // the nudge: displaced, moving()
  assert(raise_wake::moving());
  t = raise_wake::st().moved_at + raise_wake::MOVE_HOLD_MS + 10;
  feed(20, 1, false, 500);  // same tilt, past the hold: at rest again
  assert(!raise_wake::moving());

  // Refractory: two hot bursts closer than REFRACT_MS collapse to one.
  raise_wake::reset();
  feed(0, 40);
  ramp(0, 20, 100);
  ramp(20, 5, 40);
  ramp(5, 25, 100);
  assert(raise_wake::take_raise());
  assert(!raise_wake::take_raise());

  // USB: a light nudge (below PICK_G) does not fire, a real pickup does.
  raise_wake::reset();
  feed(0, 40, true, 20);
  ramp(0, 8, 200, 20, true);   // ~0.14 g, under PICK_G (=0.22)
  assert(!raise_wake::take_raise());
  feed(8, 60, true, 20);       // settle
  ramp(8, 30, 200, 20, true);  // ~0.5 g, over PICK_G
  assert(raise_wake::take_raise());

  // A raise the UI never consumed stays armed for the next take.
  raise_wake::reset();
  feed(0, 40);
  ramp(0, 20, 200);
  assert(raise_wake::take_raise());

  // Stale gap (poller was off): relearn, no spurious fire from the old baseline.
  raise_wake::reset();
  feed(0, 40);
  t += 5000;                   // longer than STALE_MS
  feed(0, 1);
  assert(!raise_wake::take_raise());

  // reset() forgets the resting direction: the first sample after it is the new baseline (the dial is wherever it was
  // left), so holding that pose does not fire, but nudging away from it does.
  raise_wake::reset();
  feed(25, 3, false, 20);
  assert(!raise_wake::take_raise());
  ramp(25, 50, 200);
  assert(raise_wake::take_raise());

  puts("raise_wake ok");
}
