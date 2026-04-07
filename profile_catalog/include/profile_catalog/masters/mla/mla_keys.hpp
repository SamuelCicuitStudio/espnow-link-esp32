#pragma once

#include "profile_catalog/shared/key_policy.hpp"

// MLA profile identity defaults.
#define PCAT_MLA_DEV_TYPE "MLA"
#define PCAT_MLA_HW_VER "MLA-HW1"
#define PCAT_MLA_SW_VER "0.1.0"
#define PCAT_MLA_BUILD_TAG "mla1"
#define PCAT_MLA_BUILD_ID BUILD_ID
#define PCAT_MLA_DEF_NAME "MLA-Node"

// MLA persisted NVS setting keys (<= 6 chars).
// Identity.
#define PCAT_MLA_KEY_DNAME "dname"
#define PCAT_MLA_KEY_DEVID "devid"
#define PCAT_MLA_KEY_ROLE "role"
#define PCAT_MLA_KEY_CFGED "cfged"
#define PCAT_MLA_KEY_RSTFL "rstfl"

// Master policy.
#define PCAT_MLA_KEY_MMODE "mmode"
#define PCAT_MLA_KEY_MLSID "mlsid"
#define PCAT_MLA_KEY_MLFP "mlfp"
#define PCAT_MLA_KEY_NMWF "nmwf"
#define PCAT_MLA_KEY_GTCFG "gtcfg"
#define PCAT_MLA_KEY_LKTM "lktm"
#define PCAT_MLA_KEY_LKMG "lkmg"
#define PCAT_MLA_KEY_ALDLY "aldly"
#define PCAT_MLA_KEY_MWIN "mwin"
#define PCAT_MLA_KEY_ALMST "almst"

// User and notification.
#define PCAT_MLA_KEY_PINCOD "pincod"
#define PCAT_MLA_KEY_USRPNR "usrpnr"
#define PCAT_MLA_KEY_GQC "gqc"
#define PCAT_MLA_KEY_BZPAS "bzpas"
#define PCAT_MLA_KEY_CBQC "cbqc"

// SIM800.
#define PCAT_MLA_KEY_SBAUD "sbaud"
#define PCAT_MLA_KEY_STMO "stmo"
#define PCAT_MLA_KEY_SPKMS "spkms"
#define PCAT_MLA_KEY_SBWMS "sbwms"
#define PCAT_MLA_KEY_SPKAH "spkah"
#define PCAT_MLA_KEY_SDTRH "sdtrh"
#define PCAT_MLA_KEY_SSLP "sslp"
#define PCAT_MLA_KEY_SACMS "sacms"

// Keypad.
#define PCAT_MLA_KEY_KPDB "kpdb"
#define PCAT_MLA_KEY_KPRLS "kprls"
#define PCAT_MLA_KEY_KPSCN "kpscn"
#define PCAT_MLA_KEY_KPSTL "kpstl"

// Buzzer + RGB.
#define PCAT_MLA_KEY_BZAHI "bzahi"
#define PCAT_MLA_KEY_BUZEN "buzen"
#define PCAT_MLA_KEY_RGBLVL "rgblvl"
#define PCAT_MLA_KEY_RGBIDL "rgbidl"
#define PCAT_MLA_KEY_RGBALT "rgbalt"
#define PCAT_MLA_KEY_RGBBRT "rgbbrt"
#define PCAT_MLA_KEY_LEDFB "ledfb"

// Input + LIS2DHTR.
#define PCAT_MLA_KEY_BTLVL "btlvl"
#define PCAT_MLA_KEY_USLVL "uslvl"
#define PCAT_MLA_KEY_BTDBMS "btdbms"
#define PCAT_MLA_KEY_BTHOLD "bthold"
#define PCAT_MLA_KEY_BTCGAP "btcgap"
#define PCAT_MLA_KEY_CBUEN "cbuen"
#define PCAT_MLA_KEY_CBFMS "cbfms"
#define PCAT_MLA_KEY_SHTHR "shthr"
#define PCAT_MLA_KEY_SHODR "shodr"
#define PCAT_MLA_KEY_SHSCL "shscl"
#define PCAT_MLA_KEY_SHRES "shres"
#define PCAT_MLA_KEY_SHEVT "shevt"
#define PCAT_MLA_KEY_SHDUR "shdur"
#define PCAT_MLA_KEY_SHAXS "shaxs"
#define PCAT_MLA_KEY_SHHPM "shhpm"
#define PCAT_MLA_KEY_SHHPC "shhpc"
#define PCAT_MLA_KEY_SHHPE "shhpe"
#define PCAT_MLA_KEY_SHLAT "shlat"
#define PCAT_MLA_KEY_SHILV "shilv"

