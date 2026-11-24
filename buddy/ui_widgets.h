#pragma once
#include "ui_state.h"

namespace buddyui {

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
  lv_obj_set_clickable(a, false);
  return a;
}
inline void show(lv_obj_t *o, bool on) {
  if (lv_obj_is_hidden(o) == on)
    lv_obj_set_hidden(o, !on);
}
// Set label text only when it changed: unchanged text would still redraw every second.
inline void set_text(lv_obj_t *l, const char *t) {
  if (strcmp(lv_label_get_text(l), t) != 0) lv_label_set_text(l, t);
}
// Full-screen page container: bare, W x W; optionally lets gestures bubble to root and/or starts hidden.
inline lv_obj_t *mk_page(lv_obj_t *parent, bool bubble, bool hidden) {
  lv_obj_t *p = lv_obj_create(parent);
  lv_obj_remove_style_all(p);
  lv_obj_set_size(p, scene::W, scene::W);
  if (bubble) lv_obj_set_event_bubble(p, true);
  if (hidden) lv_obj_set_hidden(p, true);
  return p;
}
// Bare filled rounded rect: size, radius and an opaque fill. Callers add position, events, hidden and the like.
inline lv_obj_t *mk_fill(lv_obj_t *parent, int32_t w, int32_t h, int32_t radius, uint32_t color) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_set_size(o, w, h);
  lv_obj_set_style_radius(o, radius, 0);
  lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  return o;
}
inline lv_obj_t *mk_card(lv_obj_t *parent) {
  // Recap card: radius 24, fill 0x1c1e26, rim 0x3d3f47 (focus.c:879-885). Harness x 41 w 384 clipped the round
  // glass at 4 lines (bottom corners at r ~234) and touched the face-side battery %: x 49 w 368 keeps r <= 228.
  lv_obj_t *c = mk_fill(parent, 368, LV_SIZE_CONTENT, 24, CARD);
  lv_obj_set_style_border_color(c, lv_color_hex(CARD_RIM), 0);
  lv_obj_set_style_border_width(c, 2, 0);
  lv_obj_set_style_pad_hor(c, 10, 0);
  lv_obj_set_style_pad_ver(c, 21, 0);
  lv_obj_set_x(c, 49);
  return c;
}
// Pill button sized to its label plus padding, centred across the screen (set_y places it). Filled in its kind's
// colour; pressed: darker and a touch smaller. A styled lv_obj: lv_button is the same object plus theme styles, and
// ESPHome compiles it out (LV_USE_BUTTON 0) unless a YAML widget uses it.
inline lv_obj_t *mk_button(lv_obj_t *parent, const lv_font_t *font, const char *text, Btn kind = Btn::PRIMARY) {
  const BtnInk &ink = BTN_INK[(int) kind];
  lv_obj_t *b = lv_obj_create(parent);
  lv_obj_remove_style_all(b);
  lv_obj_set_size(b, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_align(b, LV_ALIGN_TOP_MID);
  lv_obj_set_style_pad_hor(b, 36, 0);
  lv_obj_set_style_pad_ver(b, 14, 0);
  lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(ink.fill), 0);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(b, lv_color_darken(lv_color_hex(ink.fill), LV_OPA_30), LV_STATE_PRESSED);
  lv_obj_set_style_text_color(b, lv_color_hex(ink.text), 0);  // on the button: the label inherits it
  lv_obj_set_style_text_font(b, font, 0);
  lv_obj_set_style_transform_scale(b, 240, LV_STATE_PRESSED);  // 256 = 1x
  lv_obj_set_ext_click_area(b, 12);
  lv_obj_set_clickable(b, true);
  lv_obj_t *l = lv_label_create(b);
  lv_label_set_text(l, text);
  lv_obj_set_event_bubble(l, true);
  lv_obj_update_layout(b);  // size known now: pivot for the press scale, height for the caller's placement
  lv_obj_set_style_transform_pivot_x(b, lv_obj_get_width(b) / 2, 0);
  lv_obj_set_style_transform_pivot_y(b, lv_obj_get_height(b) / 2, 0);
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
// The display name of a non-Claude agent (the `agent` field), or nullptr for Claude and anything unknown.
inline const char *agent_name(const std::string &a) {
  return a == "codex" ? "Codex" : a == "copilot" ? "Copilot" : nullptr;
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
  if (!ui().set_asleep(z)) return;
  show(ui().shield, z);
  // Raise to wake (buddy/raise_wake.h): on the sleep edge relearn where the dial rests (reset() also drops a raise
  // armed as it went dark) and start the IMU at the idle poll rate.
  if (z) {
    raise_wake::reset();
    ui().poll_fast = false;
    if (ui().on_poll) ui().on_poll(false);
  }
}
inline void activity(uint32_t now) {
  if (ui().activity(now)) show(ui().shield, false);
}
inline void toggle_screen(uint32_t now) {
  if (ui().toggle(now)) show(ui().shield, ui().asleep);
}

// An anim set up but not started, so callers can add reverse, repeat, user data or a completed cb before lv_anim_start.
// A null path keeps lv_anim_init's linear one.
inline lv_anim_t make_anim(void *var, lv_anim_exec_xcb_t cb, int32_t from, int32_t to, uint32_t ms, uint32_t delay = 0,
                           lv_anim_path_cb_t path = nullptr) {
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, var);
  lv_anim_set_exec_cb(&a, cb);
  lv_anim_set_values(&a, from, to);
  lv_anim_set_duration(&a, ms);
  lv_anim_set_delay(&a, delay);
  if (path) lv_anim_set_path_cb(&a, path);
  return a;
}

}  // namespace buddyui
