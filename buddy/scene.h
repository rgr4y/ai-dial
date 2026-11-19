#pragma once
// Plays a Harness pet scene (buddy/pets_claude.h) on LVGL widgets with focus.c's maths:
//   step   = (t / step_ms) % steps                                  (focus.c:356)
//   origin = ((466 - w) / 2 + dx, 233 - h / 2 + bias + dy)          (focus.c:374)
//   body at origin + (0, step_dy[step]); overlay at origin + ov_at[step]; discs at origin + (cx, cy) / 16
// An "attached" scene (the alert bubble) has no body and borrows the origin of the scene it sits on (focus.c:961).
// Pure maths is host-testable with -DSCENE_NO_LVGL (test_buddy.cpp).
#include <cstdint>

#ifdef SCENE_NO_LVGL
struct lv_image_dsc_t { int w, h; };
typedef struct _lv_obj_t lv_obj_t;
#define LV_IMAGE_HEADER_MAGIC 0
#else
#include <lvgl.h>
#endif

namespace scene {

static constexpr int W = 466, C = 233;

#ifdef SCENE_NO_LVGL
struct Disc { int16_t cx16, cy16, r16; uint32_t rgb; };
struct Scene {
  const char *name;
  int16_t w, h, dx, dy;
  uint8_t steps; uint16_t step_ms;
  const lv_image_dsc_t *const *body; const uint8_t *loop; const int8_t *step_dy;
  const lv_image_dsc_t *const *ov; const uint8_t *ov_loop; const int16_t (*ov_at)[2];
  uint8_t discs; const Disc *disc;
  const int16_t (*count_at)[2];
};
#else
}  // namespace scene
#include "pets_claude.h"
namespace scene {
using pets::Scene;
using pets::Disc;
#endif

struct Pt { int x, y; };

inline uint8_t step_at(const Scene &s, uint32_t elapsed_ms) { return (elapsed_ms / s.step_ms) % s.steps; }
inline Pt origin(const Scene &s, int bias) { return {((W - s.w) >> 1) + s.dx, C - (s.h >> 1) + bias + s.dy}; }
inline Pt body_pos(const Scene &s, int bias, uint8_t k) {
  Pt o = origin(s, bias);
  return {o.x, o.y + (s.step_dy ? s.step_dy[k] : 0)};
}
// `base` is the scene whose origin an attached overlay uses; for normal scenes base == s.
inline Pt ov_pos(const Scene &s, const Scene &base, int bias, uint8_t k) {
  Pt o = origin(base, bias);
  return {o.x + s.ov_at[k][0], o.y + s.ov_at[k][1]};
}

#ifndef SCENE_NO_LVGL
// One playing scene: a body image, an overlay image, up to 6 discs and (for the alert) a count label.
// Create once on a parent; play() swaps the scene, tick() moves things only when the step changes.
struct Player {
  lv_obj_t *body = nullptr, *ov = nullptr, *disc[6] = {}, *count = nullptr;
  const Scene *sc = nullptr, *base = nullptr;
  int bias = 0;
  uint32_t t0 = 0;
  int last = -1;
  bool once = false;

  void create(lv_obj_t *parent, bool with_count = false) {
    body = lv_image_create(parent);
    ov = lv_image_create(parent);
    for (auto &d : disc) {
      d = lv_obj_create(parent);
      lv_obj_remove_style_all(d);
      lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
      lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
      lv_obj_add_flag(d, LV_OBJ_FLAG_HIDDEN);
    }
    for (lv_obj_t *o : {body, ov})
      lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    if (with_count) {
      count = lv_label_create(parent);
      lv_obj_set_style_text_color(count, lv_color_hex(0x006FFF), 0);  // focus.c: #006fff on the bubble
      lv_obj_add_flag(count, LV_OBJ_FLAG_HIDDEN);
    }
    stop();
  }
  // base: for an attached scene (alert), the scene it sits on.
  void play(const Scene *s, uint32_t now, int bias_ = 4, const Scene *base_ = nullptr, bool once_ = false) {
    if (s == sc && (!base_ || base_ == base))
      return;
    once = once_;
    ESP_LOGI("buddyui", "scene %s", s->name);
    sc = s;
    base = base_ ? base_ : s;
    bias = bias_;
    t0 = now;
    last = -1;
    lv_obj_remove_flag(body, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(ov, LV_OBJ_FLAG_HIDDEN);
    if (!s->body)
      lv_obj_add_flag(body, LV_OBJ_FLAG_HIDDEN);
    if (!s->ov)
      lv_obj_add_flag(ov, LV_OBJ_FLAG_HIDDEN);
    for (int i = 0; i < 6; i++)
      (i < s->discs ? lv_obj_remove_flag : lv_obj_add_flag)(disc[i], LV_OBJ_FLAG_HIDDEN);
    if (count)
      (s->count_at ? lv_obj_remove_flag : lv_obj_add_flag)(count, LV_OBJ_FLAG_HIDDEN);
    tick(now);
  }
  void stop() {
    sc = nullptr;
    lv_obj_add_flag(body, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ov, LV_OBJ_FLAG_HIDDEN);
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
    if (sc->body) {
      lv_image_set_src(body, sc->body[sc->loop[k]]);
      Pt p = body_pos(*sc, bias, k);
      lv_obj_set_pos(body, p.x, p.y);
    }
    if (sc->ov) {
      lv_image_set_src(ov, sc->ov[sc->ov_loop[k]]);
      Pt p = ov_pos(*sc, *base, bias, k);
      lv_obj_set_pos(ov, p.x, p.y);
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
    if (count && sc->count_at) {
      const int16_t *ca = sc->count_at[k];
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
