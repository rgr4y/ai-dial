#pragma once
// Character packs on LittleFS, synced from the dial bridge.
//
// The bridge serves bridge/assets/<name>/ (made by scripts/gen_buddy_pets.mjs) on its LAN port and puts
// {"assets":{"url","rev"}} in every heartbeat. When rev differs from the pack on flash, a background task pulls
// manifest.json, downloads only the files whose sha256 changed (to .tmp, verified, then renamed), writes the
// manifest last, and the dial reboots into the new pack. At boot load() expands every frame (LVGL RLE on RGB565A8)
// once into PSRAM, so drawing is the same plain RGB565A8 blit as compiled-in images.
// ponytail: reboot instead of hot-swapping a new pack; add a reload (stop players, swap, free old frames) if packs
// start changing often.
//
// Host build (claude-buddy-sim.yaml): no LittleFS and no sync; load() reads the pack straight from bridge/assets
// ($DIAL_ASSETS, else ./bridge/assets from the config directory), so a regenerated pack shows on the next sim run.
#include "esphome/core/defines.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <sys/stat.h>
#include <vector>

#ifdef USE_HOST
#include <cstdlib>
#define MALLOC_CAP_SPIRAM 0
inline void *heap_caps_malloc(size_t n, int) { return malloc(n); }
inline void heap_caps_free(void *p) { free(p); }
inline size_t heap_caps_get_free_size(int) { return 0; }
#else
#include <esp_heap_caps.h>
#include <esp_http_client.h>
#include <esp_littlefs.h>
#include <esp_partition.h>
#include <mbedtls/sha256.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif

#include "esphome/components/json/json_util.h"
#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "scene.h"

namespace character {

static const char *const TAG = "character";
#ifdef USE_HOST
inline const std::string &root() {
  static std::string r = getenv("DIAL_ASSETS") ? getenv("DIAL_ASSETS") : "bridge/assets";
  return r;
}
#define ROOT (root().c_str())
#else
static constexpr const char *ROOT = "/littlefs";
#endif

struct Pack {
  std::string rev;
  std::map<scene::Str, scene::Scene, std::less<>, scene::PsAlloc<std::pair<const scene::Str, scene::Scene>>> scenes;
};
inline Pack &pack() {
  static Pack p;
  return p;
}
inline const scene::Scene *find(const char *role) {
  auto it = pack().scenes.find(role);
  return it == pack().scenes.end() ? nullptr : &it->second;
}

// Manifests are ~20 KB: keep them in PSRAM, internal heap can't spare a block that size (allocator: scene.h).
using scene::PsAlloc;
using PString = scene::Str;
inline JsonDocument parse(const PString &s) { return esphome::json::parse_json((const uint8_t *) s.data(), s.size()); }

inline bool read_file(const std::string &path, PString &out) {
  FILE *f = fopen(path.c_str(), "rb");
  if (!f) return false;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  out.resize(n > 0 ? n : 0);
  bool ok = n >= 0 && fread(out.data(), 1, out.size(), f) == out.size();
  fclose(f);
  return ok;
}

#ifdef USE_HOST
inline bool mount() {
  ESP_LOGI(TAG, "Host: character packs from %s", ROOT);
  return true;
}
#else
inline bool mount() {
  const esp_partition_t *p = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "storage");
  if (!p) {
    ESP_LOGE(TAG, "No 'storage' partition: flash the partition table over USB once");
    return false;
  }
  esp_vfs_littlefs_conf_t conf{};
  conf.base_path = ROOT;
  conf.partition_label = "storage";
  conf.format_if_mount_failed = true;  // first boot: the partition is blank
  esp_err_t err = esp_vfs_littlefs_register(&conf);
  size_t total = 0, used = 0;
  esp_littlefs_info("storage", &total, &used);
  ESP_LOGI(TAG, "LittleFS at 0x%06" PRIX32 ": %s, %u KB used of %u KB", p->address, esp_err_to_name(err),
           (unsigned) (used >> 10), (unsigned) (total >> 10));
  return err == ESP_OK;
}
#endif

// One frame file (lv_image_header_t + lv_image_compressed_t header + RLE body) -> an uncompressed RGB565A8
// LVGL 9.6 (ESPHome 2026.11) moved this to src/libs/rle/lv_rle_private.h, off the include path. Still built
// (LV_USE_RLE; the bin decoder uses it), same signature since 9.2: declare it here.
extern "C" uint32_t lv_rle_decompress(const uint8_t *input, uint32_t input_buff_len, uint8_t *output,
                                      uint32_t output_buff_len, uint8_t blk_size);

