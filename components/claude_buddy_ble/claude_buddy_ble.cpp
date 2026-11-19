#include "claude_buddy_ble.h"

#include "esphome/components/json/json_util.h"
#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#include <cstring>
#include <esp_bt_device.h>
#include <esp_gatt_common_api.h>
#include <esp_heap_caps.h>
#include <esp_mac.h>

namespace esphome::claude_buddy_ble {

static const char *const TAG = "claude_buddy_ble";

// A second GATTS app beside esp32_ble_server's app 0, so these attributes can be encrypted-only.
static constexpr uint16_t APP_ID = 0x42;
static constexpr size_t MAX_LINE = 8192;  // turn events are capped at 4 KB by the desktop
static constexpr uint32_t DEAD_MS = 30000;

// Nordic UART UUIDs, little-endian: 6e40000X-b5a3-f393-e0a9-e50e24dcca9e
static esp_bt_uuid_t nus_uuid(uint8_t x) {
  esp_bt_uuid_t u{};
  u.len = ESP_UUID_LEN_128;
  const uint8_t b[16] = {0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, 0x93, 0xf3, 0xa3, 0xb5, x, 0x00, 0x40, 0x6e};
  memcpy(u.uuid.uuid128, b, 16);
  return u;
}

void ClaudeBuddy::check_(const char *what, esp_err_t err) {
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "%s: %s", what, esp_err_to_name(err));
    this->mark_failed();
  }
}

bool ClaudeBuddy::status_ok_(const char *what, esp_gatt_status_t st) {
  if (st == ESP_GATT_OK)
    return true;
  ESP_LOGE(TAG, "%s failed: GATT status 0x%x", what, st);
  this->mark_failed();
  return false;
}

void ClaudeBuddy::setup() {
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_BT);
  char tail[8];
  snprintf(tail, sizeof(tail), "-%02X%02X", mac[4], mac[5]);
  this->adv_name_ = this->name_prefix_ + tail;
  this->device_name_ = this->adv_name_;
}

void ClaudeBuddy::loop() {
  if (!this->started_ && this->parent_->is_active()) {
    this->started_ = true;
    // LE Secure Connections + bonding + DisplayOnly come from esp32_ble's io_capability / auth_req_mode.
    // Distribute the encryption and identity keys both ways so a host using a private address reconnects.
    uint8_t keys = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    esp_ble_gap_set_security_param(ESP_BLE_SM_SET_INIT_KEY, &keys, sizeof(keys));
    esp_ble_gap_set_security_param(ESP_BLE_SM_SET_RSP_KEY, &keys, sizeof(keys));
    esp_ble_gatt_set_local_mtu(247);
    esp_err_t err = esp_ble_gatts_app_register(APP_ID);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "gatts_app_register: %s", esp_err_to_name(err));
      this->mark_failed();
    }
  }
  bool l = this->live();
  if (l != this->was_live_) {
    this->was_live_ = l;
    this->notify_();
  }
}

void ClaudeBuddy::dump_config() {
  ESP_LOGCONFIG(TAG, "Claude Buddy:\n  Name: %s\n  Bonds: %d", this->adv_name_.c_str(), esp_ble_get_bond_device_num());
}

bool ClaudeBuddy::fresh_(Source src) const {
  const Snapshot &s = this->snaps_[src];
  return s.at_ms != 0 && millis() - s.at_ms < DEAD_MS && (src == API || this->link_ == Link::SECURE);
}

ClaudeBuddy::Source ClaudeBuddy::active_() const {
  if (!this->fresh_(API))
    return BLE;
  if (this->fresh_(BLE) && !this->snaps_[BLE].prompt_id.empty() && this->snaps_[API].prompt_id.empty())
    return BLE;
  return API;
}

bool ClaudeBuddy::live() const { return this->fresh_(BLE) || this->fresh_(API); }

void ClaudeBuddy::notify_() { this->update_cb_.call(); }

