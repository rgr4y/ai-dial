#pragma once
// The Harness "Focus face", driven by the Claude Hardware Buddy protocol instead of the Harness cable.
// Layout, inks, strings and timings follow docs/HARNESS-UI-SPEC.md (section numbers in comments).
// One screen: name on the top arc, a pet scene or Dismiss + card in the middle, status on the lower arc.
// Views are instant swaps (spec §2 Common: no fades). Everything runs on the ESPHome main loop.
#include "ui_card.h"
#include "ui_voice.h"
#include "ui_pages.h"

namespace buddyui {

// ---- mirror: what is on the glass, as JSON (docs/DIAL-PROTOCOL.md "ui") ----
// Published when it changes, on the buddy_ui text sensor (the bridge reads the view from it) and in the log.
inline void publish_state() {
  Ui &u = ui();
  auto vis = [](lv_obj_t *o) { return o && !lv_obj_is_hidden(o); };
  auto hex = [](uint32_t c) { char b[8]; snprintf(b, sizeof(b), "#%06x", (unsigned) c); return std::string(b); };
  JsonDocument d;
  d["v"] = 1;
  d["view"] = VIEW_NAMES[(int) u.view];
  d["asleep"] = u.asleep;
  d["name"] = u.arc_txt[0];
  JsonObject st = d["status"].to<JsonObject>();
  st["text"] = u.arc_txt[1];
  st["color"] = hex(u.arc_col[1]);
  if (u.shimmer) st["shimmer"] = true;
  if (u.scene.sc) d["scene"] = u.scene.sc->name;
  if (u.alert.sc) d["alert"] = lv_label_get_text(u.alert.count);
  if (vis(u.card)) {
    d["card"] = lv_label_get_text(u.card_text);
  }
  if (vis(u.bell)) d["bell"] = lv_label_get_text(u.bell_text);
  if (vis(u.word)) d["word"] = lv_label_get_text(u.word);
  if (vis(u.sub)) d["sub"] = lv_label_get_text(u.sub);
  if (vis(u.perm)) {
    JsonObject p = d["perm"].to<JsonObject>();
    p["title"] = lv_label_get_text(u.perm_title);
    p["hint"] = lv_label_get_text(u.perm_hint);
    p["count"] = lv_label_get_text(u.perm_count);
  }
  if (vis(u.dots)) d["dots"] = u.dots_txt;
  if (u.voice != Voice::OFF) {  // the bridge types on "send"; sid is where
    d["voice"] = VOICE_NAMES[(int) u.voice];
    if (!u.voice_sid.empty()) d["sid"] = u.voice_sid;
  }
  if (vis(u.usage)) {
    JsonArray cards = d["usage"].to<JsonArray>();
    for (const auto &c : u.buddy->snap().usages) {
      JsonObject o = cards.add<JsonObject>();
      o["n"] = c.name;
      JsonArray ws = o["w"].to<JsonArray>();
      for (const auto &w : c.w) {
        JsonArray r = ws.add<JsonArray>();
        r.add(w.label);
        r.add(w.pct);
        r.add(w.reset);
      }
    }
  }
  if (vis(u.alarm)) d["alarm"] = lv_label_get_text(u.alarm);  // pulse runs on the receiver clock (usage window)
  std::string out;
  serializeJson(d, out);
  if (out == u.last_state) return;
  u.last_state = out;
  ESP_LOGI("buddyui", "state %s", out.c_str());
  if (u.on_state) u.on_state(out);
}

// ---- events ----
inline void on_root(lv_event_t *e) {
  Ui &u = ui();
  lv_event_code_t code = lv_event_get_code(e);
  bool closable = u.view == View::USAGE || u.view == View::BATTERY;
  const char *t = nullptr;
  if (code == LV_EVENT_GESTURE && u.voice != Voice::OFF) return;  // a slide while holding is not a swipe
  if (code == LV_EVENT_GESTURE) {
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());
    bool right = dir == LV_DIR_RIGHT;
    if (right && u.view == View::PERMISSION && u.buddy->has_prompt()) t = "later";  // swipe right = back = "later" (spec §3)
    else if (closable) t = "back";
    else if (u.view == View::FACE && (dir == LV_DIR_LEFT || right)) t = right ? "prev" : "next";  // between sessions
  } else if (code == LV_EVENT_SHORT_CLICKED && closable) {
    t = "back";  // "Tap to close"
  }
  if (t) return tap(t);
  uint32_t now = lv_tick_get();  // PRESSED, or nothing to do: the touch still counts as activity
  activity(now);
  update(now);
}
inline void on_reopen(lv_event_t *) { tap("bell"); }  // the bell pill or alert bubble: open the put-off prompt again
inline void on_decide(lv_event_t *e) { tap(lv_event_get_target_obj(e) == ui().btn_allow ? "allow" : "deny"); }