// lv_image_dsc_t in PSRAM. Never freed: packs only change across a reboot.
inline const lv_image_dsc_t *load_frame(const std::string &path) {
  PString f;
  if (!read_file(path, f) || f.size() < 24) return nullptr;
  auto u16 = [&](size_t o) { return (uint16_t) ((uint8_t) f[o] | (uint8_t) f[o + 1] << 8); };
  auto u32 = [&](size_t o) { return (uint32_t) u16(o) | (uint32_t) u16(o + 2) << 16; };
  uint16_t w = u16(4), h = u16(6);
  uint32_t clen = u32(16), dlen = u32(20);
  // RGB565A8 (3 bytes a pixel), or A8 (alpha only; drawn in the layer's recolor, i.e. Player::set_tint).
  uint8_t cf = (uint8_t) f[1];
  bool a8 = cf == LV_COLOR_FORMAT_A8;
  if ((uint8_t) f[0] != LV_IMAGE_HEADER_MAGIC || (cf != LV_COLOR_FORMAT_RGB565A8 && !a8) || u32(12) != 1 ||
      24 + clen != f.size() || dlen != (uint32_t) w * h * (a8 ? 1 : 3))
    return nullptr;
  auto *d = (lv_image_dsc_t *) heap_caps_malloc(sizeof(lv_image_dsc_t) + dlen, MALLOC_CAP_SPIRAM);
  if (!d) return nullptr;
  auto *px = (uint8_t *) (d + 1);
  if (lv_rle_decompress((const uint8_t *) f.data() + 24, clen, px, dlen, 2) != dlen) {
    heap_caps_free(d);
    return nullptr;
  }
  memset(d, 0, sizeof(*d));
  d->header.magic = LV_IMAGE_HEADER_MAGIC;
  d->header.cf = cf;
  d->header.w = w;
  d->header.h = h;
  d->header.stride = a8 ? w : w * 2;
  d->data_size = dlen;
  d->data = px;
  return d;
}

template<typename T> scene::Vec<T> ints(JsonVariantConst a) {
  scene::Vec<T> v;
  for (JsonVariantConst x : a.as<JsonArrayConst>()) v.push_back((T) (x | 0));
  return v;
}
template<typename T> scene::Vec<std::array<T, 2>> pairs(JsonVariantConst a) {
  scene::Vec<std::array<T, 2>> v;
  for (JsonVariantConst x : a.as<JsonArrayConst>()) v.push_back({(T) (x[0] | 0), (T) (x[1] | 0)});
  return v;
}

