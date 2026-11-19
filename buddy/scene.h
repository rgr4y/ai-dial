#pragma once
// Plays a Harness pet scene (a character pack, buddy/character.h) on LVGL widgets with focus.c's maths:
//   step   = (t / step_ms) % steps                                  (focus.c:356)
//   origin = ((466 - w) / 2 + dx, 233 - h / 2 + bias + dy)          (focus.c:374)
//   layer i (0 = back) shows frames[loop[step]] at origin + at[step]; discs at origin + (cx, cy) / 16
// An "attached" scene (the alert bubble) has no body layer and borrows the origin of the scene it sits on (focus.c:961).
// Pure maths is host-testable with -DSCENE_NO_LVGL (tests/test_buddy.cpp).
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#ifdef SCENE_NO_LVGL
struct lv_image_dsc_t { int w, h; };
typedef struct _lv_obj_t lv_obj_t;
#else
#include <lvgl.h>
#endif

namespace scene {

static constexpr int W = 466, C = 233;

// Loaded at boot from the character pack on LittleFS (buddy/character.h). Empty vectors mean "none".
struct Disc { int16_t cx16, cy16, r16; uint32_t rgb; };  // juggling ball: filled disc, 1/16 px
struct Layer {                               // translate-only: one frame set, per-step frame index and offset
  std::vector<const lv_image_dsc_t *> frames;
  std::vector<uint8_t> loop;
  std::vector<std::array<int16_t, 2>> at;
};
struct Scene {
  std::string name;
  int16_t w = 0, h = 0, dx = 0, dy = 0;  // origin = ((466-w)/2+dx, 233-h/2+bias+dy)  (focus.c:374)
  uint8_t steps = 1; uint16_t step_ms = 100;
  std::vector<Layer> layers;                 // back to front
  uint8_t discs = 0;
  std::vector<Disc> disc;                    // per step: discs entries
  std::vector<std::array<int16_t, 2>> count_at;  // alert: where the waiting count goes
};
// Small pet above the recap card: zoom 6/8 and 4/8 frame sets, one (frame, dy) loop.
struct Small {
  std::vector<const lv_image_dsc_t *> z6, z4;
  std::vector<std::array<int8_t, 2>> loop;
  uint16_t step_ms = 100;
};

struct Pt { int x, y; };

inline uint8_t step_at(const Scene &s, uint32_t elapsed_ms) { return (elapsed_ms / s.step_ms) % s.steps; }
inline Pt origin(const Scene &s, int bias) { return {((W - s.w) >> 1) + s.dx, C - (s.h >> 1) + bias + s.dy}; }
// `base` is the scene whose origin an attached layer uses; for normal scenes base == s.
inline Pt layer_pos(const Scene &s, const Scene &base, int bias, size_t i, uint8_t k) {
  Pt o = origin(base, bias);
  return {o.x + s.layers[i].at[k][0], o.y + s.layers[i].at[k][1]};
}

#ifndef SCENE_NO_LVGL
// One playing scene: up to MAX_LAYERS layer images, up to 6 discs and (for the alert) a count label.
// Create once on a parent; play() swaps the scene, tick() moves things only when the step changes.
struct Player {
  static constexpr int MAX_LAYERS = 6;
  lv_obj_t *layer[MAX_LAYERS] = {}, *disc[6] = {}, *count = nullptr;
  const Scene *sc = nullptr, *base = nullptr;
  int bias = 0;
  uint32_t t0 = 0;
  int last = -1;
  bool once = false;

