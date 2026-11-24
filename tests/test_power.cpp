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

  // Face-side battery %: always on battery; on USB 60 s after plug-in, then hidden, then 60 s after a wake.
  power::Screen b;
  b.activity(1000);
  b.power_source(false, 100000);
  assert(b.show_battery_side(false, 100000) && b.show_battery_side(false, 900000));
  b.power_source(true, 200000);  // plug in
  assert(b.show_battery_side(true, 200000) && b.show_battery_side(true, 259999));
  b.power_source(true, 260000);
  assert(!b.show_battery_side(true, 260000));
  b.set_asleep(true);
  assert(b.activity(300000));  // wake
  b.power_source(true, 300050);
  assert(b.show_battery_side(true, 359999) && !b.show_battery_side(true, 360000));
  b.power_source(false, 400000);  // unplug, replug restarts the window
  b.power_source(true, 500000);
  assert(b.show_battery_side(true, 559999) && !b.show_battery_side(true, 560000));
  // Boot already on USB: plug-in at the first tick.
  power::Screen c;
  c.power_source(true, 5000);
  assert(c.show_battery_side(true, 64999) && !c.show_battery_side(true, 65000));

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

  // idle_due: battery times out (busy ignored), USB only when idle for usb_sleep_ms and not busy, not during boot,
  // not when asleep.
  const uint32_t SL = 300000, USL = 900000;
  power::Screen idle;
  idle.last_activity = 1000;
  assert(!idle.idle_due(1000 + SL, 5000, SL, USL, false, false));  // exactly at the timeout: not yet
  assert(idle.idle_due(1000 + SL + 1, 5000, SL, USL, false, false));
  assert(idle.idle_due(1000 + SL + 1, 5000, SL, USL, false, true));  // battery ignores busy
  assert(!idle.idle_due(3000, 5000, 1000, USL, false, false));       // inside boot_awake_ms
  // USB: the long timeout, and never while busy.
  assert(!idle.idle_due(1000 + SL + 1, 5000, SL, USL, true, false));
  assert(!idle.idle_due(1000 + USL, 5000, SL, USL, true, false));
  assert(idle.idle_due(1000 + USL + 1, 5000, SL, USL, true, false));
  idle.stay_awake = true;  // diagnostic override: never due, on battery or USB
  assert(!idle.idle_due(1000 + USL + 1, 5000, SL, USL, false, false));
  assert(!idle.idle_due(1000 + USL + 1, 5000, SL, USL, true, false));
  idle.stay_awake = false;
  assert(!idle.idle_due(1000 + USL + 1, 5000, SL, USL, true, true));
  idle.set_asleep(true);
  assert(!idle.idle_due(1000 + USL + 1, 5000, SL, USL, true, false));  // already asleep
  idle.set_asleep(false);
  // Wake from dark on USB: activity() lights it and restarts the USB timer.
  idle.set_asleep(true);
  assert(idle.activity(2000000) && !idle.asleep);
  assert(!idle.idle_due(2000000 + USL, 5000, SL, USL, true, false));
  assert(idle.idle_due(2000000 + USL + 1, 5000, SL, USL, true, false));
  // Unplug after a long idle on USB (not busy): due on the next tick (battery rules, no grace period).
  power::Screen un;
  un.last_activity = 1000;
  assert(!un.idle_due(1000 + SL + 1, 5000, SL, USL, true, false));
  assert(un.idle_due(1000 + SL + 2, 5000, SL, USL, false, false));

  // battery_ink: anchors exact, blend between, clamped.
  assert(power::battery_ink(10) == 0xcd3131 && power::battery_ink(50) == 0xe5e510 && power::battery_ink(90) == 0x3fb950);
  assert(power::battery_ink(0) == 0xcd3131 && power::battery_ink(-5) == 0xcd3131);
  assert(power::battery_ink(100) == 0x3fb950 && power::battery_ink(500) == 0x3fb950);
  uint32_t c30 = power::battery_ink(30);  // halfway red -> yellow
  for (int sh = 16; sh >= 0; sh -= 8) {
    int r = (0xcd3131 >> sh) & 0xff, y = (0xe5e510 >> sh) & 0xff, c = (c30 >> sh) & 0xff;
    assert(c >= (r < y ? r : y) && c <= (r < y ? y : r));
  }
  assert(c30 != 0xcd3131 && c30 != 0xe5e510);

  puts("power ok");
}
