#pragma once
// Plays a Harness pet scene (a character pack, buddy/character.h) on LVGL widgets with focus.c's maths:
//   step   = (t / step_ms) % steps                                  (focus.c:356)
//   origin = ((466 - w) / 2 + dx, 233 - h / 2 + bias + dy)          (focus.c:374)
//   layer i (0 = back) shows frames[loop[step]] at origin + at[step]; discs at origin + (cx, cy) / 16
// An "attached" scene (the alert bubble) has no body layer and borrows the origin of the scene it sits on (focus.c:961).
// Pure maths is host-testable with -DSCENE_NO_LVGL (tests/test_buddy.cpp).
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>
#if __has_include(<esp_heap_caps.h>)
#include <esp_heap_caps.h>
#endif

#ifdef SCENE_NO_LVGL
struct lv_image_dsc_t { int w, h; };
typedef struct _lv_obj_t lv_obj_t;
#else
#include <lvgl.h>
#endif

namespace scene {

static constexpr int W = 466, C = 233;

// Pack data lives in PSRAM. The dial builds with CONFIG_SPIRAM_USE_CAPS_ALLOC, so plain new/malloc only ever take
// internal RAM, and a full pack's scenes (hundreds of small vectors, map nodes, names) ran it out at boot. Host
// builds (sim, tests) have no PSRAM: the std allocator.
#if __has_include(<esp_heap_caps.h>)
template<typename T> struct PsAlloc {
  using value_type = T;
  PsAlloc() = default;
  template<typename U> PsAlloc(const PsAlloc<U> &) {}
  T *allocate(size_t n) {
    auto *p = (T *) heap_caps_malloc(n * sizeof(T), MALLOC_CAP_SPIRAM);
    if (!p) abort();
    return p;
  }
  void deallocate(T *p, size_t) { heap_caps_free(p); }
  bool operator==(const PsAlloc &) const { return true; }
  bool operator!=(const PsAlloc &) const { return false; }
};
#else
template<typename T> using PsAlloc = std::allocator<T>;
#endif
template<typename T> using Vec = std::vector<T, PsAlloc<T>>;
using Str = std::basic_string<char, std::char_traits<char>, PsAlloc<char>>;
using Frames = Vec<const lv_image_dsc_t *>;

// Loaded at boot from the character pack on LittleFS (buddy/character.h). Empty vectors mean "none".
struct Disc { int16_t cx16, cy16, r16; uint32_t rgb; };  // juggling ball: filled disc, 1/16 px
struct Layer {                               // translate-only: one frame set, per-step frame index and offset
  Frames frames;
  Vec<uint8_t> loop;
  Vec<std::array<int16_t, 2>> at;
  int32_t ink = -1;  // A8 frames: drawn in this 0xRRGGBB (the tint layer takes set_tint's instead); -1 none
};
struct Scene {
  Str name;
  int16_t w = 0, h = 0, dx = 0, dy = 0;  // origin = ((466-w)/2+dx, 233-h/2+bias+dy)  (focus.c:374)
  uint8_t steps = 1; uint16_t step_ms = 100;
  Vec<Layer> layers;                         // back to front
  uint8_t discs = 0;
  Vec<Disc> disc;                            // per step: discs entries
  Vec<std::array<int16_t, 2>> count_at;      // alert: where the waiting count goes
  int8_t tint = -1;                          // layer drawn in Player::set_tint's colour (Grok avatars); -1 none
  // IMU gaze (buddy/gaze.h): the eyes layer moves up to rx, ry px. look[step] is the scripted glance baked into that
  // layer's x, taken out while the IMU steers. layer -1: the scene has no separate eyes.
  struct Gaze { int8_t layer = -1, rx = 0, ry = 0; Vec<int16_t> look; } gaze;
};

struct Pt { int x, y; };

inline uint8_t step_at(const Scene &s, uint32_t elapsed_ms) { return (elapsed_ms / s.step_ms) % s.steps; }
inline Pt origin(const Scene &s, int bias) { return {((W - s.w) >> 1) + s.dx, C - (s.h >> 1) + bias + s.dy}; }
// `base` is the scene whose origin an attached layer uses; for normal scenes base == s.
inline Pt layer_pos(const Scene &s, const Scene &base, int bias, size_t i, uint8_t k) {
  Pt o = origin(base, bias);
  return {o.x + s.layers[i].at[k][0], o.y + s.layers[i].at[k][1]};
}
// Eyes layer position with the IMU steering: scripted glance out, offset (gx, gy px) in. Other layers unchanged.
inline Pt gazed(const Scene &s, Pt p, size_t i, uint8_t k, int gx, int gy) {
  if ((int) i != s.gaze.layer) return p;
  int glance = k < s.gaze.look.size() ? s.gaze.look[k] : 0;
  return {p.x - glance + gx, p.y + gy};
}

#ifndef SCENE_NO_LVGL
// One playing scene: up to MAX_LAYERS layer images, up to 6 discs and (for the alert) a count label.
// Create once on a parent; play() swaps the scene, tick() moves things only when the step changes.
struct Player {
  static constexpr int MAX_LAYERS = 10;
  lv_obj_t *layer[MAX_LAYERS] = {}, *disc[6] = {}, *count = nullptr;
  const Scene *sc = nullptr, *base = nullptr;
  int bias = 0;
  uint32_t t0 = 0;
  int last = -1;
  bool once = false;
  uint32_t tinted = UINT32_MAX;  // set_tint's colour on this scene; UINT32_MAX none (styles cost a redraw, set once)
  bool gaze_on = false;          // IMU steering the eyes layer (set_gaze); off: the scene's own glance
  int gx = 0, gy = 0;            // eyes offset in px while gaze_on

