// Host check for the PWR key path: buddy/power.h (sleep/wake state, dial and sim) and buddy/sim_keys.h (sim hit-test).
//   c++ -std=c++17 tests/test_power.cpp -o /tmp/test_power && /tmp/test_power
// Not covered here (ESPHome/LVGL runtime): a paused LVGL dropping presses, a key press falling through to the UI root.
#include <cassert>
#include <cstdio>
#include "../buddy/power.h"
#include "../buddy/sim_keys.h"

// The YAML interval (buddy-ui.yaml): wake checked first, then sleep. Returns what it would do this tick.
struct Tick { bool wake, sleep; };
static Tick tick(power::Screen &s) { return {s.take_wake(), s.take_sleep()}; }

int main() {
  // Dial: btn_pwr on_press -> toggle_screen. Sleep, then wake, one request each, consumed once.
  power::Screen s;
  assert(s.toggle(1000) && s.asleep);
  Tick t = tick(s);
  assert(!t.wake && t.sleep);
  t = tick(s);
  assert(!t.wake && !t.sleep);
  assert(s.toggle(2000) && !s.asleep && s.last_activity == 2000);
  t = tick(s);
  assert(t.wake && !t.sleep);

  // Sleep then wake inside one tick (PWR press reaching the root's activity handler): only the wake survives.
  // Both pending used to fade out and pause LVGL while awake.
  assert(s.toggle(3000) && s.activity(3000));
  t = tick(s);
  assert(t.wake && !t.sleep && !s.asleep);
  // And the reverse: only the sleep survives.
  s.activity(4000);
  tick(s);
  s.set_asleep(true);
  s.set_asleep(false);
  s.set_asleep(true);
  t = tick(s);
  assert(!t.wake && t.sleep && s.asleep);

  // Activity while awake keeps the time but asks for nothing.
  power::Screen a;
  assert(!a.activity(500) && a.last_activity == 500);
  t = tick(a);
  assert(!t.wake && !t.sleep);

  // Sim: touches on the case keys hit them, the glass and the gap between them do not.
  using namespace sim_keys;
  for (Key k : {PWR, BOOT}) {
    assert(hit(cx(k), cy(k)) == k);
    assert(hit(cx(k), cy(k) - (KH / 2 + REACH - 2)) == k);  // edge of the reach, along the screen's y
  }
  assert(hit(C, C) == NONE);
  assert(hit(C + D / 2 - 10, C) == NONE);  // glass edge, between the keys
  assert(hit(cx(PWR, R + 60), cy(PWR, R + 60)) == NONE);  // past the key, outside the case

  // Sim PWR path end to end: click the key while awake, then again while dark (LVGL paused, the touchscreen still sees it).
  power::Screen sim;
  if (hit(cx(PWR), cy(PWR)) == PWR) sim.toggle(10);
  assert(sim.asleep && tick(sim).sleep);
  if (hit(cx(PWR), cy(PWR)) == PWR) sim.toggle(20);
  assert(!sim.asleep && tick(sim).wake);

  // idle_due: battery times out, USB never, not during boot, not when asleep.
  power::Screen idle;
  idle.last_activity = 1000;
  assert(!idle.idle_due(1000 + 300000, 5000, 300000, false));  // exactly at the timeout: not yet
  assert(idle.idle_due(1000 + 300001, 5000, 300000, false));
  assert(!idle.idle_due(1000 + 300001, 5000, 300000, true));   // USB
  assert(!idle.idle_due(3000, 5000, 1000, false));              // inside boot_awake_ms
  idle.set_asleep(true);
  assert(!idle.idle_due(1000 + 300001, 5000, 300000, false));  // already asleep
  idle.set_asleep(false);
  // Unplug after a long idle on USB: due on the next tick (battery rules, no grace period).
  assert(!idle.idle_due(900000, 5000, 300000, true));
  assert(idle.idle_due(900001, 5000, 300000, false));

  puts("power ok");
}
