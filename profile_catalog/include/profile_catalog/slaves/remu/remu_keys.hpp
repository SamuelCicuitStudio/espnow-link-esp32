#pragma once

#include "profile_catalog/shared/key_policy.hpp"

// REMU profile identity defaults.
#define PCAT_REMU_DEV_TYPE "REMU"
#define PCAT_REMU_HW_VER "REMU-HW1"
#define PCAT_REMU_SW_VER "0.1.0"
#define PCAT_REMU_BUILD_TAG "rem1"
#define PCAT_REMU_BUILD_ID BUILD_ID
#define PCAT_REMU_DEF_NAME "REMU-Node"

// REMU NVS keys (<= 6 chars).
#define PCAT_REMU_KEY_DNAME "dname"  // Device display name.
#define PCAT_REMU_KEY_CHAN "chan"    // Radio channel.
#define PCAT_REMU_KEY_RCNT "rcnt"    // Active virtual relay count.
#define PCAT_REMU_KEY_SPLIT "splidx" // Global split index fallback.
#define PCAT_REMU_KEY_GPLS "gpuls"   // Global pulse duration fallback (ms).
#define PCAT_REMU_KEY_GHLD "ghold"   // Global hold duration fallback (ms).
#define PCAT_REMU_KEY_RPTMS "rptms"  // Global repeat window (ms).
#define PCAT_REMU_KEY_ILOCK "ilokj"  // Global interlock map (JSON).
#define PCAT_REMU_KEY_SAMAC "samac"  // Global sensor-A MAC fallback.
#define PCAT_REMU_KEY_SBMAC "sbmac"  // Global sensor-B MAC fallback.
#define PCAT_REMU_KEY_LOOPA "loopa"  // Relay loop automation enable.
#define PCAT_REMU_KEY_FANMD "fanmd"  // Fan mode (0:auto,1:eco,2:forced,3:stopped).
#define PCAT_REMU_KEY_BUZEN "buzen"  // Audio ping buzzer feedback enable.
#define PCAT_REMU_KEY_LEDFB "ledfb"  // Audio ping LED feedback enable.
#define PCAT_REMU_KEY_RGBIDL "rgbidl" // RGB idle color.
#define PCAT_REMU_KEY_RGBALT "rgbalt" // RGB alert color.
#define PCAT_REMU_KEY_RGBBRT "rgbbrt" // RGB brightness (0..255).
#define PCAT_REMU_KEY_PSHEN "pshen"  // Telemetry push enable.
#define PCAT_REMU_KEY_PSHMD "pshmd"  // Telemetry push mode.
#define PCAT_REMU_KEY_PSHI "pshint"  // Telemetry push interval (ms).
#define PCAT_REMU_KEY_PSHD "pshdlt"  // Telemetry push delta threshold.
#define PCAT_REMU_KEY_PSHG "pshgap"  // Telemetry push min gap (ms).
#define PCAT_REMU_KEY_PSHS "pshscp"  // Telemetry push metric scope.
#define PCAT_REMU_KEY_TOPV "topver"  // Topology version counter.
#define PCAT_REMU_KEY_TOPS "topsid"  // Topology seed/session id.
#define PCAT_REMU_KEY_TOPST "topst"  // Topology state (staged/committed).
#define PCAT_REMU_KEY_TOPP "topprv"  // Topology previous parent MAC.
#define PCAT_REMU_KEY_TOPN "topnxt"  // Topology next parent MAC.
#define PCAT_REMU_KEY_TOPA "topasl"  // Topology allowed-source blob.

// REMU child-bank NVS key components (generated per child vid 0..15).
// Full key format: "<prefix><hex_vid><code4>" => 6 chars total.
#define PCAT_REMU_CHILD_NVS_PREFIX 'r'
#define PCAT_REMU_CKEY_SPLT "splt"  // Per-child split index.
#define PCAT_REMU_CKEY_PULS "puls"  // Per-child pulse duration (ms).
#define PCAT_REMU_CKEY_HOLD "hold"  // Per-child hold duration (ms).
#define PCAT_REMU_CKEY_ILOK "ilok"  // Per-child interlock enable.
#define PCAT_REMU_CKEY_RTLM "rtlm"  // Per-child relay thermal limit (C).
#define PCAT_REMU_CKEY_SAMA "sama"  // Per-child sensor-A MAC.
#define PCAT_REMU_CKEY_SBMA "sbma"  // Per-child sensor-B MAC.
#define PCAT_REMU_CKEY_OENA "oena"  // Per-child direct output enable (ON/OFF).

