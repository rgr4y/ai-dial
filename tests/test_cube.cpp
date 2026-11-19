// Host check for cube.h's state machine: c++ -std=c++17 -DCUBE_NO_LVGL tests/test_cube.cpp -o /tmp/test_cube && /tmp/test_cube
#include <cassert>
#include <cstdio>
#include "../cube.h"

using namespace dialcube;

static void reset() { st() = CubeState{}; }

int main() {
  // IMU drives the cube in MOTION mode.
  reset();
  assert(on_imu(10, 20) && st().rx > 0 && st().ry > 0);
  // Deadband: once settled, noise-sized wobble doesn't move or redraw the cube.
  for (int i = 0; i < 100; i++) on_imu(10, 20);
  assert(!on_imu(10.3f, 19.8f));
  assert(on_imu(12, 20));  // a real tilt still gets through

  // A touch outside the capture circle doesn't grab the cube.
  reset();
  on_touch(5, 5, 0);
  assert(st().mode == MOTION && !st().dragging);

  // A touch inside grabs it. Dragging rotates it and blocks the IMU.
  reset();
  on_touch(233, 233, 0);
  assert(st().mode == MANUAL && st().dragging);
  assert(on_move(253, 233, 16));  // 20px right
  assert(st().ry < 0);
  assert(!on_imu(45, 45));

  // A fast release flings it: momentum keeps it turning, then friction stops it.
  on_move(273, 233, 32);
  on_release(40);
  assert(!st().dragging && st().vy != 0);
  float ry = st().ry;
  assert(tick(56, 16) && st().ry != ry);
  uint32_t t = 56;
  while (tick(t += 16, 16)) {}
  assert(st().vx == 0 && st().vy == 0 && st().mode == MANUAL);

  // A slow release (finger stopped first) doesn't fling.
  reset();
  on_touch(233, 233, 0);
  on_move(260, 233, 16);
  on_release(200);
  assert(st().vx == 0 && st().vy == 0);

  // Idle timeout hands control back to the IMU.
  reset();
  on_touch(233, 233, 0);
  on_move(260, 233, 16);
  on_release(200);
  assert(!tick(16 + IDLE_MS - 100, 16) && st().mode == MANUAL);  // idle counts from the last move (16ms)
  tick(16 + IDLE_MS + 1, 16);
  assert(st().mode == MOTION);

  // Double-tap on the centre goes back to MOTION, even mid-spin.
  reset();
  on_touch(233, 233, 0); on_move(300, 233, 16); on_release(20);  // fling
  on_touch(230, 230, 100); on_release(150);
  on_touch(236, 236, 300); on_release(350);
  assert(st().mode == MOTION && st().vx == 0 && st().vy == 0);

  // Two taps that are too far apart in time don't count.
  reset();
  on_touch(233, 233, 0); on_release(50);
  on_touch(233, 233, 1000); on_release(1050);
  assert(st().mode == MANUAL);

  // The projection keeps the cube on screen at assorted angles.
  float p[16][2];
  for (float a = -3.2f; a < 3.2f; a += 0.4f) {
    project(a, -a * 0.7f, p);
    for (auto &q : p) assert(q[0] > 0 && q[0] < 466 && q[1] > 0 && q[1] < 466);
  }

  // Rasteriser: draws inside the returned box, and erasing the old box leaves no stale pixels.
  {
    const int W = 466, H = 466;
    static uint16_t fb[W * H] = {};
    Box b1 = draw_cube(fb, W, H, W, 0.3f, 0.5f, 0xFFFF, 0, Box{1, 1, 0, 0});
    assert(b1.x0 <= b1.x1 && b1.y0 <= b1.y1);
    int lit = 0, outside = 0;
    for (int y = 0; y < H; y++)
      for (int x = 0; x < W; x++)
        if (fb[y * W + x]) { lit++; if (x < b1.x0 || x > b1.x1 || y < b1.y0 || y > b1.y1) outside++; }
    assert(lit > 1000 && outside == 0);
    Box b2 = draw_cube(fb, W, H, W, -0.8f, 1.2f, 0xFFFF, 0, b1);
    for (int y = 0; y < H; y++)
      for (int x = 0; x < W; x++)
        if (fb[y * W + x]) assert(x >= b2.x0 && x <= b2.x1 && y >= b2.y0 && y <= b2.y1);
  }

  puts("cube ok");
}
