#pragma once

#include "profile_catalog/shared/key_policy.hpp"

// SENS profile identity defaults.
#define PCAT_SENS_DEV_TYPE "SENS"
#define PCAT_SENS_HW_VER "SENS-HW1"
#define PCAT_SENS_SW_VER "0.1.0"
#define PCAT_SENS_BUILD_TAG "sen1"
#define PCAT_SENS_BUILD_ID BUILD_ID
#define PCAT_SENS_DEF_NAME "SENS-Node"

// SENS NVS keys (<= 6 chars).
#define PCAT_SENS_KEY_DNAME "dname"  // Device display name.
#define PCAT_SENS_KEY_CHAN "chan"    // Radio channel.
#define PCAT_SENS_KEY_PREV "prevmc"  // Previous neighbor MAC.
#define PCAT_SENS_KEY_NEXT "nxtmac"  // Next neighbor MAC.
#define PCAT_SENS_KEY_POSR "posrl"   // Positive-direction relay targets (JSON).
#define PCAT_SENS_KEY_NEGR "negrl"   // Negative-direction relay targets (JSON).
#define PCAT_SENS_KEY_TFNR "tfnmm"   // TFL near threshold (mm).
#define PCAT_SENS_KEY_TFFR "tffmm"   // TFL far threshold (mm).
#define PCAT_SENS_KEY_ABSP "abspc"   // A/B spacing baseline (mm).
#define PCAT_SENS_KEY_ALS0 "als0"    // ALS lower threshold (lux).
#define PCAT_SENS_KEY_ALS1 "als1"    // ALS upper threshold (lux).
#define PCAT_SENS_KEY_CFM "cfmms"    // Confirm debounce time (ms).
#define PCAT_SENS_KEY_STP "stopms"   // Stop/clear timeout (ms).
#define PCAT_SENS_KEY_RON "ronms"    // Relay ON duration (ms).
#define PCAT_SENS_KEY_ROF "rofms"    // Relay OFF duration (ms).
#define PCAT_SENS_KEY_LCNT "lcnt"    // Lead pulse count.
#define PCAT_SENS_KEY_LSTP "lstep"   // Lead step interval (ms).
#define PCAT_SENS_KEY_TFAA "tfaad"   // TFLuna-A I2C address.
#define PCAT_SENS_KEY_TFBA "tfbad"   // TFLuna-B I2C address.
#define PCAT_SENS_KEY_TFFP "tffps"   // TFLuna frame rate (fps).
#define PCAT_SENS_KEY_LOOPA "loopa"  // Loop automation enable.
#define PCAT_SENS_KEY_FANMD "fanmd"  // Fan mode (0:auto,1:eco,2:forced,3:stopped).
#define PCAT_SENS_KEY_BUZEN "buzen"  // Audio ping buzzer feedback enable.
#define PCAT_SENS_KEY_LEDFB "ledfb"  // Audio ping LED feedback enable.
#define PCAT_SENS_KEY_RGBIDL "rgbidl" // RGB idle color.
#define PCAT_SENS_KEY_RGBALT "rgbalt" // RGB alert color.
#define PCAT_SENS_KEY_RGBBRT "rgbbrt" // RGB brightness (0..255).
#define PCAT_SENS_KEY_PSHEN "pshen"  // Telemetry push enable.
#define PCAT_SENS_KEY_PSHMD "pshmd"  // Telemetry push mode.
#define PCAT_SENS_KEY_PSHI "pshint"  // Telemetry push interval (ms).
#define PCAT_SENS_KEY_PSHD "pshdlt"  // Telemetry push delta threshold.
#define PCAT_SENS_KEY_PSHG "pshgap"  // Telemetry push min gap (ms).
#define PCAT_SENS_KEY_PSHS "pshscp"  // Telemetry push metric scope.
#define PCAT_SENS_KEY_TOPV "topver"  // Topology version counter.
#define PCAT_SENS_KEY_TOPS "topsid"  // Topology seed/session id.
#define PCAT_SENS_KEY_TOPST "topst"  // Topology state (staged/committed).
#define PCAT_SENS_KEY_TOPR "toprtg"  // Serialized topology relay-target map.
#define PCAT_SENS_KEY_TOPC "topcmt"  // Topology commit epoch (s).
#define PCAT_SENS_KEY_LPRQ "lprq"    // Last lidar provisioning request payload.
#define PCAT_SENS_KEY_LPRS "lprs"    // Last lidar provisioning status token.

