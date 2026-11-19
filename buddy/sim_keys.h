#pragma once
// Simulator case keys (claude-buddy-sim.yaml): where PWR and BOOT sit on the 640 px window, and which one a touch
// hits. The sim hit-tests on the touchscreen rather than with LVGL events, because a paused LVGL (dark screen) drops
// presses. Unused on the dial. tests/test_power.cpp checks the hit-test.
#include <cmath>

namespace sim_keys {

constexpr int WIN = 640, C = WIN / 2, D = 466, CASE = 586;
constexpr int KW = 12, KH = 46, R = CASE / 2 + 4, LR = CASE / 2 + 36;  // key size, key and label radius
constexpr int REACH = 14;                                              // LVGL ext_click_area, same as the hit-test
enum Key { PWR, BOOT, COUNT, NONE = -1 };
constexpr const char *NAME[COUNT] = {"PWR", "BOOT"};
constexpr float DEG[COUNT] = {-42, 39};  // ~1:30 and ~4 o'clock seen from the front

inline float rad(Key k) { return DEG[k] * 3.14159265f / 180; }
inline float cx(Key k, int r = R) { return C + r * cosf(rad(k)); }
inline float cy(Key k, int r = R) { return C + r * sinf(rad(k)); }

inline Key hit(int x, int y) {
  for (int k = 0; k < COUNT; k++)
    if (hypotf(x - cx(Key(k)), y - cy(Key(k))) < KH / 2 + REACH) return Key(k);
  return NONE;
}

}  // namespace sim_keys
