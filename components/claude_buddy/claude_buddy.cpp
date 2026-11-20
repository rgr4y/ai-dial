#include "claude_buddy.h"

#include "esphome/components/json/json_util.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#include <cstring>

namespace esphome::claude_buddy {

static const char *const TAG = "claude_buddy";
static constexpr uint32_t DEAD_MS = 30000;

bool ClaudeBuddy::live() const { return this->snap_.at_ms != 0 && millis() - this->snap_.at_ms < DEAD_MS; }

void ClaudeBuddy::loop() {
  bool l = this->live();
  if (l != this->was_live_) {
    this->was_live_ = l;
    ESP_LOGI(TAG, "Bridge %s", l ? "live" : "gone");
    this->notify_();
  }
}

void ClaudeBuddy::queue_notice(Notice n) {
  if (this->notices_.size() >= 9)
    this->notices_.erase(this->notices_.begin());
  this->notices_.push_back(std::move(n));
}

void ClaudeBuddy::decide_(const char *decision) {
  if (this->snap_.prompt_id.empty())
    return;
  // The bridge subscribes to this text sensor: "<prompt id> once|deny".
  if (this->decision_)
    this->decision_->publish_state(this->snap_.prompt_id + " " + decision);
  this->answered_id_ = this->snap_.prompt_id;  // one decision per prompt
  ESP_LOGI(TAG, "Permission %s: %s", this->snap_.prompt_id.c_str(), decision);
  // Optimistic: drop the prompt now; the next heartbeat confirms.
  this->snap_.prompt_id.clear();
  this->snap_.prompt_tool.clear();
  this->snap_.prompt_hint.clear();
  this->notify_();
}

void ClaudeBuddy::feed_line(const std::string &line) {
  JsonDocument doc = json::parse_json(line);
  if (doc.isNull()) {
    ESP_LOGW(TAG, "Bad JSON (%u bytes)", (unsigned) line.size());
    return;
  }

  // Heartbeat snapshot. A heartbeat without `prompt` means no permission is pending.
  if (doc["total"].is<uint32_t>()) {
    Snapshot &s = this->snap_;
    s.total = doc["total"] | 0u;
    s.running = doc["running"] | 0u;
    s.waiting = doc["waiting"] | 0u;
    s.msg = doc["msg"] | "";
    s.entries.clear();
    for (JsonVariant e : doc["entries"].as<JsonArray>())
      s.entries.emplace_back(e | "");
    s.usage = doc["usage"] | "";
    s.usage_pct = doc["usage_pct"] | 0u;
    s.usages.clear();
    s.usages_key.clear();
    if (!doc["usages"].isNull()) {
      serializeJson(doc["usages"], s.usages_key);
      for (JsonObject c : doc["usages"].as<JsonArray>()) {
        UsageCard uc;
        uc.name = c["n"] | "";
        for (JsonArray w : c["w"].as<JsonArray>())
          uc.w.push_back({w[0] | "", w[1] | 0u, w[2] | ""});
        s.usages.push_back(std::move(uc));
      }
    }
    s.assets_url = doc["assets"]["url"] | "";
    s.assets_rev = doc["assets"]["rev"] | "";
    s.agent = doc["agent"] | "";
    s.avatar = {doc["avatar"] | "", doc["avatar_color"] | 0xFFFFFFu, doc["avatar_name"] | ""};
    s.bots.clear();
    for (JsonArray b : doc["bots"].as<JsonArray>())
      s.bots.push_back({b[0] | "", b[1] | 0xFFFFFFu, b[2] | ""});
    s.work.clear();
    for (JsonObject w : doc["work"].as<JsonArray>())
      s.work.push_back({w["id"] | "", w["name"] | "", w["tool"] | "", w["agent"] | "",
                        {w["avatar"] | "", w["avatar_color"] | 0xFFFFFFu, w["name"] | ""}});
    s.latest = doc["latest"] | "";
    JsonObject p = doc["prompt"];
    s.prompt_id = p ? (p["id"] | "") : "";
    if (!s.prompt_id.empty() && s.prompt_id == this->answered_id_)
      s.prompt_id.clear();  // already answered here; the bridge just hasn't caught up
    if (!s.prompt_id.empty())
      this->answered_id_.clear();
    s.prompt_tool = !s.prompt_id.empty() ? (p["tool"] | "") : "";
    s.prompt_hint = !s.prompt_id.empty() ? (p["hint"] | "") : "";
    s.at_ms = millis();
    if (s.at_ms == 0)
      s.at_ms = 1;
    this->notify_();
    return;
  }

  if (const char *evt = doc["evt"]) {
    // Voice: partial words, the final text, or an error (docs/DIAL-PROTOCOL.md "Voice").
    if (strcmp(evt, "voice") == 0) {
      VoiceLine &v = this->voice_;
      v.final = !doc["final"].isNull();
      v.text = v.final ? (doc["final"] | "") : (doc["text"] | "");
      v.error = doc["error"] | "";
      v.seq++;
      this->notify_();
      return;
    }
    // Turn event: the assistant's text shows on the done card.
    if (strcmp(evt, "turn") == 0 && strcmp(doc["role"] | "", "assistant") == 0) {
      for (JsonVariant b : doc["content"].as<JsonArray>()) {
        const char *t = b["text"];
        if (t && *t && (doc["busy"] | 0u) > 0) {
          // Other sessions are still working: keep the face on them and queue this reply (the bell).
          this->queue_notice({doc["sid"] | "", t, doc["agent"] | "",
                              {doc["avatar"] | "", doc["avatar_color"] | 0xFFFFFFu, doc["avatar_name"] | ""}});
          this->notify_();
          break;
        }
        if (t && *t) {
          this->last_text_ = t;
          this->last_text_ms_ = millis();
          this->last_bot_ = {doc["avatar"] | "", doc["avatar_color"] | 0xFFFFFFu, doc["avatar_name"] | ""};
          this->last_agent_ = doc["agent"] | "";
          this->last_sid_ = doc["sid"] | "";
          this->notify_();
          break;
        }
      }
    }
  }
}

}  // namespace esphome::claude_buddy