// Power.
#define PCAT_MLA_KEY_CHGEN "chgen"
#define PCAT_MLA_KEY_CHGMA "chgma"
#define PCAT_MLA_KEY_CHGVM "chgvm"
#define PCAT_MLA_KEY_IINMA "iinma"
#define PCAT_MLA_KEY_VINMV "vinmv"
#define PCAT_MLA_KEY_OTGEN "otgen"
#define PCAT_MLA_KEY_WDOG "wdog"
#define PCAT_MLA_KEY_LBATT "lbatt"
#define PCAT_MLA_KEY_CBATT "cbatt"
#define PCAT_MLA_KEY_BTEVL "btevl"

// Orchestration app policy (no MAC/LMK).
#define PCAT_MLA_KEY_SDCNT "sdcnt"
#define PCAT_MLA_KEY_RDCNT "rdcnt"
#define PCAT_MLA_KEY_SORD "sord"
#define PCAT_MLA_KEY_SOMT "somt"
#define PCAT_MLA_KEY_SOOP "soop"
#define PCAT_MLA_KEY_RORD "rord"
#define PCAT_MLA_KEY_ROMT "romt"
#define PCAT_MLA_KEY_ROOP "roop"
#define PCAT_MLA_KEY_SDPOL "sdpol"
#define PCAT_MLA_KEY_RDPOL "rdpol"

// Runtime telemetry cache keys (internal app/runtime use, still <= 6 chars).
#define PCAT_MLA_KEY_PCNT "pcnt"
#define PCAT_MLA_KEY_OCNT "ocnt"
#define PCAT_MLA_KEY_QDEP "qdep"
#define PCAT_MLA_KEY_QDRP "qdrp"
#define PCAT_MLA_KEY_ARMED "armed"
#define PCAT_MLA_KEY_BSOC "bsoc"
#define PCAT_MLA_KEY_BVOL "bvol"
#define PCAT_MLA_KEY_BCUR "bcur"
#define PCAT_MLA_KEY_UPMS "upms"

// Public setting keys.
#define PCAT_MLA_SET_DNAME "device_name"
#define PCAT_MLA_SET_DEVID "device_id"
#define PCAT_MLA_SET_ROLE "role_mode"
#define PCAT_MLA_SET_CFGED "configured"
#define PCAT_MLA_SET_RSTFL "reset_flag"

#define PCAT_MLA_SET_MMODE "master_mode"
#define PCAT_MLA_SET_MLSID "master_local_side"
#define PCAT_MLA_SET_MLFP "master_local_fp_enabled"
#define PCAT_MLA_SET_NMWF "normal_wifi_enable"
#define PCAT_MLA_SET_GTCFG "goto_config"
#define PCAT_MLA_SET_LKTM "lock_timeout_ms"
#define PCAT_MLA_SET_LKMG "lock_mode_emag"
#define PCAT_MLA_SET_ALDLY "auto_lock_after_close_ms"
#define PCAT_MLA_SET_MWIN "motion_warn_window_s"
#define PCAT_MLA_SET_ALMST "alarm_state"

#define PCAT_MLA_SET_PINCOD "keypad_pin"
#define PCAT_MLA_SET_USRPNR "user_phone_number"
#define PCAT_MLA_SET_GQC "gsm_queue_count"
#define PCAT_MLA_SET_BZPAS "buzzer_passive_mode"
#define PCAT_MLA_SET_CBQC "config_ble_pin_queue_count"

