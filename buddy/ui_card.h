#pragma once
#include "ui_voice.h"

namespace buddyui {

// Reply text: Inter 20 on a 29 px line pitch (the 30 / 43 px ratio).
static constexpr int CARD_PITCH = 29;

inline void on_card(lv_event_t *) { tap("card"); }
// Dismiss above the reply: done with it, same as a card tap. The reply goes and the face shows what's current:
// the working face if a turn started while reading, else home. One tap (the old double tap on the small Clawd was
// hard to land).
inline void on_dismiss(lv_event_t *) {
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

// Dismiss centred between the name's foot (y 66) and the card top.
inline void place_dismiss(int card_top) {
  lv_obj_t *d = ui().dismiss;
  lv_obj_set_y(d, 66 + (card_top - 66 - lv_obj_get_height(d)) / 2);
}

inline void card_scroll_y(void *o, int32_t y) { lv_obj_scroll_to_y((lv_obj_t *) o, y, LV_ANIM_OFF); }

// Autoscroll cycle: hold at the top, scroll down dist px, hold, back up quicker, repeat.
inline void start_card_anim(int dist) {
  lv_anim_t a = make_anim(ui().card_text, card_scroll_y, 0, dist, dist * 1000 / CARD_SCROLL_PX_S, CARD_HOLD_MS,
                          lv_anim_path_ease_in_out);
  lv_anim_set_reverse_delay(&a, CARD_HOLD_MS);
  lv_anim_set_reverse_duration(&a, dist * 1000 / CARD_BACK_PX_S);
  lv_anim_set_repeat_delay(&a, CARD_HOLD_MS);
  lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
  lv_anim_start(&a);
}
// 5 s after the last touch: back to the top at CARD_BACK_PX_S, then the normal cycle.
inline void resume_card_anim() {
  Ui &u = ui();
  u.card_manual = false;
  int32_t y = lv_obj_get_scroll_top(u.card_text);
  int dist = y + lv_obj_get_scroll_bottom(u.card_text);  // content overflow, constant while scrolling
  if (y <= 0) return start_card_anim(dist);
  lv_anim_t a = make_anim(u.card_text, card_scroll_y, y, 0, y * 1000 / CARD_BACK_PX_S, 0, lv_anim_path_ease_in_out);
  lv_anim_set_user_data(&a, (void *) (intptr_t) dist);
  lv_anim_set_completed_cb(&a, [](lv_anim_t *an) { start_card_anim((int) (intptr_t) lv_anim_get_user_data(an)); });
  lv_anim_start(&a);
}

// Raw text in: one line, capped at RECAP_MAX codepoints.
inline void show_card(const std::string &raw, uint32_t now) {
  Ui &u = ui();
  const std::string text = cut(one_line(raw), RECAP_MAX);
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
    int lines = full / CARD_PITCH + 1;
    u.card_lines = std::min(lines, 4);
    u.card_overflow = lines > 4;
    u.card_shown_at = now;
    u.card_pass_ms = CARD_SHORT_MS;
    if (lines > 4) {
      lv_obj_set_height(u.card_text, 4 * CARD_PITCH);
      int dist = full - 4 * CARD_PITCH;
      start_card_anim(dist);
      u.card_pass_ms = 2 * CARD_HOLD_MS + dist * 1000 / CARD_SCROLL_PX_S + dist * 1000 / CARD_BACK_PX_S;
    }
  }
  int h = u.card_lines * CARD_PITCH + 2 * 21;
  int top = 176 + (200 - h) / 2;  // centred in y 176-376 (focus.c:49-56)
  lv_obj_set_y(u.card, top);
  place_dismiss(top);
}

// Dismiss button and the reply card, built once in init().
inline void build_card(lv_obj_t *root) {
  Ui &u = ui();
  u.dismiss = mk_button(root, u.f.f25, "Dismiss");
  lv_obj_add_event_cb(u.dismiss, on_dismiss, LV_EVENT_CLICKED, nullptr);
  u.card = mk_card(root);
  lv_obj_set_clickable(u.card, true);
  lv_obj_add_event_cb(u.card, on_card, LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(u.card, on_hold, LV_EVENT_LONG_PRESSED, nullptr);  // hold on a reply talks to its session
  u.card_text = mk_label(u.card, u.f.f20, RECAP);
  lv_obj_set_width(u.card_text, 348);  // card 368 - 2 x 10 pad
  lv_obj_set_style_text_line_space(u.card_text, CARD_PITCH - lv_font_get_line_height(u.f.f20), 0);
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
}

}  // namespace buddyui
