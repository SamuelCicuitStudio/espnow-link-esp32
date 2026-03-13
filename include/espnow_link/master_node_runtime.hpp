#pragma once

#include <cstdint>

#include "espnow_link/cli_master.hpp"
#include "espnow_link/management_service.hpp"
#include "espnow_link/manager.hpp"
#include "espnow_link/node_runtime.hpp"

namespace espnow_link {

/**
 * @brief Runtime orchestrator for master node main loop sequencing.
 *
 * This reduces example/application boilerplate by centralizing the common
 * per-tick order:
 * `pre_hook -> input_poll -> manager -> management -> cli -> post_hook`.
 */
class MasterNodeRuntime {
 public:
  /** @brief Runtime dependency/configuration set. */
  struct Config {
    EspNowManager* manager = nullptr;
    ManagementService* management = nullptr;
    MasterCli* cli = nullptr;
    INodeRuntimePoller* input = nullptr;
    INodeRuntimeHook* pre_tick = nullptr;
    INodeRuntimeHook* post_tick = nullptr;
    uint16_t idle_delay_ms = 2;
  };

  /**
   * @brief Bind runtime dependencies.
   * @param cfg Runtime config/dependencies.
   * @return true when mandatory dependencies are valid.
   */
  bool begin(const Config& cfg);

  /** @brief Whether runtime has valid configuration and can run ticks. */
  bool ready() const { return ready_; }

  /** @brief Execute one master runtime tick with explicit timestamp. */
  void tick(uint32_t now_ms);

  /** @brief Arduino-friendly loop helper (`millis` + `delay`). */
  void loop();

 private:
  Config cfg_{};
  bool ready_ = false;
};

}  // namespace espnow_link

