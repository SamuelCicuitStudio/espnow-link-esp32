#pragma once

#include "profile_catalog/shared/key_policy.hpp"

// SEMU profile identity defaults.
#define PCAT_SEMU_DEV_TYPE "SEMU"
#define PCAT_SEMU_HW_VER "SEMU-HW1"
#define PCAT_SEMU_SW_VER "0.1.0"
#define PCAT_SEMU_BUILD_TAG "sem1"
#define PCAT_SEMU_BUILD_ID BUILD_ID
#define PCAT_SEMU_DEF_NAME "SEMU-Node"

// SEMU NVS keys (<= 6 chars).
#define PCAT_SEMU_KEY_DNAME "dname"  // Device display name.
#define PCAT_SEMU_KEY_CHAN "chan"    // Radio channel.
#define PCAT_SEMU_KEY_SCNT "scnt"    // Number of active virtual channels.
#define PCAT_SEMU_KEY_PREV "prevmc"  // Global previous neighbor MAC.
#define PCAT_SEMU_KEY_NEXT "nxtmac"  // Global next neighbor MAC.
#define PCAT_SEMU_KEY_POSR "posrl"   // Global positive-direction relay targets (JSON).
#define PCAT_SEMU_KEY_NEGR "negrl"   // Global negative-direction relay targets (JSON).
#define PCAT_SEMU_KEY_VON "vonms"    // Virtual ON duration (ms).
#define PCAT_SEMU_KEY_VLCNT "vlcnt"  // Virtual lead pulse count.
#define PCAT_SEMU_KEY_VLMS "vlms"    // Virtual lead step (ms).
#define PCAT_SEMU_KEY_VENV "venv"    // Enable virtual environmental stream.
#define PCAT_SEMU_KEY_ALS0 "als0"    // Global ALS lower threshold (lux).
#define PCAT_SEMU_KEY_ALS1 "als1"    // Global ALS upper threshold (lux).
#define PCAT_SEMU_KEY_LOOPA "loopa"  // Auto-loop traversal enable.
#define PCAT_SEMU_KEY_FANMD "fanmd"  // Fan mode (0:auto,1:eco,2:forced,3:stopped).
#define PCAT_SEMU_KEY_BUZEN "buzen"  // Audio ping buzzer feedback enable.
#define PCAT_SEMU_KEY_LEDFB "ledfb"  // Audio ping LED feedback enable.
#define PCAT_SEMU_KEY_RGBIDL "rgbidl" // RGB idle color.
#define PCAT_SEMU_KEY_RGBALT "rgbalt" // RGB alert color.
#define PCAT_SEMU_KEY_RGBBRT "rgbbrt" // RGB brightness (0..255).
#define PCAT_SEMU_KEY_PSHEN "pshen"  // Telemetry push enable.
#define PCAT_SEMU_KEY_PSHMD "pshmd"  // Telemetry push mode.
#define PCAT_SEMU_KEY_PSHI "pshint"  // Telemetry push interval (ms).
#define PCAT_SEMU_KEY_PSHD "pshdlt"  // Telemetry push delta threshold.
#define PCAT_SEMU_KEY_PSHG "pshgap"  // Telemetry push min gap (ms).
#define PCAT_SEMU_KEY_PSHS "pshscp"  // Telemetry push metric scope.
#define PCAT_SEMU_KEY_TOPV "topver"  // Topology version counter.
#define PCAT_SEMU_KEY_TOPS "topsid"  // Topology seed/session id.
#define PCAT_SEMU_KEY_TOPST "topst"  // Topology state (staged/committed).
#define PCAT_SEMU_KEY_TOPR "toprtg"  // Serialized topology relay-target map.
#define PCAT_SEMU_KEY_TOPC "topcmt"  // Topology commit epoch (s).

