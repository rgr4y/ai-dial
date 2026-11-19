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
    return set_asleep(false);
  }
  // PWR tap (ptt.c:20-22): screen off/on, through the same state the idle timeout uses.
  bool toggle(uint32_t now) { return asleep ? activity(now) : set_asleep(true); }
  // Idle timeout. Never on USB; after an unplug with an old last_activity this is due on the next tick (battery rules).
  bool idle_due(uint32_t now, uint32_t boot_awake_ms, uint32_t sleep_ms, bool on_usb) const {
    return !asleep && !on_usb && now > boot_awake_ms && now - last_activity > sleep_ms;
  }
  bool take_wake() { return std::exchange(wake, false); }
  bool take_sleep() { return std::exchange(sleep, false); }
};

}  // namespace power
