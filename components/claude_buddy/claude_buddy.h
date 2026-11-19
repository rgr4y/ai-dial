#pragma once
// Claude Buddy state on the dial, fed by the dial bridge (bridge/dial-bridge.mjs) over the ESPHome native API.
// Lines are the Claude desktop Hardware Buddy JSON (heartbeat snapshot, evt:turn); see docs/DIAL-PROTOCOL.md.
// Permission answers go back out on a text sensor the bridge subscribes to.
// (The BLE transport for Claude desktop is kept, unconfigured, in components/claude_buddy_ble.)
#include <string>
#include <vector>

#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/components/text_sensor/text_sensor.h"

namespace esphome::claude_buddy {

struct Snapshot {
  uint32_t total = 0, running = 0, waiting = 0;
  std::string msg;
  std::vector<std::string> entries;  // newest first
  uint64_t tokens = 0, tokens_today = 0;
  std::string prompt_id, prompt_tool, prompt_hint;  // prompt_id empty = no permission pending
  std::string usage;      // plan usage line from the bridge, e.g. "5h 15% · wk 19% · 2h 41m"
  uint32_t usage_pct = 0; // the higher of the 5-hour and weekly utilisation
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
  const Snapshot &snap() const { return this->snap_; }
  bool has_prompt() const { return !this->snap_.prompt_id.empty(); }
  // The bridge sends a heartbeat at least every 10 s; 30 s of silence means it is gone.
  bool live() const;
  const std::string &last_text() const { return this->last_text_; }  // latest assistant text from evt:turn
  uint32_t last_text_ms() const { return this->last_text_ms_; }
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
  bool was_live_{false};
};

class UpdateTrigger : public Trigger<> {
 public:
  explicit UpdateTrigger(ClaudeBuddy *parent) {
    parent->add_on_update_callback([this]() { this->trigger(); });
  }
};

}  // namespace esphome::claude_buddy
