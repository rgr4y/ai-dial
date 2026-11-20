#pragma once
#include "ui_widgets.h"

namespace buddyui {

// Splash entrance: the wordmark rises 24 px and fades in.
// Bridge connected: BURST_N green sparks shoot from the centre to the bezel, shrinking and fading (700 ms).
// One anim drives them all; each spark is a small circle, so redraws stay cheap.
inline void burst_step(void *, int32_t v) {
  Ui &u = ui();
  float t = v / 1000.0f, e = 1 - (1 - t) * (1 - t) * (1 - t);  // ease-out cubic
  for (int i = 0; i < Ui::BURST_N; i++) {
    float a = i * 6.2832f / Ui::BURST_N;
    int r = (int) (u.burst_r[i] * e);
    int d = (int) (14 * (1 - t)) + 3;
    lv_obj_set_size(u.burst[i], d, d);
    lv_obj_set_pos(u.burst[i], 233 + (int) (cosf(a) * r) - d / 2, 233 + (int) (sinf(a) * r) - d / 2);
    lv_obj_set_style_bg_opa(u.burst[i], (lv_opa_t) (255 * (1 - t * t)), 0);
  }
}
inline void burst() {
  Ui &u = ui();
  for (int i = 0; i < Ui::BURST_N; i++) {
    u.burst_r[i] = 170 + rand() % 60;
    lv_obj_move_foreground(u.burst[i]);
    show(u.burst[i], true);
  }
  lv_anim_t a = make_anim(&u, burst_step, 0, 1000, 700);
  lv_anim_set_completed_cb(&a, [](lv_anim_t *) {
    for (auto *o : ui().burst) show(o, false);
  });
  lv_anim_start(&a);
}

inline void splash_in() {
  Ui &u = ui();
  auto run = [](lv_obj_t *o, lv_anim_exec_xcb_t cb, int32_t from, int32_t to, uint32_t ms, uint32_t delay) {
    lv_anim_t a = make_anim(o, cb, from, to, ms, delay, lv_anim_path_ease_out);
    lv_anim_start(&a);
  };
  auto opa = [](void *o, int32_t v) { lv_obj_set_style_opa((lv_obj_t *) o, (lv_opa_t) v, 0); };
  auto ty = [](void *o, int32_t v) { lv_obj_set_style_translate_y((lv_obj_t *) o, v, 0); };
  run(u.word, opa, 0, 255, 700, 250);
  run(u.word, ty, 24, 0, 700, 250);
}

// Usage screen layout: per provider its name, then a row per rate window: label,
// bar (grows in over 500 ms), percent, time to reset. Block centred vertically.
inline void build_usage() {
  Ui &u = ui();
  const auto &cards = u.buddy->snap().usages;
  lv_obj_clean(u.usage);
  auto label = [&](const lv_font_t *f, uint32_t ink, const std::string &t, int x, int cy, int w, lv_text_align_t al) {
    lv_obj_t *l = mk_label(u.usage, f, ink);
    lv_label_set_text(l, t.c_str());
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_width(l, w);
    lv_obj_set_style_text_align(l, al, 0);
    lv_obj_set_pos(l, x, cy - lv_font_get_line_height(f) / 2);
    lv_obj_set_event_bubble(l, true);
  };
  auto bar = [&](int x, int cy, int w, uint32_t col) {
    lv_obj_t *b = mk_fill(u.usage, w, 10, 5, col);
    lv_obj_set_pos(b, x, cy - 5);
    lv_obj_set_event_bubble(b, true);
    return b;
  };
  if (cards.empty()) {
    label(u.f.f25, SECONDARY, "Loading usage\xE2\x80\xA6", 0, scene::C, scene::W, LV_TEXT_ALIGN_CENTER);
    return;
  }
  constexpr int NAME_H = 36, ROW = 32, GAP = 18, X = 78, BX = 156, BW = 124, PX = BX + BW + 60;  // PX: percent right edge
  int total = GAP * ((int) cards.size() - 1);
  for (const auto &c : cards) total += NAME_H + ROW * (int) c.w.size();
  int y = scene::C - total / 2 + 6;
  for (const auto &c : cards) {
    uint32_t brand = provider_ink(c.name);
    label(u.f.f26m, brand, c.name, X, y + NAME_H / 2, 300, LV_TEXT_ALIGN_LEFT);
    y += NAME_H;
    for (const auto &w : c.w) {
      int cy = y + ROW / 2;
      uint32_t ink = usage_ink(w.pct, brand);  // bar and percent in the brand colour until 80%
      label(u.f.f20, RELAX_LINE, w.label, X, cy, 72, LV_TEXT_ALIGN_LEFT);
      bar(BX, cy, BW, 0x2a2c34);
      lv_obj_t *fill = bar(BX, cy, 10, ink);
      lv_anim_t a = make_anim(fill, [](void *o, int32_t v) { lv_obj_set_width((lv_obj_t *) o, v); }, 10,
                              std::max<int>(10, BW * (int) std::min<uint32_t>(w.pct, 100) / 100), 500, 0,
                              lv_anim_path_ease_out);
      lv_anim_start(&a);
      char pct[8];
      snprintf(pct, sizeof(pct), "%u%%", (unsigned) w.pct);
      label(u.f.f20, ink, pct, PX - 64, cy, 64, LV_TEXT_ALIGN_RIGHT);
      label(u.f.f20, 0x7a7a86, w.reset, PX + 6, cy, 84, LV_TEXT_ALIGN_RIGHT);  // fits "47h 59m", the longest leftText()
      y += ROW;
    }
    y += GAP;
  }
}

// Bolt after the side % (bat_arc: clockwise 150..210 deg, centred on 180, r 205): rotate an upright 12x22 zigzag so
// its top points outward like the arc glyphs, centred 10 px past the end of the text.
inline void place_bat_bolt() {
  Ui &u = ui();
  static const float BOLT[4][2] = {{9, 0}, {1, 12}, {11, 12}, {3, 22}};  // centre (6, 11)
  int w = 0;
  for (char c : u.bat_txt) w += lv_font_get_glyph_width(u.f.f26m, (uint8_t) c, 0);
  const float r = 205, deg = 3.14159265f / 180;
  if (lv_obj_is_hidden(u.bat_arc)) w = -20;  // bolt alone: centred where the text would be
  float th = 180 + (w / 2.0f + 10) / r / deg;  // bolt centre angle
  float rot = (th + 90) * deg, cx = 233 + r * cosf(th * deg), cy = 233 + r * sinf(th * deg);
  float xs[4], ys[4], minx = 1e9f, miny = 1e9f;
  for (int i = 0; i < 4; i++) {
    float dx = BOLT[i][0] - 6, dy = BOLT[i][1] - 11;
    xs[i] = cx + dx * cosf(rot) - dy * sinf(rot);
    ys[i] = cy + dx * sinf(rot) + dy * cosf(rot);
    minx = std::min(minx, xs[i]);
    miny = std::min(miny, ys[i]);
  }
  minx -= 2, miny -= 2;  // room for the 4 px stroke
  for (int i = 0; i < 4; i++) u.bat_bolt_pts[i] = {(lv_value_precise_t) (xs[i] - minx), (lv_value_precise_t) (ys[i] - miny)};
  lv_line_set_points(u.bat_bolt, u.bat_bolt_pts, 4);
  lv_obj_set_pos(u.bat_bolt, (int32_t) minx, (int32_t) miny);
}

// OTA progress, called from the esphome OTA triggers (claude-buddy.yaml): 1 begin, 2 progress (pct), 0 end,
// 3 abort, 4 error (err). The upload blocks the main loop (no interval, no LVGL timer), so each call
// draws the page itself with lv_refr_now.
inline void ota(int state, float pct, uint8_t err) {
  Ui &u = ui();
  if (!u.root) return;
  uint32_t now = esphome::millis();
  if (state == 1) {  // wake without the fade: the YAML already turned the panel on and unpaused LVGL
    activity(now);
    u.wake = u.sleep = false;
    u.ota_on = true;
    u.ota_fail_at = 0;
    arc_text(u.name_arc, "Updating", NAME);
    arc_text(u.status_arc, "Receiving firmware", RELAX_LINE);
    update(now);
  } else if (!u.ota_on) {
    return;
  }
  if (state == 3 || state == 4) {
    u.ota_fail_at = now;
    char t[32] = "Update failed";
    if (state == 4) snprintf(t, sizeof(t), "Update failed (%u)", (unsigned) err);
    arc_text(u.status_arc, t, ERROR);
  } else if (state != 1) {
    int p = state == 0 ? 100 : std::max(0, std::min(100, (int) (pct + 0.5f)));
    char t[8];
    snprintf(t, sizeof(t), "%d%%", p);
    set_text(u.ota_pct, t);
    lv_obj_set_width(u.ota_fill, std::max(OTA_BAR_MIN, OTA_BAR_W * p / 100));
    if (state == 0) arc_text(u.status_arc, "Rebooting", RELAX_LINE);
  }
  lv_refr_now(nullptr);
}

inline void on_decide(lv_event_t *e) { tap(lv_event_get_target_obj(e) == ui().btn_allow ? "allow" : "deny"); }

inline void build_brand(lv_obj_t *root) {
  Ui &u = ui();
  // Brand / pairing (spec §5): wordmark "Claude" in Inter Bold 48 on a 50 px line at y 208.
  u.word = mk_label(root, u.f.f48b, TEXT);
  set_text(u.word, "Otto");
  lv_obj_align(u.word, LV_ALIGN_TOP_MID, 0, 262);
  u.sub = mk_label(root, u.f.f25, SECONDARY);
  lv_obj_set_width(u.sub, 340);
  lv_label_set_long_mode(u.sub, LV_LABEL_LONG_MODE_WRAP);
  lv_obj_align(u.sub, LV_ALIGN_TOP_MID, 0, 270);
}

inline void build_permission(lv_obj_t *root) {
  Ui &u = ui();
  // Permission (new; STOP layout, spec §2.8): question title, hint card, [deny] [allow].
  u.perm = mk_page(root, true, false);  // gestures reach root
  lv_obj_set_scrollable(u.perm, false);
  u.perm_title = mk_label(u.perm, u.f.f30, NAME);
  lv_obj_set_width(u.perm_title, 330);
  lv_label_set_long_mode(u.perm_title, LV_LABEL_LONG_MODE_DOTS);
  lv_obj_align(u.perm_title, LV_ALIGN_TOP_MID, 0, 132);
  u.perm_hint = mk_label(u.perm, u.f.f25, RECAP);
  lv_obj_set_width(u.perm_hint, 348);
  lv_obj_set_height(u.perm_hint, 3 * lv_font_get_line_height(u.f.f25));  // 3 rows (spec §2.8)
  lv_label_set_long_mode(u.perm_hint, LV_LABEL_LONG_MODE_DOTS);
  lv_obj_align(u.perm_hint, LV_ALIGN_TOP_MID, 0, 190);
  u.perm_count = mk_label(u.perm, u.f.f20, SECONDARY);
  lv_obj_align(u.perm_count, LV_ALIGN_TOP_MID, 0, 290);
  // Deny | Allow: centres 78 px either side of the middle, row centred on y 352 (clear of the status arc below
  // and the "n of m waiting" count at y 290 above).
  u.btn_deny = mk_button(u.perm, u.f.f25, "Deny", Btn::ERROR);
  u.btn_allow = mk_button(u.perm, u.f.f25, "Allow", Btn::PRIMARY);
  lv_obj_set_pos(u.btn_deny, -78, 352 - lv_obj_get_height(u.btn_deny) / 2);
  lv_obj_set_pos(u.btn_allow, 78, 352 - lv_obj_get_height(u.btn_allow) / 2);
  lv_obj_add_event_cb(u.btn_deny, on_decide, LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(u.btn_allow, on_decide, LV_EVENT_CLICKED, nullptr);
}

inline void build_battery_page(lv_obj_t *root) {
  Ui &u = ui();
  // Battery page (BOOT, after usage): ring + big %, values set in update().
  u.battery_page = mk_page(root, true, true);
  u.battery_ring = lv_arc_create(u.battery_page);
  lv_obj_set_size(u.battery_ring, 340, 340);  // outer radius 170: arc-label glyphs reach in to ~185
  lv_obj_center(u.battery_ring);
  lv_arc_set_rotation(u.battery_ring, 270);   // starts at 12 o'clock
  lv_arc_set_bg_angles(u.battery_ring, 0, 360);
  lv_arc_set_range(u.battery_ring, 0, 100);
  lv_obj_set_style_arc_width(u.battery_ring, 24, LV_PART_MAIN);
  lv_obj_set_style_arc_width(u.battery_ring, 24, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(u.battery_ring, lv_color_hex(CARD_RIM), LV_PART_MAIN);
  lv_obj_remove_style(u.battery_ring, nullptr, LV_PART_KNOB);
  lv_obj_set_clickable(u.battery_ring, false);
  lv_obj_set_event_bubble(u.battery_ring, true);
  u.battery_num = mk_label(u.battery_page, u.f.f96b, TEXT);
  lv_obj_center(u.battery_num);
}

inline void build_ota_page(lv_obj_t *root) {
  Ui &u = ui();
  // OTA page: big % and a rect bar (like the usage bars), text on the arcs; values set by ota().
  u.ota_page = mk_page(root, false, true);
  u.ota_pct = mk_label(u.ota_page, u.f.f96b, TEXT);
  lv_label_set_text(u.ota_pct, "0%");
  lv_obj_align(u.ota_pct, LV_ALIGN_CENTER, 0, -30);
  auto ota_rect = [&](int w, uint32_t col) {
    lv_obj_t *o = mk_fill(u.ota_page, w, 14, 7, col);
    lv_obj_set_pos(o, (scene::W - OTA_BAR_W) / 2, scene::C + 50);
    return o;
  };
  ota_rect(OTA_BAR_W, 0x2a2c34);
  u.ota_fill = ota_rect(OTA_BAR_MIN, provider_ink("Claude"));
}

inline void paint_brand(Vis &vis, const Frame &f) {
  Ui &u = ui();
  uint32_t now = f.now;
  bool entered = f.entered;
  // Splash (redesign of spec §2.1): the pet idling above the wordmark, and what the dial is waiting on
  // shimmering on the lower arc. Same screen whenever the bridge goes quiet. (A spinner ring round the bezel was
  // tried: too busy, and its redraws cost frame rate.)
  if (entered) splash_in();
  u.alert.stop();
  u.scene.play(character::find("relax"), now, SPLASH_BIAS);
  vis.on(u.word);
#ifdef USE_WIFI
  bool wifi = esphome::wifi::global_wifi_component && esphome::wifi::global_wifi_component->is_connected();
#else
  bool wifi = true;  // host sim: the network is the Mac's
#endif
  arc_text(u.name_arc, "", NAME);
  arc_text(u.status_arc, !wifi ? "Connecting to Wi-Fi" : u.was_live ? "Bridge offline" : "Looking for the bridge",
           SECONDARY, !u.loading);
}

inline void paint_permission(Vis &vis, const Frame &f) {
  Ui &u = ui();
  const auto &s = u.buddy->snap();
  arc_text(u.name_arc, "Claude", provider_ink("Claude", NAME));  // USAGE and BATTERY name themselves; the face names whoever it shows
  vis.on(u.perm);
  std::string title = "Allow " + (s.prompt_tool.empty() ? std::string("this") : s.prompt_tool) + "?";
  set_text(u.perm_title, title.c_str());
  set_text(u.perm_hint, s.prompt_hint.empty() ? "Needs your answer" : s.prompt_hint.c_str());
  char buf[32] = "";
  if (s.waiting > 1) snprintf(buf, sizeof(buf), "1 of %u waiting", (unsigned) s.waiting);
  set_text(u.perm_count, buf);
  arc_text(u.status_arc, "swipe right for later", RELAX_LINE);
}

inline void paint_usage(Vis &vis, const Frame &f) {
  Ui &u = ui();
  const auto &s = u.buddy->snap();
  bool entered = f.entered;
  vis.on(u.usage);
  if (entered || u.usage_key != s.usages_key) {
    u.usage_key = s.usages_key;
    build_usage();
  }
  arc_text(u.name_arc, "Usage", NAME);
  arc_text(u.status_arc, "Tap to close", RELAX_LINE);
}

inline void paint_battery(Vis &vis, const Frame &f) {
  Ui &u = ui();
  bool usb = f.usb;
  int bat = f.bat;
  vis.on(u.battery_page);
  uint32_t ink = bat >= 0 ? power::battery_ink(bat) : RELAX_LINE;
  char t[12];
  snprintf(t, sizeof(t), bat >= 0 ? "%d%%" : "No battery", bat);
  set_text(u.battery_num, t);
  lv_obj_set_style_text_font(u.battery_num, bat >= 0 ? u.f.f96b : u.f.f30, 0);  // f96b has digits and % only
  lv_obj_set_style_text_color(u.battery_num, lv_color_hex(ink), 0);
  lv_arc_set_value(u.battery_ring, bat >= 0 ? bat : 100);
  lv_obj_set_style_arc_color(u.battery_ring, lv_color_hex(ink), LV_PART_INDICATOR);
  arc_text(u.name_arc, "Battery", NAME);
  if (usb && bat >= 0) arc_text(u.status_arc, "Charging", LIVE, true);  // green working-line shimmer
  else if (usb) arc_text(u.status_arc, "USB power", RELAX_LINE);
  else arc_text(u.status_arc, "Tap to close", RELAX_LINE);
}

inline void paint_battery_side(View v, const Frame &f) {
  Ui &u = ui();
  bool usb = f.usb;
  int bat = f.bat;
  uint32_t now = f.now;
  u.power_source(usb, now);
  bool side = v == View::FACE && bat >= 0 && u.show_battery_side(usb, now);
  show(u.bat_arc, side && !(usb && bat > 80));  // charging above 80%: the bolt alone
  show(u.bat_bolt, side && usb);
  if (side) {
    char t[8];
    snprintf(t, sizeof(t), "%d%%", bat);
    bool changed = u.bat_txt != t || u.bat_shimmer != usb;
    u.bat_txt = t;
    u.bat_shimmer = usb;
    if (usb && changed) {  // green shimmer (painted per tick) + bolt placed after the text
      u.bat_phase = shimmer_phase(now);
      shimmer_paint(u.bat_arc, u.bat_txt, LIVE, u.bat_phase);
      place_bat_bolt();
    } else if (!usb) {
      if (changed) lv_arclabel_set_text(u.bat_arc, t);
      lv_obj_set_style_text_color(u.bat_arc, lv_color_hex(power::battery_ink(bat)), 0);
    }
  } else {
    u.bat_shimmer = false;
  }
}

}  // namespace buddyui
