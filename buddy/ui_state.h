#pragma once
// Ui state, theme and timings for ui.h (split out; see ui.h for the overview).
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
#include "sessions.h"
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
// Buttons (mk_button): filled, so they don't read as cards. Fill / label per kind; pressed darkens the fill.
// Primary: the default action (Dismiss). Warning: needs a second thought. Error: cancel, deny, destructive.
enum class Btn { PRIMARY, WARNING, ERROR };
struct BtnInk { uint32_t fill, text; };
static constexpr BtnInk BTN_INK[] = {{0x7c5cc4, 0xffffff}, {0xd9902f, 0x1a1206}, {0xc94a5a, 0xffffff}};

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
// Voice edge rejection: a hold starts listening only if it began and is still inside this radius (px from the centre,
// screen radius 233). touch-test.yaml: picking the dial up lands at r 173-240, sometimes held 0.5-1 s.
static constexpr float VOICE_EDGE_R = 160;
static constexpr uint32_t SLEEP_MS = 300000;    // screen auto-sleep (display_habitat.c:405)
static constexpr uint32_t USB_SLEEP_MS = 900000;  // on USB the screen goes dark after this long truly idle (AMOLED burn-in)
static constexpr uint32_t BOOT_AWAKE_MS = 60000; // never idle-sleep in the first minute after boot
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
static constexpr uint32_t CARD_HOLD_MS = 2500, CARD_SCROLL_PX_S = 60, CARD_BACK_PX_S = 120;
// Finger-scrolled card: autoscroll resumes this long after the last touch or scroll movement.
static constexpr uint32_t CARD_MANUAL_MS = 5000;
// A reply card moves to the bell once read: untouched, after one full autoscroll pass (down and back), or after
// CARD_SHORT_MS if it fits without scrolling. Once touched, after REPLY_TO_BELL_MS from the last touch. USB or battery.
static constexpr uint32_t CARD_SHORT_MS = 10000, REPLY_TO_BELL_MS = 120000;

// Voice (hold the face): LISTEN while the finger is down, WAIT for the bridge's final text, CONFIRM shows it (a tap
// cancels), SEND for one update (the bridge types it into the session), NOTE a short message. An "ask" line from the
// bridge (it could not pick a session) shows the question and listens again with no hold, VOICE_ASK_MS or until a tap.
// Protocol: DIAL-PROTOCOL.md.
// tail: mic keeps running this long after release (buffered words). SEND is held VOICE_SEND_MS: the API batches state
// (100 ms, latest value per entity), so a one-tick SEND was often replaced by OFF before the bridge saw it.
static constexpr uint32_t VOICE_CONFIRM_MS = 2000, VOICE_TAIL_MS = 300, VOICE_WAIT_MS = 10000, VOICE_NOTE_MS = 2000,
                          VOICE_MAX_MS = 60000, VOICE_ASK_MS = 4000, VOICE_SEND_MS = 500;
enum class Voice : uint8_t { OFF, LISTEN, WAIT, CONFIRM, SEND, NOTE };
static const char *const VOICE_NAMES[] = {"off", "listening", "wait", "confirm", "send", "note"};

struct Fonts { const lv_font_t *f20, *f25, *f26m, *f30, *f48b, *f96b; };

enum class View : uint8_t { BRAND, FACE, PERMISSION, USAGE, BATTERY, OTA };
static const char *const VIEW_NAMES[] = {"brand", "face", "permission", "usage", "battery", "ota"};

