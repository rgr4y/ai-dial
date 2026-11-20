#pragma once
// The FACE view of buddyui::update(): the running and idle faces, the reply card, voice, name arc, usage flash, bell.
#include "ui_card.h"
#include "ui_pages.h"
#include "sessions.h"

namespace buddyui {

// Locals the face arms and the tails after them share.
struct Face {
  const esphome::claude_buddy::Bot *shown = nullptr;  // a Grok bot on the face instead of Clawd: its name tops the face
  const char *name = "Claude";
  std::string title;  // the focused session's name, when a working session is on the face
  uint32_t title_rgb = NAME;
  bool bell_ok = false;  // the bell pill may show (not over a card)
};

inline void face_running(Vis &vis, const Frame &f, Face &fc) {
  Ui &u = ui();
  const auto &s = u.buddy->snap();
  auto &notices = u.buddy->notices();
  uint32_t now = f.now;
  int fi = f.fi;
  const esphome::claude_buddy::Work *fw = f.fw;
  bool waiting = s.waiting > 0;
  fc.bell_ok = true;
  // The focused session's look (heartbeat `work`): a Grok Bot session shows its own bot (`avatar`), Codex and Copilot
  // their pets (`agent`), else Clawd cooks or builds.
  static const esphome::claude_buddy::Bot no_bot;
  static const std::string no_agent;
  const auto &av = fw ? fw->avatar : no_bot;
  const std::string &ag = fw ? fw->agent : no_agent;
  if (fw) {
    fc.title = sessions::label(s.work, fi);
    fc.title_rgb = !av.shape.empty() ? av.rgb : provider_ink(ag.empty() ? "claude" : ag);
  }
  const scene::Scene *sc = av.shape.empty() ? nullptr : character::find(("grok_" + av.shape).c_str());
  const char *agent = agent_name(ag);
  const scene::Scene *cx = agent ? character::find((ag + "_work").c_str()) : nullptr;
  if (sc) {
    u.scene.play(sc, now, 4);
    u.scene.set_tint(av.rgb);
  } else if (cx) {
    u.scene.play(sc = cx, now, 4);
    fc.name = agent;
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
  // Live line "<verb>… m:ss" in voice green (focus.c:764-791). Verb: the focused session's tool, else a gerund.
  char el[16], line[64];
  fmt_elapsed(el, sizeof(el), now - u.work_start);
  const std::string &said = fw ? fw->tool : std::string();
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
}

inline void face_idle(Vis &vis, const Frame &f, Face &fc) {
  Ui &u = ui();
  ClaudeBuddy *b = u.buddy;
  const auto &s = b->snap();
  auto &notices = b->notices();
  uint32_t now = f.now;
  bool live = f.live;
  bool waiting = s.waiting > 0;
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
    fc.shown = g;
  } else {
    if (p >= n) p = 0;  // a bot whose shape isn't in the pack: Clawd covers its turn
    u.scene.play(pets[p], now, 4);
    if (names[p]) fc.name = names[p];
  }
  arc_text(u.status_arc, waiting || !notices.empty() ? "" : u.hint, RELAX_LINE);  // hint left out while the bell shows
  fc.bell_ok = true;
}

inline void paint_face(Vis &vis, const Frame &f) {
  Ui &u = ui();
  ClaudeBuddy *b = u.buddy;
  const auto &s = b->snap();
  auto &notices = b->notices();
  uint32_t now = f.now;
  bool running = f.running, sending = f.sending;
  Face fc;  // fc.shown: a Grok bot on the face instead of Clawd, its name tops the face
  bool recap = !running && !b->last_text().empty() && b->last_text_ms() > u.recap_cleared;
  // The reply card names whoever wrote it: a Grok bot (its name, in its colour), Codex, Copilot, else Claude.
  auto who = [&](const esphome::claude_buddy::Bot &bot, const std::string &agent) {
    if (!bot.shape.empty()) fc.shown = &bot;
    else if (const char *n = agent_name(agent)) fc.name = n;
  };
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
      if (s.work[i].id == u.voice_sid) fc.title = sessions::label(s.work, (int) i);
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
    face_running(vis, f, fc);
  } else {
    face_idle(vis, f, fc);
  }
  if (!fc.title.empty()) arc_text(u.name_arc, fc.title.c_str(), fc.title_rgb);
  else if (fc.shown && !fc.shown->name.empty()) arc_text(u.name_arc, fc.shown->name.c_str(), fc.shown->rgb);
  else arc_text(u.name_arc, fc.name, provider_ink(fc.name, NAME));
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
  if (fc.bell_ok && (held || news)) {
    char n[32];  // ponytail: no bell glyph (Harness uses FontAwesome)
    if (held && news) snprintf(n, sizeof(n), "%u waiting \xC2\xB7 %u new", held, news);
    else if (held) snprintf(n, sizeof(n), "%u waiting", held);
    else snprintf(n, sizeof(n), "%u new", news);
    set_text(u.bell_text, n);
    vis.on(u.bell);
  }
}

}  // namespace buddyui
