#pragma once
// The Harness "Focus face", driven by the Claude Hardware Buddy protocol instead of the Harness cable.
// Layout, inks, strings and timings follow docs/HARNESS-UI-SPEC.md (section numbers in comments).
// One screen: name on the top arc, a pet scene or Dismiss + card in the middle, status on the lower arc.
// Views are instant swaps (spec §2 Common: no fades). Everything runs on the ESPHome main loop.
#include "ui_card.h"
#include "ui_voice.h"
#include "ui_pages.h"
#include "ui_face.h"

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

  build_card(root);

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

  build_brand(root);

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

  build_permission(root);

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

  build_battery_page(root);

  build_ota_page(root);

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
  bool sending = now - u.sent_at < SEND_MS && u.sent_at;
  Frame f{now, live, running, entered, usb, sending, fi, bat, fw};  // what the view painters read
  paint_battery_side(v, f);  // face-side battery %: face only, always on battery, 60 s after plug-in / wake on USB

  // Everything off, then switch on what this view uses.
  Vis vis{u};

  // The pages stop the pet and the alert. Not hoisted into the face path: stop() clears sc, and a play() after it restarts.
  if (v == View::OTA || v == View::PERMISSION || v == View::USAGE || v == View::BATTERY) {
    u.scene.stop();
    u.alert.stop();
  }

  if (v == View::OTA) {  // ota() owns the widgets and arcs; nothing else may change the page
    vis.on(u.ota_page);
    return;
  }

  if (v == View::BRAND) return paint_brand(vis, f);

  if (v == View::PERMISSION) return paint_permission(vis, f);

  if (v == View::USAGE) return paint_usage(vis, f);

  if (v == View::BATTERY) return paint_battery(vis, f);

  paint_face(vis, f);
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
    float k = env * (1.0f + 0.15f * sinf(t * 2 * PI_F / 500));
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
