// Host check for buddy/scene.h maths against focus.c / the harness-pets demo:
//   c++ -std=c++17 -DSCENE_NO_LVGL tests/test_buddy.cpp -o /tmp/test_buddy && /tmp/test_buddy
#include <cassert>
#include <cstdio>
#include "../buddy/scene.h"

using namespace scene;

int main() {
  // claude_work: 117x117, dx 1, dy -4, 26 steps x 55 ms (pets.c:66).
  Scene work;
  work.name = "claude_work";
  work.w = work.h = 117; work.dx = 1; work.dy = -4; work.steps = 26; work.step_ms = 55;
  Layer body, ov;
  body.at = {{0, 0}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}};
  body.at.resize(26);
  ov.at = {{114, 43}};
  work.layers = {body, ov};

  // origin = ((466-117)>>1)+1, 233-(117>>1)+4-4 = (175, 175)
  Pt o = origin(work, 4);
  assert(o.x == 175 && o.y == 175);
  assert(step_at(work, 0) == 0 && step_at(work, 54) == 0 && step_at(work, 55) == 1);
  assert(step_at(work, 26 * 55) == 0);  // wraps
  assert(layer_pos(work, work, 4, 0, 1).y == 174);  // step_dy -1
  assert(layer_pos(work, work, 4, 1, 0).x == 175 + 114 && layer_pos(work, work, 4, 1, 0).y == 175 + 43);

  // An attached scene (alert, w = h = 0) uses its base's origin, not its own.
  Scene alert;
  alert.name = "claude_alert";
  alert.step_ms = 110;
  Layer bubble;
  bubble.at = {{-31, -21}};
  alert.layers = {bubble};
  assert(layer_pos(alert, work, 4, 0, 0).x == 175 - 31 && layer_pos(alert, work, 4, 0, 0).y == 175 - 21);

  // IMU gaze: only the eyes layer moves, and its scripted glance comes out while the IMU steers.
  work.gaze.layer = 1;
  work.gaze.look = {4};
  Pt eyes = layer_pos(work, work, 4, 1, 0);
  assert(gazed(work, eyes, 1, 0, 3, -2).x == eyes.x - 4 + 3 && gazed(work, eyes, 1, 0, 3, -2).y == eyes.y - 2);
  assert(gazed(work, layer_pos(work, work, 4, 0, 0), 0, 0, 3, -2).x == layer_pos(work, work, 4, 0, 0).x);
  assert(gazed(work, eyes, 1, 5, 3, 0).x == eyes.x + 3);  // no look entry for that step: no glance to remove

  puts("scene ok");
}
