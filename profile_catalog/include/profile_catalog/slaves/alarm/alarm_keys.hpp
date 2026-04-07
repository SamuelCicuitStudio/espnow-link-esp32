#pragma once

#include "profile_catalog/shared/key_policy.hpp"

// Alarm profile identity defaults.
#define PCAT_ALARM_DEV_TYPE "ALARM"
#define PCAT_ALARM_HW_VER "ALARM-HW1"
#define PCAT_ALARM_SW_VER "0.1.0"
#define PCAT_ALARM_BUILD_TAG "alm2"
#define PCAT_ALARM_BUILD_ID BUILD_ID
#define PCAT_ALARM_DEF_NAME "ALARM-Node"

// ALARM persisted NVS keys (<= 6 chars).
#define PCAT_ALARM_KEY_DNAME "dname"
#define PCAT_ALARM_KEY_DEVID "devid"
#define PCAT_ALARM_KEY_ROLE "role"
#define PCAT_ALARM_KEY_CHAN "chan"
#define PCAT_ALARM_KEY_CFGED "cfged"
// Link-internal pairing/security keys (not app-facing settings in SETMAP).
#define PCAT_ALARM_KEY_MSTRMC "mstrmc"
#define PCAT_ALARM_KEY_MSLMK "mslmk"
#define PCAT_ALARM_KEY_SPMAX "spmax"
#define PCAT_ALARM_KEY_SPLIST "splist"
#define PCAT_ALARM_KEY_PRTMO "prtmo"
#define PCAT_ALARM_KEY_ARMED "armed"
#define PCAT_ALARM_KEY_MOTEN "moten"
#define PCAT_ALARM_KEY_BRCHL "brchl"
#define PCAT_ALARM_KEY_CTIME "ctime"
#define PCAT_ALARM_KEY_LTIME "ltime"
#define PCAT_ALARM_KEY_CAREED "careed"
#define PCAT_ALARM_KEY_CACCEL "caccel"
#define PCAT_ALARM_KEY_CFUEL "cfuel"
#define PCAT_ALARM_KEY_CRGB "crgb"
#define PCAT_ALARM_KEY_BTLVL "btlvl"
#define PCAT_ALARM_KEY_USLVL "uslvl"
#define PCAT_ALARM_KEY_RDLVL "rdlvl"
#define PCAT_ALARM_KEY_BTDBMS "btdbms"
#define PCAT_ALARM_KEY_RDDBMS "rddbms"
#define PCAT_ALARM_KEY_BTHOLD "bthold"
#define PCAT_ALARM_KEY_BTCGAP "btcgap"
#define PCAT_ALARM_KEY_BTFMS "btfms"
#define PCAT_ALARM_KEY_CBUEN "cbuen"
#define PCAT_ALARM_KEY_CBFMS "cbfms"
#define PCAT_ALARM_KEY_LBATT "lbatt"
#define PCAT_ALARM_KEY_EBATT "ebatt"
#define PCAT_ALARM_KEY_CBATT "cbatt"
#define PCAT_ALARM_KEY_PWEVAL "pweval"
#define PCAT_ALARM_KEY_GGTICK "ggtick"
#define PCAT_ALARM_KEY_BSTALE "bstale"
#define PCAT_ALARM_KEY_SLPIN "slpin"
#define PCAT_ALARM_KEY_SLWKUS "slwkus"
#define PCAT_ALARM_KEY_SLPCHK "slpchk"
#define PCAT_ALARM_KEY_PSHEN "pshen"
#define PCAT_ALARM_KEY_PSHMD "pshmd"
#define PCAT_ALARM_KEY_PSHINT "pshint"
#define PCAT_ALARM_KEY_PSHDLT "pshdlt"
#define PCAT_ALARM_KEY_PSHGAP "pshgap"
#define PCAT_ALARM_KEY_PSHSCP "pshscp"
#define PCAT_ALARM_KEY_RGBLVL "rgblvl"
#define PCAT_ALARM_KEY_RGBIDL "rgbidl"
#define PCAT_ALARM_KEY_RGBALT "rgbalt"
#define PCAT_ALARM_KEY_RGBBRT "rgbbrt"
#define PCAT_ALARM_KEY_LEDFB "ledfb"
#define PCAT_ALARM_KEY_CBAUD "cbaud"
#define PCAT_ALARM_KEY_SHTHR "shthr"
#define PCAT_ALARM_KEY_SHODR "shodr"
#define PCAT_ALARM_KEY_SHSCL "shscl"
#define PCAT_ALARM_KEY_SHRES "shres"
#define PCAT_ALARM_KEY_SHEVT "shevt"
#define PCAT_ALARM_KEY_SHDUR "shdur"
#define PCAT_ALARM_KEY_SHAXS "shaxs"
#define PCAT_ALARM_KEY_SHHPM "shhpm"
#define PCAT_ALARM_KEY_SHHPC "shhpc"
#define PCAT_ALARM_KEY_SHHPE "shhpe"
#define PCAT_ALARM_KEY_SHLAT "shlat"
#define PCAT_ALARM_KEY_SHILV "shilv"
#define PCAT_ALARM_KEY_SHCDMS "shcdms"
#define PCAT_ALARM_KEY_SHRGAP "shrgap"
#define PCAT_ALARM_KEY_SHCFCT "shcfct"
#define PCAT_ALARM_KEY_SHCFMS "shcfms"
#define PCAT_ALARM_KEY_ALMOT "almot"
#define PCAT_ALARM_KEY_ALRED "alred"
#define PCAT_ALARM_KEY_AENTRY "aentry"
#define PCAT_ALARM_KEY_AEXIT "aexit"
#define PCAT_ALARM_KEY_ALHLD "alhld"
#define PCAT_ALARM_KEY_ALGAP "algap"
#define PCAT_ALARM_KEY_ALRPT "alrpt"
#define PCAT_ALARM_KEY_ALCLR "alclr"
#define PCAT_ALARM_KEY_ALMCLR "almclr"
#define PCAT_ALARM_KEY_ALCMB "alcmb"
#define PCAT_ALARM_KEY_ALCMHS "alcmhs"
#define PCAT_ALARM_KEY_ALSIL "alsil"
#define PCAT_ALARM_KEY_ALPAT "alpat"
#define PCAT_ALARM_KEY_ALBRD "albrd"
#define PCAT_ALARM_KEY_ALBRM "albrm"
#define PCAT_ALARM_KEY_ALMGAT "almgat"
#define PCAT_ALARM_KEY_ALMWIN "almwin"
#define PCAT_ALARM_KEY_ALMCNT "almcnt"

