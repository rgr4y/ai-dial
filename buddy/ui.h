#pragma once
// The Harness "Focus face", driven by the Claude Hardware Buddy protocol instead of the Harness cable.
// Layout, inks, strings and timings follow docs/HARNESS-UI-SPEC.md (section numbers in comments).
// One screen: name on the top arc, a pet scene or the small pet + card in the middle, status on the lower arc.
// Views are instant swaps (spec §2 Common: no fades). Everything runs on the ESPHome main loop.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <strings.h>
#include <utility>

#include "character.h"
#include "power.h"
#include "scene.h"
#include "esphome/core/defines.h"
#ifdef USE_WIFI
#include "esphome/components/wifi/wifi_component.h"
#endif

namespace buddyui {

using esphome::claude_buddy::ClaudeBuddy;

// ---- theme (spec §2 Common) ----
static constexpr uint32_t TEXT = 0xefe7de, SECONDARY = 0xada6ad, ACCENT = 0xc6aaef, SELECTION = 0x392c4a,
                          ERROR = 0xe7a6ad, NAME = 0xeaeaf0, RECAP = 0xd6d6d2, CARD = 0x1c1e26, CARD_RIM = 0x3d3f47,
                          RELAX_LINE = 0x9a9aa6, LIVE = 0x00ff2f, BLUE = 0x006fff;

// AI provider colours, picked to read on black. Brand-derived: Claude terracotta, Copilot the light end of the
// Microsoft 365 blue, Codex OpenAI green. Grok's brand is black and white, so a cool silver that still differs
// from TEXT. Unknown names get TEXT.
// Brand colour for text that starts with a provider name ("Claude", "Grok 21% · 3d"); `other` if none matches.
inline uint32_t provider_ink(const std::string &name, uint32_t other = TEXT) {
  static const struct { const char *name; uint32_t ink; } INKS[] = {
      {"claude", 0xd97757}, {"copilot", 0x4ba6f5}, {"codex", 0x10a37f}, {"grok", 0xb4bfd4}};
  for (const auto &p : INKS)
    if (strncasecmp(name.c_str(), p.name, strlen(p.name)) == 0) return p.ink;
  return other;
}

// ---- timings ----
static constexpr uint32_t SLEEP_MS = 300000;    // screen auto-sleep (display_habitat.c:405)
static constexpr uint32_t BOOT_AWAKE_MS = 60000; // never idle-sleep in the first minute after boot
static constexpr uint32_t ENTRIES_MS = 15000;   // the entries view drops back to the face
static constexpr uint32_t OTA_FAIL_MS = 5000;  // failed-update page stays this long
static constexpr int OTA_BAR_W = 300, OTA_BAR_MIN = 14;
static constexpr uint32_t USAGE_SCREEN_MS = 30000;  // the usage screen (BOOT) drops back to the face
static constexpr uint32_t SEND_MS = 2400;       // claude_send (posting a letter) after answering, one loop
static constexpr uint32_t GERUND_MS = 6000;     // focus.c:760
static constexpr uint32_t USAGE_EVERY_MS = 15000, USAGE_SHOW_MS = 3000;  // plan usage flashes on the lower arc
static constexpr uint32_t USAGE_WARN = 0xe5e510, USAGE_HOT = 0xcd3131;   // theme question / failed inks
// From 80% the brand colour gives way to warning yellow, from 95% to red.
inline uint32_t usage_ink(uint32_t pct, uint32_t base) { return pct >= 95 ? USAGE_HOT : pct >= 80 ? USAGE_WARN : base; }
static constexpr int SPLASH_BIAS = -50;      // splash: pet sits above the wordmark
static constexpr uint32_t USAGE_ALARM_PCT = 80;  // at or above: the flash also pulses a big red % mid-screen
static constexpr int RECAP_MAX = 2000;          // codepoints; focus.c:793 cut at 90, the card now scrolls instead
// Recap card past 4 lines: hold at the top, scroll to the end, hold, scroll back up quicker, repeat.
static constexpr uint32_t CARD_HOLD_MS = 2500, CARD_SCROLL_PX_S = 30, CARD_BACK_PX_S = 120;
// Finger-scrolled card: autoscroll resumes this long after the last touch or scroll movement.
static constexpr uint32_t CARD_MANUAL_MS = 5000;

struct Fonts { const lv_font_t *f20, *f25, *f26m, *f30, *f48b, *f96b; };

enum class View : uint8_t { BRAND, FACE, PERMISSION, ENTRIES, USAGE, BATTERY, OTA };
static const char *const VIEW_NAMES[] = {"brand", "face", "permission", "entries", "usage", "battery", "ota"};

struct Ui : power::Screen {  // asleep, last_activity, wake/sleep requests: power.h
  Fonts f{};
  lv_obj_t *root = nullptr;
  lv_obj_t *name_arc = nullptr, *status_arc = nullptr;
  scene::Player scene, alert;
  bool last_deny = false;                   // the last answer was a deny: play deny_send (else send)
  int card_lines = 0;  // recap card height in lines (1-4), set when its text changes
  bool card_overflow = false, card_manual = false;  // text past 4 lines; finger has taken over the scroll
  uint32_t card_manual_at = 0;                      // last touch / scroll movement while manual
  lv_obj_t *small = nullptr, *card = nullptr, *card_text = nullptr, *bell = nullptr, *bell_text = nullptr;
  lv_obj_t *word = nullptr, *sub = nullptr;                                  // brand (bridge offline)
  lv_obj_t *perm = nullptr, *perm_title = nullptr, *perm_hint = nullptr, *perm_count = nullptr;
  lv_obj_t *btn_deny = nullptr, *btn_allow = nullptr;
  lv_obj_t *list = nullptr, *list_text = nullptr;
  lv_obj_t *usage = nullptr;  // usage screen: rebuilt from the snapshot's cards (build_usage)
  static constexpr int BURST_N = 18;
  lv_obj_t *burst[BURST_N] = {};     // bridge connected: green sparks fly out from the centre
  int16_t burst_r[BURST_N] = {};
  lv_obj_t *alarm = nullptr;  // usage >= USAGE_ALARM_PCT: big red "87%" pulsing in place of the pet

