#pragma once

#include <cstdint>

#include "espnow_link/types.hpp"

namespace espnow_link {

struct PersistenceConfig {
  bool enabled = true;
  const char* namespace_name = "espnow_link";
  uint8_t schema_version = 1;
};

struct ManagerConfig {
  Role local_role = Role::Slave;
  uint8_t channel = 1;
  uint32_t discovery_interval_ms = 1000;
  uint32_t discovery_peer_ttl_ms = 3000;
  const char* discovery_name = nullptr;
  uint8_t discovery_name_max_len = 24;
  uint32_t pair_ack_timeout_ms = 1200;
  uint32_t secure_grace_ms = 150;
  uint32_t unpair_ack_timeout_ms = 1200;
  uint32_t channel_switch_timeout_ms = 2000;
  uint32_t channel_switch_grace_ms = 500;
  bool enable_encryption = true;
  bool master_initiated_only = true;
  bool auto_pair_on_discovery = true;
  bool force_local_unpair_on_timeout = false;
  uint32_t unpair_ack_grace_ms = 120;
  bool time_sync_enabled = true;
  uint32_t time_sync_interval_ms = 5000;
  bool time_sync_apply_on_slave = true;
  uint32_t time_sync_min_update_delta_s = 1;
  uint64_t time_sync_min_valid_epoch_s = 946684800ULL;
  uint32_t mandatory_event_min_gap_ms = 60;
  uint8_t mandatory_event_max_queue = 12;
  uint8_t mandatory_event_retry_max = 3;
  uint8_t mandatory_event_fail_backoff_threshold = 5;
  uint32_t mandatory_event_backoff_ms = 2000;
  PersistenceConfig persistence{};
};

}  // namespace espnow_link