// Public setting keys.
#define PCAT_ALARM_SET_DNAME "device_name"
#define PCAT_ALARM_SET_DEVID "device_id"
#define PCAT_ALARM_SET_ROLE "role_mode"
#define PCAT_ALARM_SET_CHAN "channel"
#define PCAT_ALARM_SET_CFGED "configured"
#define PCAT_ALARM_SET_ARMED "armed"
#define PCAT_ALARM_SET_MOTEN "motion_enabled"
#define PCAT_ALARM_SET_BRCHL "breach_latched"
#define PCAT_ALARM_SET_CTIME "current_time_epoch_s"
#define PCAT_ALARM_SET_LTIME "last_time_epoch_s"
#define PCAT_ALARM_SET_CAREED "cap_reed"
#define PCAT_ALARM_SET_CACCEL "cap_accel"
#define PCAT_ALARM_SET_CFUEL "cap_fuel_gauge"
#define PCAT_ALARM_SET_CRGB "cap_rgb"
#define PCAT_ALARM_SET_BTLVL "boot_active_level"
#define PCAT_ALARM_SET_USLVL "user_active_level"
#define PCAT_ALARM_SET_RDLVL "reed_active_level"
#define PCAT_ALARM_SET_BTDBMS "btn_debounce_ms"
#define PCAT_ALARM_SET_RDDBMS "reed_debounce_ms"
#define PCAT_ALARM_SET_BTHOLD "btn_hold_ms"
#define PCAT_ALARM_SET_BTCGAP "btn_consecutive_gap_ms"
#define PCAT_ALARM_SET_BTFMS "btn_finalize_ms"
#define PCAT_ALARM_SET_CBUEN "combo_boot_user_enable"
#define PCAT_ALARM_SET_CBFMS "combo_finalize_ms"
#define PCAT_ALARM_SET_LBATT "low_battery_pct"
#define PCAT_ALARM_SET_EBATT "emergency_battery_pct"
#define PCAT_ALARM_SET_CBATT "critical_battery_pct"
#define PCAT_ALARM_SET_PWEVAL "power_eval_ms"
#define PCAT_ALARM_SET_GGTICK "gauge_tick_ms"
#define PCAT_ALARM_SET_BSTALE "allow_stale_battery"
#define PCAT_ALARM_SET_SLPIN "sleep_inactive_ms"
#define PCAT_ALARM_SET_SLWKUS "sleep_wakeup_us"
#define PCAT_ALARM_SET_SLPCHK "sleep_check_min_ms"
#define PCAT_ALARM_SET_PSHEN "push_enabled"
#define PCAT_ALARM_SET_PSHMD "push_mode"
#define PCAT_ALARM_SET_PSHINT "push_interval_ms"
#define PCAT_ALARM_SET_PSHDLT "push_delta_abs"
#define PCAT_ALARM_SET_PSHGAP "push_min_gap_ms"
#define PCAT_ALARM_SET_PSHSCP "push_metric_scope"
#define PCAT_ALARM_SET_RGBLVL "rgb_active_level"
#define PCAT_ALARM_SET_RGBIDL "rgb_idle_color"
#define PCAT_ALARM_SET_RGBALT "rgb_alert_color"
#define PCAT_ALARM_SET_RGBBRT "rgb_brightness"
#define PCAT_ALARM_SET_LEDFB "led_feedback_enable"
#define PCAT_ALARM_SET_CBAUD "cli_baud"
#define PCAT_ALARM_SET_SHTHR "motion_threshold_raw"
#define PCAT_ALARM_SET_SHODR "motion_odr"
#define PCAT_ALARM_SET_SHSCL "motion_scale"
#define PCAT_ALARM_SET_SHRES "motion_resolution"
#define PCAT_ALARM_SET_SHEVT "motion_event_mode"
#define PCAT_ALARM_SET_SHDUR "motion_duration"
#define PCAT_ALARM_SET_SHAXS "motion_axis_mask"
#define PCAT_ALARM_SET_SHHPM "motion_hpf_mode"
#define PCAT_ALARM_SET_SHHPC "motion_hpf_cutoff"
#define PCAT_ALARM_SET_SHHPE "motion_hpf_enable"
#define PCAT_ALARM_SET_SHLAT "motion_latch_enable"
#define PCAT_ALARM_SET_SHILV "motion_int_level"
#define PCAT_ALARM_SET_SHCDMS "motion_cooldown_ms"
#define PCAT_ALARM_SET_SHRGAP "motion_report_gap_ms"
#define PCAT_ALARM_SET_SHCFCT "motion_confirm_count"
#define PCAT_ALARM_SET_SHCFMS "motion_confirm_window_ms"
#define PCAT_ALARM_SET_ALMOT "alarm_motion_enable"
#define PCAT_ALARM_SET_ALRED "alarm_reed_enable"
#define PCAT_ALARM_SET_AENTRY "alarm_entry_delay_ms"
#define PCAT_ALARM_SET_AEXIT "alarm_exit_delay_ms"
#define PCAT_ALARM_SET_ALHLD "alarm_trigger_hold_ms"
#define PCAT_ALARM_SET_ALGAP "alarm_retrigger_gap_ms"
#define PCAT_ALARM_SET_ALRPT "alarm_report_repeat_ms"
#define PCAT_ALARM_SET_ALCLR "alarm_auto_clear_s"
#define PCAT_ALARM_SET_ALMCLR "alarm_require_master_clear"
#define PCAT_ALARM_SET_ALCMB "alarm_combo_enabled"
#define PCAT_ALARM_SET_ALCMHS "alarm_combo_hold_ms"
#define PCAT_ALARM_SET_ALSIL "alarm_silent_mode"
#define PCAT_ALARM_SET_ALPAT "alarm_led_pattern"
#define PCAT_ALARM_SET_ALBRD "alarm_breach_on_reed"
#define PCAT_ALARM_SET_ALBRM "alarm_breach_on_motion"
#define PCAT_ALARM_SET_ALMGAT "alarm_motion_gate_after_boot_ms"
#define PCAT_ALARM_SET_ALMWIN "alarm_motion_window_ms"
#define PCAT_ALARM_SET_ALMCNT "alarm_motion_count"