void ClaudeBuddy::gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
  switch (event) {
    case ESP_GAP_BLE_PASSKEY_NOTIF_EVT:
      this->passkey_ = param->ble_security.key_notif.passkey;
      this->link_ = Link::PASSKEY;
      ESP_LOGI(TAG, "Passkey %06" PRIu32, this->passkey_);
      this->notify_();
      break;
    case ESP_GAP_BLE_AUTH_CMPL_EVT:
      if (this->conn_id_ == 0xFFFF)
        break;
      if (param->ble_security.auth_cmpl.success) {
        this->link_ = Link::SECURE;
        ESP_LOGI(TAG, "Link encrypted");
      } else {
        this->link_ = Link::FAILED;
        ESP_LOGW(TAG, "Pairing failed, reason 0x%x", param->ble_security.auth_cmpl.fail_reason);
      }
      this->passkey_ = 0;
      this->notify_();
      break;
    default:
      break;
  }
}

// Which NUS attribute a 128-bit UUID is: 0x01 service, 0x02 RX, 0x03 TX; 0 if not ours.
static uint8_t nus_kind(const esp_bt_uuid_t &u) {
  esp_bt_uuid_t ref = nus_uuid(0x01);
  if (u.len != ESP_UUID_LEN_128 || memcmp(u.uuid.uuid128, ref.uuid.uuid128, 12) != 0 ||
      memcmp(u.uuid.uuid128 + 13, ref.uuid.uuid128 + 13, 3) != 0)
    return 0;
  return u.uuid.uuid128[12];
}

