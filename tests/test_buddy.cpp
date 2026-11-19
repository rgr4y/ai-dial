// Host check for buddy/scene.h maths against focus.c / the harness-pets demo:
//   c++ -std=c++17 -DSCENE_NO_LVGL tests/test_buddy.cpp -o /tmp/test_buddy && /tmp/test_buddy
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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

  // flatten: 2 layers x 2 steps. Bottom: 4x4 opaque (colour 0x001F) at (0,0). Top: 2x2 at (1,1) at step 0 alpha 255
  // (0xF800), at step 1 alpha 128 (0xF800). Overlap pixel (1,1) -> 255 / ~half blend.
  auto mk = [](uint16_t c, uint8_t a, int w, int h) {
    auto *d = new lv_image_dsc_t{};
    d->header.w = w; d->header.h = h;
    auto *b = new uint8_t[w * h * 3];
    for (int i = 0; i < w * h; i++) { memcpy(b + i * 2, &c, 2); b[w * h * 2 + i] = a; }
    d->data = b;
    return d;
  };
  Scene fl;
  fl.steps = 2;
  Layer lo, hi;
  lo.frames = {mk(0x001F, 255, 4, 4)}; lo.loop = {0, 0}; lo.at = {{0, 0}, {0, 0}};
  hi.frames = {mk(0xF800, 255, 2, 2), mk(0xF800, 128, 2, 2)}; hi.loop = {0, 1}; hi.at = {{1, 1}, {1, 1}};
  fl.layers = {lo, hi};
  Scene *flat = flatten(fl);
  assert(flat && flat->layers.size() == 1 && flat->steps == 2);
  const lv_image_dsc_t *f0 = flat->layers[0].frames[0], *f1 = flat->layers[0].frames[1];
  assert(f0->header.w == 4 && f0->header.h == 4 && flat->layers[0].at[0][0] == 0);
  auto px = [](const lv_image_dsc_t *f, int x, int y) { uint16_t c; memcpy(&c, f->data + (y * 4 + x) * 2, 2); return c; };
  assert(px(f0, 1, 1) == 0xF800 && f0->data[32 + 5] == 255);  // top wins at alpha 255
  assert(px(f0, 0, 0) == 0x001F);                            // bottom only
  uint16_t m = px(f1, 1, 1);                                 // 128/255 red + 127/255 blue
  assert(std::abs((int) (m >> 11) - 16) <= 1 && std::abs((int) (m & 31) - 15) <= 1 && f1->data[32 + 5] == 255);

  puts("scene ok");
}