// Telemetry keys.
#define PCAT_ALARM_MET_BATTPCT "battery_soc_pct"
#define PCAT_ALARM_MET_BATTV "battery_v"
#define PCAT_ALARM_MET_BATTI "battery_i"
#define PCAT_ALARM_MET_REED "reed_state"
#define PCAT_ALARM_MET_MOTION "motion_state"
#define PCAT_ALARM_MET_BREACH "breach_state"
#define PCAT_ALARM_MET_ARMED "armed_state"
#define PCAT_ALARM_MET_UPTIME "uptime_ms"

// Event keys.
#define PCAT_ALARM_EVT_REEDOPEN "reed_open"
#define PCAT_ALARM_EVT_REEDCLOSE "reed_close"
#define PCAT_ALARM_EVT_MOTION "motion_triggered"
#define PCAT_ALARM_EVT_BREACHSET "breach_set"
#define PCAT_ALARM_EVT_BREACHCLR "breach_cleared"
#define PCAT_ALARM_EVT_BATLOW "battery_low"
#define PCAT_ALARM_EVT_BATCRIT "battery_critical"
#define PCAT_ALARM_EVT_SLPPEND "sleep_pending"
#define PCAT_ALARM_EVT_SLPENTER "sleep_enter"
#define PCAT_ALARM_EVT_SLPWAKE "sleep_wake"