// SEMU child-bank NVS key components (generated per child vid 0..7).
// Full key format: "<prefix><hex_vid><code4>" => 6 chars total.
#define PCAT_SEMU_CHILD_NVS_PREFIX 's'
#define PCAT_SEMU_CKEY_PREV "prvm"  // Per-child previous neighbor MAC.
#define PCAT_SEMU_CKEY_NEXT "nxtm"  // Per-child next neighbor MAC.
#define PCAT_SEMU_CKEY_POSR "posr"  // Per-child positive relay targets (JSON).
#define PCAT_SEMU_CKEY_NEGR "negr"  // Per-child negative relay targets (JSON).
#define PCAT_SEMU_CKEY_TFNR "tfnr"  // Per-child detection falling delta (cm).
#define PCAT_SEMU_CKEY_TFFR "tffr"  // Per-child detection release delta (cm).
#define PCAT_SEMU_CKEY_ABSP "absp"  // Per-child A/B spacing baseline (mm).
#define PCAT_SEMU_CKEY_CALA "cala"  // Per-child TFLuna-A calibrated baseline distance (mm).
#define PCAT_SEMU_CKEY_CALB "calb"  // Per-child TFLuna-B calibrated baseline distance (mm).
#define PCAT_SEMU_CKEY_ALS0 "als0"  // Per-child ALS lower threshold (lux).
#define PCAT_SEMU_CKEY_ALS1 "als1"  // Per-child ALS upper threshold (lux).
#define PCAT_SEMU_CKEY_CFMS "cfms"  // Per-child detection A/B edge window (ms).
#define PCAT_SEMU_CKEY_STMS "stms"  // Per-child detection clear hold (ms).
#define PCAT_SEMU_CKEY_RONM "ronm"  // Per-child relay-on duration (ms).
#define PCAT_SEMU_CKEY_ROFM "rofm"  // Per-child relay-off duration (ms).
#define PCAT_SEMU_CKEY_LCNT "lcnt"  // Per-child lead pulse count.
#define PCAT_SEMU_CKEY_LSTM "lstm"  // Per-child lead step interval (ms).
#define PCAT_SEMU_CKEY_TFAA "tfaa"  // Per-child TFLuna-A I2C address.
#define PCAT_SEMU_CKEY_TFBA "tfba"  // Per-child TFLuna-B I2C address.
#define PCAT_SEMU_CKEY_TFFP "tffp"  // Per-child TFLuna frame rate (fps).

// Public setting keys.
#define PCAT_SEMU_SET_DNAME "device_name"
#define PCAT_SEMU_SET_CHAN "channel"
#define PCAT_SEMU_SET_SCNT "sensor_count"
#define PCAT_SEMU_SET_PREV "prev_mac"
#define PCAT_SEMU_SET_NEXT "next_mac"
#define PCAT_SEMU_SET_POSR "pos_relays"
#define PCAT_SEMU_SET_NEGR "neg_relays"
#define PCAT_SEMU_SET_VON "von_ms"
#define PCAT_SEMU_SET_VLCNT "vlead_count"
#define PCAT_SEMU_SET_VLMS "vlead_ms"
#define PCAT_SEMU_SET_VENV "venv_enable"
#define PCAT_SEMU_SET_ALS0 "als_t0_lux"
#define PCAT_SEMU_SET_ALS1 "als_t1_lux"
#define PCAT_SEMU_SET_LOOPA "LoopAuto"
#define PCAT_SEMU_SET_FANMD "fan_mode"
#define PCAT_SEMU_SET_BUZEN "buzzer_enable"
#define PCAT_SEMU_SET_LEDFB "led_feedback_enable"
#define PCAT_SEMU_SET_RGBIDL "rgb_idle_color"
#define PCAT_SEMU_SET_RGBALT "rgb_alert_color"
#define PCAT_SEMU_SET_RGBBRT "rgb_brightness"
#define PCAT_SEMU_SET_PSHEN "push_enabled"
#define PCAT_SEMU_SET_PSHMD "push_mode"
#define PCAT_SEMU_SET_PSHI "push_interval_ms"
#define PCAT_SEMU_SET_PSHD "push_delta_abs"
#define PCAT_SEMU_SET_PSHG "push_min_gap_ms"
#define PCAT_SEMU_SET_PSHS "push_metric_scope"
#define PCAT_SEMU_SET_TOPV "topo_version"
#define PCAT_SEMU_SET_TOPS "topo_seed_id"
#define PCAT_SEMU_SET_TOPST "topo_state"
#define PCAT_SEMU_SET_TOPR "topo_relay_targets_blob"
#define PCAT_SEMU_SET_TOPC "topo_commit_epoch_s"
// Legacy alias retained for transition; strict SEMU profile does not expose/accept it.
#define PCAT_SEMU_SET_LPRSEMU "lidar.provision.semu"
// Legacy alias retained for transition; strict SEMU profile does not expose/accept it.
#define PCAT_SEMU_SET_LPRSTA "lidar.provision.status"

// Telemetry keys.
#define PCAT_SEMU_MET_TEMP "env_temp_c"
#define PCAT_SEMU_MET_HUM "env_hum_pct"
#define PCAT_SEMU_MET_PRES "env_press_pa"
#define PCAT_SEMU_MET_LUX "lux"

