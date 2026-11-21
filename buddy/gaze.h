#pragma once
// IMU gaze: while the dial moves, the eyes of a scene with a gaze layer (Grok bots, Clawd cooking) look the way it
// tilts; once it has been still for HOLD_MS they ease home and the scene's own glance/blink loop takes over again.
//
// Input is the accelerometer in g, in screen axes (+x right, +y down; claude-buddy.yaml axis_map). The gravity
// direction is compared with a baseline that slowly follows it, so a held tilt fades out: the eyes react to motion,
// not to the angle the dial rests at. Output: look() in -1..1 per axis; scene::Player scales it by the scene's range.
// No LVGL or ESPHome here: tests/test_gaze.cpp runs it on the host.
#include <cmath>
#include <cstdint>

namespace gaze {

// FOLLOW and EASE are per 50 ms (REF_MS); update() rescales them to the real gap between samples, so the UI tick
// (buddy-ui.yaml frame_ms) can change without changing how the eyes feel.
constexpr float REF_MS = 50;
constexpr float FOLLOW = 0.06f;    // baseline closes this much of the gap per 50 ms: ~0.8 s time constant
constexpr float GAIN = 4.0f;       // gravity-direction change to full look: 0.25 (~15 deg of fresh tilt) = 1
constexpr float START = 0.05f;     // change that counts as moving (~3 deg); below it the dial is still
constexpr uint32_t HOLD_MS = 800;  // still this long: ease home
constexpr float EASE = 0.35f;      // eye offset closes this much of the gap to its target per 50 ms
constexpr float HOME = 0.04f;      // eased this close to centre: hand back to the scene

struct State {
  bool init = false, on = false;
  float bx = 0, by = 0, x = 0, y = 0;  // baseline gravity direction (x, y of the unit vector); eye offset
  uint32_t moved = 0, last = 0;  // last: previous sample's time
};
inline State &st() {
  static State s;
  return s;
}
// Forget the baseline (screen went dark, poller stopped): the next sample starts still.
inline void reset() { st() = State{}; }

inline float clamp1(float v) { return v < -1 ? -1 : v > 1 ? 1 : v; }

// Fraction k per REF_MS, applied over dt ms. A late sample (missed ticks) counts as at most 4 periods.
inline float per_dt(float k, uint32_t dt) {
  float n = std::fmin(dt, 4 * REF_MS) / REF_MS;
  return 1 - std::pow(1 - k, n);
}

inline void update(float ax, float ay, float az, uint32_t now) {
  State &s = st();
  float m = std::sqrt(ax * ax + ay * ay + az * az);
  if (!(m > 0.2f)) return;  // failed read (NaN) or free fall: no direction to compare
  float gx = ax / m, gy = ay / m;
  if (!s.init) {
    s = State{};
    s.init = true, s.bx = gx, s.by = gy, s.last = now;
    return;
  }
  uint32_t dt = now - s.last;
  s.last = now;
  float follow = per_dt(FOLLOW, dt), ease = per_dt(EASE, dt);
  float dx = gx - s.bx, dy = gy - s.by;
  s.bx += dx * follow, s.by += dy * follow;
  if (std::fabs(dx) > START || std::fabs(dy) > START) s.moved = now, s.on = true;
  if (!s.on) return;
  bool moving = now - s.moved < HOLD_MS;
  float tx = moving ? clamp1(dx * GAIN) : 0, ty = moving ? clamp1(dy * GAIN) : 0;
  s.x += (tx - s.x) * ease, s.y += (ty - s.y) * ease;
  if (!moving && std::fabs(s.x) < HOME && std::fabs(s.y) < HOME) s.on = false, s.x = s.y = 0;
}

struct Look { bool on; float x, y; };
inline Look look() { return {st().on, st().x, st().y}; }

}  // namespace gaze