void ClaudeBuddy::gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if,
                                      esp_ble_gatts_cb_param_t *param) {
  if (event == ESP_GATTS_REG_EVT) {
    if (param->reg.app_id != APP_ID)
      return;
    ESP_LOGI(TAG, "GATTS REG if=%d status=0x%x stage=%d", gatts_if, param->reg.status, (int) this->stage_);
    if (this->stage_ != Stage::REGISTERING)
      return;  // duplicate
    if (!status_ok_("GATTS register", param->reg.status))
      return;
    this->gatts_if_ = gatts_if;
    esp_gatt_srvc_id_t svc{};
    svc.is_primary = true;
    svc.id.uuid = nus_uuid(0x01);
    this->stage_ = Stage::CREATING;
    check_("create service", esp_ble_gatts_create_service(gatts_if, &svc, 8));
    return;
  }
  if (gatts_if != this->gatts_if_)
    return;

  // Service build, one step per event, each accepted only in its stage:
  //   CREATING -> TX char -> TX CCCD -> RX char -> start. Anything out of order is logged and dropped.
  auto unexpected = [&](const char *what) {
    ESP_LOGW(TAG, "Ignoring %s in stage %d", what, (int) this->stage_);
  };
  switch (event) {
    case ESP_GATTS_CREATE_EVT: {
      ESP_LOGI(TAG, "GATTS CREATE status=0x%x handle=%u", param->create.status, param->create.service_handle);
      if (this->stage_ != Stage::CREATING || nus_kind(param->create.service_id.id.uuid) != 0x01)
        return unexpected("CREATE");
      if (!status_ok_("create service", param->create.status))
        return;
      this->svc_handle_ = param->create.service_handle;
      esp_bt_uuid_t u = nus_uuid(0x03);
      // AUTO_RSP needs a value buffer, or add_char returns ESP_ERR_INVALID_ARG (esp_gatts_api.c:499-501).
      static uint8_t tx_init[1] = {0};
      esp_attr_value_t v{.attr_max_len = 512, .attr_len = 1, .attr_value = tx_init};
      esp_attr_control_t ctl{.auto_rsp = ESP_GATT_AUTO_RSP};
      this->stage_ = Stage::ADD_TX;
      check_("add TX", esp_ble_gatts_add_char(this->svc_handle_, &u, ESP_GATT_PERM_READ_ENC_MITM,
                                              ESP_GATT_CHAR_PROP_BIT_NOTIFY, &v, &ctl));
      break;
    }
    case ESP_GATTS_ADD_CHAR_EVT: {
      uint8_t kind = nus_kind(param->add_char.char_uuid);
      ESP_LOGI(TAG, "GATTS ADD_CHAR kind=%u status=0x%x handle=%u", kind, param->add_char.status,
               param->add_char.attr_handle);
      if (kind == 0x03 && this->stage_ == Stage::ADD_TX) {
        if (!status_ok_("add TX", param->add_char.status))
          return;
        this->tx_handle_ = param->add_char.attr_handle;
        static uint8_t cccd[2] = {0, 0};
        esp_attr_value_t v{.attr_max_len = 2, .attr_len = 2, .attr_value = cccd};
        esp_bt_uuid_t u{.len = ESP_UUID_LEN_16, .uuid = {.uuid16 = ESP_GATT_UUID_CHAR_CLIENT_CONFIG}};
        esp_attr_control_t ctl{.auto_rsp = ESP_GATT_AUTO_RSP};
        this->stage_ = Stage::ADD_CCCD;
        check_("add CCCD", esp_ble_gatts_add_char_descr(this->svc_handle_, &u,
                                                       ESP_GATT_PERM_READ_ENC_MITM | ESP_GATT_PERM_WRITE_ENC_MITM, &v,
                                                       &ctl));
      } else if (kind == 0x02 && this->stage_ == Stage::ADD_RX) {
        if (!status_ok_("add RX", param->add_char.status))
          return;
        this->rx_handle_ = param->add_char.attr_handle;
        this->stage_ = Stage::STARTING;
        check_("start service", esp_ble_gatts_start_service(this->svc_handle_));
      } else {
        unexpected("ADD_CHAR");
      }
      break;
    }
    case ESP_GATTS_ADD_CHAR_DESCR_EVT: {
      ESP_LOGI(TAG, "GATTS ADD_DESCR status=0x%x handle=%u", param->add_char_descr.status,
               param->add_char_descr.attr_handle);
      if (this->stage_ != Stage::ADD_CCCD)
        return unexpected("ADD_DESCR");
      if (!status_ok_("add CCCD", param->add_char_descr.status))
        return;
      this->cccd_handle_ = param->add_char_descr.attr_handle;
      esp_bt_uuid_t u = nus_uuid(0x02);
      static uint8_t rx_init[1] = {0};
      esp_attr_value_t v{.attr_max_len = 512, .attr_len = 1, .attr_value = rx_init};
      esp_attr_control_t ctl{.auto_rsp = ESP_GATT_AUTO_RSP};
      this->stage_ = Stage::ADD_RX;
      check_("add RX", esp_ble_gatts_add_char(this->svc_handle_, &u, ESP_GATT_PERM_WRITE_ENC_MITM,
                                              ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_WRITE_NR, &v,
                                              &ctl));
      break;
    }
    case ESP_GATTS_START_EVT:
      ESP_LOGI(TAG, "GATTS START status=0x%x handle=%u", param->start.status, param->start.service_handle);
      if (this->stage_ != Stage::STARTING || param->start.service_handle != this->svc_handle_)
        return unexpected("START");
      if (!status_ok_("start service", param->start.status))
        return;
      this->stage_ = Stage::RUNNING;
      esp_ble_gap_set_device_name(this->adv_name_.c_str());
      this->parent_->advertising_add_service_uuid(esp32_ble::ESPBTUUID::from_uuid(nus_uuid(0x01)));
      this->parent_->advertising_start();
      ESP_LOGI(TAG, "Advertising as %s", this->adv_name_.c_str());
      break;

    case ESP_GATTS_CONNECT_EVT:
      this->conn_id_ = param->connect.conn_id;
      memcpy(this->peer_, param->connect.remote_bda, sizeof(esp_bd_addr_t));
      this->link_ = Link::CONNECTED;
      this->mtu_ = 23;
      this->notify_on_ = false;
      this->rx_buf_.clear();
      // Ask for encryption now: bonded hosts resume silently, new ones get the passkey prompt.
      esp_ble_set_encryption(this->peer_, ESP_BLE_SEC_ENCRYPT_MITM);
      this->notify_();
      break;
    case ESP_GATTS_DISCONNECT_EVT:
      if (param->disconnect.conn_id != this->conn_id_)
        break;
      ESP_LOGI(TAG, "Disconnected, reason 0x%x", param->disconnect.reason);
      this->conn_id_ = 0xFFFF;
      this->link_ = Link::ADVERTISING;
      this->passkey_ = 0;
      this->snaps_[BLE] = Snapshot{};
      this->parent_->advertising_refresh();
      this->notify_();
      break;
    case ESP_GATTS_MTU_EVT:
      this->mtu_ = param->mtu.mtu;
      break;
    case ESP_GATTS_WRITE_EVT: {
      const auto &w = param->write;
      if (w.handle == this->cccd_handle_ && w.len >= 1) {
        this->notify_on_ = w.value[0] & 1;
      } else if (w.handle == this->rx_handle_) {
        if (w.is_prep)
          ESP_LOGW(TAG, "Prepared write ignored");  // ponytail: no long-write support; desktop sends <= MTU chunks
        for (uint16_t i = 0; i < w.len && !w.is_prep; i++) {
          char c = static_cast<char>(w.value[i]);
          if (c == '\n') {
            if (!this->rx_buf_.empty())
              this->handle_line_(this->rx_buf_);
            this->rx_buf_.clear();
          } else if (c != '\r') {
            if (this->rx_buf_.size() >= MAX_LINE)
              this->rx_buf_.clear();  // overlong line: drop it
            this->rx_buf_ += c;
          }
        }
      }
      break;
    }
    default:
      break;
  }
}