#define PCAT_ALARM_SETMAP                                                                                                                 \
  "device_name,device_id,role_mode,channel,configured,"                                                                                  \
  "armed,motion_enabled,breach_latched,current_time_epoch_s,last_time_epoch_s,cap_reed,cap_accel,cap_fuel_gauge,cap_rgb,"              \
  "boot_active_level,user_active_level,reed_active_level,btn_debounce_ms,reed_debounce_ms,btn_hold_ms,btn_consecutive_gap_ms,"          \
  "btn_finalize_ms,combo_boot_user_enable,combo_finalize_ms,low_battery_pct,emergency_battery_pct,critical_battery_pct,power_eval_ms,"  \
  "gauge_tick_ms,allow_stale_battery,sleep_inactive_ms,sleep_wakeup_us,sleep_check_min_ms,push_enabled,push_mode,push_interval_ms,"      \
  "push_delta_abs,push_min_gap_ms,push_metric_scope,rgb_active_level,rgb_idle_color,rgb_alert_color,rgb_brightness,led_feedback_enable," \
  "cli_baud,motion_threshold_raw,motion_odr,motion_scale,motion_resolution,motion_event_mode,motion_duration,motion_axis_mask,"           \
  "motion_hpf_mode,motion_hpf_cutoff,motion_hpf_enable,motion_latch_enable,motion_int_level,motion_cooldown_ms,motion_report_gap_ms,"    \
  "motion_confirm_count,motion_confirm_window_ms,alarm_motion_enable,alarm_reed_enable,alarm_entry_delay_ms,alarm_exit_delay_ms,"        \
  "alarm_trigger_hold_ms,alarm_retrigger_gap_ms,alarm_report_repeat_ms,alarm_auto_clear_s,alarm_require_master_clear,alarm_combo_enabled," \
  "alarm_combo_hold_ms,alarm_silent_mode,alarm_led_pattern,alarm_breach_on_reed,alarm_breach_on_motion,alarm_motion_gate_after_boot_ms," \
  "alarm_motion_window_ms,alarm_motion_count"

