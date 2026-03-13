#include "espnow_link/master_node_runtime.hpp"

#if defined(ARDUINO)
#include <Arduino.h>
#endif

namespace espnow_link {

bool MasterNodeRuntime::begin(const Config& cfg) {
  cfg_ = cfg;
  ready_ = (cfg_.manager != nullptr && cfg_.cli != nullptr);
  return ready_;
}

void MasterNodeRuntime::tick(uint32_t now_ms) {
  if (!ready_) {
    return;
  }
  if (cfg_.pre_tick != nullptr) {
    cfg_.pre_tick->onRuntimeTick(now_ms);
  }
  if (cfg_.input != nullptr) {
    cfg_.input->poll();
  }
  cfg_.manager->tick(now_ms);
  if (cfg_.management != nullptr) {
    cfg_.management->tick(now_ms);
  }
  cfg_.cli->tick(now_ms);
  if (cfg_.post_tick != nullptr) {
    cfg_.post_tick->onRuntimeTick(now_ms);
  }
}

void MasterNodeRuntime::loop() {
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