#define PCAT_MLA_SET_SBAUD "sim_baud"
#define PCAT_MLA_SET_STMO "sim_timeout_ms"
#define PCAT_MLA_SET_SPKMS "sim_pwrkey_pulse_ms"
#define PCAT_MLA_SET_SBWMS "sim_boot_wait_ms"
#define PCAT_MLA_SET_SPKAH "sim_pwrkey_active_high"
#define PCAT_MLA_SET_SDTRH "sim_dtr_sleep_high"
#define PCAT_MLA_SET_SSLP "sim_sleep_enabled"
#define PCAT_MLA_SET_SACMS "sim_alert_cooldown_ms"

#define PCAT_MLA_SET_KPDB "keypad_debounce_ms"
#define PCAT_MLA_SET_KPRLS "keypad_release_ms"
#define PCAT_MLA_SET_KPSCN "keypad_scan_idle_ms"
#define PCAT_MLA_SET_KPSTL "keypad_settle_us"

#define PCAT_MLA_SET_BZAHI "buzzer_active_high"
#define PCAT_MLA_SET_BUZEN "buzzer_enabled"
#define PCAT_MLA_SET_RGBLVL "rgb_active_level"
#define PCAT_MLA_SET_RGBIDL "rgb_idle_color"
#define PCAT_MLA_SET_RGBALT "rgb_alert_color"
#define PCAT_MLA_SET_RGBBRT "rgb_brightness"
#define PCAT_MLA_SET_LEDFB "led_feedback_enable"

#define PCAT_MLA_SET_BTLVL "boot_active_level"
#define PCAT_MLA_SET_USLVL "user_active_level"
#define PCAT_MLA_SET_BTDBMS "btn_debounce_ms"
#define PCAT_MLA_SET_BTHOLD "btn_hold_ms"
#define PCAT_MLA_SET_BTCGAP "btn_consecutive_gap_ms"
#define PCAT_MLA_SET_CBUEN "combo_boot_user_enable"
#define PCAT_MLA_SET_CBFMS "combo_finalize_ms"
#define PCAT_MLA_SET_SHTHR "motion_threshold_raw"
#define PCAT_MLA_SET_SHODR "motion_odr"
#define PCAT_MLA_SET_SHSCL "motion_scale"
#define PCAT_MLA_SET_SHRES "motion_resolution"
#define PCAT_MLA_SET_SHEVT "motion_event_mode"
#define PCAT_MLA_SET_SHDUR "motion_duration"
#define PCAT_MLA_SET_SHAXS "motion_axis_mask"
#define PCAT_MLA_SET_SHHPM "motion_hpf_mode"
#define PCAT_MLA_SET_SHHPC "motion_hpf_cutoff"
#define PCAT_MLA_SET_SHHPE "motion_hpf_enable"
#define PCAT_MLA_SET_SHLAT "motion_latch_enable"
#define PCAT_MLA_SET_SHILV "motion_int_level"

#define PCAT_MLA_SET_CHGEN "charger_enable"
#define PCAT_MLA_SET_CHGMA "charge_current_ma"
#define PCAT_MLA_SET_CHGVM "charge_voltage_mv"
#define PCAT_MLA_SET_IINMA "input_current_limit_ma"
#define PCAT_MLA_SET_VINMV "input_voltage_limit_mv"
#define PCAT_MLA_SET_OTGEN "otg_enable"
#define PCAT_MLA_SET_WDOG "watchdog_timeout_s"
#define PCAT_MLA_SET_LBATT "low_battery_pct"
#define PCAT_MLA_SET_CBATT "critical_battery_pct"
#define PCAT_MLA_SET_BTEVL "battery_eval_ms"

#define PCAT_MLA_SET_SDCNT "side_slave_count"
#define PCAT_MLA_SET_RDCNT "rear_slave_count"
#define PCAT_MLA_SET_SORD "side_owner_reed"
#define PCAT_MLA_SET_SOMT "side_owner_motion"
#define PCAT_MLA_SET_SOOP "side_owner_open"
#define PCAT_MLA_SET_RORD "rear_owner_reed"
#define PCAT_MLA_SET_ROMT "rear_owner_motion"
#define PCAT_MLA_SET_ROOP "rear_owner_open"
#define PCAT_MLA_SET_SDPOL "side_policy_json"
#define PCAT_MLA_SET_RDPOL "rear_policy_json"

