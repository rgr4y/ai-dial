#pragma once
#include "ui_widgets.h"

namespace buddyui {

// Finger up (or, for an ask, the timer / a tap): keep the mic open for the tail, then wait for the final text.
inline void end_listen(uint32_t now) {
  Ui &u = ui();
  if (u.on_sound) u.on_sound(Sound::END);
  u.voice = Voice::WAIT;
  u.voice_ask = false;
  u.voice_at = now;
  u.voice_stop_at = now + VOICE_TAIL_MS;
}

// ---- voice ----
inline void voice_note(const char *t, uint32_t now) {
  Ui &u = ui();
  if (u.on_sound) u.on_sound(Sound::ERROR);
  u.voice = Voice::NOTE;
  u.voice_text = t;
  u.voice_at = now;
}
// Open the mic and enter LISTEN; ask marks the bridge's follow-up question (no finger, ends on a timer or a tap).
inline void start_listen(uint32_t now, bool ask) {
  Ui &u = ui();
  if (!u.on_voice || !u.on_voice(true)) {
    voice_note("Voice is offline", now);
    return;
  }
  u.voice = Voice::LISTEN;
  u.voice_ask = ask;
  u.voice_at = now;
  u.voice_stopped = false;
  if (u.on_sound) u.on_sound(Sound::LISTENING);
}
// The session the face is about: the focused working one, an opened notice's, or the one whose reply is on the card.
// Empty when the face is about nobody: the bridge router then needs a session name.
inline std::string voice_target() {
  Ui &u = ui();
  ClaudeBuddy *b = u.buddy;
  if (u.viewing) return u.notice.sid;
  if (b->snap().running > 0) return u.focus_id;
  if (!b->last_text().empty() && b->last_text_ms() > u.recap_cleared) return b->last_sid();
  return "";
}
// Hold on the face (spec §3, 650 ms): start listening. The release is polled in tick().
inline void on_hold(lv_event_t *) {
  Ui &u = ui();
  uint32_t now = lv_tick_get();
  if (u.voice != Voice::OFF || u.view != View::FACE || u.asleep) return;
  activity(now);
  lv_point_t at = {};
  if (lv_indev_t *in = lv_indev_active()) lv_indev_get_point(in, &at);
  auto inside = [](lv_point_t p) { return hypotf(p.x - (float) scene::C, p.y - (float) scene::C) < VOICE_EDGE_R; };
  if (!inside(u.press_pt) || !inside(at)) return;  // a grip at the rim, not a hold on the face
  u.voice_sid = voice_target();
  u.voice_text.clear();
  u.voice_ask = false;
  // No session on the face (idle, card dismissed) still listens: the bridge's router picks one by name, else asks.
  start_listen(now, false);
  update(now);
}
// Stop capture once, whatever state the voice flow ends in.
inline void voice_stop_mic() {
  Ui &u = ui();
  if (u.voice_stopped) return;
  u.voice_stopped = true;
  if (u.on_voice) u.on_voice(false);
}

inline void voice_step(uint32_t now) {
  Ui &u = ui();
  ClaudeBuddy *b = u.buddy;
  // Voice: a bridge line (words, final, error), then the timers: confirm -> send, wait / note give up, send -> off.
  const auto &vl = b->voice();
  if (vl.seq != u.voice_seen) {
    u.voice_seen = vl.seq;
    if (u.voice == Voice::WAIT && !vl.ask.empty()) {
      u.voice_text = vl.ask;  // the question stays on the card while the mic re-arms
      start_listen(now, true);
    } else if ((u.voice == Voice::LISTEN || u.voice == Voice::WAIT) && vl.error.empty() && !vl.final) {
      u.voice_text = vl.text;
    } else if (u.voice == Voice::WAIT && !vl.error.empty()) {
      voice_note(vl.error.c_str(), now);
    } else if (u.voice == Voice::WAIT && vl.final) {
      if (vl.text.empty()) {
        voice_note("Didn't catch that", now);
      } else {
        u.voice_text = vl.text;
        u.voice = Voice::CONFIRM;
        u.voice_at = now;
      }
    }
  }
  if (u.voice == Voice::SEND && now - u.voice_at >= VOICE_SEND_MS) u.voice = Voice::OFF;
  else if (u.voice == Voice::CONFIRM && now - u.voice_at >= VOICE_CONFIRM_MS) {
    u.voice = Voice::SEND;  // held VOICE_SEND_MS so the bridge sees it: it types the text; the face plays "Sent"
    u.voice_at = now;
    u.sent_at = now;
    u.last_deny = false;
  } else if (u.voice == Voice::WAIT && now - u.voice_at > VOICE_WAIT_MS) voice_note("No transcript", now);
  else if (u.voice == Voice::NOTE && now - u.voice_at >= VOICE_NOTE_MS) u.voice = Voice::OFF;
}

}  // namespace buddyui
