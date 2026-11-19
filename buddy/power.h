#pragma once
// Screen sleep/wake state behind the PWR key, the idle timeout and wake touches (ui.h owns the shield and LVGL).
// No LVGL here, so tests/test_power.cpp runs it on the host.
#include <cstdint>
#include <utility>

namespace power {

struct Screen {
  uint32_t last_activity = 0;
  bool asleep = false;
  bool wake = false, sleep = false;  // one-shot requests for the YAML side (light fade, LVGL pause)
  uint32_t woke_at = 0;                          // last sleep -> wake edge (0 = boot)
  uint32_t usb_since = 0;                        // last plug-in edge
  bool usb_was = false;

  // True when the state changed. Only the latest edge stays pending: sleep then wake in one tick must not leave
  // both requests for the YAML, which would fade out and pause LVGL while awake.
  bool set_asleep(bool z) {
    if (asleep == z) return false;
    asleep = z;
    sleep = z;
    wake = !z;
    return true;
  }
  bool activity(uint32_t now) {
    last_activity = now;
    bool woke = set_asleep(false);
    if (woke) woke_at = now;
    return woke;
  }
  // PWR tap (ptt.c:20-22): screen off/on, through the same state the idle timeout uses.
  bool toggle(uint32_t now) { return asleep ? activity(now) : set_asleep(true); }
  // Idle timeout. Never on USB; after an unplug with an old last_activity this is due on the next tick (battery rules).
  bool idle_due(uint32_t now, uint32_t boot_awake_ms, uint32_t sleep_ms, bool on_usb) const {
    return !asleep && !on_usb && now > boot_awake_ms && now - last_activity > sleep_ms;
  }
  // Call every tick. Boot already on USB counts as a plug-in at that tick.
  void power_source(bool on_usb, uint32_t now) {
    if (on_usb && !usb_was) usb_since = now;
    usb_was = on_usb;
  }
  // Face-side battery %: always on battery; on USB for 60 s after plug-in and after each wake (boot counts as a wake).
  bool show_battery_side(bool on_usb, uint32_t now) const {
    return !on_usb || now - usb_since < 60000 || now - woke_at < 60000;
  }
  bool take_wake() { return std::exchange(wake, false); }
  bool take_sleep() { return std::exchange(sleep, false); }
};

// Battery page ink: red <=10%, yellow at 50%, green >=90%, linear RGB blend between.
inline uint32_t battery_ink(int pct) {
  constexpr uint32_t RED = 0xcd3131, YEL = 0xe5e510, GRN = 0x3fb950;
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  if (pct <= 10) return RED;
  if (pct >= 90) return GRN;
  uint32_t a = pct < 50 ? RED : YEL, b = pct < 50 ? YEL : GRN;
  int lo = pct < 50 ? 10 : 50, span = 40, t = pct - lo;
  uint32_t out = 0;
  for (int sh = 16; sh >= 0; sh -= 8) {
    int ca = (a >> sh) & 0xff, cb = (b >> sh) & 0xff;
    out |= (uint32_t) (ca + (cb - ca) * t / span) << sh;
  }
  return out;
}

}  // namespace power
