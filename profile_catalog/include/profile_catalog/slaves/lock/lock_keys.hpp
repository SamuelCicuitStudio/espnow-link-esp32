#pragma once

#include "profile_catalog/shared/key_policy.hpp"

// Lock profile identity defaults.
#define PCAT_LOCK_DEV_TYPE "LOCK"
#define PCAT_LOCK_HW_VER "LOCK-HW1"
#define PCAT_LOCK_SW_VER "0.1.0"
#define PCAT_LOCK_BUILD_TAG "lok2"
#define PCAT_LOCK_BUILD_ID BUILD_ID
#define PCAT_LOCK_DEF_NAME "LOCK-Node"

// LOCK persisted NVS keys (<= 6 chars).
#define PCAT_LOCK_KEY_DNAME "dname"
#define PCAT_LOCK_KEY_DEVID "devid"
#define PCAT_LOCK_KEY_ROLE "role"
#define PCAT_LOCK_KEY_CHAN "chan"
#define PCAT_LOCK_KEY_CFGED "cfged"
#define PCAT_LOCK_KEY_MSTRMC "mstrmc"
#define PCAT_LOCK_KEY_MSLMK "mslmk"
#define PCAT_LOCK_KEY_SPMAX "spmax"
#define PCAT_LOCK_KEY_SPLIST "splist"
#define PCAT_LOCK_KEY_PRTMO "prtmo"
#define PCAT_LOCK_KEY_ARMED "armed"
#define PCAT_LOCK_KEY_MOTEN "moten"
#define PCAT_LOCK_KEY_BRCHL "brchl"
#define PCAT_LOCK_KEY_CTIME "ctime"
#define PCAT_LOCK_KEY_LTIME "ltime"
#define PCAT_LOCK_KEY_CAOPN "caopn"
#define PCAT_LOCK_KEY_CAREED "careed"
#define PCAT_LOCK_KEY_CACCEL "caccel"
#define PCAT_LOCK_KEY_CAFP "cafp"
#define PCAT_LOCK_KEY_CAMTR "camtr"
#define PCAT_LOCK_KEY_CFUEL "cfuel"
#define PCAT_LOCK_KEY_CRGB "crgb"
#define PCAT_LOCK_KEY_BTLVL "btlvl"
#define PCAT_LOCK_KEY_USLVL "uslvl"
#define PCAT_LOCK_KEY_RDLVL "rdlvl"
#define PCAT_LOCK_KEY_BTDBMS "btdbms"
#define PCAT_LOCK_KEY_RDDBMS "rddbms"
#define PCAT_LOCK_KEY_BTHOLD "bthold"
#define PCAT_LOCK_KEY_BTCGAP "btcgap"
#define PCAT_LOCK_KEY_BTFMS "btfms"
#define PCAT_LOCK_KEY_CBUEN "cbuen"
#define PCAT_LOCK_KEY_CBFMS "cbfms"
#define PCAT_LOCK_KEY_LBATT "lbatt"
#define PCAT_LOCK_KEY_EBATT "ebatt"
#define PCAT_LOCK_KEY_CBATT "cbatt"
#define PCAT_LOCK_KEY_PWEVAL "pweval"
#define PCAT_LOCK_KEY_GGTICK "ggtick"
#define PCAT_LOCK_KEY_BSTALE "bstale"
#define PCAT_LOCK_KEY_SLPIN "slpin"
#define PCAT_LOCK_KEY_SLWKUS "slwkus"
#define PCAT_LOCK_KEY_SLPCHK "slpchk"
#define PCAT_LOCK_KEY_PSHEN "pshen"
#define PCAT_LOCK_KEY_PSHMD "pshmd"
#define PCAT_LOCK_KEY_PSHINT "pshint"
#define PCAT_LOCK_KEY_PSHDLT "pshdlt"
#define PCAT_LOCK_KEY_PSHGAP "pshgap"
#define PCAT_LOCK_KEY_PSHSCP "pshscp"
#define PCAT_LOCK_KEY_RGBLVL "rgblvl"
#define PCAT_LOCK_KEY_RGBIDL "rgbidl"
#define PCAT_LOCK_KEY_RGBALT "rgbalt"
#define PCAT_LOCK_KEY_RGBBRT "rgbbrt"
#define PCAT_LOCK_KEY_LEDFB "ledfb"
#define PCAT_LOCK_KEY_CBAUD "cbaud"
#define PCAT_LOCK_KEY_SHTHR "shthr"
#define PCAT_LOCK_KEY_SHODR "shodr"
#define PCAT_LOCK_KEY_SHSCL "shscl"
#define PCAT_LOCK_KEY_SHRES "shres"
#define PCAT_LOCK_KEY_SHEVT "shevt"
#define PCAT_LOCK_KEY_SHDUR "shdur"
#define PCAT_LOCK_KEY_SHAXS "shaxs"
#define PCAT_LOCK_KEY_SHHPM "shhpm"
#define PCAT_LOCK_KEY_SHHPC "shhpc"
#define PCAT_LOCK_KEY_SHHPE "shhpe"
#define PCAT_LOCK_KEY_SHLAT "shlat"
#define PCAT_LOCK_KEY_SHILV "shilv"
#define PCAT_LOCK_KEY_SHCDMS "shcdms"
#define PCAT_LOCK_KEY_SHRGAP "shrgap"
#define PCAT_LOCK_KEY_SHCFCT "shcfct"
#define PCAT_LOCK_KEY_SHCFMS "shcfms"
#define PCAT_LOCK_KEY_LCKST "lckst"
#define PCAT_LOCK_KEY_LKDIR "lkdir"
#define PCAT_LOCK_KEY_LKEMAG "lkemag"
#define PCAT_LOCK_KEY_LKTOUT "lktout"
#define PCAT_LOCK_KEY_MSETTL "msettl"
#define PCAT_LOCK_KEY_MPWM "mpwm"
#define PCAT_LOCK_KEY_MBRK "mbrk"
#define PCAT_LOCK_KEY_OPLVL "oplvl"
#define PCAT_LOCK_KEY_OPDBMS "opdbms"
#define PCAT_LOCK_KEY_E1LVL "e1lvl"
#define PCAT_LOCK_KEY_E2LVL "e2lvl"
#define PCAT_LOCK_KEY_EDBMS "edbms"
#define PCAT_LOCK_KEY_ARLEN "arlen"
#define PCAT_LOCK_KEY_ARLMS "arlms"
#define PCAT_LOCK_KEY_URTMO "urtmo"
#define PCAT_LOCK_KEY_FRCLK "frclk"
#define PCAT_LOCK_KEY_AUARM "auarm"
#define PCAT_LOCK_KEY_LKRTRY "lkrtry"
#define PCAT_LOCK_KEY_LKRGMS "lkrgms"
#define PCAT_LOCK_KEY_RDBREH "rdbreh"
#define PCAT_LOCK_KEY_FPENA "fpena"
#define PCAT_LOCK_KEY_FPDEV "fpdev"
#define PCAT_LOCK_KEY_FPSEC "fpsec"
#define PCAT_LOCK_KEY_FPPKT "fppkt"
#define PCAT_LOCK_KEY_FBAUD "fbaud"
#define PCAT_LOCK_KEY_FPCMIN "fpcmin"
#define PCAT_LOCK_KEY_FPSTMS "fpstms"
#define PCAT_LOCK_KEY_FPETMS "fpetms"
#define PCAT_LOCK_KEY_FPLCNT "fplcnt"
#define PCAT_LOCK_KEY_FPLKMS "fplkms"
#define PCAT_LOCK_KEY_FPWKMS "fpwkms"
#define PCAT_LOCK_KEY_FPLMOD "fplmod"