// Telemetry keys.
#define PCAT_MLA_MET_PCNT "paired_count"
#define PCAT_MLA_MET_OCNT "online_count"
#define PCAT_MLA_MET_OFFCNT "offline_count"
#define PCAT_MLA_MET_QDEP "queue_depth"
#define PCAT_MLA_MET_QDRP "queue_drop"
#define PCAT_MLA_MET_ARMED "armed_state"
#define PCAT_MLA_MET_ALMST "alarm_state"
#define PCAT_MLA_MET_BATTPCT "battery_soc_pct"
#define PCAT_MLA_MET_BATTV "battery_v"
#define PCAT_MLA_MET_BATTI "battery_i"
#define PCAT_MLA_MET_UPTIME "uptime_ms"

// Event keys.
#define PCAT_MLA_EVT_SLV_ON "slave_online"
#define PCAT_MLA_EVT_SLV_OFF "slave_offline"
#define PCAT_MLA_EVT_PAIRCAP "pair_capacity_reached"
#define PCAT_MLA_EVT_ARMSET "arm_set"
#define PCAT_MLA_EVT_ARMCLR "arm_cleared"
#define PCAT_MLA_EVT_BRSET "breach_set"
#define PCAT_MLA_EVT_BRCLR "breach_cleared"
#define PCAT_MLA_EVT_UNLREQ "unlock_request"
#define PCAT_MLA_EVT_UNLDON "unlock_done"
#define PCAT_MLA_EVT_LKDONE "lock_done"
#define PCAT_MLA_EVT_MDMALT "modem_alert_sent"

#define PCAT_MLA_SETMAP \
  "device_name,device_id,role_mode,configured,reset_flag,master_mode,master_local_side,master_local_fp_enabled,normal_wifi_enable,goto_config,lock_timeout_ms,lock_mode_emag,auto_lock_after_close_ms,motion_warn_window_s,alarm_state,keypad_pin,user_phone_number,gsm_queue_count,buzzer_passive_mode,config_ble_pin_queue_count,sim_baud,sim_timeout_ms,sim_pwrkey_pulse_ms,sim_boot_wait_ms,sim_pwrkey_active_high,sim_dtr_sleep_high,sim_sleep_enabled,sim_alert_cooldown_ms,keypad_debounce_ms,keypad_release_ms,keypad_scan_idle_ms,keypad_settle_us,buzzer_active_high,buzzer_enabled,rgb_active_level,rgb_idle_color,rgb_alert_color,rgb_brightness,led_feedback_enable,boot_active_level,user_active_level,btn_debounce_ms,btn_hold_ms,btn_consecutive_gap_ms,combo_boot_user_enable,combo_finalize_ms,motion_threshold_raw,motion_odr,motion_scale,motion_resolution,motion_event_mode,motion_duration,motion_axis_mask,motion_hpf_mode,motion_hpf_cutoff,motion_hpf_enable,motion_latch_enable,motion_int_level,charger_enable,charge_current_ma,charge_voltage_mv,input_current_limit_ma,input_voltage_limit_mv,otg_enable,watchdog_timeout_s,low_battery_pct,critical_battery_pct,battery_eval_ms,side_slave_count,rear_slave_count,side_owner_reed,side_owner_motion,side_owner_open,rear_owner_reed,rear_owner_motion,rear_owner_open,side_policy_json,rear_policy_json"

#define PCAT_MLA_METMAP "paired_count,online_count,offline_count,queue_depth,queue_drop,armed_state,alarm_state,battery_soc_pct,battery_v,battery_i,uptime_ms"
#define PCAT_MLA_EVMAP "slave_online,slave_offline,pair_capacity_reached,arm_set,arm_cleared,breach_set,breach_cleared,unlock_request,unlock_done,lock_done,modem_alert_sent"