// Load /littlefs/<name>/manifest.json and every frame it names. Missing or bad pack: no scenes, the UI shows none.
// `first` runs once the "relax" scene alone is loaded (4 frames), so boot can draw the splash pet before the rest.
inline bool load(const char *name, void (*first)() = nullptr) {
  std::string dir = std::string(ROOT) + "/" + name + "/";
  PString js;
  if (!read_file(dir + "manifest.json", js)) {
    ESP_LOGW(TAG, "No pack for %s yet: waiting for the bridge", name);
    return false;
  }
  JsonDocument doc = parse(js);
  if (doc.isNull()) {
    ESP_LOGE(TAG, "Bad manifest for %s", name);
    return false;
  }
  uint32_t t0 = esphome::millis();
  size_t before = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  std::map<scene::Str, scene::Frames, std::less<>, PsAlloc<std::pair<const scene::Str, scene::Frames>>> sets;
  // Decode one frame set unless already done. False on a bad frame.
  auto need = [&](const char *key) {
    if (sets.count(key)) return true;
    auto &v = sets[key];
    for (JsonVariantConst p : doc["sets"][key].as<JsonArrayConst>()) {
      esphome::App.feed_wdt();  // the whole pack decodes in one go at boot: longer than the 5 s task watchdog
      const lv_image_dsc_t *d = load_frame(dir + (p | ""));
      if (!d) {
        ESP_LOGE(TAG, "Bad frame %s: pack ignored", p | "");
        return false;  // ponytail: leaks the frames loaded so far; only on a corrupt pack, until the next sync
      }
      v.push_back(d);
    }
    return true;
  };
  auto set = [&](JsonVariantConst n) { return n.isNull() ? scene::Frames{} : sets[n | ""]; };
  Pack &pk = pack();
  auto build = [&](const char *key, JsonObjectConst o) {
    scene::Scene &s = pk.scenes[key];
    s.name = o["name"] | key;
    s.w = o["w"] | 0, s.h = o["h"] | 0, s.dx = o["dx"] | 0, s.dy = o["dy"] | 0;
    s.steps = o["steps"] | 1, s.step_ms = o["step_ms"] | 100;
    s.layers.clear();
    for (JsonVariantConst l : o["layers"].as<JsonArrayConst>())
      s.layers.push_back({set(l["set"]), ints<uint8_t>(l["loop"]), pairs<int16_t>(l["at"]), l["ink"] | -1});
    s.count_at = pairs<int16_t>(o["count_at"]);
    s.tint = o["tint"] | -1;
    JsonObjectConst g = o["gaze"];  // absent (older packs, scenes without separate eyes): no IMU gaze
    s.gaze = {};
    if (!g.isNull()) s.gaze = {(int8_t) (g["layer"] | -1), (int8_t) (g["x"] | 0), (int8_t) (g["y"] | 0), ints<int16_t>(g["look"])};
    s.discs = o["discs"] | 0;
    s.disc.clear();
    for (JsonVariantConst d : o["disc"].as<JsonArrayConst>())
      s.disc.push_back({(int16_t) (d[0] | 0), (int16_t) (d[1] | 0), (int16_t) (d[2] | 0), d[3] | 0u});
  };
  JsonObjectConst relax = doc["scenes"]["relax"].as<JsonObjectConst>();
  if (first && !relax.isNull()) {
    for (JsonVariantConst l : relax["layers"].as<JsonArrayConst>())
      if (!need(l["set"] | "")) return false;  // a bad relax frame: the relax scene isn't built yet
    build("relax", relax);
    first();
  }
  for (JsonPairConst kv : doc["sets"].as<JsonObjectConst>())
    if (!need(kv.key().c_str())) return false;  // ponytail: an early relax stays up on a corrupt pack
  pk.rev = doc["rev"] | "";
  for (JsonPairConst kv : doc["scenes"].as<JsonObjectConst>()) build(kv.key().c_str(), kv.value());
  ESP_LOGI(TAG, "Loaded %s rev %s: %u scenes, %u KB PSRAM, %u ms", name, pk.rev.c_str(), (unsigned) pk.scenes.size(),
           (unsigned) ((before - heap_caps_get_free_size(MALLOC_CAP_SPIRAM)) >> 10), (unsigned) (esphome::millis() - t0));
  return true;
}

#ifdef USE_HOST
inline void want(const std::string &, const std::string &) {}  // host reads bridge/assets directly
inline bool synced() { return false; }
#else
// ---- sync from the bridge (background task) ----
inline std::atomic<int> &sync_state() {  // 0 idle, 1 running, 2 done (reboot), 3 failed
  static std::atomic<int> s{0};
  return s;
}
struct SyncJob {
  std::string url, rev;
};

// GET url into a file (or memory when f is null); returns bytes and the sha256 hex, -1 on any error.
inline long http_get(const std::string &url, FILE *f, PString *mem, std::string *sha) {
  esp_http_client_config_t cfg{};
  cfg.url = url.c_str();
  cfg.timeout_ms = 10000;
  esp_http_client_handle_t c = esp_http_client_init(&cfg);
  if (!c) return -1;
  long total = -1;
  if (esp_http_client_open(c, 0) == ESP_OK && esp_http_client_fetch_headers(c) >= 0 &&
      esp_http_client_get_status_code(c) == 200) {
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);
    static char buf[2048];
    total = 0;
    int n;
    while ((n = esp_http_client_read(c, buf, sizeof(buf))) > 0) {
      mbedtls_sha256_update(&ctx, (const uint8_t *) buf, n);
      if (f && fwrite(buf, 1, n, f) != (size_t) n) {
        n = -1;
        break;
      }
      if (mem) mem->append(buf, n);
      total += n;
    }
    if (n < 0) total = -1;
    std::array<uint8_t, 32> d;
    mbedtls_sha256_finish(&ctx, d.data());
    mbedtls_sha256_free(&ctx);
    if (sha) *sha = esphome::format_hex(d);
  }
  esp_http_client_close(c);
  esp_http_client_cleanup(c);
  return total;
}