// Public setting keys.
#define PCAT_LOCK_SET_DNAME "device_name"
#define PCAT_LOCK_SET_DEVID "device_id"
#define PCAT_LOCK_SET_ROLE "role_mode"
#define PCAT_LOCK_SET_CHAN "channel"
#define PCAT_LOCK_SET_CFGED "configured"
#define PCAT_LOCK_SET_MSTRMC "master_mac"
#define PCAT_LOCK_SET_MSLMK "master_lmk_hex"
#define PCAT_LOCK_SET_SPMAX "secure_peer_max"
#define PCAT_LOCK_SET_SPLIST "secure_peer_list"
#define PCAT_LOCK_SET_PRTMO "pair_timeout_ms"
#define PCAT_LOCK_SET_ARMED "armed"
#define PCAT_LOCK_SET_MOTEN "motion_enabled"
#define PCAT_LOCK_SET_BRCHL "breach_latched"
#define PCAT_LOCK_SET_CTIME "current_time_epoch_s"
#define PCAT_LOCK_SET_LTIME "last_time_epoch_s"
#define PCAT_LOCK_SET_CAOPN "cap_open_button"
#define PCAT_LOCK_SET_CAREED "cap_reed"
#define PCAT_LOCK_SET_CACCEL "cap_accel"
#define PCAT_LOCK_SET_CAFP "cap_fingerprint"
#define PCAT_LOCK_SET_CAMTR "cap_motor"
#define PCAT_LOCK_SET_CFUEL "cap_fuel_gauge"
#define PCAT_LOCK_SET_CRGB "cap_rgb"
#define PCAT_LOCK_SET_BTLVL "boot_active_level"
#define PCAT_LOCK_SET_USLVL "user_active_level"
#define PCAT_LOCK_SET_RDLVL "reed_active_level"
#define PCAT_LOCK_SET_BTDBMS "btn_debounce_ms"
#define PCAT_LOCK_SET_RDDBMS "reed_debounce_ms"
#define PCAT_LOCK_SET_BTHOLD "btn_hold_ms"
#define PCAT_LOCK_SET_BTCGAP "btn_consecutive_gap_ms"
#define PCAT_LOCK_SET_BTFMS "btn_finalize_ms"
#define PCAT_LOCK_SET_CBUEN "combo_boot_user_enable"
#define PCAT_LOCK_SET_CBFMS "combo_finalize_ms"
#define PCAT_LOCK_SET_LBATT "low_battery_pct"
#define PCAT_LOCK_SET_EBATT "emergency_battery_pct"
#define PCAT_LOCK_SET_CBATT "critical_battery_pct"
#define PCAT_LOCK_SET_PWEVAL "power_eval_ms"
#define PCAT_LOCK_SET_GGTICK "gauge_tick_ms"
#define PCAT_LOCK_SET_BSTALE "allow_stale_battery"
#define PCAT_LOCK_SET_SLPIN "sleep_inactive_ms"
#define PCAT_LOCK_SET_SLWKUS "sleep_wakeup_us"
#define PCAT_LOCK_SET_SLPCHK "sleep_check_min_ms"
#define PCAT_LOCK_SET_PSHEN "push_enabled"
#define PCAT_LOCK_SET_PSHMD "push_mode"
#define PCAT_LOCK_SET_PSHINT "push_interval_ms"
#define PCAT_LOCK_SET_PSHDLT "push_delta_abs"
#define PCAT_LOCK_SET_PSHGAP "push_min_gap_ms"
#define PCAT_LOCK_SET_PSHSCP "push_metric_scope"
#define PCAT_LOCK_SET_RGBLVL "rgb_active_level"
#define PCAT_LOCK_SET_RGBIDL "rgb_idle_color"
#define PCAT_LOCK_SET_RGBALT "rgb_alert_color"
#define PCAT_LOCK_SET_RGBBRT "rgb_brightness"
#define PCAT_LOCK_SET_LEDFB "led_feedback_enable"
#define PCAT_LOCK_SET_CBAUD "cli_baud"
#define PCAT_LOCK_SET_SHTHR "motion_threshold_raw"
#define PCAT_LOCK_SET_SHODR "motion_odr"
#define PCAT_LOCK_SET_SHSCL "motion_scale"
#define PCAT_LOCK_SET_SHRES "motion_resolution"
#define PCAT_LOCK_SET_SHEVT "motion_event_mode"
#define PCAT_LOCK_SET_SHDUR "motion_duration"
#define PCAT_LOCK_SET_SHAXS "motion_axis_mask"
#define PCAT_LOCK_SET_SHHPM "motion_hpf_mode"
#define PCAT_LOCK_SET_SHHPC "motion_hpf_cutoff"
#define PCAT_LOCK_SET_SHHPE "motion_hpf_enable"
#define PCAT_LOCK_SET_SHLAT "motion_latch_enable"
#define PCAT_LOCK_SET_SHILV "motion_int_level"
#define PCAT_LOCK_SET_SHCDMS "motion_cooldown_ms"
#define PCAT_LOCK_SET_SHRGAP "motion_report_gap_ms"
#define PCAT_LOCK_SET_SHCFCT "motion_confirm_count"
#define PCAT_LOCK_SET_SHCFMS "motion_confirm_window_ms"
#define PCAT_LOCK_SET_LCKST "lock_state"
#define PCAT_LOCK_SET_LKDIR "lock_direction_default"
#define PCAT_LOCK_SET_LKEMAG "lock_mode_emag"
#define PCAT_LOCK_SET_LKTOUT "lock_timeout_ms"
#define PCAT_LOCK_SET_MSETTL "motor_settle_ms"
#define PCAT_LOCK_SET_MPWM "motor_pwm_pct"
#define PCAT_LOCK_SET_MBRK "motor_brake_pct"
#define PCAT_LOCK_SET_OPLVL "open_active_level"
#define PCAT_LOCK_SET_OPDBMS "open_debounce_ms"
#define PCAT_LOCK_SET_E1LVL "endstop_a_level"
#define PCAT_LOCK_SET_E2LVL "endstop_b_level"
#define PCAT_LOCK_SET_EDBMS "endstop_debounce_ms"
#define PCAT_LOCK_SET_ARLEN "auto_relock_enable"
#define PCAT_LOCK_SET_ARLMS "auto_relock_ms"
#define PCAT_LOCK_SET_URTMO "unlock_request_timeout_ms"
#define PCAT_LOCK_SET_FRCLK "force_lock_ignore_open"
#define PCAT_LOCK_SET_AUARM "allow_unlock_when_armed"
#define PCAT_LOCK_SET_LKRTRY "lock_retry_count"
#define PCAT_LOCK_SET_LKRGMS "lock_retry_gap_ms"
#define PCAT_LOCK_SET_RDBREH "reed_open_is_breach"
#define PCAT_LOCK_SET_FPENA "fingerprint_enabled"
#define PCAT_LOCK_SET_FPDEV "fp_device_configured"
#define PCAT_LOCK_SET_FPSEC "fp_security_level"
#define PCAT_LOCK_SET_FPPKT "fp_packet_size"
#define PCAT_LOCK_SET_FBAUD "fp_baud"
#define PCAT_LOCK_SET_FPCMIN "fp_match_conf_min"
#define PCAT_LOCK_SET_FPSTMS "fp_search_timeout_ms"
#define PCAT_LOCK_SET_FPETMS "fp_enroll_timeout_ms"
#define PCAT_LOCK_SET_FPLCNT "fp_lockout_fail_count"
#define PCAT_LOCK_SET_FPLKMS "fp_lockout_ms"
#define PCAT_LOCK_SET_FPWKMS "fp_wake_pulse_ms"
#define PCAT_LOCK_SET_FPLMOD "fp_led_mode"