// Public setting keys.
#define PCAT_SENS_SET_DNAME "device_name"
#define PCAT_SENS_SET_CHAN "channel"
#define PCAT_SENS_SET_PREV "prev_mac"
#define PCAT_SENS_SET_NEXT "next_mac"
#define PCAT_SENS_SET_POSR "pos_relays"
#define PCAT_SENS_SET_NEGR "neg_relays"
#define PCAT_SENS_SET_TFNR "tf_near_mm"
#define PCAT_SENS_SET_TFFR "tf_far_mm"
#define PCAT_SENS_SET_ABSP "ab_spacing_mm"
#define PCAT_SENS_SET_ALS0 "als_t0_lux"
#define PCAT_SENS_SET_ALS1 "als_t1_lux"
#define PCAT_SENS_SET_CFM "confirm_ms"
#define PCAT_SENS_SET_STP "stop_ms"
#define PCAT_SENS_SET_RON "relay_on_ms"
#define PCAT_SENS_SET_ROF "relay_off_ms"
#define PCAT_SENS_SET_LCNT "lead_count"
#define PCAT_SENS_SET_LSTP "lead_step_ms"
#define PCAT_SENS_SET_TFAA "tfl_a_addr"
#define PCAT_SENS_SET_TFBA "tfl_b_addr"
#define PCAT_SENS_SET_TFFP "tfl_fps"
#define PCAT_SENS_SET_LOOPA "LoopAuto"
#define PCAT_SENS_SET_FANMD "fan_mode"
#define PCAT_SENS_SET_BUZEN "buzzer_enable"
#define PCAT_SENS_SET_LEDFB "led_feedback_enable"
#define PCAT_SENS_SET_RGBIDL "rgb_idle_color"
#define PCAT_SENS_SET_RGBALT "rgb_alert_color"
#define PCAT_SENS_SET_RGBBRT "rgb_brightness"
#define PCAT_SENS_SET_PSHEN "push_enabled"
#define PCAT_SENS_SET_PSHMD "push_mode"
#define PCAT_SENS_SET_PSHI "push_interval_ms"
#define PCAT_SENS_SET_PSHD "push_delta_abs"
#define PCAT_SENS_SET_PSHG "push_min_gap_ms"
#define PCAT_SENS_SET_PSHS "push_metric_scope"
#define PCAT_SENS_SET_TOPV "topo_version"
#define PCAT_SENS_SET_TOPS "topo_seed_id"
#define PCAT_SENS_SET_TOPST "topo_state"
#define PCAT_SENS_SET_TOPR "topo_relay_targets_blob"
#define PCAT_SENS_SET_TOPC "topo_commit_epoch_s"
#define PCAT_SENS_SET_LPRSENS "lidar.provision.sens"
#define PCAT_SENS_SET_LPRSTA "lidar.provision.status"

// Telemetry keys.
#define PCAT_SENS_MET_TFLA "tfl_a_mm"
#define PCAT_SENS_MET_TFLB "tfl_b_mm"
#define PCAT_SENS_MET_TEMP "env_temp_c"
#define PCAT_SENS_MET_HUM "env_hum_pct"
#define PCAT_SENS_MET_PRES "env_press_pa"
#define PCAT_SENS_MET_LUX "lux"

// Defaults and ranges.
#define PCAT_SENS_SET_CHAN_DEF 1U
#define PCAT_SENS_SET_CHAN_MIN 1U
#define PCAT_SENS_SET_CHAN_MAX 14U
#define PCAT_SENS_SET_TFNR_DEF 200U
#define PCAT_SENS_SET_TFNR_MIN 0U
#define PCAT_SENS_SET_TFNR_MAX 65535U
#define PCAT_SENS_SET_TFFR_DEF 3200U
#define PCAT_SENS_SET_TFFR_MIN 0U
#define PCAT_SENS_SET_TFFR_MAX 65535U
#define PCAT_SENS_SET_ABSP_DEF 350U
#define PCAT_SENS_SET_ABSP_MIN 0U
#define PCAT_SENS_SET_ABSP_MAX 65535U
#define PCAT_SENS_SET_ALS0_DEF 180U
#define PCAT_SENS_SET_ALS0_MIN 1U
#define PCAT_SENS_SET_ALS0_MAX 65535U
#define PCAT_SENS_SET_ALS1_DEF 300U
#define PCAT_SENS_SET_ALS1_MIN 1U
#define PCAT_SENS_SET_ALS1_MAX 65535U
#define PCAT_SENS_SET_CFM_DEF 140U
#define PCAT_SENS_SET_CFM_MIN 0U
#define PCAT_SENS_SET_CFM_MAX 65535U
#define PCAT_SENS_SET_STP_DEF 1200U
#define PCAT_SENS_SET_STP_MIN 0U
#define PCAT_SENS_SET_STP_MAX 65535U
#define PCAT_SENS_SET_RON_DEF 600U
#define PCAT_SENS_SET_RON_MIN 0U
#define PCAT_SENS_SET_RON_MAX 65535U
#define PCAT_SENS_SET_ROF_DEF 0U
#define PCAT_SENS_SET_ROF_MIN 0U
#define PCAT_SENS_SET_ROF_MAX 65535U
#define PCAT_SENS_SET_LCNT_DEF 3U
#define PCAT_SENS_SET_LCNT_MIN 0U
#define PCAT_SENS_SET_LCNT_MAX 255U
#define PCAT_SENS_SET_LSTP_DEF 250U
#define PCAT_SENS_SET_LSTP_MIN 0U
#define PCAT_SENS_SET_LSTP_MAX 65535U
#define PCAT_SENS_SET_TFAA_DEF 0x10U
#define PCAT_SENS_SET_TFAA_MIN 0U
#define PCAT_SENS_SET_TFAA_MAX 255U
#define PCAT_SENS_SET_TFBA_DEF 0x11U
#define PCAT_SENS_SET_TFBA_MIN 0U
#define PCAT_SENS_SET_TFBA_MAX 255U
#define PCAT_SENS_SET_TFFP_DEF 0U
#define PCAT_SENS_SET_TFFP_MIN 0U
#define PCAT_SENS_SET_TFFP_MAX 250U
#define PCAT_SENS_SET_LOOPA_DEF 0
#define PCAT_SENS_SET_FANMD_DEF 0U
#define PCAT_SENS_SET_FANMD_MIN 0U
#define PCAT_SENS_SET_FANMD_MAX 3U
#define PCAT_SENS_SET_BUZEN_DEF 1
#define PCAT_SENS_SET_LEDFB_DEF 1
#define PCAT_SENS_SET_RGBIDL_DEF "#00ffaa"
#define PCAT_SENS_SET_RGBALT_DEF "#ff3366"
#define PCAT_SENS_SET_RGBBRT_DEF 180U
#define PCAT_SENS_SET_RGBBRT_MIN 0U
#define PCAT_SENS_SET_RGBBRT_MAX 255U
#define PCAT_SENS_SET_PSHEN_DEF 0
#define PCAT_SENS_SET_PSHMD_DEF "hybrid"
#define PCAT_SENS_SET_PSHI_DEF 2000U
#define PCAT_SENS_SET_PSHI_MIN 200U
#define PCAT_SENS_SET_PSHI_MAX 60000U
#define PCAT_SENS_SET_PSHD_DEF 0.0f
#define PCAT_SENS_SET_PSHD_MIN 0.0f
#define PCAT_SENS_SET_PSHD_MAX 100000.0f
#define PCAT_SENS_SET_PSHG_DEF 200U
#define PCAT_SENS_SET_PSHG_MIN 50U
#define PCAT_SENS_SET_PSHG_MAX 10000U
#define PCAT_SENS_SET_PSHS_DEF "all"
#define PCAT_SENS_SET_TOPV_DEF 0U
#define PCAT_SENS_SET_TOPC_DEF 0U
#define PCAT_SENS_SET_LPRSENS_DEF "slot=A,source_addr=16"
#define PCAT_SENS_SET_LPRSTA_DEF "status=1"

