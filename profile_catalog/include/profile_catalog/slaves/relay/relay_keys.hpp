#pragma once

#include "profile_catalog/shared/key_policy.hpp"

// RELAY profile identity defaults.
#define PCAT_RELAY_DEV_TYPE "RELAY"
#define PCAT_RELAY_HW_VER "RELAY-HW1"
#define PCAT_RELAY_SW_VER "0.1.0"
#define PCAT_RELAY_BUILD_TAG "rly1"
#define PCAT_RELAY_BUILD_ID BUILD_ID
#define PCAT_RELAY_DEF_NAME "RELAY-Node"

// RELAY NVS keys (<= 6 chars).
#define PCAT_RELAY_KEY_DNAME "dname"  // Device display name.
#define PCAT_RELAY_KEY_CHAN "chan"    // Radio channel.
#define PCAT_RELAY_KEY_SPLIT "splidx" // Relay split index.
#define PCAT_RELAY_KEY_PULSE "pulse"  // Relay pulse duration (ms).
#define PCAT_RELAY_KEY_HOLD "holdms"  // Relay hold duration (ms).
#define PCAT_RELAY_KEY_ILOCK "ilock"  // Relay interlock enable.
#define PCAT_RELAY_KEY_RTLIM "rtlim"  // Relay thermal limit (C).
#define PCAT_RELAY_KEY_SAMAC "samac"  // Sensor-A MAC.
#define PCAT_RELAY_KEY_SBMAC "sbmac"  // Sensor-B MAC.
#define PCAT_RELAY_KEY_R1EN "r1en"    // Relay output-1 enable.
#define PCAT_RELAY_KEY_R2EN "r2en"    // Relay output-2 enable.
#define PCAT_RELAY_KEY_LOOPA "loopa"  // Relay loop automation enable.
#define PCAT_RELAY_KEY_FANMD "fanmd"  // Fan mode (0:auto,1:eco,2:forced,3:stopped).
#define PCAT_RELAY_KEY_BUZEN "buzen"  // Audio ping buzzer feedback enable.
#define PCAT_RELAY_KEY_LEDFB "ledfb"  // Audio ping LED feedback enable.
#define PCAT_RELAY_KEY_RGBIDL "rgbidl" // RGB idle color.
#define PCAT_RELAY_KEY_RGBALT "rgbalt" // RGB alert color.
#define PCAT_RELAY_KEY_RGBBRT "rgbbrt" // RGB brightness (0..255).
#define PCAT_RELAY_KEY_PSHEN "pshen"  // Telemetry push enable.
#define PCAT_RELAY_KEY_PSHMD "pshmd"  // Telemetry push mode.
#define PCAT_RELAY_KEY_PSHI "pshint"  // Telemetry push interval (ms).
#define PCAT_RELAY_KEY_PSHD "pshdlt"  // Telemetry push delta threshold.
#define PCAT_RELAY_KEY_PSHG "pshgap"  // Telemetry push min gap (ms).
#define PCAT_RELAY_KEY_PSHS "pshscp"  // Telemetry push metric scope.
#define PCAT_RELAY_KEY_TOPV "topver"  // Topology version counter.
#define PCAT_RELAY_KEY_TOPS "topsid"  // Topology seed/session id.
#define PCAT_RELAY_KEY_TOPST "topst"  // Topology state (staged/committed).
#define PCAT_RELAY_KEY_TOPP "topprv"  // Topology previous parent MAC.
#define PCAT_RELAY_KEY_TOPN "topnxt"  // Topology next parent MAC.
#define PCAT_RELAY_KEY_TOPA "topasl"  // Topology allowed-source blob.

// Public setting keys.
#define PCAT_RELAY_SET_DNAME "device_name"
#define PCAT_RELAY_SET_CHAN "channel"
#define PCAT_RELAY_SET_SPLIT "split_idx"
#define PCAT_RELAY_SET_PULSE "pulse_ms"
#define PCAT_RELAY_SET_HOLD "hold_ms"
#define PCAT_RELAY_SET_ILOCK "interlock"
#define PCAT_RELAY_SET_RTLIM "rt_limit_c"
#define PCAT_RELAY_SET_SAMAC "sensor_a_mac"
#define PCAT_RELAY_SET_SBMAC "sensor_b_mac"
#define PCAT_RELAY_SET_R1EN "relay1_enable"
#define PCAT_RELAY_SET_R2EN "relay2_enable"
#define PCAT_RELAY_SET_LOOPA "LoopAuto"
#define PCAT_RELAY_SET_FANMD "fan_mode"
#define PCAT_RELAY_SET_BUZEN "buzzer_enable"
#define PCAT_RELAY_SET_LEDFB "led_feedback_enable"
#define PCAT_RELAY_SET_RGBIDL "rgb_idle_color"
#define PCAT_RELAY_SET_RGBALT "rgb_alert_color"
#define PCAT_RELAY_SET_RGBBRT "rgb_brightness"
#define PCAT_RELAY_SET_PSHEN "push_enabled"
#define PCAT_RELAY_SET_PSHMD "push_mode"
#define PCAT_RELAY_SET_PSHI "push_interval_ms"
#define PCAT_RELAY_SET_PSHD "push_delta_abs"
#define PCAT_RELAY_SET_PSHG "push_min_gap_ms"
#define PCAT_RELAY_SET_PSHS "push_metric_scope"
#define PCAT_RELAY_SET_TOPV "topo_version"
#define PCAT_RELAY_SET_TOPS "topo_seed_id"
#define PCAT_RELAY_SET_TOPST "topo_state"
#define PCAT_RELAY_SET_TOPP "topo_prev_mac"
#define PCAT_RELAY_SET_TOPN "topo_next_mac"
#define PCAT_RELAY_SET_TOPA "topo_allowed_sources_blob"