  ClaudeBuddy *buddy = nullptr;
  View view = View::BRAND;
  bool entries_open = false;
  uint32_t entries_at = 0;
  bool usage_open = false;
  uint32_t usage_at = 0;
  bool loading = false;  // boot pack load in progress: LVGL is blocked, so no shimmer (it would freeze mid-sweep)
  bool battery_open = false;
  uint32_t battery_at = 0;
  bool ota_on = false;       // OTA page up (from ota(), until 5 s after a failure)
  uint32_t ota_fail_at = 0;  // when the update failed; 0 = running or rebooting
  lv_obj_t *ota_page = nullptr, *ota_fill = nullptr, *ota_pct = nullptr;
  lv_obj_t *battery_page = nullptr, *battery_ring = nullptr, *battery_num = nullptr;  // battery page (BOOT after usage)
  lv_obj_t *bat_arc = nullptr, *bat_bolt = nullptr;  // face-side battery %, left edge; charging bolt after it
  std::string bat_txt;
  bool bat_shimmer = false;  // on USB: the side % shimmers green like the working line
  uint8_t bat_phase = 0;
  lv_point_precise_t bat_bolt_pts[4] = {};
  std::string usage_key;  // usages_key the usage screen was built from
  std::string deferred_id;            // prompt the user put off with "later": face + alert until it changes
  bool was_running = false;
  bool build = false;                 // cook or build, picked per 0 -> >0 edge (focus.c:219-231)
  uint32_t work_start = 0;            // local stopwatch: the protocol carries no start time
  uint32_t recap_cleared = 0;         // turn text at or before this is not shown
  uint32_t sent_at = 0;               // claude_send plays until sent_at + SEND_MS
  const char *hint = "What's next?";
  lv_obj_t *shield = nullptr;          // while dark: eats touches so a wake tap can't hit [ allow ]
  bool small_was = false;
  std::function<int()> battery;       // YAML: battery % when a cell is connected (also while charging), -1 otherwise
  std::function<bool()> on_usb;       // YAML: true when VBUS is present; unset = battery rules
  bool was_live = false;               // the bridge has talked since boot (splash says "offline", not "looking")
  std::string arc_txt[2];
  uint32_t arc_col[2] = {0, 0};
  std::function<void(const std::string &)> on_state;  // mirror JSON out (YAML: buddy_ui text sensor)
  std::string last_state;
  bool shimmer = false;   // lower-arc status shimmer on (the working line)
  uint8_t shimmer_phase = 0;
  uint32_t small_t0 = 0;
  const std::vector<const lv_image_dsc_t *> *small_set = nullptr;
};

inline Ui &ui() {
  static Ui u;
  return u;
}

// ---- helpers ----
inline lv_obj_t *mk_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color) {
  lv_obj_t *l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  return l;
}
// Size an arclabel to x,y,w,h and keep its arc centred on the glass centre. center_offset is uint32_t but is added
// in 32-bit maths (lv_arclabel.c:509-510), so a negative offset wraps back to the right point.
inline void arc_band(lv_obj_t *a, int x, int y, int w, int h) {
  lv_obj_set_pos(a, x, y);
  lv_obj_set_size(a, w, h);
  lv_arclabel_set_center_offset_x(a, (uint32_t) (scene::C - (x + w / 2)));
  lv_arclabel_set_center_offset_y(a, (uint32_t) (scene::C - (y + h / 2)));
}
inline lv_obj_t *mk_arc(lv_obj_t *parent, const lv_font_t *font, bool top) {
  // spec §2 Common, Arcs: Inter Medium 26, name on the top arc from y 12, status on the matching lower arc,
  // ~390 px span. Clockwise over the top and counter-clockwise under the bottom so both read left to right.
  lv_obj_t *a = lv_arclabel_create(parent);
  // Only the band the text uses (+ ~24 px for rotated glyphs): a full-screen arclabel made every shimmer step
  // (~15/s) invalidate and redraw the whole glass, pet included.
  arc_band(a, 0, top ? 0 : 316, scene::W, 150);
  lv_obj_set_style_text_font(a, font, 0);
  // Harness centres both labels (terminal.c:730 s16 = pen + adv/2 - total/2) on radius 205 (name) / 202 (status),
  // text band about 194-213 px from the centre (terminal.c:691 ARC_PROP_MID, :744-748).
  // LVGL angles run clockwise from 3 o'clock. Counter-clockwise labels place letters from angle_start + size
  // backwards (lv_arclabel.c:495), so the bottom span is 35..145 deg, read right-to-left in angle = left-to-right.
  lv_arclabel_set_radius(a, top ? 205 : 202);
  lv_arclabel_set_angle_start(a, top ? 215 : 35);
  lv_arclabel_set_angle_size(a, 110);  // ~390 px of arc at r 205 (HT_ARC_SPAN)
  lv_arclabel_set_dir(a, top ? LV_ARCLABEL_DIR_CLOCKWISE : LV_ARCLABEL_DIR_COUNTER_CLOCKWISE);
  lv_arclabel_set_text_horizontal_align(a, LV_ARCLABEL_TEXT_ALIGN_CENTER);
  lv_arclabel_set_text_vertical_align(a, LV_ARCLABEL_TEXT_ALIGN_CENTER);
  lv_arclabel_set_overflow(a, LV_ARCLABEL_OVERFLOW_ELLIPSIS);
  lv_obj_remove_flag(a, LV_OBJ_FLAG_CLICKABLE);
  return a;
}
inline void show(lv_obj_t *o, bool on) {
  if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN) == on)
    (on ? lv_obj_remove_flag : lv_obj_add_flag)(o, LV_OBJ_FLAG_HIDDEN);
}
// Set label text only when it changed: unchanged text would still redraw every second.
inline void set_text(lv_obj_t *l, const char *t) {
  if (strcmp(lv_label_get_text(l), t) != 0) lv_label_set_text(l, t);
}
inline lv_obj_t *mk_card(lv_obj_t *parent) {
  // Recap card: radius 24, fill 0x1c1e26, rim 0x3d3f47 (focus.c:879-885). Harness x 41 w 384 clipped the round
  // glass at 4 lines (bottom corners at r ~234) and touched the face-side battery %: x 49 w 368 keeps r <= 228.
  lv_obj_t *c = lv_obj_create(parent);
  lv_obj_remove_style_all(c);
  lv_obj_set_style_bg_color(c, lv_color_hex(CARD), 0);
  lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(c, lv_color_hex(CARD_RIM), 0);
  lv_obj_set_style_border_width(c, 2, 0);
  lv_obj_set_style_radius(c, 24, 0);
  lv_obj_set_style_pad_hor(c, 10, 0);
  lv_obj_set_style_pad_ver(c, 21, 0);
  lv_obj_set_x(c, 49);
  lv_obj_set_width(c, 368);
  lv_obj_set_height(c, LV_SIZE_CONTENT);
  return c;
}
// "[ label ]" control in Inter 25 (spec §2.8). Pressed: accent ink on the selection colour.
inline lv_obj_t *mk_control(lv_obj_t *parent, const lv_font_t *font, const char *text, uint32_t ink, int x, int y,
                            int w) {
  lv_obj_t *b = lv_obj_create(parent);
  lv_obj_remove_style_all(b);
  lv_obj_set_pos(b, x, y - 20);
  lv_obj_set_size(b, w, 66);  // hit box: label plus margin, 66 px tall (ui_habitat.c:907-922)
  lv_obj_set_style_radius(b, 33, 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(SELECTION), LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_STATE_PRESSED);
  lv_obj_t *l = mk_label(b, font, ink);
  lv_obj_set_style_text_color(l, lv_color_hex(ACCENT), LV_STATE_PRESSED);
  lv_label_set_text(l, text);
  lv_obj_center(l);
  lv_obj_add_flag(l, LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
  return b;
}
// Elapsed: "0:34", "1:05", "1h 5m", "2d 3h" (focus.c:742).
inline void fmt_elapsed(char *out, size_t n, uint32_t ms) {
  uint32_t s = ms / 1000;
  if (s < 3600) snprintf(out, n, "%u:%02u", (unsigned) (s / 60), (unsigned) (s % 60));
  else if (s < 86400) snprintf(out, n, "%uh %um", (unsigned) (s / 3600), (unsigned) (s % 3600 / 60));
  else snprintf(out, n, "%ud %uh", (unsigned) (s / 86400), (unsigned) (s % 86400 / 3600));
}
// Cut to max codepoints, ending in "…" (focus.c:793-822). UTF-8 aware.
inline std::string cut(const std::string &s, int max) {
  int cps = 0;
  for (size_t i = 0; i < s.size(); i++) {
    if ((s[i] & 0xC0) != 0x80 && ++cps > max) return s.substr(0, i) + "\xE2\x80\xA6";
  }
  return s;
}
inline std::string one_line(std::string s) {
  for (char &c : s)
    if (c == '\n' || c == '\r' || c == '\t') c = ' ';
  return s;
}

// focus.c:91-102 hints, minus the ones that teach voice / tabs / panes (no such features here).
static const char *const HINTS[] = {"What's next?", "Your move", "Ready when you are", "Let's build it",
                                    "Do anything", "Make it happen", "Start something"};
static const char *const GERUNDS[] = {"Working", "Brewing", "Cooking", "Churning", "Frosting", "Simmering",
                                      "Tinkering", "Conjuring", "Composing", "Percolating", "Wrangling",
                                      "Hatching", "Concocting", "Puttering"};  // focus.c:753-762

inline void pick_hint() {
  // Random, never the same twice in a row (focus.c:117-134).
  const char *h;
  do h = HINTS[rand() % (sizeof(HINTS) / sizeof(HINTS[0]))]; while (h == ui().hint);
  ui().hint = h;
}

inline uint8_t shimmer_phase(uint32_t now);
inline void shimmer_paint(lv_obj_t *a, const std::string &text, uint32_t fg, uint8_t phase);
inline void place_bat_bolt();
inline void arc_text(lv_obj_t *a, const char *text, uint32_t color, bool shimmer = false) {
  int i = a == ui().name_arc ? 0 : 1;
  if (i == 1) ui().shimmer = shimmer;
  if (ui().arc_txt[i] == text && ui().arc_col[i] == color) return;
  if (i == 1) ui().shimmer_phase = 0;
  ui().arc_txt[i] = text;
  ui().arc_col[i] = color;
  if (i == 1 && shimmer) {  // paint the sweep now, not plain text for a frame
    ui().shimmer_phase = shimmer_phase(lv_tick_get());
    return shimmer_paint(ui().status_arc, ui().arc_txt[1], ui().arc_col[1], ui().shimmer_phase);
  }
  lv_obj_set_style_text_color(a, lv_color_hex(color), 0);
  lv_arclabel_set_text(a, text);
}

// State changes in power.h; the shield follows them here.
inline void set_asleep(bool z) {
  if (ui().set_asleep(z)) show(ui().shield, z);
}
inline void activity(uint32_t now) {
  if (ui().activity(now)) show(ui().shield, false);
}
inline void toggle_screen(uint32_t now) {
  if (ui().toggle(now)) show(ui().shield, ui().asleep);
}

void update(uint32_t now);
inline void tap(const std::string &t);

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
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, &u);
  lv_anim_set_exec_cb(&a, burst_step);
  lv_anim_set_values(&a, 0, 1000);
  lv_anim_set_duration(&a, 700);
  lv_anim_set_completed_cb(&a, [](lv_anim_t *) {
    for (auto *o : ui().burst) show(o, false);
  });
  lv_anim_start(&a);
}