// Defaults and ranges.
#define PCAT_SEMU_SET_CHAN_DEF 1U
#define PCAT_SEMU_SET_CHAN_MIN 1U
#define PCAT_SEMU_SET_CHAN_MAX 14U
#define PCAT_SEMU_SET_SCNT_DEF 8U
#define PCAT_SEMU_SET_SCNT_MIN 1U
#define PCAT_SEMU_SET_SCNT_MAX 8U
#define PCAT_SEMU_SET_VON_DEF 600U
#define PCAT_SEMU_SET_VON_MIN 0U
#define PCAT_SEMU_SET_VON_MAX 65535U
#define PCAT_SEMU_SET_VLCNT_DEF 3U
#define PCAT_SEMU_SET_VLCNT_MIN 0U
#define PCAT_SEMU_SET_VLCNT_MAX 255U
#define PCAT_SEMU_SET_VLMS_DEF 250U
#define PCAT_SEMU_SET_VLMS_MIN 0U
#define PCAT_SEMU_SET_VLMS_MAX 65535U
#define PCAT_SEMU_SET_VENV_DEF 1
#define PCAT_SEMU_SET_ALS0_DEF 180U
#define PCAT_SEMU_SET_ALS0_MIN 1U
#define PCAT_SEMU_SET_ALS0_MAX 65535U
#define PCAT_SEMU_SET_ALS1_DEF 300U
#define PCAT_SEMU_SET_ALS1_MIN 1U
#define PCAT_SEMU_SET_ALS1_MAX 65535U
#define PCAT_SEMU_SET_CALA_DEF 0U
#define PCAT_SEMU_SET_CALA_MIN 0U
#define PCAT_SEMU_SET_CALA_MAX 65535U
#define PCAT_SEMU_SET_CALB_DEF 0U
#define PCAT_SEMU_SET_CALB_MIN 0U
#define PCAT_SEMU_SET_CALB_MAX 65535U
#define PCAT_SEMU_SET_LOOPA_DEF 0
#define PCAT_SEMU_SET_FANMD_DEF 0U
#define PCAT_SEMU_SET_FANMD_MIN 0U
#define PCAT_SEMU_SET_FANMD_MAX 3U
#define PCAT_SEMU_SET_BUZEN_DEF 1
#define PCAT_SEMU_SET_LEDFB_DEF 1
#define PCAT_SEMU_SET_RGBIDL_DEF "#00ffaa"
#define PCAT_SEMU_SET_RGBALT_DEF "#ff3366"
#define PCAT_SEMU_SET_RGBBRT_DEF 180U
#define PCAT_SEMU_SET_RGBBRT_MIN 0U
#define PCAT_SEMU_SET_RGBBRT_MAX 255U
#define PCAT_SEMU_SET_PSHEN_DEF 0
#define PCAT_SEMU_SET_PSHMD_DEF "hybrid"
#define PCAT_SEMU_SET_PSHI_DEF 2000U
#define PCAT_SEMU_SET_PSHI_MIN 200U
#define PCAT_SEMU_SET_PSHI_MAX 60000U
#define PCAT_SEMU_SET_PSHD_DEF 0.0f
#define PCAT_SEMU_SET_PSHD_MIN 0.0f
#define PCAT_SEMU_SET_PSHD_MAX 100000.0f
#define PCAT_SEMU_SET_PSHG_DEF 200U
#define PCAT_SEMU_SET_PSHG_MIN 50U
#define PCAT_SEMU_SET_PSHG_MAX 10000U
#define PCAT_SEMU_SET_PSHS_DEF "all"
#define PCAT_SEMU_SET_TOPV_DEF 0U
#define PCAT_SEMU_SET_TOPC_DEF 0U

// Key maps used in capabilities.
#define PCAT_SEMU_SETMAP "device_name,channel,sensor_count,prev_mac,next_mac,pos_relays,neg_relays,von_ms,vlead_count,vlead_ms,venv_enable,als_t0_lux,als_t1_lux,LoopAuto,fan_mode,buzzer_enable,led_feedback_enable,rgb_idle_color,rgb_alert_color,rgb_brightness,push_enabled,push_mode,push_interval_ms,push_delta_abs,push_min_gap_ms,push_metric_scope,topo_version,topo_seed_id,topo_state,topo_relay_targets_blob,topo_commit_epoch_s"
#define PCAT_SEMU_METMAP "env_temp_c,env_hum_pct,env_press_pa,lux,v{0..7}.tfl_a_mm,v{0..7}.tfl_b_mm,v{0..7}.tfl_a_flux,v{0..7}.tfl_b_flux,v{0..7}.tfl_a_temp_c,v{0..7}.tfl_b_temp_c"
#define PCAT_SEMU_EVMAP "trigger_sent,topology_applied,virtual_sensor_fault"