  void create(lv_obj_t *parent, bool with_count = false) {
    for (auto &l : layer)
      l = lv_image_create(parent);
    for (auto &d : disc) {
      d = lv_obj_create(parent);
      lv_obj_remove_style_all(d);
      lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
      lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
      lv_obj_set_hidden(d, true);
    }
    for (auto *l : layer)
      lv_obj_set_clickable(l, false);
    if (with_count) {
      count = lv_label_create(parent);
      lv_obj_set_style_text_color(count, lv_color_hex(0x006FFF), 0);  // focus.c: #006fff on the bubble
      lv_obj_set_hidden(count, true);
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
    if (s == sc && (!base_ || base_ == base)) {
      // Same scene at a new bias (splash relax -50 -> face relax 4): move it, keep the animation phase.
      if (bias_ != bias) {
        bias = bias_;
        last = -1;
        tick(now);
      }
      return;
    }
    once = once_;
    ESP_LOGI("buddyui", "scene %s", s->name.c_str());
    sc = s;
    base = base_ ? base_ : s;
    bias = bias_;
    t0 = now;
    last = -1;
    if (s->layers.size() > MAX_LAYERS)
      ESP_LOGE("buddyui", "scene %s has %u layers, drawing the first %d", s->name.c_str(), (unsigned) s->layers.size(), MAX_LAYERS);
    for (size_t i = 0; i < MAX_LAYERS; i++) {
      lv_obj_set_hidden(layer[i], i >= s->layers.size());
      bool ink = i < s->layers.size() && s->layers[i].ink >= 0;
      if (ink) lv_obj_set_style_image_recolor(layer[i], lv_color_hex(s->layers[i].ink), 0);
      lv_obj_set_style_image_recolor_opa(layer[i], ink ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    }
    tinted = UINT32_MAX;
    for (int i = 0; i < 6; i++)
      lv_obj_set_hidden(disc[i], i >= s->discs);
    if (count)
      lv_obj_set_hidden(count, s->count_at.empty());
    tick(now);
  }
  // Paint the scene's tint layer (white art) in rgb. After play(); a new scene clears it.
  void set_tint(uint32_t rgb) {
    if (!sc || sc->tint < 0 || sc->tint >= MAX_LAYERS || rgb == tinted)
      return;
    tinted = rgb;
    lv_obj_set_style_image_recolor(layer[sc->tint], lv_color_hex(rgb), 0);
    lv_obj_set_style_image_recolor_opa(layer[sc->tint], LV_OPA_COVER, 0);
  }
  // IMU gaze (buddy/gaze.h look(), -1..1). Call every tick: moves the eyes layer between steps too, as the
  // dial tilts, without touching the other layers.
  void set_gaze(bool on, float x, float y) {
    int nx = 0, ny = 0;
    if (on && sc) nx = (int) lroundf(x * sc->gaze.rx), ny = (int) lroundf(y * sc->gaze.ry);
    if (on == gaze_on && nx == gx && ny == gy) return;
    gaze_on = on, gx = nx, gy = ny;
    if (!sc || last < 0 || sc->gaze.layer < 0 || sc->gaze.layer >= (int) sc->layers.size() || sc->gaze.layer >= MAX_LAYERS)
      return;
    size_t i = sc->gaze.layer;
    Pt p = layer_pos(*sc, *base, bias, i, last);
    if (gaze_on) p = gazed(*sc, p, i, last, gx, gy);
    lv_obj_set_pos(layer[i], p.x, p.y);
  }
  void stop() {
    sc = nullptr;
    for (auto *l : layer)
      lv_obj_set_hidden(l, true);
    for (auto *d : disc)
      lv_obj_set_hidden(d, true);
    if (count)
      lv_obj_set_hidden(count, true);
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
      if (gaze_on) p = gazed(*sc, p, i, k, gx, gy);
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
        lv_obj_set_hidden(count, false);
        lv_obj_update_layout(count);
        lv_obj_set_pos(count, o.x + ca[0] - lv_obj_get_width(count) / 2, o.y + ca[1] - lv_obj_get_height(count) / 2);
      } else {
        lv_obj_set_hidden(count, true);
      }
    }
  }
};
#endif

}  // namespace scene
