// Wireframe cube for the dial: IMU tilt, or touch drag/fling with inertia.
//
// Modes:
//   MOTION: the cube eases toward the IMU pitch/roll.
//   MANUAL: a touch that starts inside the capture circle grabs the cube. Dragging rotates it,
//           and releasing mid-swipe keeps it spinning with decaying momentum.
// Back to MOTION: double-tap near the centre, or CUBE_IDLE_MS with no cube movement.
//
// Pure C++ apart from cube_render(). Build with -DCUBE_NO_LVGL for the host test (tests/test_cube.cpp).
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>

namespace dialcube {

// ---- Tuning ----
constexpr float CX = 233.0f, CY = 233.0f;  // screen centre (466x466)
constexpr float CAPTURE_R = 117.0f;        // grab circle: half the screen's diameter
constexpr float CENTER_R = 70.0f;          // double-tap target
constexpr float DRAG_K = 0.01f;            // radians per pixel dragged
constexpr float FRICTION = 0.94f;          // velocity multiplier per 16ms
constexpr float VEL_STOP = 0.0002f;        // rad/ms; below this the spin has stopped
constexpr uint32_t STALE_MS = 60;          // finger still this long before release: no fling
constexpr uint32_t TAP_MS = 250;           // longest touch that still counts as a tap
constexpr float TAP_SLOP = 15.0f;          // most a tap can move, in px
constexpr uint32_t DOUBLE_TAP_MS = 400;
constexpr uint32_t IDLE_MS = 5000;         // no cube movement for this long: back to MOTION
constexpr float IMU_EASE = 0.3f;           // fraction of the gap to the IMU angle closed per update
constexpr float IMU_DEADBAND = 0.6f * (float) M_PI / 180.0f;  // ignore tilt changes under 0.6° (sensor noise)

enum Mode : uint8_t { MOTION, MANUAL };

struct CubeState {
  Mode mode = MOTION;
  float rx = 0, ry = 0;  // current rotation, radians
  float vx = 0, vy = 0;  // angular velocity, rad/ms (about X, about Y)
  bool dragging = false;
  float lx = 0, ly = 0;           // last touch position
  float sx = 0, sy = 0;           // touch-down position
  uint32_t down_ms = 0, move_ms = 0, activity_ms = 0, last_tap_ms = 0;
  bool last_tap_center = false;
};

inline CubeState &st() { static CubeState s; return s; }

inline float sq(float v) { return v * v; }
inline bool in_circle(float x, float y, float r) { return sq(x - CX) + sq(y - CY) <= r * r; }

// IMU update in degrees. Returns true if the cube moved (needs a redraw).
inline bool on_imu(float pitch_deg, float roll_deg) {
  CubeState &s = st();
  if (s.mode != MOTION || std::isnan(pitch_deg) || std::isnan(roll_deg)) return false;
  const float tp = pitch_deg * (float) M_PI / 180.0f, tr = roll_deg * (float) M_PI / 180.0f;
  // Deadband: if the cube is already within noise of the tilt, don't move or redraw it,
  // so a still device stays idle.
  if (std::fabs(tp - s.rx) < IMU_DEADBAND && std::fabs(tr - s.ry) < IMU_DEADBAND) return false;
  s.rx += (tp - s.rx) * IMU_EASE;
  s.ry += (tr - s.ry) * IMU_EASE;
  return true;
}

inline void on_touch(float x, float y, uint32_t now) {
  CubeState &s = st();
  s.sx = s.lx = x; s.sy = s.ly = y; s.down_ms = s.move_ms = now;
  if (!in_circle(x, y, CAPTURE_R)) return;  // outside the grab circle: leave it to LVGL
  if (s.mode == MOTION) {
    // Start manual from wherever the IMU left the angles, wrapped so later easing takes the short way.
    s.rx = std::remainder(s.rx, 2.0f * (float) M_PI);
    s.ry = std::remainder(s.ry, 2.0f * (float) M_PI);
  }
  s.mode = MANUAL;
  s.dragging = true;
  s.vx = s.vy = 0;
  s.activity_ms = now;
}

// Returns true if the cube moved.
inline bool on_move(float x, float y, uint32_t now) {
  CubeState &s = st();
  if (!s.dragging) return false;
  const float dx = x - s.lx, dy = y - s.ly;
  const uint32_t dt = now - s.move_ms;
  if (dx == 0 && dy == 0) return false;
  // Drag down tips the front face down (+X). Drag right turns the front to the right (-Y).
  const float drx = dy * DRAG_K, dry = -dx * DRAG_K;
  s.rx += drx; s.ry += dry;
  if (dt > 0) {  // smoothed velocity for the fling
    s.vx = 0.5f * s.vx + 0.5f * drx / dt;
    s.vy = 0.5f * s.vy + 0.5f * dry / dt;
  }
  s.lx = x; s.ly = y; s.move_ms = s.activity_ms = now;
  return true;
}

inline void on_release(uint32_t now) {
  CubeState &s = st();
  const bool tap = now - s.down_ms <= TAP_MS && sq(s.lx - s.sx) + sq(s.ly - s.sy) <= TAP_SLOP * TAP_SLOP;
  if (tap) {
    const bool center = in_circle(s.sx, s.sy, CENTER_R);
    if (center && s.last_tap_center && now - s.last_tap_ms <= DOUBLE_TAP_MS) {
      s.mode = MOTION;  // double-tap: hand the cube back to the IMU
      s.dragging = false;
      s.vx = s.vy = 0;
      s.last_tap_ms = 0;
      return;
    }
    s.last_tap_ms = now;
    s.last_tap_center = center;
  }
  if (!s.dragging) return;
  s.dragging = false;
  if (tap || now - s.move_ms > STALE_MS) s.vx = s.vy = 0;  // finger had stopped: no fling
}

// Call it often (16ms). Applies the fling and the idle timeout. Returns true if the cube moved.
inline bool tick(uint32_t now, uint32_t dt) {
  CubeState &s = st();
  if (s.mode != MANUAL || s.dragging) return false;
  const bool spinning = std::fabs(s.vx) > VEL_STOP || std::fabs(s.vy) > VEL_STOP;
  if (spinning) {
    s.rx += s.vx * dt; s.ry += s.vy * dt;
    const float f = std::pow(FRICTION, dt / 16.0f);
    s.vx *= f; s.vy *= f;
    s.activity_ms = now;
    return true;
  }
  s.vx = s.vy = 0;
  if (now - s.activity_ms >= IDLE_MS) s.mode = MOTION;  // the IMU eases it back from here
  return false;
}

// Project the 8 cube vertices to screen coordinates.
inline void project_vertices(float rx, float ry, float px[8], float py[8]) {
  static const float V[8][3] = {{-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1},
                                {-1, -1, 1},  {1, -1, 1},  {1, 1, 1},  {-1, 1, 1}};
  const float S = 110.0f, D = 4.0f;  // scale, camera distance
  const float cp = cosf(rx), sp = sinf(rx), cr = cosf(ry), sr = sinf(ry);
  for (int i = 0; i < 8; i++) {
    const float vx = V[i][0], vy = V[i][1], vz = V[i][2];
    const float y1 = vy * cp - vz * sp, z1 = vy * sp + vz * cp;   // about X
    const float x2 = vx * cr + z1 * sr, z2 = -vx * sr + z1 * cr;  // about Y
    const float k = S * D / (D + z2);
    px[i] = CX + x2 * k;
    py[i] = CY + y1 * k;
  }
}

// The cube as 16 polyline points (all 12 edges, some drawn twice). Used by the test.
inline void project(float rx, float ry, float out[16][2]) {
  static const uint8_t PATH[16] = {0, 1, 2, 3, 0, 4, 5, 6, 7, 4, 5, 1, 2, 6, 7, 3};
  float px[8], py[8];
  project_vertices(rx, ry, px, py);
  for (int i = 0; i < 16; i++) { out[i][0] = px[PATH[i]]; out[i][1] = py[PATH[i]]; }
}

// ---- Software rasteriser into a 16-bit framebuffer (the LVGL canvas buffer) ----
// LVGL's own line drawing anti-aliases and masks over each segment's whole bounding box, which
// cost ~50ms per frame for the cube. This is plain 3px Bresenham: a few thousand pixel writes.
constexpr int THICK = 1;  // half-width: draws 2*THICK+1 = 3px lines

struct Box { int x0, y0, x1, y1; };  // inclusive; x0 > x1 means empty

inline void fill_rect(uint16_t *buf, int stride_px, Box b, uint16_t c) {
  for (int y = b.y0; y <= b.y1; y++)
    for (int x = b.x0; x <= b.x1; x++) buf[y * stride_px + x] = c;
}

inline void draw_line(uint16_t *buf, int w, int h, int stride_px, int x0, int y0, int x1, int y1, uint16_t c) {
  const int dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0);
  const int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  for (;;) {
    for (int oy = -THICK; oy <= THICK; oy++)
      for (int ox = -THICK; ox <= THICK; ox++) {
        const int x = x0 + ox, y = y0 + oy;
        if (x >= 0 && y >= 0 && x < w && y < h) buf[y * stride_px + x] = c;
      }
    if (x0 == x1 && y0 == y1) break;
    const int e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}

// Erase the previous cube box, draw the cube, and return the box it now covers.
// The caller invalidates prev union new.
inline Box draw_cube(uint16_t *buf, int w, int h, int stride_px, float rx, float ry, uint16_t fg, uint16_t bg, Box prev) {
  static const uint8_t E[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6},
                                   {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
  if (prev.x0 <= prev.x1) fill_rect(buf, stride_px, prev, bg);
  float px[8], py[8];
  project_vertices(rx, ry, px, py);
  Box b{w, h, -1, -1};
  for (int i = 0; i < 8; i++) {
    const int x = (int) lroundf(px[i]), y = (int) lroundf(py[i]);
    b.x0 = std::min(b.x0, x); b.y0 = std::min(b.y0, y); b.x1 = std::max(b.x1, x); b.y1 = std::max(b.y1, y);
  }
  for (auto &e : E)
    draw_line(buf, w, h, stride_px, (int) lroundf(px[e[0]]), (int) lroundf(py[e[0]]),
              (int) lroundf(px[e[1]]), (int) lroundf(py[e[1]]), fg);
  b = {std::max(0, b.x0 - THICK), std::max(0, b.y0 - THICK), std::min(w - 1, b.x1 + THICK), std::min(h - 1, b.y1 + THICK)};
  return b;
}

inline uint32_t &color() { static uint32_t c = 0x00E5FF; return c; }
inline uint32_t &render_count() { static uint32_t n = 0; return n; }  // cube redraws, for the FPS label

#ifndef CUBE_NO_LVGL
// Draw the cube straight into the canvas buffer and invalidate only the changed box.
inline void render(lv_obj_t *canvas) {
  static Box prev{1, 1, 0, 0};
  static bool cleared = false;
  lv_draw_buf_t *db = lv_canvas_get_draw_buf(canvas);
  if (db == nullptr) return;
  render_count()++;
  auto *buf = reinterpret_cast<uint16_t *>(db->data);
  const int w = db->header.w, h = db->header.h, stride_px = db->header.stride / 2;
  const uint16_t bg = 0;  // black: AMOLED pixels off
  if (!cleared) {  // the malloc'd canvas buffer starts as garbage
    fill_rect(buf, stride_px, {0, 0, w - 1, h - 1}, bg);
    lv_obj_invalidate(canvas);
    cleared = true;
  }
  const Box b = draw_cube(buf, w, h, stride_px, st().rx, st().ry, lv_color_to_u16(lv_color_hex(color())), bg, prev);
  lv_area_t a;
  lv_obj_get_coords(canvas, &a);  // the canvas origin on screen
  const int ox = a.x1, oy = a.y1;
  const Box u = prev.x0 <= prev.x1
                    ? Box{std::min(prev.x0, b.x0), std::min(prev.y0, b.y0), std::max(prev.x1, b.x1), std::max(prev.y1, b.y1)}
                    : b;
  a = {(int32_t) (ox + u.x0), (int32_t) (oy + u.y0), (int32_t) (ox + u.x1), (int32_t) (oy + u.y1)};
  lv_obj_invalidate_area(canvas, &a);
  prev = b;
}
#endif

}  // namespace dialcube
