#pragma once
// Claude desktop Hardware Buddy over BLE NUS (encrypted, bonded, passkey shown on device).
// Everything here runs on the main loop: ESP32BLE queues the Bluedroid callbacks and dispatches them from loop().
#include <string>
#include <vector>

#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/components/esp32_ble/ble.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"

#include <esp_gap_ble_api.h>
#include <esp_gatts_api.h>

namespace esphome::claude_buddy_ble {

// Link state, in the order the pairing flow walks through it.
enum class Link : uint8_t {
  ADVERTISING,  // nobody connected: show "Claude-XXXX" + how to connect
  CONNECTED,    // connected, not encrypted yet
  PASSKEY,      // first pairing: show the 6-digit passkey until the OS accepts it
  SECURE,       // encrypted (fresh bond or stored LTK)
  FAILED,       // pairing rejected / wrong passkey; back to ADVERTISING on disconnect
};

struct Snapshot {
  uint32_t total = 0, running = 0, waiting = 0;
  std::string msg;
  std::vector<std::string> entries;  // newest first
  uint64_t tokens = 0, tokens_today = 0;
  std::string prompt_id, prompt_tool, prompt_hint;  // prompt_id empty = no permission pending
  uint32_t at_ms = 0;                               // millis() of the last heartbeat; 0 = never
};

class ClaudeBuddy : public Component, public Parented<esp32_ble::ESP32BLE> {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_BLUETOOTH; }

  void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param);
  void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param);

  void set_name_prefix(const std::string &p) { this->name_prefix_ = p; }
  void set_battery_level(sensor::Sensor *s) { this->bat_pct_ = s; }
  void set_battery_voltage(sensor::Sensor *s) { this->bat_v_ = s; }
  void set_charging(binary_sensor::BinarySensor *s) { this->charging_ = s; }
  void add_on_update_callback(std::function<void()> &&cb) { this->update_cb_.add(std::move(cb)); }

  // --- for YAML lambdas ---
  Link link() const { return this->link_; }
  uint32_t passkey() const { return this->passkey_; }
  const std::string &adv_name() const { return this->adv_name_; }
  // Two sources fill the same screen: Claude desktop over BLE, and the dial bridge daemon over the ESPHome API
  // (bridge/). The bridge wins while it is fresh, unless only the desktop has a prompt waiting.
  const Snapshot &snap() const { return this->snaps_[this->active_()]; }
  bool has_prompt() const { return !this->snap().prompt_id.empty(); }
  // A line from the bridge (api action buddy_line), same JSON as the BLE protocol.
  void feed_line(const std::string &line) { this->handle_line_(line, API); }
  void set_decision_sensor(text_sensor::TextSensor *t) { this->decision_ = t; }
  // Heartbeats come at least every 10 s; 30 s of silence means the desktop is gone.
  bool live() const;
  const std::string &last_text() const { return this->last_text_; }  // latest assistant text from evt:turn
  uint32_t last_text_ms() const { return this->last_text_ms_; }
  const std::string &owner() const { return this->owner_; }
  const std::string &device_name() const { return this->device_name_; }
  void approve() { this->decide_("once"); }
  void deny() { this->decide_("deny"); }
  void unpair();  // forget every bond; next connect shows a fresh passkey

 protected:
  void notify_();
  void check_(const char *what, esp_err_t err);
  bool status_ok_(const char *what, esp_gatt_status_t st);
  void send_(const std::string &line);
  void decide_(const char *decision);
  enum Source : uint8_t { BLE = 0, API = 1 };
  void handle_line_(const std::string &line, Source src = BLE);
  bool fresh_(Source src) const;
  Source active_() const;
  void ack_(const char *cmd, bool ok = true);
  void send_status_();

  std::string name_prefix_, adv_name_, device_name_, owner_;
  sensor::Sensor *bat_pct_{nullptr}, *bat_v_{nullptr};
  binary_sensor::BinarySensor *charging_{nullptr};
  CallbackManager<void()> update_cb_;

  bool started_{false};
  enum class Stage : uint8_t { REGISTERING, CREATING, ADD_TX, ADD_CCCD, ADD_RX, STARTING, RUNNING };
  Stage stage_{Stage::REGISTERING};
  esp_gatt_if_t gatts_if_{ESP_GATT_IF_NONE};
  uint16_t svc_handle_{0}, tx_handle_{0}, cccd_handle_{0}, rx_handle_{0};
  uint16_t conn_id_{0xFFFF}, mtu_{23};
  esp_bd_addr_t peer_{};
  bool notify_on_{false};

  Link link_{Link::ADVERTISING};
  uint32_t passkey_{0};
  std::string rx_buf_;
  std::string answered_id_;  // last prompt decided on this device
  Snapshot snaps_[2];
  text_sensor::TextSensor *decision_{nullptr};
  std::string last_text_;
  uint32_t last_text_ms_{0};
  bool was_live_{false};
  uint32_t approvals_{0}, denials_{0};
};

class UpdateTrigger : public Trigger<> {
 public:
  explicit UpdateTrigger(ClaudeBuddy *parent) {
    parent->add_on_update_callback([this]() { this->trigger(); });
  }
};

}  // namespace esphome::claude_buddy_ble