inline void splash_in() {
  Ui &u = ui();
  auto run = [](lv_obj_t *o, lv_anim_exec_xcb_t cb, int32_t from, int32_t to, uint32_t ms, uint32_t delay) {
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, o);
    lv_anim_set_exec_cb(&a, cb);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_duration(&a, ms);
    lv_anim_set_delay(&a, delay);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
  };
  auto opa = [](void *o, int32_t v) { lv_obj_set_style_opa((lv_obj_t *) o, (lv_opa_t) v, 0); };
  auto ty = [](void *o, int32_t v) { lv_obj_set_style_translate_y((lv_obj_t *) o, v, 0); };
  run(u.word, opa, 0, 255, 700, 250);
  run(u.word, ty, 24, 0, 700, 250);
}

// ---- mirror: what is on the glass, as JSON (docs/DIAL-PROTOCOL.md "ui") ----
// Published when it changes, on the buddy_ui text sensor (the bridge reads the view from it) and in the log.
inline void publish_state() {
  Ui &u = ui();
  auto vis = [](lv_obj_t *o) { return o && !lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN); };
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
    d["small"] = u.small_set == &character::pack().small.z6 ? 6 : 4;
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
  if (vis(u.list)) d["list"] = lv_label_get_text(u.list_text);
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
  bool closable = u.view == View::ENTRIES || u.view == View::USAGE || u.view == View::BATTERY;
  const char *t = nullptr;
  if (code == LV_EVENT_LONG_PRESSED) {
    t = "hold";  // face hold (650 ms, fires while still held): recent entries + stats
  } else if (code == LV_EVENT_GESTURE) {
    bool right = lv_indev_get_gesture_dir(lv_indev_active()) == LV_DIR_RIGHT;
    if (right && u.view == View::PERMISSION && u.buddy->has_prompt()) t = "later";  // swipe right = back = "later" (spec §3)
    else if (closable) t = "back";
  } else if (code == LV_EVENT_SHORT_CLICKED && closable) {
    t = "back";  // "Tap to close"
  }
  if (t) return tap(t);
  uint32_t now = lv_tick_get();  // PRESSED, or nothing to do: the touch still counts as activity
  activity(now);
  update(now);
}
inline void on_card(lv_event_t *) { tap("card"); }
// Double tap on the small Clawd above the reply: done with it, same as a card tap. The reply goes and the face
// shows what's current: the working face if a turn started while reading, else home.
// LVGL's streak window is the long-press time (650 ms, lv_indev.c indev_proc_short_click).
inline void on_small(lv_event_t *) {
  ui().card_manual = false;
  tap("card");
}
inline void card_scroll_y(void *o, int32_t y);
// Finger on the overflowing reply text: stop the autoscroll, restart the 5 s count on every touch/move.
// LVGL sends no CLICKED / LONG_PRESSED after a scroll starts (lv_indev.c:1526, :1426), so a drag can't tap/hold.
inline void on_card_text(lv_event_t *e) {
  Ui &u = ui();
  uint32_t now = lv_tick_get();
  if (lv_event_get_code(e) == LV_EVENT_PRESSED) {
    if (!u.card_overflow) return;
    lv_anim_delete(u.card_text, card_scroll_y);
    u.card_manual = true;
  } else if (!u.card_manual) {
    return;  // SCROLL from our own autoscroll / resume anim
  }
  u.card_manual_at = now;
  activity(now);
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
    u.entries_open = false;
    u.usage_open = false;
    u.battery_open = false;
  }
  update(now);
}