// REMU child public setting suffixes.
#define PCAT_REMU_CSET_OENA "output_enable"  // Child ON/OFF key: v<vid>.output_enable.

// Public setting keys.
#define PCAT_REMU_SET_DNAME "device_name"
#define PCAT_REMU_SET_CHAN "channel"
#define PCAT_REMU_SET_RCNT "relay_count"
#define PCAT_REMU_SET_SPLIT "split_idx"
#define PCAT_REMU_SET_GPLS "global_pulse_ms"
#define PCAT_REMU_SET_GHLD "global_hold_ms"
#define PCAT_REMU_SET_RPTMS "repeat_ms"
#define PCAT_REMU_SET_ILOCK "interlock_json"
#define PCAT_REMU_SET_SAMAC "sensor_a_mac"
#define PCAT_REMU_SET_SBMAC "sensor_b_mac"
#define PCAT_REMU_SET_LOOPA "LoopAuto"
#define PCAT_REMU_SET_FANMD "fan_mode"
#define PCAT_REMU_SET_BUZEN "buzzer_enable"
#define PCAT_REMU_SET_LEDFB "led_feedback_enable"
#define PCAT_REMU_SET_RGBIDL "rgb_idle_color"
#define PCAT_REMU_SET_RGBALT "rgb_alert_color"
#define PCAT_REMU_SET_RGBBRT "rgb_brightness"
#define PCAT_REMU_SET_PSHEN "push_enabled"
#define PCAT_REMU_SET_PSHMD "push_mode"
#define PCAT_REMU_SET_PSHI "push_interval_ms"
#define PCAT_REMU_SET_PSHD "push_delta_abs"
#define PCAT_REMU_SET_PSHG "push_min_gap_ms"
#define PCAT_REMU_SET_PSHS "push_metric_scope"
#define PCAT_REMU_SET_TOPV "topo_version"
#define PCAT_REMU_SET_TOPS "topo_seed_id"
#define PCAT_REMU_SET_TOPST "topo_state"
#define PCAT_REMU_SET_TOPP "topo_prev_mac"
#define PCAT_REMU_SET_TOPN "topo_next_mac"
#define PCAT_REMU_SET_TOPA "topo_allowed_sources_blob"

// Telemetry keys.
#define PCAT_REMU_MET_BITMAP "relay_bitmap" // Global relay state bitmap.
#define PCAT_REMU_MET_RCOUNT "relay_count"  // Active virtual relay count.
#define PCAT_REMU_MET_TEMP "env_temp_c"     // Shared DS18B20 ambient temperature.
#define PCAT_REMU_MET_UPTIME "uptime_ms"    // Node uptime.

// Defaults and ranges.
#define PCAT_REMU_SET_CHAN_DEF 1U
#define PCAT_REMU_SET_CHAN_MIN 1U
#define PCAT_REMU_SET_CHAN_MAX 14U
#define PCAT_REMU_SET_RCNT_DEF 16U
#define PCAT_REMU_SET_RCNT_MIN 1U
#define PCAT_REMU_SET_RCNT_MAX 16U
#define PCAT_REMU_SET_SPLIT_DEF 0U
#define PCAT_REMU_SET_SPLIT_MIN 0U
#define PCAT_REMU_SET_SPLIT_MAX 255U
#define PCAT_REMU_SET_GPLS_DEF 500U
#define PCAT_REMU_SET_GPLS_MIN 0U
#define PCAT_REMU_SET_GPLS_MAX 65535U
#define PCAT_REMU_SET_GHLD_DEF 30000U
#define PCAT_REMU_SET_GHLD_MIN 0U
#define PCAT_REMU_SET_GHLD_MAX 65535U
#define PCAT_REMU_SET_RPTMS_DEF 1000U
#define PCAT_REMU_SET_RPTMS_MIN 0U
#define PCAT_REMU_SET_RPTMS_MAX 65535U
#define PCAT_REMU_SET_LOOPA_DEF 0
#define PCAT_REMU_SET_FANMD_DEF 0U
#define PCAT_REMU_SET_FANMD_MIN 0U
#define PCAT_REMU_SET_FANMD_MAX 3U
#define PCAT_REMU_SET_BUZEN_DEF 1
#define PCAT_REMU_SET_LEDFB_DEF 1
#define PCAT_REMU_SET_RGBIDL_DEF "#00ffaa"
#define PCAT_REMU_SET_RGBALT_DEF "#ff3366"
#define PCAT_REMU_SET_RGBBRT_DEF 180U
#define PCAT_REMU_SET_RGBBRT_MIN 0U
#define PCAT_REMU_SET_RGBBRT_MAX 255U
#define PCAT_REMU_SET_PSHEN_DEF 0
#define PCAT_REMU_SET_PSHMD_DEF "hybrid"
#define PCAT_REMU_SET_PSHI_DEF 2000U
#define PCAT_REMU_SET_PSHI_MIN 200U
#define PCAT_REMU_SET_PSHI_MAX 60000U
#define PCAT_REMU_SET_PSHD_DEF 0.0f
#define PCAT_REMU_SET_PSHD_MIN 0.0f
#define PCAT_REMU_SET_PSHD_MAX 100000.0f
#define PCAT_REMU_SET_PSHG_DEF 200U
#define PCAT_REMU_SET_PSHG_MIN 50U
#define PCAT_REMU_SET_PSHG_MAX 10000U
#define PCAT_REMU_SET_PSHS_DEF "all"
#define PCAT_REMU_SET_TOPV_DEF 0U