// Telemetry bounds.
#define PCAT_MLA_MET_PCNT_MIN 0U
#define PCAT_MLA_MET_PCNT_MAX 15U
#define PCAT_MLA_MET_OCNT_MIN 0U
#define PCAT_MLA_MET_OCNT_MAX 15U
#define PCAT_MLA_MET_QDEP_MIN 0U
#define PCAT_MLA_MET_QDEP_MAX 512U
#define PCAT_MLA_MET_QDRP_MIN 0U
#define PCAT_MLA_MET_QDRP_MAX 65535U

// Compile-time key-length policy enforcement.
PCAT_ASSERT_NVS_KEY_LEN(PCAT_MLA_KEY_DNAME);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_MLA_KEY_DEVID);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_MLA_KEY_ROLE);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_MLA_KEY_CFGED);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_MLA_KEY_RSTFL);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_MLA_KEY_MMODE);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_MLA_KEY_MLSID);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_MLA_KEY_MLFP);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_MLA_KEY_NMWF);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_MLA_KEY_GTCFG);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_MLA_KEY_LKTM);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_MLA_KEY_LKMG);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_MLA_KEY_ALDLY);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_MLA_KEY_MWIN);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_MLA_KEY_ALMST);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_MLA_KEY_PINCOD);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_MLA_KEY_USRPNR);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_MLA_KEY_GQC);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_MLA_KEY_BZPAS);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_MLA_KEY_CBQC);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_MLA_KEY_SBAUD);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_MLA_KEY_STMO);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_MLA_KEY_SPKMS);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_MLA_KEY_SBWMS);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_MLA_KEY_SPKAH);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_MLA_KEY_SDTRH);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_MLA_KEY_SSLP);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_MLA_KEY_SACMS);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_MLA_KEY_KPDB);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_MLA_KEY_KPRLS);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_MLA_KEY_KPSCN);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_MLA_KEY_KPSTL);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_MLA_KEY_BZAHI);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_MLA_KEY_BUZEN);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_MLA_KEY_RGBLVL);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_MLA_KEY_RGBIDL);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_MLA_KEY_RGBALT);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_MLA_KEY_RGBBRT);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_MLA_KEY_LEDFB);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_MLA_KEY_BTLVL);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_MLA_KEY_USLVL);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_MLA_KEY_BTDBMS);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_MLA_KEY_BTHOLD);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_MLA_KEY_BTCGAP);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_MLA_KEY_CBUEN);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_MLA_KEY_CBFMS);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_MLA_KEY_SHTHR);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_MLA_KEY_SHODR);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_MLA_KEY_SHSCL);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_MLA_KEY_SHRES);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_MLA_KEY_SHEVT);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_MLA_KEY_SHDUR);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_MLA_KEY_SHAXS);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_MLA_KEY_SHHPM);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_MLA_KEY_SHHPC);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_MLA_KEY_SHHPE);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_MLA_KEY_SHLAT);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_MLA_KEY_SHILV);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_MLA_KEY_CHGEN);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_MLA_KEY_CHGMA);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_MLA_KEY_CHGVM);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_MLA_KEY_IINMA);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_MLA_KEY_VINMV);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_MLA_KEY_OTGEN);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_MLA_KEY_WDOG);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_MLA_KEY_LBATT);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_MLA_KEY_CBATT);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_MLA_KEY_BTEVL);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_MLA_KEY_SDCNT);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_MLA_KEY_RDCNT);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_MLA_KEY_SORD);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_MLA_KEY_SOMT);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_MLA_KEY_SOOP);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_MLA_KEY_RORD);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_MLA_KEY_ROMT);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_MLA_KEY_ROOP);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_MLA_KEY_SDPOL);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_MLA_KEY_RDPOL);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_MLA_KEY_PCNT);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_MLA_KEY_OCNT);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_MLA_KEY_QDEP);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_MLA_KEY_QDRP);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_MLA_KEY_ARMED);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_MLA_KEY_BSOC);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_MLA_KEY_BVOL);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_MLA_KEY_BCUR);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_MLA_KEY_UPMS);