// Telemetry keys.
#define PCAT_LOCK_MET_BATTPCT "battery_soc_pct"
#define PCAT_LOCK_MET_BATTV "battery_v"
#define PCAT_LOCK_MET_BATTI "battery_i"
#define PCAT_LOCK_MET_REED "reed_state"
#define PCAT_LOCK_MET_MOTION "motion_state"
#define PCAT_LOCK_MET_LOCK "lock_state"
#define PCAT_LOCK_MET_MOTOR "motor_state"
#define PCAT_LOCK_MET_BREACH "breach_state"
#define PCAT_LOCK_MET_ARMED "armed_state"
#define PCAT_LOCK_MET_UPTIME "uptime_ms"

// Event keys.
#define PCAT_LOCK_EVT_LOCKDONE "lock_done"
#define PCAT_LOCK_EVT_UNLOCKDONE "unlock_done"
#define PCAT_LOCK_EVT_OPENBTN "open_button_pressed"
#define PCAT_LOCK_EVT_REEDOPEN "reed_open"
#define PCAT_LOCK_EVT_REEDCLOSE "reed_close"
#define PCAT_LOCK_EVT_MOTION "motion_triggered"
#define PCAT_LOCK_EVT_BREACHSET "breach_set"
#define PCAT_LOCK_EVT_BREACHCLR "breach_cleared"
#define PCAT_LOCK_EVT_FPMATCH "fp_match"
#define PCAT_LOCK_EVT_FPFAIL "fp_fail"
#define PCAT_LOCK_EVT_BATLOW "battery_low"
#define PCAT_LOCK_EVT_BATCRIT "battery_critical"
#define PCAT_LOCK_EVT_SLPPEND "sleep_pending"
#define PCAT_LOCK_EVT_SLPENTER "sleep_enter"
#define PCAT_LOCK_EVT_SLPWAKE "sleep_wake"