  void create(lv_obj_t *parent, bool with_count = false) {
    for (auto &l : layer)
      l = lv_image_create(parent);
    for (auto &d : disc) {
      d = lv_obj_create(parent);
      lv_obj_remove_style_all(d);
      lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
      lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
      lv_obj_add_flag(d, LV_OBJ_FLAG_HIDDEN);
    }
    for (auto *l : layer)
      lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
    if (with_count) {
      count = lv_label_create(parent);
      lv_obj_set_style_text_color(count, lv_color_hex(0x006FFF), 0);  // focus.c: #006fff on the bubble
      lv_obj_add_flag(count, LV_OBJ_FLAG_HIDDEN);
    }
    stop();
  }
  // base: for an attached scene (alert), the scene it sits on.
  // s == nullptr (no character loaded yet) shows nothing.
  void play(const Scene *s, uint32_t now, int bias_ = 4, const Scene *base_ = nullptr, bool once_ = false) {
    if (!s) {
      if (sc) stop();
      return;
    }
    if (s == sc && (!base_ || base_ == base))
      return;
    once = once_;
    ESP_LOGI("buddyui", "scene %s", s->name.c_str());
    sc = s;
    base = base_ ? base_ : s;
    bias = bias_;
    t0 = now;
    last = -1;
    if (s->layers.size() > MAX_LAYERS)
      ESP_LOGE("buddyui", "scene %s has %u layers, drawing the first %d", s->name.c_str(), (unsigned) s->layers.size(), MAX_LAYERS);
    for (size_t i = 0; i < MAX_LAYERS; i++)
      (i < s->layers.size() ? lv_obj_remove_flag : lv_obj_add_flag)(layer[i], LV_OBJ_FLAG_HIDDEN);
    for (int i = 0; i < 6; i++)
      (i < s->discs ? lv_obj_remove_flag : lv_obj_add_flag)(disc[i], LV_OBJ_FLAG_HIDDEN);
    if (count)
      (!s->count_at.empty() ? lv_obj_remove_flag : lv_obj_add_flag)(count, LV_OBJ_FLAG_HIDDEN);
    tick(now);
  }
  void stop() {
    sc = nullptr;
    for (auto *l : layer)
      lv_obj_add_flag(l, LV_OBJ_FLAG_HIDDEN);
    for (auto *d : disc)
      lv_obj_add_flag(d, LV_OBJ_FLAG_HIDDEN);
    if (count)
      lv_obj_add_flag(count, LV_OBJ_FLAG_HIDDEN);
  }
  void tick(uint32_t now) {
    if (!sc)
      return;
    // once: play through, then hold the last step (the alert bubble, focus.c:259-269).
    uint32_t age = now - t0;
    uint8_t k = once && age >= (uint32_t) sc->steps * sc->step_ms ? sc->steps - 1 : step_at(*sc, age);
    if (k == last)
      return;
    last = k;
    for (size_t i = 0; i < sc->layers.size() && i < MAX_LAYERS; i++) {
      const Layer &l = sc->layers[i];
      lv_image_set_src(layer[i], l.frames[l.loop[k]]);
      Pt p = layer_pos(*sc, *base, bias, i, k);
      lv_obj_set_pos(layer[i], p.x, p.y);
    }
    Pt o = origin(*base, bias);
    for (int i = 0; i < sc->discs; i++) {
      const Disc &d = sc->disc[k * sc->discs + i];
      int r = d.r16 >> 4;
      lv_obj_set_size(disc[i], 2 * r, 2 * r);
      lv_obj_set_pos(disc[i], o.x + (d.cx16 >> 4) - r, o.y + (d.cy16 >> 4) - r);
      lv_obj_set_style_bg_color(disc[i], lv_color_hex(d.rgb), 0);
    }
    // The waiting count, centred on base origin + count_at; (0,0) means not shown this step (focus.c:965-976).
    if (count && !sc->count_at.empty()) {
      const auto &ca = sc->count_at[k];
      if (ca[0] || ca[1]) {
        lv_obj_remove_flag(count, LV_OBJ_FLAG_HIDDEN);
        lv_obj_update_layout(count);
        lv_obj_set_pos(count, o.x + ca[0] - lv_obj_get_width(count) / 2, o.y + ca[1] - lv_obj_get_height(count) / 2);
      } else {
        lv_obj_add_flag(count, LV_OBJ_FLAG_HIDDEN);
      }
    }
  }
};
#endif

}  // namespace scene