// Remote tap (api action buddy_tap): the same effect as touching the glass.
// targets: allow, deny, later, card, bell, hold, back, wake, screen, boot
inline void tap(const std::string &t) {
  Ui &u = ui();
  uint32_t now = lv_tick_get();
  bool was_asleep = u.asleep;
  activity(now);
  if (t == "boot") return boot(now);
  if (t == "screen") toggle_screen(now);
  if (was_asleep || t == "wake" || t == "screen") return update(now);  // a wake touch only wakes
  if ((t == "allow" || t == "deny") && u.view == View::PERMISSION) {
    if (t == "allow") u.buddy->approve();
    else u.buddy->deny();
    u.sent_at = now;
    u.last_deny = t == "deny";
  } else if (t == "later" && u.view == View::PERMISSION && u.buddy->has_prompt()) {
    u.deferred_id = u.buddy->snap().prompt_id;
  } else if (t == "card" && u.view == View::FACE) {
    u.recap_cleared = u.buddy->last_text_ms();
    pick_hint();
  } else if (t == "bell") {
    u.deferred_id.clear();
  } else if (t == "hold" && u.view == View::FACE) {
    u.entries_open = true;
    u.entries_at = now;
  } else if (t == "back") {
    u.entries_open = false;
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
  lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(root, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(root, on_root, LV_EVENT_LONG_PRESSED, nullptr);
  lv_obj_add_event_cb(root, on_root, LV_EVENT_GESTURE, nullptr);
  lv_obj_add_event_cb(root, on_root, LV_EVENT_SHORT_CLICKED, nullptr);
  lv_obj_add_event_cb(root, on_root, LV_EVENT_PRESSED, nullptr);  // any touch counts as activity
  if (lv_indev_t *in = lv_indev_get_next(nullptr)) {
    lv_indev_set_long_press_time(in, 650);  // spec §3: hold 650 ms
  }

  u.scene.create(root);
  u.alert.create(root, true);
  lv_obj_set_style_text_font(u.alert.count, u.f.f26m, 0);
  lv_obj_add_flag(u.alert.layer[0], LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_ext_click_area(u.alert.layer[0], 14);  // bubble tap margin (ui_habitat.c:1206)
  lv_obj_add_event_cb(u.alert.layer[0], on_reopen, LV_EVENT_CLICKED, nullptr);

  u.small = lv_image_create(root);
  lv_obj_add_flag(u.small, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(u.small, LV_OBJ_FLAG_EVENT_BUBBLE);  // press / hold still reach on_root
  lv_obj_set_ext_click_area(u.small, 30);  // card is on top where they overlap; a card tap also dismisses
  lv_obj_add_event_cb(u.small, on_small, LV_EVENT_DOUBLE_CLICKED, nullptr);
  u.card = mk_card(root);
  lv_obj_add_flag(u.card, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(u.card, on_card, LV_EVENT_CLICKED, nullptr);
  u.card_text = mk_label(u.card, u.f.f30, RECAP);
  lv_obj_set_width(u.card_text, 348);  // card 368 - 2 x 10 pad
  lv_obj_set_style_text_line_space(u.card_text, 43 - lv_font_get_line_height(u.f.f30), 0);  // 43 px pitch
  lv_label_set_long_mode(u.card_text, LV_LABEL_LONG_MODE_WRAP);  // fixed height past 4 lines: clips and scrolls
  lv_obj_set_scrollbar_mode(u.card_text, LV_SCROLLBAR_MODE_OFF);
  // Drag target: it must be the pressed object for the indev to scroll it. EVENT_BUBBLE passes CLICKED up to u.card.
  lv_obj_add_flag(u.card_text, LV_OBJ_FLAG_CLICKABLE);  // one flag per call: C++ won't take an OR'd int as lv_obj_flag_t
  lv_obj_add_flag(u.card_text, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(u.card_text, LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_remove_flag(u.card_text, LV_OBJ_FLAG_SCROLL_ELASTIC);  // clamp at both ends (lv_indev_scroll.c elastic_diff)
  lv_obj_set_scroll_dir(u.card_text, LV_DIR_VER);
  // Vertical only: text 348 > card content 344 (border counts), so a sideways drag chained up and slid the card.
  lv_obj_remove_flag(u.card_text, LV_OBJ_FLAG_SCROLL_CHAIN_HOR);
  lv_obj_remove_flag(u.card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(u.card_text, on_card_text, LV_EVENT_PRESSED, nullptr);
  lv_obj_add_event_cb(u.card_text, on_card_text, LV_EVENT_SCROLL, nullptr);
  lv_obj_add_event_cb(u.card_text, on_card_text, LV_EVENT_RELEASED, nullptr);
  lv_obj_center(u.card_text);

  // Bell pill (spec §2.6): 0x006fff, 32 px tall, fully rounded, centred at y 400.
  u.bell = lv_obj_create(root);
  lv_obj_remove_style_all(u.bell);
  lv_obj_set_style_bg_color(u.bell, lv_color_hex(BLUE), 0);
  lv_obj_set_style_bg_opa(u.bell, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(u.bell, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_pad_hor(u.bell, 13, 0);
  lv_obj_set_size(u.bell, LV_SIZE_CONTENT, 32);
  lv_obj_align(u.bell, LV_ALIGN_TOP_MID, 0, 400 - 16);
  lv_obj_add_flag(u.bell, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_ext_click_area(u.bell, 20);
  lv_obj_add_event_cb(u.bell, on_reopen, LV_EVENT_CLICKED, nullptr);
  u.bell_text = mk_label(u.bell, u.f.f20, NAME);
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
    o = lv_obj_create(root);
    lv_obj_remove_style_all(o);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(LIVE), 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    show(o, false);
  }
  u.alarm = mk_label(root, u.f.f96b, USAGE_HOT);
  lv_obj_center(u.alarm);
  lv_obj_set_style_transform_pivot_x(u.alarm, LV_PCT(50), 0);
  lv_obj_set_style_transform_pivot_y(u.alarm, LV_PCT(50), 0);
  show(u.alarm, false);

  // Permission (new; STOP layout, spec §2.8): question title, hint card, [deny] [allow].
  u.perm = lv_obj_create(root);
  lv_obj_remove_style_all(u.perm);
  lv_obj_set_size(u.perm, scene::W, scene::W);
  lv_obj_add_flag(u.perm, LV_OBJ_FLAG_EVENT_BUBBLE);  // gestures reach root
  lv_obj_remove_flag(u.perm, LV_OBJ_FLAG_SCROLLABLE);
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
  u.btn_deny = mk_control(u.perm, u.f.f25, "[ deny ]", ERROR, 83, 355, 156);
  u.btn_allow = mk_control(u.perm, u.f.f25, "[ allow ]", TEXT, 243, 355, 140);
  lv_obj_add_event_cb(u.btn_deny, on_decide, LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(u.btn_allow, on_decide, LV_EVENT_CLICKED, nullptr);

  // Entries (face hold): recent transcript lines + sessions, in place of the Harness inbox.
  u.list = lv_obj_create(root);
  lv_obj_remove_style_all(u.list);
  lv_obj_set_size(u.list, scene::W, scene::W);
  lv_obj_add_flag(u.list, LV_OBJ_FLAG_EVENT_BUBBLE);
  u.list_text = mk_label(u.list, u.f.f25, TEXT);
  lv_obj_set_width(u.list_text, 340);
  lv_obj_set_style_text_align(u.list_text, LV_TEXT_ALIGN_LEFT, 0);
  lv_label_set_long_mode(u.list_text, LV_LABEL_LONG_MODE_WRAP);
  lv_obj_align(u.list_text, LV_ALIGN_TOP_MID, 0, 96);
  lv_obj_add_flag(u.list_text, LV_OBJ_FLAG_EVENT_BUBBLE);

  // Usage screen (BOOT): children made by build_usage().
  u.usage = lv_obj_create(root);
  lv_obj_remove_style_all(u.usage);
  lv_obj_set_size(u.usage, scene::W, scene::W);
  lv_obj_add_flag(u.usage, LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_add_flag(u.usage, LV_OBJ_FLAG_HIDDEN);

  // Battery page (BOOT, after usage): ring + big %, values set in update().
  u.battery_page = lv_obj_create(root);
  lv_obj_remove_style_all(u.battery_page);
  lv_obj_set_size(u.battery_page, scene::W, scene::W);
  lv_obj_add_flag(u.battery_page, LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_add_flag(u.battery_page, LV_OBJ_FLAG_HIDDEN);
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
  lv_obj_remove_flag(u.battery_ring, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(u.battery_ring, LV_OBJ_FLAG_EVENT_BUBBLE);
  u.battery_num = mk_label(u.battery_page, u.f.f96b, TEXT);
  lv_obj_center(u.battery_num);

  // OTA page: big % and a rect bar (like the usage bars), text on the arcs; values set by ota().
  u.ota_page = lv_obj_create(root);
  lv_obj_remove_style_all(u.ota_page);
  lv_obj_set_size(u.ota_page, scene::W, scene::W);
  lv_obj_add_flag(u.ota_page, LV_OBJ_FLAG_HIDDEN);
  u.ota_pct = mk_label(u.ota_page, u.f.f96b, TEXT);
  lv_label_set_text(u.ota_pct, "0%");
  lv_obj_align(u.ota_pct, LV_ALIGN_CENTER, 0, -30);
  auto ota_rect = [&](int w, uint32_t col) {
    lv_obj_t *o = lv_obj_create(u.ota_page);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, w, 14);
    lv_obj_set_pos(o, (scene::W - OTA_BAR_W) / 2, scene::C + 50);
    lv_obj_set_style_radius(o, 7, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(col), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
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
  lv_obj_add_flag(u.bat_arc, LV_OBJ_FLAG_HIDDEN);
  // Charging bolt after the side %: a stroked zigzag (Inter has no bolt glyph), turned to follow the arc text.
  u.bat_bolt = lv_line_create(root);
  lv_obj_set_style_line_width(u.bat_bolt, 4, 0);
  lv_obj_set_style_line_rounded(u.bat_bolt, true, 0);
  lv_obj_set_style_line_color(u.bat_bolt, lv_color_hex(LIVE), 0);
  lv_obj_remove_flag(u.bat_bolt, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(u.bat_bolt, LV_OBJ_FLAG_HIDDEN);
  // Shield last = topmost. Hidden while awake; while dark it takes the touch and only wakes the screen
  // (touch_habitat.c:133-138 swallows the wake contact the same way).
  u.shield = lv_obj_create(root);
  lv_obj_remove_style_all(u.shield);
  lv_obj_set_size(u.shield, scene::W, scene::W);
  lv_obj_add_flag(u.shield, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(u.shield, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_event_cb(u.shield, [](lv_event_t *) { activity(lv_tick_get()); }, LV_EVENT_PRESSED, nullptr);

  pick_hint();
  u.last_activity = lv_tick_get();
  // boot: no wordmark fade. The pack load blocks LVGL right after the first frame, and the fade would start at opacity 0.
  update(u.last_activity);
}

// Small pet above a card, centred between the name's foot (y 66) and the card top (focus.c:925-944).
inline void place_small(int card_top, bool big, uint32_t now) {
  Ui &u = ui();
  const scene::Small &sm = character::pack().small;
  u.small_set = big ? &sm.z6 : &sm.z4;
  if (u.small_set->empty() || sm.loop.empty()) {  // no pack on flash yet
    show(u.small, false);
    return;
  }
  if (!u.small_was) u.small_t0 = now;
  u.small_was = true;
  show(u.small, true);
  const auto &st = sm.loop[((now - u.small_t0) / sm.step_ms) % sm.loop.size()];
  const lv_image_dsc_t *img = (*u.small_set)[std::min<size_t>(st[0], u.small_set->size() - 1)];
  lv_image_set_src(u.small, img);
  int z = big ? 6 : 4;
  lv_obj_set_pos(u.small, (scene::W - (int) img->header.w) / 2,
                 66 + (card_top - 66 - (int) img->header.h) / 2 + st[1] * z / 4);
}

inline void card_scroll_y(void *o, int32_t y) { lv_obj_scroll_to_y((lv_obj_t *) o, y, LV_ANIM_OFF); }

// Autoscroll cycle: hold at the top, scroll down dist px, hold, back up quicker, repeat.
inline void start_card_anim(int dist) {
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, ui().card_text);
  lv_anim_set_exec_cb(&a, card_scroll_y);
  lv_anim_set_values(&a, 0, dist);
  lv_anim_set_delay(&a, CARD_HOLD_MS);
  lv_anim_set_duration(&a, dist * 1000 / CARD_SCROLL_PX_S);
  lv_anim_set_reverse_delay(&a, CARD_HOLD_MS);
  lv_anim_set_reverse_duration(&a, dist * 1000 / CARD_BACK_PX_S);
  lv_anim_set_repeat_delay(&a, CARD_HOLD_MS);
  lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
  lv_anim_start(&a);
}
// 5 s after the last touch: back to the top at CARD_BACK_PX_S, then the normal cycle.
inline void resume_card_anim() {
  Ui &u = ui();
  u.card_manual = false;
  int32_t y = lv_obj_get_scroll_top(u.card_text);
  int dist = y + lv_obj_get_scroll_bottom(u.card_text);  // content overflow, constant while scrolling
  if (y <= 0) return start_card_anim(dist);
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, u.card_text);
  lv_anim_set_exec_cb(&a, card_scroll_y);
  lv_anim_set_values(&a, y, 0);
  lv_anim_set_duration(&a, y * 1000 / CARD_BACK_PX_S);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
  lv_anim_set_user_data(&a, (void *) (intptr_t) dist);
  lv_anim_set_completed_cb(&a, [](lv_anim_t *an) { start_card_anim((int) (intptr_t) lv_anim_get_user_data(an)); });
  lv_anim_start(&a);
}

inline void show_card(const std::string &text, uint32_t now) {
  Ui &u = ui();
  show(u.card, true);
  if (strcmp(lv_label_get_text(u.card_text), text.c_str()) != 0 || u.card_lines == 0) {
    // New text: measure it at full height, then pin to 4 lines and scroll the rest through.
    lv_anim_delete(u.card_text, card_scroll_y);
    u.card_manual = false;
    lv_obj_scroll_to_y(u.card_text, 0, LV_ANIM_OFF);
    lv_obj_set_height(u.card_text, LV_SIZE_CONTENT);
    lv_label_set_text(u.card_text, text.c_str());
    lv_obj_update_layout(u.card);
    int full = lv_obj_get_height(u.card_text);
    int lines = full / 43 + 1;
    u.card_lines = std::min(lines, 4);
    u.card_overflow = lines > 4;
    if (lines > 4) {
      lv_obj_set_height(u.card_text, 4 * 43);
      int dist = full - 4 * 43;
      start_card_anim(dist);
    }
  }
  int h = u.card_lines * 43 + 2 * 21;
  int top = 176 + (200 - h) / 2;  // centred in y 176-376 (focus.c:49-56)
  lv_obj_set_y(u.card, top);
  place_small(top, u.card_lines <= 3, now);
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
    lv_obj_add_flag(l, LV_OBJ_FLAG_EVENT_BUBBLE);
  };
  auto bar = [&](int x, int cy, int w, uint32_t col) {
    lv_obj_t *b = lv_obj_create(u.usage);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, w, 10);
    lv_obj_set_pos(b, x, cy - 5);
    lv_obj_set_style_radius(b, 5, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(col), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_add_flag(b, LV_OBJ_FLAG_EVENT_BUBBLE);
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
      lv_anim_t a;
      lv_anim_init(&a);
      lv_anim_set_var(&a, fill);
      lv_anim_set_values(&a, 10, std::max<int>(10, BW * (int) std::min<uint32_t>(w.pct, 100) / 100));
      lv_anim_set_duration(&a, 500);
      lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
      lv_anim_set_exec_cb(&a, [](void *o, int32_t v) { lv_obj_set_width((lv_obj_t *) o, v); });
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
    u.entries_open = false;
    activity(now);
  } else if (!running && u.was_running) {
    activity(now);  // done: wake to show the recap
  }
  u.was_running = running;
  if (u.deferred_id != s.prompt_id && !s.prompt_id.empty()) u.deferred_id.clear();
  if (u.entries_open && now - u.entries_at > ENTRIES_MS) u.entries_open = false;
  if (u.usage_open && now - u.usage_at > USAGE_SCREEN_MS) u.usage_open = false;
  if (u.battery_open && now - u.battery_at > USAGE_SCREEN_MS) u.battery_open = false;
  if (u.ota_on && u.ota_fail_at && now - u.ota_fail_at > OTA_FAIL_MS) u.ota_on = false, u.ota_fail_at = 0;

  View v = u.ota_on                                     ? View::OTA
           : !live                                      ? View::BRAND
           : b->has_prompt() && u.deferred_id.empty()   ? View::PERMISSION
           : u.usage_open                               ? View::USAGE
           : u.battery_open                             ? View::BATTERY
           : u.entries_open                             ? View::ENTRIES
                                                        : View::FACE;
  if (v == View::PERMISSION && u.view != View::PERMISSION) activity(now);  // a new question wakes the dial
  if (v == View::FACE && u.view != View::FACE) pick_hint();
  bool entered = v != u.view;
  if (entered) {
    ESP_LOGI("buddyui", "view %s (running %u, waiting %u)", VIEW_NAMES[(int) v], (unsigned) s.running, (unsigned) s.waiting);
  }
  if (entered && u.view == View::BRAND) burst();  // the bridge just connected
  u.view = v;
  if (v != View::BRAND) u.was_live = true;
  {  // face-side battery %: face only, always on battery, 60 s after plug-in / wake on USB
    bool usb = u.on_usb && u.on_usb();
    u.power_source(usb, now);
    int bat = u.battery ? u.battery() : -1;
    bool side = v == View::FACE && bat >= 0 && u.show_battery_side(usb, now);
    show(u.bat_arc, side);
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
  // Visibility as a diff: views say what they show (vis.on), the guard applies it once on any return.
  struct Vis {
    Ui &u;
    lv_obj_t *o[11];
    bool want[11] = {};
    explicit Vis(Ui &ui_) : u(ui_), o{ui_.small, ui_.card, ui_.bell, ui_.word, ui_.sub, ui_.perm, ui_.list, ui_.alarm, ui_.usage, ui_.battery_page, ui_.ota_page} {}
    void on(lv_obj_t *x) {
      for (int i = 0; i < 11; i++)
        if (o[i] == x) want[i] = true;
    }
    ~Vis() {
      for (int i = 0; i < 11; i++) show(o[i], want[i]);
      u.small_was = want[0];
      publish_state();
    }
  } vis{u};
  bool sending = now - u.sent_at < SEND_MS && u.sent_at;
  const char *name = "Claude";

  if (v == View::OTA) {  // ota() owns the widgets and arcs; nothing else may change the page
    u.scene.stop();
    u.alert.stop();
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

  arc_text(u.name_arc, name, provider_ink(name, NAME));

  if (v == View::PERMISSION) {
    u.scene.stop();
    u.alert.stop();
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
    u.scene.stop();
    u.alert.stop();
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
    u.scene.stop();
    u.alert.stop();
    vis.on(u.battery_page);
    int bat = u.battery ? u.battery() : -1;
    uint32_t ink = bat >= 0 ? power::battery_ink(bat) : RELAX_LINE;
    char t[12];
    snprintf(t, sizeof(t), bat >= 0 ? "%d%%" : "No battery", bat);
    set_text(u.battery_num, t);
    lv_obj_set_style_text_font(u.battery_num, bat >= 0 ? u.f.f96b : u.f.f30, 0);  // f96b has digits and % only
    lv_obj_set_style_text_color(u.battery_num, lv_color_hex(ink), 0);
    lv_arc_set_value(u.battery_ring, bat >= 0 ? bat : 100);
    lv_obj_set_style_arc_color(u.battery_ring, lv_color_hex(ink), LV_PART_INDICATOR);
    bool usb = u.on_usb && u.on_usb();
    arc_text(u.name_arc, "Battery", NAME);
    if (usb && bat >= 0) arc_text(u.status_arc, "Charging", LIVE, true);  // green working-line shimmer
    else if (usb) arc_text(u.status_arc, "USB power", RELAX_LINE);
    else arc_text(u.status_arc, "Tap to close", RELAX_LINE);
    return;
  }

  if (v == View::ENTRIES) {
    u.scene.stop();
    u.alert.stop();
    vis.on(u.list);
    std::string t;
    for (const auto &e : s.entries) t += cut(one_line(e), 28) + "\n";
    if (s.entries.empty()) t = "Nothing here yet.\n";
    set_text(u.list_text, t.c_str());
    char buf[64];
    snprintf(buf, sizeof(buf), "%u session%s", (unsigned) s.total, s.total == 1 ? "" : "s");
    arc_text(u.name_arc, "Recent", NAME);
    arc_text(u.status_arc, s.usage.empty() ? buf : s.usage.c_str(), provider_ink(s.usage, RELAX_LINE));
    return;
  }

  // ---- FACE (spec §2.9) ----
  bool recap = !running && !b->last_text().empty() && b->last_text_ms() > u.recap_cleared;
  bool waiting = s.waiting > 0;  // only reachable here once the prompt was put off
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
  } else if (reading) {
    u.scene.stop();
    u.alert.stop();
    vis.on(u.card);
    vis.on(u.small);
  } else if (running) {
    const scene::Scene *sc = u.build ? character::find("build") : character::find("work");
    u.scene.play(sc, now, 4);
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
    // Live line "<verb>… m:ss" in voice green (focus.c:764-791). Verb: the heartbeat's msg, else a gerund.
    char el[16], line[64];
    fmt_elapsed(el, sizeof(el), now - u.work_start);
    std::string verb = s.msg.empty() ? GERUNDS[(now - u.work_start) / GERUND_MS % (sizeof(GERUNDS) / sizeof(GERUNDS[0]))]
                                     : cut(one_line(s.msg), 18);
    snprintf(line, sizeof(line), "%s\xE2\x80\xA6 %s", verb.c_str(), el);
    arc_text(u.status_arc, line, LIVE, true);
  } else if (recap) {
    u.scene.stop();
    u.alert.stop();
    vis.on(u.card);
    vis.on(u.small);
    show_card(cut(one_line(b->last_text()), RECAP_MAX), now);
    arc_text(u.status_arc, "", RELAX_LINE);
  } else {
    u.alert.stop();
    u.scene.play(character::find("relax"), now, 4);
    arc_text(u.status_arc, waiting ? "" : u.hint, RELAX_LINE);  // hint left out while the bell shows
  }
  // Plan usage: 3 s of every 15 s on the lower arc, over whatever the face shows there (Harness's display carries
  // "what your harness has spent today"; this is the plan's 5-hour / weekly use from the bridge).
  if (!s.usage.empty() && !sending && now % USAGE_EVERY_MS < USAGE_SHOW_MS) {
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
  if (waiting && !running) {
    char n[16];
    snprintf(n, sizeof(n), "%u waiting", (unsigned) s.waiting);  // ponytail: no bell glyph (Harness uses FontAwesome)
    set_text(u.bell_text, n);
    vis.on(u.bell);
  }
}

// 50 ms tick (the Harness code-drawn motion tick): advance pets, rerun state for the stopwatch and timeouts.
// Status shimmer, ported from Harness (terminal.c:259-268 phase, :1039-1049 band, :1600-1640 ink):
// phase 1..21 advances every 64 ms for 20 steps, then rests 12 (2048 ms period). The band centre runs from
// 48 px before the text to 48 px after it; ink within 48 px of the centre scales from 100/255 to full
// brightness, everything else stays at 100/255. Harness draws it per pixel; here per letter (arclabel recolor).
inline uint8_t shimmer_phase(uint32_t now) {
  unsigned step = (now / 64) % 32;
  return (uint8_t) (1 + (step < 20 ? step : 20));
}
inline void shimmer_paint(lv_obj_t *a, const std::string &text, uint32_t fg, uint8_t phase) {
  Ui &u = ui();
  const lv_font_t *f = u.f.f26m;
  int total = 0;
  for (size_t i = 0; i < text.size(); i++)
    if ((text[i] & 0xC0) != 0x80) total += lv_font_get_glyph_width(f, (uint8_t) text[i], 0);
  int center = -48 + (total + 96) * (phase - 1) / 20, pen = 0;
  std::string out;
  for (size_t i = 0; i < text.size();) {
    size_t n = 1;
    while (i + n < text.size() && (text[i + n] & 0xC0) == 0x80) n++;
    if (text[i] == '#') { i += n; continue; }  // the recolor marker; never in our status lines
    uint32_t cp = n == 1 ? (uint8_t) text[i] : 0x2026;  // only multi-byte char we use is the ellipsis
    int w = lv_font_get_glyph_width(f, cp, 0);
    int dist = abs(pen + w / 2 - center);
    unsigned level = dist >= 48 ? 0 : 15 - (unsigned) dist * 15 / 48;
    unsigned op = 100 + level * 155 / 15;
    uint32_t c = ((((fg >> 16) & 0xFF) * op / 255) << 16) | ((((fg >> 8) & 0xFF) * op / 255) << 8) | ((fg & 0xFF) * op / 255);
    char hex[10];
    snprintf(hex, sizeof(hex), "#%06x ", (unsigned) c);
    out += hex;
    out.append(text, i, n);
    out += '#';
    pen += w;
    i += n;
  }
  lv_arclabel_set_text(a, out.c_str());
}
// Bolt after the side % (bat_arc: clockwise 150..210 deg, centred on 180, r 205): rotate an upright 12x22 zigzag so
// its top points outward like the arc glyphs, centred 10 px past the end of the text.
inline void place_bat_bolt() {
  Ui &u = ui();
  static const float BOLT[4][2] = {{9, 0}, {1, 12}, {11, 12}, {3, 22}};  // centre (6, 11)
  int w = 0;
  for (char c : u.bat_txt) w += lv_font_get_glyph_width(u.f.f26m, (uint8_t) c, 0);
  const float r = 205, deg = 3.14159265f / 180;
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

inline void tick(uint32_t now) {
  Ui &u = ui();
  if (!u.root) return;
  // Dark: LVGL is paused (YAML), so skip the per-frame animation work; state still updates once a second.
  if (!u.asleep) {
    u.scene.tick(now);
    u.alert.tick(now);
  }
  if (u.bat_shimmer && !u.asleep) {  // charging: side % shimmers green, same sweep as the working line
    uint8_t ph = shimmer_phase(now);
    if (ph != u.bat_phase) {
      u.bat_phase = ph;
      shimmer_paint(u.bat_arc, u.bat_txt, LIVE, ph);
    }
  }
  if (u.shimmer && !u.asleep) {
    uint8_t ph = shimmer_phase(now);
    if (ph != u.shimmer_phase) {
      u.shimmer_phase = ph;
      shimmer_paint(u.status_arc, u.arc_txt[1], u.arc_col[1], ph);
    }
  }
  if (!u.asleep && !lv_obj_has_flag(u.alarm, LV_OBJ_FLAG_HIDDEN)) {
    // Bounce in, pulse twice a second, bounce out, across the 3 s usage flash. 256 = 1x.
    uint32_t t = now % USAGE_EVERY_MS;
    float env = std::min(1.0f, std::min(t / 250.0f, (USAGE_SHOW_MS - (float) std::min(t, USAGE_SHOW_MS)) / 250.0f));
    float k = env * (1.0f + 0.15f * sinf(t * 2 * 3.14159f / 500));
    lv_obj_set_style_transform_scale(u.alarm, (int32_t) (256 * k), 0);
  }
  if (!u.asleep && !lv_obj_has_flag(u.small, LV_OBJ_FLAG_HIDDEN) && u.small_set) {
    lv_obj_update_layout(u.card);
    place_small(lv_obj_get_y(u.card), u.small_set == &character::pack().small.z6, now);
  }
  if (u.card_manual && !u.asleep && !lv_obj_has_flag(u.card, LV_OBJ_FLAG_HIDDEN) && now - u.card_manual_at >= CARD_MANUAL_MS &&
      lv_indev_get_state(lv_indev_get_next(nullptr)) != LV_INDEV_STATE_PRESSED) {
    resume_card_anim();
    update(now);  // a turn that started while reading takes the face now
  }
  static uint8_t in_usage = 0;  // start/end the usage flash on time, not on the next 1 s update
  uint8_t w = (uint8_t) std::min<uint32_t>(now % USAGE_EVERY_MS / USAGE_SHOW_MS, 2);
  if (w != in_usage) {
    in_usage = w;
    update(now);
  }
  static uint32_t last_state = 0;
  if (now - last_state >= 1000) {  // stopwatch seconds, entries timeout, send scene end
    last_state = now;
    update(now);
  }
  if (u.idle_due(now, BOOT_AWAKE_MS, SLEEP_MS, u.on_usb && u.on_usb())) set_asleep(true);
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

// YAML hooks, polled from the 50 ms interval: take_wake() -> fade the panel in, take_sleep() -> fade it out.
inline bool take_wake() { return ui().take_wake(); }
inline bool take_sleep() { return ui().take_sleep(); }

}  // namespace buddyui
