// Host check for buddy/scene.h maths against focus.c / the harness-pets demo:
//   c++ -std=c++17 -DSCENE_NO_LVGL test_buddy.cpp -o /tmp/test_buddy && /tmp/test_buddy
#include <cassert>
#include <cstdio>
#include "buddy/scene.h"

using namespace scene;

int main() {
  // claude_work: 117x117, dx 1, dy -4, 26 steps x 55 ms (pets.c:66).
  Scene work;
  work.name = "claude_work";
  work.w = work.h = 117; work.dx = 1; work.dy = -4; work.steps = 26; work.step_ms = 55;
  work.step_dy = {0, -1, -1, -1, -1, -1};
  work.step_dy.resize(26);
  work.ov_at = {{114, 43}};

  // origin = ((466-117)>>1)+1, 233-(117>>1)+4-4 = (175, 175)
  Pt o = origin(work, 4);
  assert(o.x == 175 && o.y == 175);
  assert(step_at(work, 0) == 0 && step_at(work, 54) == 0 && step_at(work, 55) == 1);
  assert(step_at(work, 26 * 55) == 0);  // wraps
  assert(body_pos(work, 4, 1).y == 174);  // step_dy -1
  assert(ov_pos(work, work, 4, 0).x == 175 + 114 && ov_pos(work, work, 4, 0).y == 175 + 43);

  // An attached scene (alert, w = h = 0) uses its base's origin, not its own.
  Scene alert;
  alert.name = "claude_alert";
  alert.step_ms = 110;
  alert.ov_at = {{-31, -21}};
  assert(ov_pos(alert, work, 4, 0).x == 175 - 31 && ov_pos(alert, work, 4, 0).y == 175 - 21);

  puts("scene ok");
}