void ClaudeBuddy::send_(const std::string &line) {
  if (this->conn_id_ == 0xFFFF || this->tx_handle_ == 0)
    return;
  size_t chunk = this->mtu_ > 3 ? this->mtu_ - 3 : 20;
  for (size_t off = 0; off < line.size(); off += chunk) {
    size_t n = std::min(chunk, line.size() - off);
    esp_ble_gatts_send_indicate(this->gatts_if_, this->conn_id_, this->tx_handle_, n,
                                (uint8_t *) line.data() + off, false);
  }
}

void ClaudeBuddy::ack_(const char *cmd, bool ok) {
  JsonDocument d;
  d["ack"] = cmd;
  d["ok"] = ok;
  d["n"] = 0;
  std::string out;
  serializeJson(d, out);
  this->send_(out + "\n");
}

void ClaudeBuddy::send_status_() {
  JsonDocument d;
  d["ack"] = "status";
  d["ok"] = true;
  JsonObject data = d["data"].to<JsonObject>();
  data["name"] = this->device_name_;
  data["sec"] = this->link_ == Link::SECURE;
  JsonObject bat = data["bat"].to<JsonObject>();
  if (this->bat_pct_ && this->bat_pct_->has_state())
    bat["pct"] = (int) this->bat_pct_->state;
  if (this->bat_v_ && this->bat_v_->has_state())
    bat["mV"] = (int) (this->bat_v_->state * (this->bat_v_->state < 10 ? 1000 : 1));  // V or mV sensors
  if (this->charging_ && this->charging_->has_state())
    bat["usb"] = this->charging_->state;  // charging implies USB power
  JsonObject sys = data["sys"].to<JsonObject>();
  sys["up"] = millis() / 1000;
  sys["heap"] = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  JsonObject stats = data["stats"].to<JsonObject>();
  stats["appr"] = this->approvals_;
  stats["deny"] = this->denials_;
  std::string out;
  serializeJson(d, out);
  this->send_(out + "\n");
}