#define PCAT_REMU_SETMAP "device_name,channel,relay_count,split_idx,global_pulse_ms,global_hold_ms,repeat_ms,interlock_json,sensor_a_mac,sensor_b_mac,LoopAuto,fan_mode,buzzer_enable,led_feedback_enable,rgb_idle_color,rgb_alert_color,rgb_brightness,push_enabled,push_mode,push_interval_ms,push_delta_abs,push_min_gap_ms,push_metric_scope,topo_version,topo_seed_id,topo_state,topo_prev_mac,topo_next_mac,topo_allowed_sources_blob"
#define PCAT_REMU_METMAP "relay_bitmap,relay_count,env_temp_c,uptime_ms"
#define PCAT_REMU_EVMAP "relay_triggered,topology_applied,source_rejected"

#define PCAT_ASSERT_REMU_CHILD_CODE4_LEN(code_literal) \
  static_assert((sizeof(code_literal) - 1U) == 4U, "REMU child nvs code4 must be 4 chars: " #code_literal)

PCAT_ASSERT_NVS_KEY_LEN(PCAT_REMU_KEY_DNAME);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_REMU_KEY_CHAN);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_REMU_KEY_RCNT);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_REMU_KEY_SPLIT);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_REMU_KEY_GPLS);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_REMU_KEY_GHLD);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_REMU_KEY_RPTMS);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_REMU_KEY_ILOCK);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_REMU_KEY_SAMAC);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_REMU_KEY_SBMAC);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_REMU_KEY_LOOPA);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_REMU_KEY_FANMD);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_REMU_KEY_BUZEN);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_REMU_KEY_LEDFB);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_REMU_KEY_RGBIDL);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_REMU_KEY_RGBALT);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_REMU_KEY_RGBBRT);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_REMU_KEY_PSHEN);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_REMU_KEY_PSHMD);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_REMU_KEY_PSHI);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_REMU_KEY_PSHD);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_REMU_KEY_PSHG);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_REMU_KEY_PSHS);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_REMU_KEY_TOPV);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_REMU_KEY_TOPS);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_REMU_KEY_TOPST);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_REMU_KEY_TOPP);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_REMU_KEY_TOPN);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_REMU_KEY_TOPA);

PCAT_ASSERT_REMU_CHILD_CODE4_LEN(PCAT_REMU_CKEY_SPLT);
PCAT_ASSERT_REMU_CHILD_CODE4_LEN(PCAT_REMU_CKEY_PULS);
PCAT_ASSERT_REMU_CHILD_CODE4_LEN(PCAT_REMU_CKEY_HOLD);
PCAT_ASSERT_REMU_CHILD_CODE4_LEN(PCAT_REMU_CKEY_ILOK);
PCAT_ASSERT_REMU_CHILD_CODE4_LEN(PCAT_REMU_CKEY_RTLM);
PCAT_ASSERT_REMU_CHILD_CODE4_LEN(PCAT_REMU_CKEY_SAMA);
PCAT_ASSERT_REMU_CHILD_CODE4_LEN(PCAT_REMU_CKEY_SBMA);
PCAT_ASSERT_REMU_CHILD_CODE4_LEN(PCAT_REMU_CKEY_OENA);