#define PCAT_LOCK_SETMAP                                                                                                             \
  "device_name,device_id,role_mode,channel,configured,master_mac,master_lmk_hex,secure_peer_max,secure_peer_list,pair_timeout_ms," \
  "armed,motion_enabled,breach_latched,current_time_epoch_s,last_time_epoch_s,cap_open_button,cap_reed,cap_accel,cap_fingerprint,"  \
  "cap_motor,cap_fuel_gauge,cap_rgb,boot_active_level,user_active_level,reed_active_level,btn_debounce_ms,reed_debounce_ms,"        \
  "btn_hold_ms,btn_consecutive_gap_ms,btn_finalize_ms,combo_boot_user_enable,combo_finalize_ms,low_battery_pct,emergency_battery_pct," \
  "critical_battery_pct,power_eval_ms,gauge_tick_ms,allow_stale_battery,sleep_inactive_ms,sleep_wakeup_us,sleep_check_min_ms,"      \
  "push_enabled,push_mode,push_interval_ms,push_delta_abs,push_min_gap_ms,push_metric_scope,rgb_active_level,rgb_idle_color,"        \
  "rgb_alert_color,rgb_brightness,led_feedback_enable,cli_baud,motion_threshold_raw,motion_odr,motion_scale,motion_resolution,"      \
  "motion_event_mode,motion_duration,motion_axis_mask,motion_hpf_mode,motion_hpf_cutoff,motion_hpf_enable,motion_latch_enable,"      \
  "motion_int_level,motion_cooldown_ms,motion_report_gap_ms,motion_confirm_count,motion_confirm_window_ms,lock_state,"               \
  "lock_direction_default,lock_mode_emag,lock_timeout_ms,motor_settle_ms,motor_pwm_pct,motor_brake_pct,open_active_level,"           \
  "open_debounce_ms,endstop_a_level,endstop_b_level,endstop_debounce_ms,auto_relock_enable,auto_relock_ms,unlock_request_timeout_ms," \
  "force_lock_ignore_open,allow_unlock_when_armed,lock_retry_count,lock_retry_gap_ms,reed_open_is_breach,fingerprint_enabled,"       \
  "fp_device_configured,fp_security_level,fp_packet_size,fp_baud,fp_match_conf_min,fp_search_timeout_ms,fp_enroll_timeout_ms,"       \
  "fp_lockout_fail_count,fp_lockout_ms,fp_wake_pulse_ms,fp_led_mode"

