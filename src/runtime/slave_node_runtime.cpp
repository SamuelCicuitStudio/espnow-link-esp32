#include "espnow_link/slave_node_runtime.hpp"

#if defined(ARDUINO)
#include <Arduino.h>
#endif

namespace espnow_link {

bool SlaveNodeRuntime::begin(const Config& cfg) {
  cfg_ = cfg;
  ready_ = (cfg_.manager != nullptr);
  return ready_;
}

void SlaveNodeRuntime::tick(uint32_t now_ms) {
  if (!ready_) {
    return;
  }
  if (cfg_.pre_tick != nullptr) {
    cfg_.pre_tick->onRuntimeTick(now_ms);
  }
  cfg_.manager->tick(now_ms);
  if (cfg_.post_tick != nullptr) {
    cfg_.post_tick->onRuntimeTick(now_ms);
  }
}

void SlaveNodeRuntime::loop() {
#if defined(ARDUINO)
  if (!ready_) {
    delay(50);
    return;
  }
  tick(millis());
  if (cfg_.idle_delay_ms > 0U) {
    delay(cfg_.idle_delay_ms);
  }
#endif
}

}  // namespace espnow_link

