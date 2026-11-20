// Host check for buddy/gaze.h: still -> off, tilt -> look that way, hold still -> back home and off.
//   c++ -std=c++17 tests/test_gaze.cpp -o /tmp/test_gaze && /tmp/test_gaze
#include <cassert>
#include <cmath>
#include <cstdio>
#include "../buddy/gaze.h"

int main() {
  uint32_t t = 0;
  auto feed = [&](float deg_x, float deg_y, int n) {  // tilt about each axis, n samples 50 ms apart
    for (int i = 0; i < n; i++, t += 50) {
      float ax = std::sin(deg_x * 3.14159265f / 180), ay = std::sin(deg_y * 3.14159265f / 180);
      gaze::update(ax, ay, std::sqrt(std::fmax(0.f, 1 - ax * ax - ay * ay)), t);
    }
  };

  feed(0, 0, 40);
  assert(!gaze::look().on);  // flat and still: the scene's own loop

  gaze::update(NAN, NAN, NAN, t);  // failed read: ignored
  assert(!gaze::look().on);

  feed(25, 0, 4);  // tip right edge down: eyes look right
  gaze::Look l = gaze::look();
  assert(l.on && l.x > 0.5f && std::fabs(l.y) < 0.05f);

  feed(25, -25, 4);  // and top edge down: up and right
  l = gaze::look();
  assert(l.on && l.y < -0.3f);

  feed(25, -25, 60);  // held still at that tilt for 3 s: eased home, scene back in charge
  l = gaze::look();
  assert(!l.on && l.x == 0 && l.y == 0);

  feed(25, -25, 1);  // still means still: no flicker back on
  assert(!gaze::look().on);

  gaze::reset();  // screen dark: next sample is a fresh baseline, even at a new tilt
  feed(-40, 10, 1);
  assert(!gaze::look().on);

  puts("gaze ok");
}