#define PCAT_ALARM_METMAP "battery_soc_pct,battery_v,battery_i,reed_state,motion_state,breach_state,armed_state,uptime_ms"
#define PCAT_ALARM_EVMAP "reed_open,reed_close,motion_triggered,breach_set,breach_cleared,battery_low,battery_critical,sleep_pending,sleep_enter,sleep_wake"

PCAT_ASSERT_NVS_KEY_LEN(PCAT_ALARM_KEY_DNAME);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ALARM_KEY_DEVID);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ALARM_KEY_ROLE);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ALARM_KEY_CHAN);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ALARM_KEY_CFGED);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ALARM_KEY_MSTRMC);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ALARM_KEY_MSLMK);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ALARM_KEY_SPMAX);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ALARM_KEY_SPLIST);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ALARM_KEY_PRTMO);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ALARM_KEY_ARMED);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ALARM_KEY_MOTEN);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ALARM_KEY_BRCHL);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ALARM_KEY_CTIME);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ALARM_KEY_LTIME);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ALARM_KEY_CAREED);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ALARM_KEY_CACCEL);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ALARM_KEY_CFUEL);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ALARM_KEY_CRGB);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ALARM_KEY_BTLVL);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ALARM_KEY_USLVL);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ALARM_KEY_RDLVL);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ALARM_KEY_BTDBMS);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ALARM_KEY_RDDBMS);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ALARM_KEY_BTHOLD);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ALARM_KEY_BTCGAP);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ALARM_KEY_BTFMS);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ALARM_KEY_CBUEN);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ALARM_KEY_CBFMS);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ALARM_KEY_LBATT);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ALARM_KEY_EBATT);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ALARM_KEY_CBATT);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ALARM_KEY_PWEVAL);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ALARM_KEY_GGTICK);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ALARM_KEY_BSTALE);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ALARM_KEY_SLPIN);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ALARM_KEY_SLWKUS);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ALARM_KEY_SLPCHK);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ALARM_KEY_PSHEN);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ALARM_KEY_PSHMD);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ALARM_KEY_PSHINT);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ALARM_KEY_PSHDLT);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ALARM_KEY_PSHGAP);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ALARM_KEY_PSHSCP);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ALARM_KEY_RGBLVL);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ALARM_KEY_RGBIDL);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ALARM_KEY_RGBALT);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ALARM_KEY_RGBBRT);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ALARM_KEY_LEDFB);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ALARM_KEY_CBAUD);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ALARM_KEY_SHTHR);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ALARM_KEY_SHODR);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ALARM_KEY_SHSCL);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ALARM_KEY_SHRES);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ALARM_KEY_SHEVT);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ALARM_KEY_SHDUR);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ALARM_KEY_SHAXS);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ALARM_KEY_SHHPM);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ALARM_KEY_SHHPC);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ALARM_KEY_SHHPE);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ALARM_KEY_SHLAT);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ALARM_KEY_SHILV);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ALARM_KEY_SHCDMS);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ALARM_KEY_SHRGAP);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ALARM_KEY_SHCFCT);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ALARM_KEY_SHCFMS);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ALARM_KEY_ALMOT);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ALARM_KEY_ALRED);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ALARM_KEY_AENTRY);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ALARM_KEY_AEXIT);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ALARM_KEY_ALHLD);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ALARM_KEY_ALGAP);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ALARM_KEY_ALRPT);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ALARM_KEY_ALCLR);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ALARM_KEY_ALMCLR);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ALARM_KEY_ALCMB);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ALARM_KEY_ALCMHS);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ALARM_KEY_ALSIL);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ALARM_KEY_ALPAT);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ALARM_KEY_ALBRD);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ALARM_KEY_ALBRM);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ALARM_KEY_ALMGAT);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ALARM_KEY_ALMWIN);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ALARM_KEY_ALMCNT);