void ClaudeBuddy::decide_(const char *decision) {
  Source src = this->active_();
  Snapshot &snap = this->snaps_[src];
  if (snap.prompt_id.empty())
    return;
  if (src == API) {
    // The bridge subscribes to this text sensor: "<prompt id> once|deny".
    if (this->decision_)
      this->decision_->publish_state(snap.prompt_id + " " + decision);
  } else {
    JsonDocument d;
    d["cmd"] = "permission";
    d["id"] = snap.prompt_id;
    d["decision"] = decision;
    std::string out;
    serializeJson(d, out);
    this->send_(out + "\n");
  }
  (strcmp(decision, "deny") == 0 ? this->denials_ : this->approvals_)++;
  this->answered_id_ = snap.prompt_id;  // one decision per prompt
  ESP_LOGI(TAG, "Permission %s: %s (%s)", snap.prompt_id.c_str(), decision, src == API ? "bridge" : "desktop");
  // Optimistic: drop the prompt now; the next heartbeat confirms.
  snap.prompt_id.clear();
  snap.prompt_tool.clear();
  snap.prompt_hint.clear();
  this->notify_();
}

void ClaudeBuddy::unpair() {
  int n = esp_ble_get_bond_device_num();
  if (n > 0) {
    std::vector<esp_ble_bond_dev_t> list(n);
    esp_ble_get_bond_device_list(&n, list.data());
    for (int i = 0; i < n; i++)
      esp_ble_remove_bond_device(list[i].bd_addr);
  }
  ESP_LOGI(TAG, "Removed %d bond(s)", n);
  if (this->conn_id_ != 0xFFFF)
    esp_ble_gap_disconnect(this->peer_);
}

void ClaudeBuddy::handle_line_(const std::string &line, Source src) {
  JsonDocument doc = json::parse_json(line);
  if (doc.isNull()) {
    ESP_LOGW(TAG, "Bad JSON (%u bytes)", (unsigned) line.size());
    return;
  }

  // Heartbeat snapshot. A heartbeat without `prompt` means no permission is pending.
  if (doc["total"].is<uint32_t>()) {
    Snapshot &s = this->snaps_[src];
    s.total = doc["total"] | 0u;
    s.running = doc["running"] | 0u;
    s.waiting = doc["waiting"] | 0u;
    s.msg = doc["msg"] | "";
    s.entries.clear();
    for (JsonVariant e : doc["entries"].as<JsonArray>())
      s.entries.emplace_back(e | "");
    s.tokens = doc["tokens"] | (uint64_t) 0;
    s.tokens_today = doc["tokens_today"] | (uint64_t) 0;
    JsonObject p = doc["prompt"];
    s.prompt_id = p ? (p["id"] | "") : "";
    if (!s.prompt_id.empty() && s.prompt_id == this->answered_id_)
      s.prompt_id.clear();  // already answered here; the desktop just hasn't caught up
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
    if (strcmp(evt, "turn") == 0 && strcmp(doc["role"] | "", "assistant") == 0) {
      for (JsonVariant b : doc["content"].as<JsonArray>()) {
        const char *t = b["text"];
        if (t && *t) {
          this->last_text_ = t;
          this->last_text_ms_ = millis();
          this->notify_();
          break;
        }
      }
    }
    return;
  }

  if (doc["time"].is<JsonArray>())
    return;  // ponytail: clock comes from SNTP over Wi-Fi; desktop time sync ignored

  const char *cmd = doc["cmd"];
  if (!cmd || src != BLE)
    return;  // commands and acks belong to the desktop link
  if (strcmp(cmd, "status") == 0) {
    this->send_status_();
  } else if (strcmp(cmd, "name") == 0) {
    this->device_name_ = doc["name"] | this->adv_name_.c_str();
    this->ack_("name");
    this->notify_();
  } else if (strcmp(cmd, "owner") == 0) {
    this->owner_ = doc["name"] | "";
    this->ack_("owner");
    this->notify_();
  } else if (strcmp(cmd, "unpair") == 0) {
    this->ack_("unpair");
    this->unpair();
  }
  // char_begin / file / chunk: deliberately not acked, so the desktop reports folder push unsupported.
}

}  // namespace esphome::claude_buddy_ble