#define PCAT_ASSERT_SEMU_CHILD_CODE4_LEN(code_literal) \
  static_assert((sizeof(code_literal) - 1U) == 4U, "SEMU child nvs code4 must be 4 chars: " #code_literal)

PCAT_ASSERT_NVS_KEY_LEN(PCAT_SEMU_KEY_DNAME);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_SEMU_KEY_CHAN);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_SEMU_KEY_SCNT);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_SEMU_KEY_PREV);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_SEMU_KEY_NEXT);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_SEMU_KEY_POSR);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_SEMU_KEY_NEGR);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_SEMU_KEY_VON);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_SEMU_KEY_VLCNT);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_SEMU_KEY_VLMS);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_SEMU_KEY_VENV);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_SEMU_KEY_ALS0);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_SEMU_KEY_ALS1);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_SEMU_KEY_LOOPA);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_SEMU_KEY_FANMD);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_SEMU_KEY_BUZEN);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_SEMU_KEY_LEDFB);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_SEMU_KEY_RGBIDL);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_SEMU_KEY_RGBALT);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_SEMU_KEY_RGBBRT);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_SEMU_KEY_PSHEN);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_SEMU_KEY_PSHMD);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_SEMU_KEY_PSHI);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_SEMU_KEY_PSHD);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_SEMU_KEY_PSHG);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_SEMU_KEY_PSHS);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_SEMU_KEY_TOPV);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_SEMU_KEY_TOPS);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_SEMU_KEY_TOPST);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_SEMU_KEY_TOPR);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_SEMU_KEY_TOPC);

PCAT_ASSERT_SEMU_CHILD_CODE4_LEN(PCAT_SEMU_CKEY_PREV);
PCAT_ASSERT_SEMU_CHILD_CODE4_LEN(PCAT_SEMU_CKEY_NEXT);
PCAT_ASSERT_SEMU_CHILD_CODE4_LEN(PCAT_SEMU_CKEY_POSR);
PCAT_ASSERT_SEMU_CHILD_CODE4_LEN(PCAT_SEMU_CKEY_NEGR);
PCAT_ASSERT_SEMU_CHILD_CODE4_LEN(PCAT_SEMU_CKEY_TFNR);
PCAT_ASSERT_SEMU_CHILD_CODE4_LEN(PCAT_SEMU_CKEY_TFFR);
PCAT_ASSERT_SEMU_CHILD_CODE4_LEN(PCAT_SEMU_CKEY_ABSP);
PCAT_ASSERT_SEMU_CHILD_CODE4_LEN(PCAT_SEMU_CKEY_CALA);
PCAT_ASSERT_SEMU_CHILD_CODE4_LEN(PCAT_SEMU_CKEY_CALB);
PCAT_ASSERT_SEMU_CHILD_CODE4_LEN(PCAT_SEMU_CKEY_ALS0);
PCAT_ASSERT_SEMU_CHILD_CODE4_LEN(PCAT_SEMU_CKEY_ALS1);
PCAT_ASSERT_SEMU_CHILD_CODE4_LEN(PCAT_SEMU_CKEY_CFMS);
PCAT_ASSERT_SEMU_CHILD_CODE4_LEN(PCAT_SEMU_CKEY_STMS);
PCAT_ASSERT_SEMU_CHILD_CODE4_LEN(PCAT_SEMU_CKEY_RONM);
PCAT_ASSERT_SEMU_CHILD_CODE4_LEN(PCAT_SEMU_CKEY_ROFM);
PCAT_ASSERT_SEMU_CHILD_CODE4_LEN(PCAT_SEMU_CKEY_LCNT);
PCAT_ASSERT_SEMU_CHILD_CODE4_LEN(PCAT_SEMU_CKEY_LSTM);
PCAT_ASSERT_SEMU_CHILD_CODE4_LEN(PCAT_SEMU_CKEY_TFAA);
PCAT_ASSERT_SEMU_CHILD_CODE4_LEN(PCAT_SEMU_CKEY_TFBA);
PCAT_ASSERT_SEMU_CHILD_CODE4_LEN(PCAT_SEMU_CKEY_TFFP);
