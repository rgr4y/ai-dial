#pragma once
// Raise to wake: nudging or lifting the dial lights the screen. Two tiers, matching "only if it's picked up and/or on
// battery":
//   - on battery: a light nudge fires (displacement from the resting direction past NUDGE_G).
//   - on USB: only a deliberate nudge/pickup fires (past PICK_G). USB is the desk/tethered mode, so a shared desk
//     wobbling must not light it; picking it up still does.
//
// The trigger is the displacement from where the dial was resting, and the baseline only follows once the dial has
// been still a moment. So it fires flat on a desk, on the stand and in a hand alike, and a held tilt settles (the
// baseline takes it) instead of re-firing. Every rate is per real time, so the same code works at the slow idle poll
// rate and the fast active one.
//
// feed() also reports moving(): the YAML holds the IMU at the fast poll rate while this is true (and idles slow
// otherwise), so a nudge is confirmed in ~2 samples without sampling an idle desk at the fast rate.
//
// Input is the accelerometer in g, screen axes (claude-buddy.yaml axis_map), the same stream gaze.h reads. feed() is
// called from the IMU listener (or the sim's fake IMU); take_raise()/moving() from the UI tick (buddy-ui.yaml), the
// only side that knows the screen is asleep. No LVGL or ESPHome here: tests/test_raise_wake.cpp runs it on the host.
#include <cmath>
#include <cstdint>

namespace raise_wake {

// Displacement from the resting direction that fires (g).
constexpr float NUDGE_G = 0.10f;    // battery: ~6 deg of fresh tilt
constexpr float PICK_G = 0.22f;     // USB: ~13 deg, a real pickup
constexpr float MOVED_G = 0.05f;    // at or above this it is displaced, not at rest
constexpr float MOVED_SPD = 0.15f;  // or a direction-change speed (g/s) this high: 0.1 g in 0.5 s reads 0.2
constexpr float STILL_SPD = 0.06f;  // below this for SETTLE_MS the dial is at rest and the baseline may follow
constexpr uint32_t SETTLE_MS = 300;     // still this long before the baseline takes the new resting direction
constexpr float TAU = 900.0f;           // baseline time constant once it is allowed to follow (ms)
constexpr uint32_t HOT_WINDOW_MS = 900; // displacements this close together count as one nudge to confirm
constexpr uint8_t HOT_MIN = 2;          // ... and it needs this many (a lone noisy sample does not fire)
constexpr uint32_t COARSE_MS = 150;     // a sample gap this long means the idle rate: a single clear jump then confirms
constexpr uint32_t REFRACT_MS = 600;    // one physical nudge is one wake, not a burst
constexpr uint32_t MOVE_HOLD_MS = 1000; // report moving() this long after the last motion (YAML poll-rate hold)
constexpr uint32_t STALE_MS = 3000;     // feed gap longer than this (poller restarted): relearn the resting direction

struct State {
  bool init = false, enabled = true, fired = false, raised = false;
  float bx = 0, by = 0;  // resting (baseline) gravity direction in the horizontal plane
  float px = 0, py = 0;  // previous sample's direction, for the motion speed
  uint32_t last = 0, still_at = 0, moved_at = 0, raised_at = 0, hot_first = 0;
  uint8_t hot_n = 0;
};
inline State &st() {
  static State s;
  return s;
}
// Forget the baseline (screen woke or slept, poller restarted): the next sample starts still.
inline void reset() { st() = State{}; }
inline void set_enabled(bool on) { st().enabled = on; }

// Consume a pending raise (UI thread).
inline bool take_raise() {
  bool r = st().raised;
  st().raised = false;
  return r;
}
// True while the dial has moved recently: the YAML holds the fast poll rate while this is true.
inline bool moving() {
  State &s = st();
  return s.enabled && s.moved_at != 0 && s.last - s.moved_at < MOVE_HOLD_MS;
}

// One accelerometer sample. on_usb raises the threshold from a nudge to a pickup; the baseline still follows.
inline void feed(float ax, float ay, float az, bool on_usb, uint32_t now) {
  State &s = st();
  if (!s.enabled) return;
  float m = std::sqrt(ax * ax + ay * ay + az * az);
  bool gap = !s.init || now - s.last > STALE_MS;
  if (!(m > 0.2f)) {  // failed read (NaN) or free fall: no direction to compare
    s.last = now;
    return;
  }
  if (gap) {  // first sample, or the poller was off: relearn where it rests
    s.init = true, s.bx = ax, s.by = ay, s.px = ax, s.py = ay, s.fired = false, s.hot_n = 0, s.still_at = now;
    s.last = now;
    return;
  }
  uint32_t dt = now - s.last;
  if (dt == 0) dt = 1;
  s.last = now;

  float dx = ax - s.bx, dy = ay - s.by;
  float d = std::sqrt(dx * dx + dy * dy);
  // Direction-change speed (g/s) and the raw jump from the previous sample, both independent of the sample rate. At
  // the idle rate one sample may be all a quick nudge gets, so a single clear jump (dt long) also confirms alone.
  float jmp = std::sqrt((ax - s.px) * (ax - s.px) + (ay - s.py) * (ay - s.py));
  float spd = jmp * 1000.0f / (float) dt;
  s.px = ax, s.py = ay;

  if (spd >= MOVED_SPD) s.moved_at = now;  // actual movement: escalate the poll (a still-but-tilted dial may idle slow)
  if (spd < STILL_SPD) {
    if (!s.still_at) s.still_at = now;
  } else {
    s.still_at = 0;
  }
  // The baseline follows only once the dial has been still a moment, so a nudge/pickup leaves d large and fires, then
  // the baseline takes the new resting direction and d decays, re-arming. A held tilt therefore settles, no re-fire.
  if (s.still_at && now - s.still_at >= SETTLE_MS) {
    float f = 1.0f - std::exp(-(float) dt / TAU);
    s.bx += dx * f, s.by += dy * f;
  }

  float thr = on_usb ? PICK_G : NUDGE_G;
  // At the idle rate (long gap) a clear single jump past the threshold confirms alone: a flick between two coarse
  // samples leaves d and the next gap near zero, so without this 2 Hz idle would miss anything shorter than a tick.
  bool displaced = d >= thr, jumped = dt >= COARSE_MS && jmp >= thr;
  if (displaced) {
    if (now - s.hot_first > HOT_WINDOW_MS) s.hot_first = now, s.hot_n = 0;
    if (s.hot_n < 255) s.hot_n++;
  }
  if ((displaced && s.hot_n >= HOT_MIN) || jumped) {
    if (!s.fired && (!s.raised_at || now - s.raised_at >= REFRACT_MS))
      s.raised = true, s.raised_at = now;  // stays set until taken, so a wake is never lost to the tick cadence
    s.fired = true;  // spend the excursion: no repeat while held past the threshold, re-arms below MOVED_G
  } else if (d < MOVED_G && !jumped) {
    s.fired = false, s.hot_n = 0, s.hot_first = 0;
  }
}

}  // namespace raise_wake
