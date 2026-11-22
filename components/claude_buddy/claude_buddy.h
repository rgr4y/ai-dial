#pragma once
// Claude Buddy state on the dial, fed by the dial bridge (bridge/dial-bridge.mjs) over the ESPHome native API.
// Lines are the Claude desktop Hardware Buddy JSON (heartbeat snapshot, evt:turn); see docs/DIAL-PROTOCOL.md.
// Permission answers go back out on a text sensor the bridge subscribes to.
#include <string>
#include <vector>

#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/components/text_sensor/text_sensor.h"

namespace esphome::claude_buddy {

// A Grok Bot bot: art/grok shape (pack scene "grok_<shape>"), colour 0xRRGGBB, name.
struct Bot {
  std::string shape;
  uint32_t rgb = 0xFFFFFF;
  std::string name;
};

// A working session (heartbeat `work`): id, display name, current tool, and its look: agent ("codex") or a Grok bot.
struct Work {
  std::string id, name, tool, agent;
  Bot avatar;  // shape empty unless a Grok bot session
};

// A reply from a session while other sessions were still working (evt:turn with `busy`): the dial shows it as a notice.
struct Notice {
  std::string sid, text, agent;
  Bot bot;
  uint32_t at = 0;  // millis() it arrived (or the card's reply time); loop() drops it after NOTICE_MAX_AGE_MS
};

// The last {"evt":"voice"} line from the bridge: partial words (text), the final (final = true), or an error to show.
// seq counts lines, so the UI can tell a new one from the same text twice.
struct VoiceLine {
  std::string text, error, ask;  // ask: the bridge's question; the dial speaks nothing, it listens again
  bool final = false;
  uint32_t seq = 0;
};

// One provider on the usage screen: rate windows of (label, percent used, time to reset).
struct UsageCard {
  struct Win {
    std::string label;
    uint32_t pct;
    std::string reset;
  };
  std::string name;
  std::vector<Win> w;
};

struct Snapshot {
  uint32_t total = 0, running = 0, waiting = 0;
  std::string prompt_id, prompt_tool, prompt_hint;  // prompt_id empty = no permission pending
  std::string usage;      // plan usage line from the bridge, e.g. "5h 15% · wk 19% · 2h 41m"
  uint32_t usage_pct = 0; // the higher of the 5-hour and weekly utilisation
  std::vector<UsageCard> usages;  // only while the dial shows the usage screen (bridge sends it then)
  std::string usages_key;         // usages as raw JSON: the UI rebuilds when it changes
  std::string assets_url, assets_rev;  // character pack the bridge serves (buddy/character.h)
  std::vector<Bot> bots;  // every known Grok bot: the idle face cycles them
  std::vector<Work> work;     // every working session, in start order
  std::string latest;         // id of the working session active last: focus falls back to it
  uint32_t at_ms = 0;                               // millis() of the last heartbeat; 0 = never
};

class ClaudeBuddy : public Component {
 public:
  void loop() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  void set_decision_sensor(text_sensor::TextSensor *t) { this->decision_ = t; }
  void add_on_update_callback(std::function<void()> &&cb) { this->update_cb_.add(std::move(cb)); }

  // --- for YAML lambdas ---
  void feed_line(const std::string &line);  // api action buddy_line
  bool mute_api = false;  // sim: a pinned scenario (claude-buddy-sim.yaml, build/sim-pin.jsonl) owns the face
  const Snapshot &snap() const { return this->snap_; }
  bool has_prompt() const { return !this->snap_.prompt_id.empty(); }
  // The bridge sends a heartbeat at least every 10 s; 30 s of silence means it is gone.
  bool live() const;
  const std::string &last_text() const { return this->last_text_; }  // latest assistant text from evt:turn
  uint32_t last_text_ms() const { return this->last_text_ms_; }
  // Who wrote last_text: a Grok bot (shape set), another agent ("codex", "copilot"), or Claude Code (both empty).
  const Bot &last_bot() const { return this->last_bot_; }
  const std::string &last_agent() const { return this->last_agent_; }
  const std::string &last_sid() const { return this->last_sid_; }  // session that wrote last_text
  const VoiceLine &voice() const { return this->voice_; }
  std::vector<Notice> &notices() { return this->notices_; }  // oldest first; the UI pops them when read
  // Queue a notice (bell), dropping the oldest past 9.
  void queue_notice(Notice n);
  // The reply on the card (last_text) becomes a notice, as if it had come in while other sessions worked. A voice
  // conversation's reply (`read`) is already read: it just leaves.
  void queue_last_reply() {
    if (!this->last_read_)
      this->queue_notice({this->last_sid_, this->last_text_, this->last_agent_, this->last_bot_, this->last_text_ms_});
  }
  void approve() { this->decide_("once"); }
  void deny() { this->decide_("deny"); }

 protected:
  void notify_() { this->update_cb_.call(); }
  void decide_(const char *decision);

  text_sensor::TextSensor *decision_{nullptr};
  CallbackManager<void()> update_cb_;
  Snapshot snap_;
  std::string answered_id_;  // last prompt decided on this device
  std::string last_text_;
  uint32_t last_text_ms_{0};
  bool last_read_{false};  // last_text answers a voice send (evt:turn `read`)
  Bot last_bot_;
  std::string last_agent_, last_sid_;
  VoiceLine voice_;
  std::vector<Notice> notices_;
  bool was_live_{false};
};

class UpdateTrigger : public Trigger<> {
 public:
  explicit UpdateTrigger(ClaudeBuddy *parent) {
    parent->add_on_update_callback([this]() { this->trigger(); });
  }
};

}  // namespace esphome::claude_buddy