// BOOT key: "later" on an open prompt, the usage screen from the face, the battery page from usage, back to the face from anything else.
inline void boot(uint32_t now) {
  Ui &u = ui();
  bool was_asleep = u.asleep;
  activity(now);
  if (was_asleep) return update(now);
  if (u.view == View::PERMISSION && u.buddy->has_prompt()) {
    u.deferred_id = u.buddy->snap().prompt_id;
  } else if (u.view == View::FACE) {
    u.usage_open = true;
    u.usage_at = now;
  } else if (u.view == View::USAGE) {
    u.usage_open = false;
    u.battery_open = true;
    u.battery_at = now;
  } else {
    u.usage_open = false;
    u.battery_open = false;
  }
  update(now);
}

// Remote tap (api action buddy_tap): the same effect as touching the glass.
// targets: allow, deny, later, card, bell, next, prev, back, wake, screen, boot
inline void tap(const std::string &t) {
  Ui &u = ui();
  uint32_t now = lv_tick_get();
  bool was_asleep = u.asleep;
  activity(now);
  if (t == "boot") return boot(now);
  if (t == "screen") toggle_screen(now);
  if (was_asleep || t == "wake" || t == "screen") return update(now);  // a wake touch only wakes
  if (t == "card" && u.voice != Voice::OFF) {  // the hold's own release lands as a card click: swallowed; in confirm it cancels
    if (u.voice == Voice::CONFIRM) u.voice = Voice::OFF;
    if (u.voice == Voice::LISTEN && u.voice_ask) end_listen(now);  // a tap ends the follow-up listen
    return update(now);
  }
  if ((t == "allow" || t == "deny") && u.view == View::PERMISSION) {
    if (t == "allow") u.buddy->approve();
    else u.buddy->deny();
    u.sent_at = now;
    u.last_deny = t == "deny";
  } else if (t == "later" && u.view == View::PERMISSION && u.buddy->has_prompt()) {
    u.deferred_id = u.buddy->snap().prompt_id;
  } else if (t == "card" && u.view == View::FACE) {
    if (u.viewing) {
      u.viewing = false;  // a notice's reply: the face goes back to what it showed
    } else {
      u.recap_cleared = u.buddy->last_text_ms();
      pick_hint();
    }
  } else if (t == "bell") {
    // The bell pill (not the alert bubble) opens the oldest notice first: its session takes focus if it still works,
    // and its reply shows on the card. With none queued it reopens the put-off prompt.
    auto &n = u.buddy->notices();
    if (n.empty() || lv_obj_is_hidden(u.bell)) {
      u.deferred_id.clear();
    } else {
      u.notice = n.front();
      n.erase(n.begin());
      u.viewing = true;
      u.notice_n = n.size();
      for (const auto &w : u.buddy->snap().work)
        if (w.id == u.notice.sid) u.focus_id = w.id;
    }
  } else if ((t == "next" || t == "prev") && u.view == View::FACE) {
    // Swipe on the busy face: next / previous working session (start order, wrapping). Also closes a notice's card.
    const auto &w = u.buddy->snap().work;
    if (!w.empty() && (w.size() > 1 || u.viewing)) {
      int i = sessions::focus_index(w, u.focus_id, u.buddy->snap().latest);
      if (w.size() > 1) i = sessions::step(i, (int) w.size(), t == "next" ? 1 : -1);
      u.focus_id = w[i].id;
      u.viewing = false;
    }
  } else if (t == "back") {
    u.usage_open = false;
    u.battery_open = false;
  }
  update(now);
}