enum class Sound { LISTENING, END, ERROR };  // earcons, played by the YAML on_sound callback
struct Ui : power::Screen {  // asleep, last_activity, wake/sleep requests: power.h
  Fonts f{};
  lv_obj_t *root = nullptr;
  lv_obj_t *name_arc = nullptr, *status_arc = nullptr;
  scene::Player scene, alert;
  bool last_deny = false;                   // the last answer was a deny: play deny_send (else send)
  int card_lines = 0;  // recap card height in lines (1-4), set when its text changes
  bool card_overflow = false, card_manual = false;  // text past 4 lines; finger has taken over the scroll
  uint32_t card_manual_at = 0;                      // last touch / scroll movement while manual
  uint32_t card_shown_at = 0, card_pass_ms = 0;     // card text last changed; its read time (one pass, or CARD_SHORT_MS)
  lv_obj_t *dismiss = nullptr, *card = nullptr, *card_text = nullptr, *bell = nullptr, *bell_text = nullptr;
  lv_obj_t *word = nullptr, *sub = nullptr;                                  // brand (bridge offline)
  lv_obj_t *perm = nullptr, *perm_title = nullptr, *perm_hint = nullptr, *perm_count = nullptr;
  lv_obj_t *btn_deny = nullptr, *btn_allow = nullptr;
  // Page dots (more than one working session): one per session, the focused one lit. Only DOTS_MAX are drawn.
  static constexpr int DOTS_MAX = 8, DOT_PITCH = 18, DOT_D = 12;
  lv_obj_t *dots = nullptr, *dot[DOTS_MAX] = {};
  std::string dots_txt;  // "2/3": what the dots show, also the mirror's `dots`
  lv_point_t press_pt = {};  // where the current touch began (voice edge rejection)
  std::string focus_id;  // the working session the face shows (sticky: only a swipe, the bell or its end moves it)
  bool viewing = false;  // a notice's reply is on the card (opened with the bell)
  esphome::claude_buddy::Notice notice;  // that reply
  size_t notice_n = 0;   // notices seen so far: a new one wakes the dial
  Voice voice = Voice::OFF;
  std::string voice_text, voice_sid;  // words on the card; the session they will be typed into (fixed at the hold)
  uint32_t voice_at = 0, voice_stop_at = 0, voice_seen = 0;  // state entered; when to stop the mic; bridge lines seen
  bool voice_stopped = true;
  bool voice_ask = false;  // this LISTEN is the bridge's follow-up question: no finger, ends on a timer or a tap
  std::function<void(Sound)> on_sound;  // YAML: play an earcon on the speaker; unset (sim) = silent
  std::function<bool(bool)> on_voice;  // YAML: start (true) / stop (false) capture; start returns false if it can't
  lv_obj_t *usage = nullptr;  // usage screen: rebuilt from the snapshot's cards (build_usage)
  static constexpr int BURST_N = 18;
  lv_obj_t *burst[BURST_N] = {};     // bridge connected: green sparks fly out from the centre
  int16_t burst_r[BURST_N] = {};
  lv_obj_t *alarm = nullptr;  // usage >= USAGE_ALARM_PCT: big red "87%" pulsing in place of the pet

  ClaudeBuddy *buddy = nullptr;
  View view = View::BRAND;
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
  uint32_t idle_turn = 0;             // idle provider cycle position: steps only while the idle face is up
  uint32_t idle_k = 0, idle_seen = 0; // usage-flash slot (now / USAGE_EVERY_MS) and time of the last idle update
  uint32_t sent_at = 0;               // claude_send plays until sent_at + SEND_MS
  const char *hint = "What's next?";
  lv_obj_t *shield = nullptr;          // while dark: eats touches so a wake tap can't hit Allow
  std::function<int()> battery;       // YAML: battery % when a cell is connected (also while charging), -1 otherwise
  std::function<bool()> on_usb;       // YAML: true when VBUS is present; unset = battery rules
  bool was_live = false;               // the bridge has talked since boot (splash says "offline", not "looking")
  std::string arc_txt[2];
  uint32_t arc_col[2] = {0, 0};
  std::function<void(const std::string &)> on_state;  // mirror JSON out (YAML: buddy_ui text sensor)
  std::string last_state;
  bool shimmer = false;   // lower-arc status shimmer on (the working line)
  uint8_t shimmer_phase = 0;
};

inline Ui &ui() {
  static Ui u;
  return u;
}
inline void show(lv_obj_t *o, bool on);
inline void publish_state();
// Visibility as a diff: views say what they show (vis.on), the guard applies it once on any return.
struct Vis {
  Ui &u;
  lv_obj_t *o[11];
  bool want[11] = {};
  explicit Vis(Ui &ui_) : u(ui_), o{ui_.dismiss, ui_.card, ui_.bell, ui_.word, ui_.sub, ui_.perm, ui_.dots, ui_.alarm, ui_.usage, ui_.battery_page, ui_.ota_page} {}
  void on(lv_obj_t *x) {
    for (int i = 0; i < 11; i++)
      if (o[i] == x) want[i] = true;
  }
  ~Vis() {
    for (int i = 0; i < 11; i++) show(o[i], want[i]);
    publish_state();
  }
};
// What one update() worked out before painting; the view painters read it.
struct Frame {
  uint32_t now;
  bool live, running, entered, usb, sending;
  int fi, bat;                              // focused work index (-1: none), battery % (-1: no cell)
  const esphome::claude_buddy::Work *fw;    // the focused working session, or nullptr
};
void update(uint32_t now);
inline void tap(const std::string &t);
inline void place_bat_bolt();

}  // namespace buddyui