inline void sync_task(void *arg) {
  auto *job = (SyncJob *) arg;
  bool ok = false;
  PString js;
  if (http_get(job->url + "manifest.json", nullptr, &js, nullptr) > 0) {
    JsonDocument doc = parse(js);
    std::string name = doc["name"] | "";
    if (!doc.isNull() && !name.empty() && name.find('/') == std::string::npos && (doc["rev"] | "") == job->rev) {
      std::string dir = std::string(ROOT) + "/" + name + "/";
      PString old_js;
      JsonDocument old;
      if (read_file(dir + "manifest.json", old_js)) old = parse(old_js);
      std::string bare = dir.substr(0, dir.size() - 1);  // LittleFS mkdir rejects a trailing slash (ENOENT)
      if (mkdir(bare.c_str(), 0775) != 0 && errno != EEXIST)
        ESP_LOGE(TAG, "Sync: mkdir %s: %s (LittleFS mounted: %d)", bare.c_str(), strerror(errno),
                 esp_littlefs_mounted("storage"));
      ok = true;
      unsigned got = 0, kept = 0;
      for (JsonPairConst kv : doc["files"].as<JsonObjectConst>()) {
        std::string path = kv.key().c_str(), want = kv.value()["sha256"] | "";
        long size = kv.value()["size"] | -1L;
        if (path.find("..") != std::string::npos || path[0] == '/') {
          ok = false;
          break;
        }
        struct stat st;
        std::string dst = dir + path;
        if (want == (old["files"][path]["sha256"] | "") && stat(dst.c_str(), &st) == 0 && st.st_size == size) {
          kept++;
          continue;
        }
        size_t slash = path.rfind('/');
        if (slash != std::string::npos) mkdir((dir + path.substr(0, slash)).c_str(), 0775);
        std::string tmp = dst + ".tmp", sha;
        FILE *f = fopen(tmp.c_str(), "wb");
        if (!f) ESP_LOGE(TAG, "Sync: can't create %s: %s", tmp.c_str(), strerror(errno));
        long n = f ? http_get(job->url + path, f, nullptr, &sha) : -1;
        if (f) fclose(f);
        if (n != size || sha != want) {
          ESP_LOGE(TAG, "Sync: %s failed (%ld of %ld bytes)", path.c_str(), n, size);
          remove(tmp.c_str());
          ok = false;
          break;
        }
        remove(dst.c_str());
        rename(tmp.c_str(), dst.c_str());
        got++;
      }
      if (ok) {  // manifest last: a pack on flash is only ever complete
        std::string tmp = dir + "manifest.json.tmp";
        FILE *f = fopen(tmp.c_str(), "wb");
        ok = f && fwrite(js.data(), 1, js.size(), f) == js.size();
        if (f) fclose(f);
        remove((dir + "manifest.json").c_str());
        ok = ok && rename(tmp.c_str(), (dir + "manifest.json").c_str()) == 0;
      }
      ESP_LOGI(TAG, "Sync %s rev %s: %s, %u downloaded, %u unchanged", name.c_str(), job->rev.c_str(),
               ok ? "done" : "FAILED", got, kept);
    }
  }
  if (!ok)
    ESP_LOGW(TAG, "Sync from %s failed (internal heap %u free, largest %u); retrying on a later heartbeat",
             job->url.c_str(), (unsigned) heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned) heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
  sync_state() = ok ? 2 : 3;
  delete job;
  vTaskDelete(nullptr);
}

// Called with every heartbeat's assets; starts a sync when the bridge has a different rev. Retries a failed one
// after a minute.
inline void want(const std::string &url, const std::string &rev) {
  static uint32_t failed_at = 0;
  if (url.empty() || rev.empty() || rev == pack().rev) return;
  int s = sync_state();
  if (s == 1 || s == 2) return;
  if (s == 3) {
    if (!failed_at) failed_at = esphome::millis();
    if (esphome::millis() - failed_at < 60000) return;
  }
  failed_at = 0;
  sync_state() = 1;
  ESP_LOGI(TAG, "Bridge has rev %s (have '%s'): syncing from %s", rev.c_str(), pack().rev.c_str(), url.c_str());
  if (xTaskCreate(sync_task, "char_sync", 8192, new SyncJob{url, rev}, 2, nullptr) != pdPASS) sync_state() = 3;
}
inline bool synced() { return sync_state() == 2; }
#endif

}  // namespace character
