// Host check for buddy/scene.h maths against focus.c / the harness-pets demo:
//   c++ -std=c++17 -DSCENE_NO_LVGL test_buddy.cpp -o /tmp/test_buddy && /tmp/test_buddy
#include <cassert>
#include <cstdio>
#include "buddy/scene.h"

using namespace scene;

int main() {
  // claude_work: 117x117, dx 1, dy -4, 26 steps x 55 ms (pets.c:66).
  static const int8_t dy[26] = {0, -1, -1, -1, -1, -1};
  static const int16_t at[26][2] = {{114, 43}};
  Scene work{"claude_work", 117, 117, 1, -4, 26, 55, nullptr, nullptr, dy, nullptr, nullptr, at, 0, nullptr, nullptr};

  // origin = ((466-117)>>1)+1, 233-(117>>1)+4-4 = (175, 175)
  Pt o = origin(work, 4);
  assert(o.x == 175 && o.y == 175);
  assert(step_at(work, 0) == 0 && step_at(work, 54) == 0 && step_at(work, 55) == 1);
  assert(step_at(work, 26 * 55) == 0);  // wraps
  assert(body_pos(work, 4, 1).y == 174);  // step_dy -1
  assert(ov_pos(work, work, 4, 0).x == 175 + 114 && ov_pos(work, work, 4, 0).y == 175 + 43);

  // An attached scene (alert, w = h = 0) uses its base's origin, not its own.
  static const int16_t aat[1][2] = {{-31, -21}};
  Scene alert{"claude_alert", 0, 0, 0, 0, 1, 110, nullptr, nullptr, nullptr, nullptr, nullptr, aat, 0, nullptr, nullptr};
  assert(ov_pos(alert, work, 4, 0).x == 175 - 31 && ov_pos(alert, work, 4, 0).y == 175 - 21);

  puts("scene ok");
}