#define PCAT_LOCK_METMAP "battery_soc_pct,battery_v,battery_i,reed_state,motion_state,lock_state,motor_state,breach_state,armed_state,uptime_ms"
#define PCAT_LOCK_EVMAP "lock_done,unlock_done,open_button_pressed,reed_open,reed_close,motion_triggered,breach_set,breach_cleared,fp_match,fp_fail,battery_low,battery_critical,sleep_pending,sleep_enter,sleep_wake"

PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_DNAME);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_DEVID);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_ROLE);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_CHAN);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_CFGED);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_MSTRMC);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_MSLMK);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_SPMAX);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_SPLIST);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_PRTMO);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_ARMED);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_MOTEN);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_BRCHL);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_CTIME);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_LTIME);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_CAOPN);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_CAREED);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_CACCEL);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_CAFP);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_CAMTR);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_CFUEL);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_CRGB);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_BTLVL);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_USLVL);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_RDLVL);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_BTDBMS);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_RDDBMS);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_BTHOLD);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_BTCGAP);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_BTFMS);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_CBUEN);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_CBFMS);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_LBATT);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_EBATT);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_CBATT);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_PWEVAL);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_GGTICK);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_BSTALE);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_SLPIN);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_SLWKUS);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_SLPCHK);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_PSHEN);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_PSHMD);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_PSHINT);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_PSHDLT);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_PSHGAP);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_PSHSCP);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_RGBLVL);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_RGBIDL);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_RGBALT);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_RGBBRT);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_LEDFB);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_CBAUD);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_SHTHR);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_SHODR);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_SHSCL);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_SHRES);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_SHEVT);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_SHDUR);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_SHAXS);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_SHHPM);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_SHHPC);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_SHHPE);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_SHLAT);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_SHILV);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_SHCDMS);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_SHRGAP);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_SHCFCT);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_SHCFMS);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_LCKST);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_LKDIR);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_LKEMAG);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_LKTOUT);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_MSETTL);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_MPWM);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_MBRK);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_OPLVL);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_OPDBMS);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_E1LVL);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_E2LVL);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_EDBMS);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_ARLEN);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_ARLMS);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_URTMO);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_FRCLK);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_AUARM);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_LKRTRY);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_LKRGMS);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_RDBREH);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_FPENA);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_FPDEV);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_FPSEC);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_FPPKT);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_FBAUD);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_FPCMIN);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_FPSTMS);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_FPETMS);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_FPLCNT);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_FPLKMS);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_FPWKMS);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_FPLMOD);