// ---- build ----
inline void init(lv_obj_t *root, ClaudeBuddy *buddy, const Fonts &fonts) {
  Ui &u = ui();
  u.root = root;
  u.buddy = buddy;
  u.f = fonts;
  lv_obj_set_style_bg_color(root, lv_color_hex(0x000000), 0);
  lv_obj_set_scrollable(root, false);
  lv_obj_set_clickable(root, true);
  lv_obj_add_event_cb(root, on_root, LV_EVENT_GESTURE, nullptr);
  lv_obj_add_event_cb(root, on_root, LV_EVENT_SHORT_CLICKED, nullptr);
  lv_obj_add_event_cb(root, on_root, LV_EVENT_PRESSED, nullptr);  // any touch counts as activity
  lv_obj_add_event_cb(root, on_hold, LV_EVENT_LONG_PRESSED, nullptr);  // hold = talk (the card's text bubbles to the card)
  if (lv_indev_t *in = lv_indev_get_next(nullptr)) {
    lv_indev_set_long_press_time(in, 650);  // spec §3: hold 650 ms
    lv_indev_add_event_cb(in, [](lv_event_t *) { lv_indev_get_point(lv_indev_active(), &ui().press_pt); },
                          LV_EVENT_PRESSED, nullptr);
  }

  u.scene.create(root);
  u.alert.create(root, true);
  lv_obj_set_style_text_font(u.alert.count, u.f.f26m, 0);
  lv_obj_set_clickable(u.alert.layer[0], true);
  lv_obj_set_ext_click_area(u.alert.layer[0], 14);  // bubble tap margin (ui_habitat.c:1206)
  lv_obj_add_event_cb(u.alert.layer[0], on_reopen, LV_EVENT_CLICKED, nullptr);

  u.dismiss = mk_button(root, u.f.f25, "Dismiss");
  lv_obj_add_event_cb(u.dismiss, on_dismiss, LV_EVENT_CLICKED, nullptr);
  u.card = mk_card(root);
  lv_obj_set_clickable(u.card, true);
  lv_obj_add_event_cb(u.card, on_card, LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(u.card, on_hold, LV_EVENT_LONG_PRESSED, nullptr);  // hold on a reply talks to its session
  u.card_text = mk_label(u.card, u.f.f30, RECAP);
  lv_obj_set_width(u.card_text, 348);  // card 368 - 2 x 10 pad
  lv_obj_set_style_text_line_space(u.card_text, 43 - lv_font_get_line_height(u.f.f30), 0);  // 43 px pitch
  lv_label_set_long_mode(u.card_text, LV_LABEL_LONG_MODE_WRAP);  // fixed height past 4 lines: clips and scrolls
  lv_obj_set_scrollbar_mode(u.card_text, LV_SCROLLBAR_MODE_OFF);
  // Drag target: it must be the pressed object for the indev to scroll it. EVENT_BUBBLE passes CLICKED up to u.card.
  lv_obj_set_clickable(u.card_text, true);
  lv_obj_set_scrollable(u.card_text, true);
  lv_obj_set_event_bubble(u.card_text, true);
  lv_obj_set_scroll_elastic(u.card_text, false);  // clamp at both ends (lv_indev_scroll.c elastic_diff)
  lv_obj_set_scroll_dir(u.card_text, LV_DIR_VER);
  // Vertical only: text 348 > card content 344 (border counts), so a sideways drag chained up and slid the card.
  lv_obj_set_scroll_chain_hor(u.card_text, false);
  lv_obj_set_scrollable(u.card, false);
  lv_obj_add_event_cb(u.card_text, on_card_text, LV_EVENT_PRESSED, nullptr);
  lv_obj_add_event_cb(u.card_text, on_card_text, LV_EVENT_SCROLL, nullptr);
  lv_obj_add_event_cb(u.card_text, on_card_text, LV_EVENT_RELEASED, nullptr);
  lv_obj_center(u.card_text);

  // Bell pill (spec §2.6 colour, 0x006fff, fully rounded): 54 px tall in Inter 25 (spec: 32 / 20, too small a tap),
  // centred at y 388 so its corners stay inside the glass.
  u.bell = mk_fill(root, LV_SIZE_CONTENT, 54, LV_RADIUS_CIRCLE, BLUE);
  lv_obj_set_style_bg_color(u.bell, lv_color_darken(lv_color_hex(BLUE), LV_OPA_30), LV_STATE_PRESSED);
  lv_obj_set_style_pad_hor(u.bell, 34, 0);
  lv_obj_align(u.bell, LV_ALIGN_TOP_MID, 0, 388 - 27);
  lv_obj_set_clickable(u.bell, true);
  lv_obj_set_ext_click_area(u.bell, 20);
  lv_obj_add_event_cb(u.bell, on_reopen, LV_EVENT_CLICKED, nullptr);
  u.bell_text = mk_label(u.bell, u.f.f25, NAME);
  lv_obj_center(u.bell_text);

  // Brand / pairing (spec §5): wordmark "Claude" in Inter Bold 48 on a 50 px line at y 208.
  u.word = mk_label(root, u.f.f48b, TEXT);
  set_text(u.word, "Otto");
  lv_obj_align(u.word, LV_ALIGN_TOP_MID, 0, 262);
  u.sub = mk_label(root, u.f.f25, SECONDARY);
  lv_obj_set_width(u.sub, 340);
  lv_label_set_long_mode(u.sub, LV_LABEL_LONG_MODE_WRAP);
  lv_obj_align(u.sub, LV_ALIGN_TOP_MID, 0, 270);

  // Usage alarm (new): big red percentage, scaled from its centre in tick().
  for (auto &o : u.burst) {
    o = mk_fill(root, 14, 14, LV_RADIUS_CIRCLE, LIVE);  // burst_step sets size and bg_opa on every frame
    lv_obj_set_clickable(o, false);
    show(o, false);
  }
  u.alarm = mk_label(root, u.f.f96b, USAGE_HOT);
  lv_obj_center(u.alarm);
  lv_obj_set_style_transform_pivot_x(u.alarm, LV_PCT(50), 0);
  lv_obj_set_style_transform_pivot_y(u.alarm, LV_PCT(50), 0);
  show(u.alarm, false);

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

  // Page dots above the status line: DOTS_MAX small circles, placed and lit per update().
  u.dots = lv_obj_create(root);
  lv_obj_remove_style_all(u.dots);
  lv_obj_set_size(u.dots, Ui::DOTS_MAX * Ui::DOT_PITCH, Ui::DOT_D);
  lv_obj_set_clickable(u.dots, false);
  lv_obj_set_hidden(u.dots, true);
  for (int i = 0; i < Ui::DOTS_MAX; i++) {
    lv_obj_t *d = u.dot[i] = mk_fill(u.dots, Ui::DOT_D, Ui::DOT_D, LV_RADIUS_CIRCLE, 0x4a4c56);  // update() recolours
    lv_obj_set_clickable(d, false);
  }

  // Usage screen (BOOT): children made by build_usage().
  u.usage = mk_page(root, true, true);

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

  // Arcs last so they draw on top.
  u.name_arc = mk_arc(root, u.f.f26m, true);
  u.status_arc = mk_arc(root, u.f.f26m, false);
  lv_arclabel_set_recolor(u.status_arc, true);  // per-letter colour for the shimmer
  // Left edge, 150..210 deg clockwise: reads bottom to top, centred on 9 o'clock.
  u.bat_arc = mk_arc(root, u.f.f26m, true);
  lv_arclabel_set_angle_start(u.bat_arc, 150);
  lv_arclabel_set_angle_size(u.bat_arc, 60);
  arc_band(u.bat_arc, 0, 100, 100, 266);  // r 205 at 150..210 deg: x ~12-72, y ~114-352
  lv_arclabel_set_recolor(u.bat_arc, true);  // per-letter colour for the charging shimmer
  lv_obj_set_hidden(u.bat_arc, true);
  // Charging bolt after the side %: a stroked zigzag (Inter has no bolt glyph), turned to follow the arc text.
  u.bat_bolt = lv_line_create(root);
  lv_obj_set_style_line_width(u.bat_bolt, 4, 0);
  lv_obj_set_style_line_rounded(u.bat_bolt, true, 0);
  lv_obj_set_style_line_color(u.bat_bolt, lv_color_hex(LIVE), 0);
  lv_obj_set_clickable(u.bat_bolt, false);
  lv_obj_set_hidden(u.bat_bolt, true);
  // Shield last = topmost. Hidden while awake; while dark it takes the touch and only wakes the screen
  // (touch_habitat.c:133-138 swallows the wake contact the same way).
  u.shield = mk_page(root, false, true);
  lv_obj_set_clickable(u.shield, true);
  lv_obj_add_event_cb(u.shield, [](lv_event_t *) { activity(lv_tick_get()); }, LV_EVENT_PRESSED, nullptr);

  pick_hint();
  u.last_activity = lv_tick_get();
  // boot: no wordmark fade. The pack load blocks LVGL right after the first frame, and the fade would start at opacity 0.
  update(u.last_activity);
}

// ---- state -> screen ----
inline void update(uint32_t now) {
  Ui &u = ui();
  if (!u.root) return;
  ClaudeBuddy *b = u.buddy;
  const auto &s = b->snap();
  bool live = b->live();
  character::want(s.assets_url, s.assets_rev);

  // Work edge: new stopwatch, new cook/build pick, and the old recap goes (spec §2.4: cleared on turn start).
  bool running = live && s.running > 0;
  if (running && !u.was_running) {
    u.work_start = now;
    u.build = rand() & 1;
    u.recap_cleared = b->last_text_ms();
    u.focus_id.clear();  // a fresh run starts on the most recently active session
    activity(now);
  } else if (!running && u.was_running) {
    activity(now);  // done: wake to show the recap
  }
  u.was_running = running;
  // Focus is a session id: it stays put while that session works, and falls back to the most recently active when it ends.
  int fi = running ? sessions::focus_index(s.work, u.focus_id, s.latest) : -1;
  if (fi >= 0) u.focus_id = s.work[fi].id;
  const esphome::claude_buddy::Work *fw = fi >= 0 ? &s.work[fi] : nullptr;
  auto &notices = b->notices();
  if (notices.size() > u.notice_n) activity(now);  // a reply from another session wakes the dial
  u.notice_n = notices.size();
  // A read reply card ages out: it becomes a bell notice ("1 new") and the face goes idle. Not a notice opened from the
  // bell, not mid-voice. Untouched since the card appeared: one autoscroll pass; touched: REPLY_TO_BELL_MS of quiet.
  // notice_n follows the push so it doesn't wake the dial.
  uint32_t read_ms = u.last_activity > u.card_shown_at ? REPLY_TO_BELL_MS : u.card_pass_ms;
  if (!running && u.view == View::FACE && !u.viewing && u.voice == Voice::OFF && !b->last_text().empty() &&
      b->last_text_ms() > u.recap_cleared && u.card_pass_ms && now - u.last_activity >= read_ms) {
    b->queue_last_reply();
    u.recap_cleared = b->last_text_ms();
    u.notice_n = notices.size();
  }
  if (u.deferred_id != s.prompt_id && !s.prompt_id.empty()) u.deferred_id.clear();
  voice_step(now);
  if (u.usage_open && now - u.usage_at > USAGE_SCREEN_MS) u.usage_open = false;
  if (u.battery_open && now - u.battery_at > USAGE_SCREEN_MS) u.battery_open = false;
  if (u.ota_on && u.ota_fail_at && now - u.ota_fail_at > OTA_FAIL_MS) u.ota_on = false, u.ota_fail_at = 0;

  View v = u.ota_on                                     ? View::OTA
           : !live                                      ? View::BRAND
           : b->has_prompt() && u.deferred_id.empty()   ? View::PERMISSION
           : u.usage_open                               ? View::USAGE
           : u.battery_open                             ? View::BATTERY
                                                        : View::FACE;
  if (v != View::FACE && u.voice != Voice::OFF && u.voice != Voice::SEND) {  // words nobody can see are never sent
    voice_stop_mic();
    u.voice = Voice::OFF;
  }
  if (v == View::PERMISSION && u.view != View::PERMISSION) activity(now);  // a new question wakes the dial
  if (v == View::FACE && u.view != View::FACE) pick_hint();
  bool entered = v != u.view;
  if (entered) {
    ESP_LOGI("buddyui", "view %s (running %u, waiting %u)", VIEW_NAMES[(int) v], (unsigned) s.running, (unsigned) s.waiting);
  }
  if (entered && u.view == View::BRAND) burst();  // the bridge just connected
  u.view = v;
  if (v != View::BRAND) u.was_live = true;
  bool usb = u.on_usb && u.on_usb();  // one PMU read each per update(): the face-side % and the battery page share them
  int bat = u.battery ? u.battery() : -1;
  {  // face-side battery %: face only, always on battery, 60 s after plug-in / wake on USB
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

  // Everything off, then switch on what this view uses.
  Vis vis{u};
  bool sending = now - u.sent_at < SEND_MS && u.sent_at;
  [[maybe_unused]] Frame f{now, live, running, entered, usb, sending, fi, bat, fw};  // the view painters will read this
  const char *name = "Claude";

  // The pages stop the pet and the alert. Not hoisted into the face path: stop() clears sc, and a play() after it restarts.
  if (v == View::OTA || v == View::PERMISSION || v == View::USAGE || v == View::BATTERY) {
    u.scene.stop();
    u.alert.stop();
  }

  if (v == View::OTA) {  // ota() owns the widgets and arcs; nothing else may change the page
    vis.on(u.ota_page);
    return;
  }

  if (v == View::BRAND) {
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
    return;
  }

  if (v == View::PERMISSION) {
    arc_text(u.name_arc, name, provider_ink(name, NAME));  // USAGE and BATTERY name themselves; the face names whoever it shows
    vis.on(u.perm);
    std::string title = "Allow " + (s.prompt_tool.empty() ? std::string("this") : s.prompt_tool) + "?";
    set_text(u.perm_title, title.c_str());
    set_text(u.perm_hint, s.prompt_hint.empty() ? "Needs your answer" : s.prompt_hint.c_str());
    char buf[32] = "";
    if (s.waiting > 1) snprintf(buf, sizeof(buf), "1 of %u waiting", (unsigned) s.waiting);
    set_text(u.perm_count, buf);
    arc_text(u.status_arc, "swipe right for later", RELAX_LINE);
    return;
  }

  if (v == View::USAGE) {
    vis.on(u.usage);
    if (entered || u.usage_key != s.usages_key) {
      u.usage_key = s.usages_key;
      build_usage();
    }
    arc_text(u.name_arc, "Usage", NAME);
    arc_text(u.status_arc, "Tap to close", RELAX_LINE);
    return;
  }

  if (v == View::BATTERY) {
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
    return;
  }

  // ---- FACE (spec §2.9) ----
  const esphome::claude_buddy::Bot *shown = nullptr;  // a Grok bot on the face instead of Clawd: its name tops the face
  bool recap = !running && !b->last_text().empty() && b->last_text_ms() > u.recap_cleared;
  // The reply card names whoever wrote it: a Grok bot (its name, in its colour), Codex, Copilot, else Claude.
  auto who = [&](const esphome::claude_buddy::Bot &bot, const std::string &agent) {
    if (!bot.shape.empty()) shown = &bot;
    else if (const char *n = agent_name(agent)) name = n;
  };
  bool waiting = s.waiting > 0;  // only reachable here once the prompt was put off
  bool bell_ok = false;          // the bell pill may show (not over a card)
  std::string title;             // the focused session's name, when a working session is on the face
  uint32_t title_rgb = NAME;
  // Finger still on the reply (scrolled within CARD_MANUAL_MS): keep it up through a new turn; tick() hands back.
  bool reading = u.card_manual && (now - u.card_manual_at < CARD_MANUAL_MS ||
                                   lv_indev_get_state(lv_indev_get_next(nullptr)) == LV_INDEV_STATE_PRESSED);
  if (sending) {
    u.alert.stop();
    const scene::Scene *deny = u.last_deny ? character::find("deny_send") : nullptr;
    // Once, timed from the tap: the last step holds (letter posted) instead of looping back to the letter in hand, and
    // a permission redraw in between resumes the scene rather than restarting it. Voice face bias 0 (focus.c:670).
    u.scene.play(deny ? deny : character::find("send"), u.sent_at, 0, nullptr, true);
    arc_text(u.status_arc, "Sent", RELAX_LINE);
  } else if (u.voice != Voice::OFF) {
    // Words on the reply card while holding, the final text for a moment (tap to cancel), or a short note.
    u.scene.stop();
    u.alert.stop();
    vis.on(u.card);
    bool live_words = u.voice == Voice::LISTEN || u.voice == Voice::WAIT;
    show_card(live_words && u.voice_text.empty() ? "\xE2\x80\xA6" : u.voice_text, now);
    if (u.voice == Voice::LISTEN) arc_text(u.status_arc, "Listening\xE2\x80\xA6", LIVE, true);
    else if (u.voice == Voice::WAIT) arc_text(u.status_arc, "Transcribing\xE2\x80\xA6", RELAX_LINE);
    else if (u.voice == Voice::CONFIRM) arc_text(u.status_arc, "Tap to cancel", RELAX_LINE);
    else arc_text(u.status_arc, "", RELAX_LINE);
    for (size_t i = 0; i < s.work.size(); i++)
      if (s.work[i].id == u.voice_sid) title = sessions::label(s.work, (int) i);
  } else if (reading) {
    u.scene.stop();
    u.alert.stop();
    vis.on(u.card);
    vis.on(u.dismiss);
    who(b->last_bot(), b->last_agent());
  } else if (u.viewing || recap) {
    // A notice opened from the bell (until Dismiss), or the finished turn's reply: that reply on the card.
    const auto &bot = u.viewing ? u.notice.bot : b->last_bot();
    const auto &agent = u.viewing ? u.notice.agent : b->last_agent();
    const auto &text = u.viewing ? u.notice.text : b->last_text();
    u.scene.stop();
    u.alert.stop();
    vis.on(u.card);
    vis.on(u.dismiss);
    who(bot, agent);
    show_card(text, now);
    arc_text(u.status_arc, "", RELAX_LINE);
  } else if (running) {
    bell_ok = true;
    // The focused session's look (heartbeat `work`; an older bridge sends only the flat fields for the latest one): a
    // Grok Bot session shows its own bot (`avatar`), Codex and Copilot their pets (`agent`), else Clawd cooks or builds.
    const auto &av = fw ? fw->avatar : s.avatar;
    const std::string &ag = fw ? fw->agent : s.agent;
    if (fw) {
      title = sessions::label(s.work, fi);
      title_rgb = !av.shape.empty() ? av.rgb : provider_ink(ag.empty() ? "claude" : ag);
    }
    const scene::Scene *sc = av.shape.empty() ? nullptr : character::find(("grok_" + av.shape).c_str());
    const char *agent = agent_name(ag);
    const scene::Scene *cx = agent ? character::find((ag + "_work").c_str()) : nullptr;
    if (sc) {
      u.scene.play(sc, now, 4);
      u.scene.set_tint(av.rgb);
      if (!fw) shown = &s.avatar;
    } else if (cx) {
      u.scene.play(sc = cx, now, 4);
      name = agent;
    } else {
      sc = u.build ? character::find("build") : character::find("work");
      u.scene.play(sc, now, 4);
    }
    if (waiting) {
      // Alert bubble on the working face, count = waiting prompts (spec §4: semantic shift from "other agents").
      u.alert.play(character::find("alert"), now, 4, sc, true);
      char n[4];
      if (s.waiting > 9) snprintf(n, sizeof(n), "9+");
      else snprintf(n, sizeof(n), "%u", (unsigned) s.waiting);
      lv_obj_set_style_text_font(u.alert.count, s.waiting > 9 ? u.f.f20 : u.f.f26m, 0);  // focus.c:968
      set_text(u.alert.count, n);
    } else {
      u.alert.stop();
    }
    // Live line "<verb>… m:ss" in voice green (focus.c:764-791). Verb: the focused session's tool (the heartbeat's msg
    // when there is no `work`), else a gerund.
    char el[16], line[64];
    fmt_elapsed(el, sizeof(el), now - u.work_start);
    const std::string &said = fw ? fw->tool : s.msg;
    std::string verb = said.empty() ? GERUNDS[(now - u.work_start) / GERUND_MS % (sizeof(GERUNDS) / sizeof(GERUNDS[0]))]
                                    : cut(one_line(said), 18);
    snprintf(line, sizeof(line), "%s\xE2\x80\xA6 %s", verb.c_str(), el);
    arc_text(u.status_arc, line, LIVE, true);
    // Page dots when several sessions work. Above the bell pill when that is up, else just above the status line.
    if (s.work.size() > 1) {
      int n = std::min<int>((int) s.work.size(), Ui::DOTS_MAX), lit = std::min(fi, n - 1);
      char key[16];
      snprintf(key, sizeof(key), "%d/%d", lit + 1, (int) s.work.size());
      bool bell = !notices.empty();
      lv_obj_align(u.dots, LV_ALIGN_TOP_MID, 0, bell ? 336 : 370);
      if (u.dots_txt != key) {
        u.dots_txt = key;
        for (int i = 0; i < Ui::DOTS_MAX; i++) {
          show(u.dot[i], i < n);
          lv_obj_set_pos(u.dot[i], (Ui::DOTS_MAX - n) * Ui::DOT_PITCH / 2 + i * Ui::DOT_PITCH, 0);
          lv_obj_set_style_bg_color(u.dot[i], lv_color_hex(i == lit ? 0xffffff : 0x4a4c56), 0);
        }
      }
      vis.on(u.dots);
    }
  } else {
    u.alert.stop();
    // Idle: providers take turns, switching as each usage flash starts: Clawd juggles, Codex's pet and Copilot's head
    // relax, Grok shows its next bot (one bot per Grok turn, so a long roster can't crowd the others out).
    static const char *const PETS[][2] = {{"codex_relax", "Codex"}, {"copilot_relax", "Copilot"}};
    const scene::Scene *pets[3] = {character::find("relax")};
    const char *names[3] = {nullptr};
    uint32_t n = 1;
    for (const auto &pet : PETS)  // a pet's _xl (Copilot's head, scripts/anim/copilot.mjs) fills the face
      if ((pets[n] = character::find((std::string(pet[0]) + "_xl").c_str())) || (pets[n] = character::find(pet[0])))
        names[n++] = pet[1];
    bool grok = live && !s.bots.empty();
    // The cycle steps at each usage-flash boundary the idle face is up for, and holds while anything else shows (busy,
    // reply, prompt, dark), so coming back to idle resumes it instead of landing wherever the clock says.
    uint32_t k = now / USAGE_EVERY_MS;
    if (k != u.idle_k && now - u.idle_seen < 2500) u.idle_turn++;  // tick() updates at least once a second
    u.idle_k = k, u.idle_seen = now;
    uint32_t turn = u.idle_turn, providers = n + grok, p = turn % providers;
    if (grok && b->mute_api) p = n;  // sim pin (scripts/sim-panel.mjs): a pinned roster shows only its bots
    const esphome::claude_buddy::Bot *g = grok && p == n ? &s.bots[turn / providers % s.bots.size()] : nullptr;
    const scene::Scene *bot = g ? character::find(("grok_" + g->shape).c_str()) : nullptr;
    if (bot) {
      u.scene.play(bot, now, 4);
      u.scene.set_tint(g->rgb);
      shown = g;
    } else {
      if (p >= n) p = 0;  // a bot whose shape isn't in the pack: Clawd covers its turn
      u.scene.play(pets[p], now, 4);
      if (names[p]) name = names[p];
    }
    arc_text(u.status_arc, waiting || !notices.empty() ? "" : u.hint, RELAX_LINE);  // hint left out while the bell shows
    bell_ok = true;
  }
  if (!title.empty()) arc_text(u.name_arc, title.c_str(), title_rgb);
  else if (shown && !shown->name.empty()) arc_text(u.name_arc, shown->name.c_str(), shown->rgb);
  else arc_text(u.name_arc, name, provider_ink(name, NAME));
  // Plan usage: 3 s of every 15 s on the lower arc, over whatever the face shows there (Harness's display carries
  // "what your harness has spent today"; this is the plan's 5-hour / weekly use from the bridge).
  if (!s.usage.empty() && !sending && u.voice == Voice::OFF && now % USAGE_EVERY_MS < USAGE_SHOW_MS) {
    arc_text(u.status_arc, s.usage.c_str(), usage_ink(s.usage_pct, provider_ink(s.usage, RELAX_LINE)));
    // Nearly out: the pet steps aside for a pulsing red percentage (no scared/alarmed pet art exists in Harness).
    if (s.usage_pct >= USAGE_ALARM_PCT && !recap) {
      u.scene.stop();
      u.alert.stop();
      char pct[8];
      snprintf(pct, sizeof(pct), "%u%%", (unsigned) s.usage_pct);
      set_text(u.alarm, pct);
      vis.on(u.alarm);
    }
  }
  // Bell pill: put-off prompts when nothing is running (the alert bubble covers them otherwise) and replies from other
  // sessions. Tapping it opens the oldest reply (tap("bell")).
  unsigned held = waiting && !running ? s.waiting : 0, news = (unsigned) notices.size();
  if (bell_ok && (held || news)) {
    char n[32];  // ponytail: no bell glyph (Harness uses FontAwesome)
    if (held && news) snprintf(n, sizeof(n), "%u waiting \xC2\xB7 %u new", held, news);
    else if (held) snprintf(n, sizeof(n), "%u waiting", held);
    else snprintf(n, sizeof(n), "%u new", news);
    set_text(u.bell_text, n);
    vis.on(u.bell);
  }
}

// 50 ms tick (the Harness code-drawn motion tick): advance pets, rerun state for the stopwatch and timeouts.
inline void tick(uint32_t now) {
  Ui &u = ui();
  if (!u.root) return;
  // Dark: LVGL is paused (YAML), so skip the per-frame animation work; state still updates once a second.
  if (!u.asleep) {
    u.scene.tick(now);
    u.alert.tick(now);
  }
  // Repaint a shimmering arc only when its sweep phase has moved; neither runs while asleep.
  auto shimmer = [now](bool on, uint8_t &ph, lv_obj_t *a, const std::string &t, uint32_t c) {
    if (!on) return;
    uint8_t p = shimmer_phase(now);
    if (p == ph) return;
    ph = p;
    shimmer_paint(a, t, c, p);
  };
  if (!u.asleep) {
    shimmer(u.bat_shimmer, u.bat_phase, u.bat_arc, u.bat_txt, LIVE);  // charging: side % shimmers green, same sweep as the working line
    shimmer(u.shimmer, u.shimmer_phase, u.status_arc, u.arc_txt[1], u.arc_col[1]);
  }
  if (!u.asleep && !lv_obj_is_hidden(u.alarm)) {
    // Bounce in, pulse twice a second, bounce out, across the 3 s usage flash. 256 = 1x.
    uint32_t t = now % USAGE_EVERY_MS;
    float env = std::min(1.0f, std::min(t / 250.0f, (USAGE_SHOW_MS - (float) std::min(t, USAGE_SHOW_MS)) / 250.0f));
    float k = env * (1.0f + 0.15f * sinf(t * 2 * 3.14159f / 500));
    lv_obj_set_style_transform_scale(u.alarm, (int32_t) (256 * k), 0);
  }
  if (u.card_manual && !u.asleep && !lv_obj_is_hidden(u.card) && now - u.card_manual_at >= CARD_MANUAL_MS &&
      lv_indev_get_state(lv_indev_get_next(nullptr)) != LV_INDEV_STATE_PRESSED) {
    resume_card_anim();
    update(now);  // a turn that started while reading takes the face now
  }
  if (u.voice == Voice::LISTEN &&
      (u.voice_ask ? now - u.voice_at > VOICE_ASK_MS
                   : lv_indev_get_state(lv_indev_get_next(nullptr)) != LV_INDEV_STATE_PRESSED || now - u.voice_at > VOICE_MAX_MS)) {
    end_listen(now);
    update(now);
  }
  if (u.voice == Voice::WAIT && now >= u.voice_stop_at) voice_stop_mic();
  static uint8_t in_usage = 0;  // start/end the usage flash on time, not on the next 1 s update
  uint8_t w = (uint8_t) std::min<uint32_t>(now % USAGE_EVERY_MS / USAGE_SHOW_MS, 2);
  if (w != in_usage) {
    in_usage = w;
    update(now);
  }
  static uint32_t last_state = 0;
  if (now - last_state >= 1000) {  // stopwatch seconds, usage timeouts, send scene end
    last_state = now;
    update(now);
  }
  {  // busy holds the USB screen lit: a session running, an open prompt, voice in use, or a reply card still up
    ClaudeBuddy *b = u.buddy;
    bool busy = b->snap().running > 0 || b->has_prompt() || u.voice != Voice::OFF || u.viewing ||
                (!b->last_text().empty() && b->last_text_ms() > u.recap_cleared);
    if (u.idle_due(now, BOOT_AWAKE_MS, SLEEP_MS, USB_SLEEP_MS, u.on_usb && u.on_usb(), busy)) set_asleep(true);
  }
}

}  // namespace buddyui