// Key maps used in capabilities.
#define PCAT_SENS_SETMAP "device_name,channel,prev_mac,next_mac,pos_relays,neg_relays,tf_near_mm,tf_far_mm,ab_spacing_mm,als_t0_lux,als_t1_lux,confirm_ms,stop_ms,relay_on_ms,relay_off_ms,lead_count,lead_step_ms,tfl_a_addr,tfl_b_addr,tfl_fps,LoopAuto,fan_mode,buzzer_enable,led_feedback_enable,rgb_idle_color,rgb_alert_color,rgb_brightness,push_enabled,push_mode,push_interval_ms,push_delta_abs,push_min_gap_ms,push_metric_scope,topo_version,topo_seed_id,topo_state,topo_relay_targets_blob,topo_commit_epoch_s,lidar.provision.sens,lidar.provision.status"
#define PCAT_SENS_METMAP "tfl_a_mm,tfl_b_mm,env_temp_c,env_hum_pct,env_press_pa,lux"
#define PCAT_SENS_EVMAP "trigger_sent,topology_applied,sensor_fault"

// Compile-time NVS key policy checks.
PCAT_ASSERT_NVS_KEY_LEN(PCAT_SENS_KEY_DNAME);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_SENS_KEY_CHAN);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_SENS_KEY_PREV);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_SENS_KEY_NEXT);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_SENS_KEY_POSR);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_SENS_KEY_NEGR);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_SENS_KEY_TFNR);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_SENS_KEY_TFFR);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_SENS_KEY_ABSP);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_SENS_KEY_ALS0);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_SENS_KEY_ALS1);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_SENS_KEY_CFM);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_SENS_KEY_STP);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_SENS_KEY_RON);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_SENS_KEY_ROF);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_SENS_KEY_LCNT);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_SENS_KEY_LSTP);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_SENS_KEY_TFAA);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_SENS_KEY_TFBA);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_SENS_KEY_TFFP);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_SENS_KEY_LOOPA);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_SENS_KEY_FANMD);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_SENS_KEY_BUZEN);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_SENS_KEY_LEDFB);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_SENS_KEY_RGBIDL);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_SENS_KEY_RGBALT);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_SENS_KEY_RGBBRT);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_SENS_KEY_PSHEN);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_SENS_KEY_PSHMD);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_SENS_KEY_PSHI);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_SENS_KEY_PSHD);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_SENS_KEY_PSHG);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_SENS_KEY_PSHS);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_SENS_KEY_TOPV);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_SENS_KEY_TOPS);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_SENS_KEY_TOPST);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_SENS_KEY_TOPR);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_SENS_KEY_TOPC);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_SENS_KEY_LPRQ);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_SENS_KEY_LPRS);