// Telemetry keys.
#define PCAT_RELAY_MET_BITMAP "relay_bitmap" // Relay state bitmap.
#define PCAT_RELAY_MET_UPTIME "uptime_ms"    // Node uptime.
#define PCAT_RELAY_MET_TEMP "env_temp_c"     // Shared ambient temperature.

// Defaults and ranges.
#define PCAT_RELAY_SET_CHAN_DEF 1U
#define PCAT_RELAY_SET_CHAN_MIN 1U
#define PCAT_RELAY_SET_CHAN_MAX 14U
#define PCAT_RELAY_SET_SPLIT_DEF 0U
#define PCAT_RELAY_SET_SPLIT_MIN 0U
#define PCAT_RELAY_SET_SPLIT_MAX 255U
#define PCAT_RELAY_SET_PULSE_DEF 500U
#define PCAT_RELAY_SET_PULSE_MIN 0U
#define PCAT_RELAY_SET_PULSE_MAX 65535U
#define PCAT_RELAY_SET_HOLD_DEF 30000U
#define PCAT_RELAY_SET_HOLD_MIN 0U
#define PCAT_RELAY_SET_HOLD_MAX 65535U
#define PCAT_RELAY_SET_ILOCK_DEF 1
#define PCAT_RELAY_SET_RTLIM_DEF 80U
#define PCAT_RELAY_SET_RTLIM_MIN 0U
#define PCAT_RELAY_SET_RTLIM_MAX 255U
#define PCAT_RELAY_SET_R1EN_DEF 0
#define PCAT_RELAY_SET_R2EN_DEF 0
#define PCAT_RELAY_SET_LOOPA_DEF 0
#define PCAT_RELAY_SET_FANMD_DEF 0U
#define PCAT_RELAY_SET_FANMD_MIN 0U
#define PCAT_RELAY_SET_FANMD_MAX 3U
#define PCAT_RELAY_SET_BUZEN_DEF 1
#define PCAT_RELAY_SET_LEDFB_DEF 1
#define PCAT_RELAY_SET_RGBIDL_DEF "#00ffaa"
#define PCAT_RELAY_SET_RGBALT_DEF "#ff3366"
#define PCAT_RELAY_SET_RGBBRT_DEF 180U
#define PCAT_RELAY_SET_RGBBRT_MIN 0U
#define PCAT_RELAY_SET_RGBBRT_MAX 255U
#define PCAT_RELAY_SET_PSHEN_DEF 0
#define PCAT_RELAY_SET_PSHMD_DEF "hybrid"
#define PCAT_RELAY_SET_PSHI_DEF 2000U
#define PCAT_RELAY_SET_PSHI_MIN 200U
#define PCAT_RELAY_SET_PSHI_MAX 60000U
#define PCAT_RELAY_SET_PSHD_DEF 0.0f
#define PCAT_RELAY_SET_PSHD_MIN 0.0f
#define PCAT_RELAY_SET_PSHD_MAX 100000.0f
#define PCAT_RELAY_SET_PSHG_DEF 200U
#define PCAT_RELAY_SET_PSHG_MIN 50U
#define PCAT_RELAY_SET_PSHG_MAX 10000U
#define PCAT_RELAY_SET_PSHS_DEF "all"
#define PCAT_RELAY_SET_TOPV_DEF 0U

#define PCAT_RELAY_SETMAP "device_name,channel,split_idx,pulse_ms,hold_ms,interlock,rt_limit_c,sensor_a_mac,sensor_b_mac,relay1_enable,relay2_enable,LoopAuto,fan_mode,buzzer_enable,led_feedback_enable,rgb_idle_color,rgb_alert_color,rgb_brightness,push_enabled,push_mode,push_interval_ms,push_delta_abs,push_min_gap_ms,push_metric_scope,topo_version,topo_seed_id,topo_state,topo_prev_mac,topo_next_mac,topo_allowed_sources_blob"
#define PCAT_RELAY_METMAP "relay_bitmap,uptime_ms,env_temp_c"
#define PCAT_RELAY_EVMAP "relay_on,relay_off,topology_applied"

PCAT_ASSERT_NVS_KEY_LEN(PCAT_RELAY_KEY_DNAME);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_RELAY_KEY_CHAN);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_RELAY_KEY_SPLIT);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_RELAY_KEY_PULSE);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_RELAY_KEY_HOLD);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_RELAY_KEY_ILOCK);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_RELAY_KEY_RTLIM);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_RELAY_KEY_SAMAC);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_RELAY_KEY_SBMAC);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_RELAY_KEY_R1EN);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_RELAY_KEY_R2EN);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_RELAY_KEY_LOOPA);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_RELAY_KEY_FANMD);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_RELAY_KEY_BUZEN);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_RELAY_KEY_LEDFB);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_RELAY_KEY_RGBIDL);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_RELAY_KEY_RGBALT);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_RELAY_KEY_RGBBRT);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_RELAY_KEY_PSHEN);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_RELAY_KEY_PSHMD);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_RELAY_KEY_PSHI);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_RELAY_KEY_PSHD);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_RELAY_KEY_PSHG);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_RELAY_KEY_PSHS);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_RELAY_KEY_TOPV);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_RELAY_KEY_TOPS);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_RELAY_KEY_TOPST);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_RELAY_KEY_TOPP);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_RELAY_KEY_TOPN);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_RELAY_KEY_TOPA);
