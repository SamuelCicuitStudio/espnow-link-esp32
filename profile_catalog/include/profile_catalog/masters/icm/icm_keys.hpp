#pragma once  // Prevent multiple inclusion of this header.

#include "profile_catalog/shared/key_policy.hpp"  // Provides 6-char NVS key length policy checks.

// ICM profile identity defaults.
#define PCAT_ICM_DEV_TYPE "ICM"             // Logical device type reported by master descriptor.
#define PCAT_ICM_HW_VER "ICM-HW1"           // Default hardware revision string for ICM.
#define PCAT_ICM_SW_VER "0.1.0"             // Default software version string for ICM.
#define PCAT_ICM_BUILD_TAG "icm1"           // Short profile tag used by build-id helper.
#define PCAT_ICM_BUILD_ID BUILD_ID          // External override build-id (from build flags) or empty.
#define PCAT_ICM_DEF_NAME "ICM-Master"      // Default human-readable master name.

// ICM NVS setting/telemetry storage keys exposed to CLI/API.
// Internal link-management keys (LMK/PMK registry, topology, per-slave MAC/LMK slots)
// are intentionally not defined here because they are managed inside the library runtime.
// All keys below must remain <= 6 chars.
#define PCAT_ICM_KEY_DNAME "dname"          // Persisted master display name.
#define PCAT_ICM_KEY_MXPRS "mxprs"          // Persisted hard max-pairs capacity.
#define PCAT_ICM_KEY_LIVEN "liven"          // Persisted liveness monitor enable flag.
#define PCAT_ICM_KEY_DISCA "disca"          // Persisted discovery-auto behavior flag.
#define PCAT_ICM_KEY_QBUDG "qbudg"          // Persisted management queue budget.
#define PCAT_ICM_KEY_FANMD "fanmd"          // Persisted fan mode (0:auto,1:eco,2:forced,3:stopped).
#define PCAT_ICM_KEY_BUZEN "buzen"          // Persisted buzzer enable flag.
#define PCAT_ICM_KEY_LEDFB "ledfb"          // Persisted LED feedback enable flag.
#define PCAT_ICM_KEY_TEMP "tmpc"            // Cached telemetry: local board temperature (C).
#define PCAT_ICM_KEY_PCNT "pcnt"            // Cached telemetry: paired slave count.
#define PCAT_ICM_KEY_OCNT "ocnt"            // Cached telemetry: online slave count.
#define PCAT_ICM_KEY_QDEP "qdep"            // Cached telemetry: queue depth.
#define PCAT_ICM_KEY_QDRP "qdrp"            // Cached telemetry: queue drop count.
// Extended ICM persisted setting keys (CLI/API-exposed).
#define PCAT_ICM_KEY_BLENM "BLENAM"         // BLE advertising name.
#define PCAT_ICM_KEY_BLEPK "BLEPK_"         // BLE passkey (6 digits).
#define PCAT_ICM_KEY_PIN6 "PIN___"          // UI pairing PIN (6 digits).
#define PCAT_ICM_KEY_APSID "APSSID"         // AP SSID.
#define PCAT_ICM_KEY_APKEY "APKEY_"         // AP passphrase.
#define PCAT_ICM_KEY_STSID "STSSID"         // STA SSID.
#define PCAT_ICM_KEY_STKEY "STKEY_"         // STA passphrase.
#define PCAT_ICM_KEY_WEBUSR "WEBUSR"        // Web UI username.
#define PCAT_ICM_KEY_WEBPWD "WEBPWD"        // Web UI password.
#define PCAT_ICM_KEY_ICMCFG "ICMCFG"        // ICM config JSON blob.
#define PCAT_ICM_KEY_SLVCFG "SLVCFG"        // Slave defaults JSON blob.
#define PCAT_ICM_KEY_FWSEL "FWSEL"          // Last selected ICM firmware.
#define PCAT_ICM_KEY_SFWSEL "SFWSEL"        // Last selected slave firmware.
#define PCAT_ICM_KEY_PRMOD "ICPRMD"         // Pair mode.
#define PCAT_ICM_KEY_TOPINT "ICTINT"        // Topology interval seconds.
#define PCAT_ICM_KEY_BCRTRY "ICBRRT"        // Broadcast retry count.
#define PCAT_ICM_KEY_TSINT "ICTSIN"         // Timestamp interval seconds.
#define PCAT_ICM_KEY_RTCsrc "ICTSRC"        // RTC source mode.
#define PCAT_ICM_KEY_AUTOTS "ICAUTS"        // Auto timestamp sync enable.
#define PCAT_ICM_KEY_TZOFF "ICTZOF"         // Timezone offset minutes (string).
#define PCAT_ICM_KEY_SYNBOT "ICSYNB"        // Sync on boot.
#define PCAT_ICM_KEY_RLYCMD "ICRCMD"        // Relay command mode.
#define PCAT_ICM_KEY_MANOVR "ICMANO"        // Manual override enable.
#define PCAT_ICM_KEY_PMSAUT "ICPMSA"        // PMS auto mode.
#define PCAT_ICM_KEY_RLYDEF "ICRLDS"        // Relay default state.
#define PCAT_ICM_KEY_TMPTH "ICTHTH"         // Temperature threshold.
#define PCAT_ICM_KEY_TMPWRN "ICTHWR"        // Temperature warning threshold.
#define PCAT_ICM_KEY_BUZMOD "ICBUZM"        // Buzzer mode.
#define PCAT_ICM_KEY_SAFELK "ICSFLO"        // Safety lock enable.
#define PCAT_ICM_KEY_FALTAL "ICFALE"        // Fault alert enable.
#define PCAT_ICM_KEY_RGBIDL "ICRGBI"        // RGB idle color.
#define PCAT_ICM_KEY_RGBALT "ICRGBA"        // RGB alert color.
#define PCAT_ICM_KEY_RGBBRT "ICRGBB"        // RGB brightness.
#define PCAT_ICM_KEY_BLKACT "ICBLNK"        // Blink on activity.
#define PCAT_ICM_KEY_AUTRCN "ICAUTR"        // Auto reconnect.
#define PCAT_ICM_KEY_FLTRTY "ICFRL"         // Fault retry limit.
#define PCAT_ICM_KEY_PRIOVR "ICPRIO"        // Priority override.
#define PCAT_ICM_KEY_ALRNEW "ICALRT"        // Alert on new faults.
#define PCAT_ICM_KEY_WEBUPD "ICWUPD"        // Web auto-update UI.
#define PCAT_ICM_KEY_USRNM "ICUNAM"         // User first name.
#define PCAT_ICM_KEY_USRSR "ICUSUR"         // User surname.
#define PCAT_ICM_KEY_USRPH "ICUPHN"         // User phone.
#define PCAT_ICM_KEY_USREM "ICUEML"         // User email.
#define PCAT_ICM_KEY_EMLEN "ICEMEN"         // Email alerts enable.
#define PCAT_ICM_KEY_SMTHST "ICSMTH"        // SMTP host.
#define PCAT_ICM_KEY_SMTPRT "ICSMTP"        // SMTP port.
#define PCAT_ICM_KEY_SMTUSR "ICSMUS"        // SMTP user.
#define PCAT_ICM_KEY_SMTPWD "ICSMPW"        // SMTP password.
#define PCAT_ICM_KEY_SMTNAM "ICSMNM"        // SMTP sender name.
#define PCAT_ICM_KEY_EMLWLC "ICEMWC"        // Email welcome enable.
#define PCAT_ICM_KEY_WTHEME "ICWTHM"        // Web theme.
#define PCAT_ICM_KEY_UIRFRS "ICRUI"         // UI refresh ms.
#define PCAT_ICM_KEY_AUTSAV "ICASAV"        // Auto-save settings.
#define PCAT_ICM_KEY_BCMAC "ICBCMC"         // Broadcast MAC.
#define PCAT_ICM_KEY_PNGINT "ICPING"        // Ping interval s.
#define PCAT_ICM_KEY_PNGTO "ICPTO_"         // Ping timeout s.
#define PCAT_ICM_KEY_RSTDLY "ICRDLY"        // Reset delay ms.
#define PCAT_ICM_KEY_MAXNOD "ICNMAX"        // Max nodes.
#define PCAT_ICM_KEY_REQACK "ICRACK"        // Require ack flag.
#define PCAT_ICM_KEY_EVTEN "ICEVEN"         // Event system enable.
#define PCAT_ICM_KEY_EVTPOL "ICEVPL"        // Event poll ms.
#define PCAT_ICM_KEY_EVTOFF "ICEVTO"        // Event offline timeout ms.
#define PCAT_ICM_KEY_EVTFAIL "ICEVFL"       // Event fail retries.
#define PCAT_ICM_KEY_EVTPUSH "ICEVPU"       // Event push ms.
#define PCAT_ICM_KEY_HOSTNM "ICHOST"        // Hostname.
#define PCAT_ICM_KEY_CNTRY "ICCTRY"         // Country code.
#define PCAT_ICM_KEY_STADHC "ICDHCP"        // STA DHCP enable.
#define PCAT_ICM_KEY_STAIP "ICSIP_"         // STA static IP.
#define PCAT_ICM_KEY_STAGW "ICSGW_"         // STA gateway.
#define PCAT_ICM_KEY_STASUB "ICSSUB"        // STA subnet.
#define PCAT_ICM_KEY_STADN1 "ICSDN1"        // STA DNS1.
#define PCAT_ICM_KEY_STADN2 "ICSDN2"        // STA DNS2.
#define PCAT_ICM_KEY_APCH "ICAPCH"          // AP channel.
#define PCAT_ICM_KEY_APMAX "ICAPMC"         // AP max clients.
#define PCAT_ICM_KEY_APHID "ICAPHD"         // AP hidden SSID.
#define PCAT_ICM_KEY_APAUT "ICAPAU"         // AP auth mode.
#define PCAT_ICM_KEY_WTXDBM "ICWTPW"        // Wi-Fi TX dBm.
#define PCAT_ICM_KEY_WSLEEP "ICWSLP"        // Wi-Fi sleep mode.
#define PCAT_ICM_KEY_ENCEN "ICESNC"         // ESP-NOW encryption enable.
#define PCAT_ICM_KEY_MDNSEN "ICMDNS"        // mDNS enable.
#define PCAT_ICM_KEY_CAPTEN "ICCAPT"        // Captive portal enable.
#define PCAT_ICM_KEY_BTXDBM "ICBTPW"        // BLE TX dBm.
#define PCAT_ICM_KEY_BADVMS "ICBADV"        // BLE adv interval ms.
// ICM frontend orchestration persisted defaults.
#define PCAT_ICM_KEY_OWAIT "ICOWTM"         // Frontend orchestration wait timeout ms.
#define PCAT_ICM_KEY_OEVRG "ICOERC"         // Frontend event ring capacity.
#define PCAT_ICM_KEY_OBCFM "ICOBCT"         // Frontend batch confirm default.
#define PCAT_ICM_KEY_ORFRC "ICORFC"         // Frontend batch refresh-cache default.

// ICM schema setting keys (CLI/descriptor-facing names).
#define PCAT_ICM_SET_DNAME "device_name"    // Public setting key for master name.
#define PCAT_ICM_SET_MXPRS "max_pairs"      // Public setting key for max paired slaves.
#define PCAT_ICM_SET_LIVEN "live_enabled"   // Public setting key for liveness monitor.
#define PCAT_ICM_SET_DISCA "discovery_auto" // Public setting key for auto discovery behavior.
#define PCAT_ICM_SET_QBUDG "queue_budget"   // Public setting key for queue budget.
#define PCAT_ICM_SET_FANMD "fan_mode"       // Public setting key for fan mode.
#define PCAT_ICM_SET_BUZEN "buzzer_enable"  // Public setting key for buzzer enable.
#define PCAT_ICM_SET_LEDFB "led_feedback_enable"  // Public setting key for LED feedback.
// Extended ICM public setting keys.
#define PCAT_ICM_SET_BLENM "ble_name"
#define PCAT_ICM_SET_BLEPK "ble_passkey"
#define PCAT_ICM_SET_PIN6 "pairing_pin"
#define PCAT_ICM_SET_APSID "ap_ssid"
#define PCAT_ICM_SET_APKEY "ap_key"
#define PCAT_ICM_SET_STSID "sta_ssid"
#define PCAT_ICM_SET_STKEY "sta_key"
#define PCAT_ICM_SET_WEBUSR "web_user"
#define PCAT_ICM_SET_WEBPWD "web_password"
#define PCAT_ICM_SET_ICMCFG "icm_config_json"
#define PCAT_ICM_SET_SLVCFG "slave_config_json"
#define PCAT_ICM_SET_FWSEL "fw_selected"
#define PCAT_ICM_SET_SFWSEL "slave_fw_selected"
#define PCAT_ICM_SET_PRMOD "pair_mode"
#define PCAT_ICM_SET_TOPINT "topology_interval"
#define PCAT_ICM_SET_BCRTRY "broadcast_retry"
#define PCAT_ICM_SET_TSINT "timestamp_interval"
#define PCAT_ICM_SET_RTCsrc "rtc_source"
#define PCAT_ICM_SET_AUTOTS "auto_timestamp"
#define PCAT_ICM_SET_TZOFF "tz_offset"
#define PCAT_ICM_SET_SYNBOT "sync_on_boot"
#define PCAT_ICM_SET_RLYCMD "relay_command_mode"
#define PCAT_ICM_SET_MANOVR "manual_override"
#define PCAT_ICM_SET_PMSAUT "pms_auto"
#define PCAT_ICM_SET_RLYDEF "relay_default_state"
#define PCAT_ICM_SET_TMPTH "temp_threshold"
#define PCAT_ICM_SET_TMPWRN "temp_warning"
#define PCAT_ICM_SET_BUZMOD "buzzer_mode"
#define PCAT_ICM_SET_SAFELK "safe_lock"
#define PCAT_ICM_SET_FALTAL "fault_alert"
#define PCAT_ICM_SET_RGBIDL "rgb_idle_color"
#define PCAT_ICM_SET_RGBALT "rgb_alert_color"
#define PCAT_ICM_SET_RGBBRT "rgb_brightness"
#define PCAT_ICM_SET_BLKACT "blink_activity"
#define PCAT_ICM_SET_AUTRCN "auto_reconnect"
#define PCAT_ICM_SET_FLTRTY "fault_retry"
#define PCAT_ICM_SET_PRIOVR "priority_override"
#define PCAT_ICM_SET_ALRNEW "alert_new"
#define PCAT_ICM_SET_WEBUPD "web_update"
#define PCAT_ICM_SET_USRNM "user_name"
#define PCAT_ICM_SET_USRSR "user_surname"
#define PCAT_ICM_SET_USRPH "user_phone"
#define PCAT_ICM_SET_USREM "user_email"
#define PCAT_ICM_SET_EMLEN "email_enable"
#define PCAT_ICM_SET_SMTHST "smtp_host"
#define PCAT_ICM_SET_SMTPRT "smtp_port"
#define PCAT_ICM_SET_SMTUSR "smtp_user"
#define PCAT_ICM_SET_SMTPWD "smtp_pass"
#define PCAT_ICM_SET_SMTNAM "smtp_name"
#define PCAT_ICM_SET_EMLWLC "email_welcome"
#define PCAT_ICM_SET_WTHEME "web_theme"
#define PCAT_ICM_SET_UIRFRS "ui_refresh_ms"
#define PCAT_ICM_SET_AUTSAV "auto_save"
#define PCAT_ICM_SET_BCMAC "broadcast_mac"
#define PCAT_ICM_SET_PNGINT "ping_interval"
#define PCAT_ICM_SET_PNGTO "ping_timeout"
#define PCAT_ICM_SET_RSTDLY "reset_delay_ms"
#define PCAT_ICM_SET_MAXNOD "max_nodes"
#define PCAT_ICM_SET_REQACK "require_ack"
#define PCAT_ICM_SET_EVTEN "event_enable"
#define PCAT_ICM_SET_EVTPOL "event_poll_ms"
#define PCAT_ICM_SET_EVTOFF "event_offline_timeout_ms"
#define PCAT_ICM_SET_EVTFAIL "event_fail_limit"
#define PCAT_ICM_SET_EVTPUSH "event_push_ms"
#define PCAT_ICM_SET_HOSTNM "hostname"
#define PCAT_ICM_SET_CNTRY "country"
#define PCAT_ICM_SET_STADHC "sta_dhcp"
#define PCAT_ICM_SET_STAIP "sta_ip"
#define PCAT_ICM_SET_STAGW "sta_gateway"
#define PCAT_ICM_SET_STASUB "sta_subnet"
#define PCAT_ICM_SET_STADN1 "sta_dns1"
#define PCAT_ICM_SET_STADN2 "sta_dns2"
#define PCAT_ICM_SET_APCH "ap_channel"
#define PCAT_ICM_SET_APMAX "ap_max_clients"
#define PCAT_ICM_SET_APHID "ap_hidden"
#define PCAT_ICM_SET_APAUT "ap_auth"
#define PCAT_ICM_SET_WTXDBM "wifi_tx_dbm"
#define PCAT_ICM_SET_WSLEEP "wifi_sleep"
#define PCAT_ICM_SET_ENCEN "espnow_encryption"
#define PCAT_ICM_SET_MDNSEN "mdns_enable"
#define PCAT_ICM_SET_CAPTEN "captive_enable"
#define PCAT_ICM_SET_BTXDBM "ble_tx_dbm"
#define PCAT_ICM_SET_BADVMS "ble_adv_ms"
// ICM frontend orchestration setting keys.
#define PCAT_ICM_SET_OWAIT "orch_wait_ms"
#define PCAT_ICM_SET_OEVRG "orch_event_ring"
#define PCAT_ICM_SET_OBCFM "orch_batch_confirm"
#define PCAT_ICM_SET_ORFRC "orch_refresh_cache"

// ICM setting defaults and ranges (single source of truth).
#define PCAT_ICM_SET_DNAME_DEF PCAT_ICM_DEF_NAME  // Default master name.
#define PCAT_ICM_SET_DNAME_MIN_LEN 1              // Minimum allowed master-name length.
#define PCAT_ICM_SET_DNAME_MAX_LEN 31             // Maximum allowed master-name length.

#define PCAT_ICM_SET_MXPRS_DEF 14U                // Default hard max-pairs value.
#define PCAT_ICM_SET_MXPRS_MIN 1U                 // Minimum hard max-pairs value.
#define PCAT_ICM_SET_MXPRS_MAX 14U                // Maximum hard max-pairs value (current policy).

#define PCAT_ICM_SET_LIVEN_DEF 1                  // Default liveness monitor enabled.
#define PCAT_ICM_SET_DISCA_DEF 0                  // Default discovery_auto disabled.

#define PCAT_ICM_SET_QBUDG_DEF 48U                // Default management queue budget.
#define PCAT_ICM_SET_QBUDG_MIN 16U                // Minimum queue budget accepted by provider.
#define PCAT_ICM_SET_QBUDG_MAX 512U               // Maximum queue budget accepted by provider.
#define PCAT_ICM_SET_FANMD_DEF 0U                 // Default fan mode: auto.
#define PCAT_ICM_SET_FANMD_MIN 0U                 // Minimum fan mode value.
#define PCAT_ICM_SET_FANMD_MAX 3U                 // Maximum fan mode value.
#define PCAT_ICM_SET_BUZEN_DEF 1                  // Default buzzer enabled.
#define PCAT_ICM_SET_LEDFB_DEF 1                  // Default LED feedback enabled.

// ICM schema telemetry keys.
#define PCAT_ICM_MET_PCNT "paired_count"          // Telemetry key: total paired slave count.
#define PCAT_ICM_MET_OCNT "online_count"          // Telemetry key: currently online slave count.
#define PCAT_ICM_MET_OFFCNT "offline_count"       // Telemetry key: currently offline slave count.
#define PCAT_ICM_MET_QDEP "queue_depth"           // Telemetry key: management queue depth.
#define PCAT_ICM_MET_QDRP "queue_drop"            // Telemetry key: management queue drop counter.
#define PCAT_ICM_MET_LIVEN "live_enabled"         // Telemetry key: live monitor state.
#define PCAT_ICM_MET_TEMP "temp_c"                // Telemetry key: local board temperature.

// ICM telemetry ranges.
#define PCAT_ICM_MET_PCNT_MIN 0U                  // Minimum paired_count telemetry value.
#define PCAT_ICM_MET_PCNT_MAX 14U                 // Maximum paired_count telemetry value.

#define PCAT_ICM_MET_OCNT_MIN 0U                  // Minimum online_count telemetry value.
#define PCAT_ICM_MET_OCNT_MAX 14U                 // Maximum online_count telemetry value.

#define PCAT_ICM_MET_OFFCNT_MIN 0U                // Minimum offline_count telemetry value.
#define PCAT_ICM_MET_OFFCNT_MAX 14U               // Maximum offline_count telemetry value.

#define PCAT_ICM_MET_QDEP_MIN 0U                  // Minimum queue_depth telemetry value.
#define PCAT_ICM_MET_QDEP_MAX 512U                // Maximum queue_depth telemetry value.

#define PCAT_ICM_MET_QDRP_MIN 0U                  // Minimum queue_drop telemetry value.
#define PCAT_ICM_MET_QDRP_MAX 65535U              // Maximum queue_drop telemetry value (u16 backed).

#define PCAT_ICM_MET_LIVEN_MIN 0                  // Minimum live_enabled telemetry value.
#define PCAT_ICM_MET_LIVEN_MAX 1                  // Maximum live_enabled telemetry value.
#define PCAT_ICM_MET_TEMP_MIN -55.0f              // Minimum temperature telemetry value.
#define PCAT_ICM_MET_TEMP_MAX 125.0f              // Maximum temperature telemetry value.

// ICM descriptor text snippets derived from ranges.
#define PCAT_ICM_DESC_SET_DNAME "type=str;rw=1;min=1;max=31"                   // Descriptor metadata for device_name.
#define PCAT_ICM_DESC_SET_MXPRS "type=u16;rw=1;min=1;max=14"                   // Descriptor metadata for max_pairs.
#define PCAT_ICM_DESC_SET_LIVEN "type=bool;rw=1;min=0;max=1"                   // Descriptor metadata for live_enabled.
#define PCAT_ICM_DESC_SET_DISCA "type=bool;rw=1;min=0;max=1"                   // Descriptor metadata for discovery_auto.
#define PCAT_ICM_DESC_SET_QBUDG "type=u16;rw=1;min=16;max=512"                 // Descriptor metadata for queue_budget.
#define PCAT_ICM_DESC_SET_FANMD "type=u16;rw=1;min=0;max=3;enum=0:auto|1:eco|2:forced|3:stopped"  // Descriptor metadata for fan_mode.
#define PCAT_ICM_DESC_SET_BUZEN "type=bool;rw=1;min=0;max=1"                   // Descriptor metadata for buzzer_enable.
#define PCAT_ICM_DESC_SET_LEDFB "type=bool;rw=1;min=0;max=1"                   // Descriptor metadata for led_feedback_enable.

#define PCAT_ICM_DESC_MET_PCNT "type=u16;unit=count;min=0;max=14;pull=1;push=1"    // Descriptor metadata for paired_count.
#define PCAT_ICM_DESC_MET_OCNT "type=u16;unit=count;min=0;max=14;pull=1;push=1"    // Descriptor metadata for online_count.
#define PCAT_ICM_DESC_MET_OFFCNT "type=u16;unit=count;min=0;max=14;pull=1;push=1"  // Descriptor metadata for offline_count.
#define PCAT_ICM_DESC_MET_QDEP "type=u16;unit=count;min=0;max=512;pull=1;push=1"   // Descriptor metadata for queue_depth.
#define PCAT_ICM_DESC_MET_QDRP "type=u16;unit=count;min=0;max=65535;pull=1;push=1" // Descriptor metadata for queue_drop.
#define PCAT_ICM_DESC_MET_LIVEN "type=bool;unit=bool;min=0;max=1;pull=1;push=1"    // Descriptor metadata for live_enabled.
#define PCAT_ICM_DESC_MET_TEMP "type=f32;unit=C;min=-55.0;max=125.0;pull=1;push=1" // Descriptor metadata for temp_c.

// ICM schema event keys.
#define PCAT_ICM_EVT_SLV_ON "slave_online"               // Event key: slave transitioned online.
#define PCAT_ICM_EVT_SLV_OFF "slave_offline"             // Event key: slave transitioned offline.
#define PCAT_ICM_EVT_PAIR_CAP "pair_capacity_reached"    // Event key: max-pair limit reached.
#define PCAT_ICM_EVT_OTA_ST "ota_pipeline_started"       // Event key: OTA pipeline started.
#define PCAT_ICM_EVT_OTA_DONE "ota_pipeline_complete"    // Event key: OTA pipeline completed.

// Compile-time key-length policy enforcement (<= 6 chars each).
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ICM_KEY_DNAME);  // Validate dname key length.
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ICM_KEY_MXPRS);  // Validate mxprs key length.
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ICM_KEY_LIVEN);  // Validate liven key length.
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ICM_KEY_DISCA);  // Validate disca key length.
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ICM_KEY_QBUDG);  // Validate qbudg key length.
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ICM_KEY_FANMD);  // Validate fanmd key length.
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ICM_KEY_BUZEN);  // Validate buzen key length.
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ICM_KEY_LEDFB);  // Validate ledfb key length.
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ICM_KEY_TEMP);   // Validate tmpc key length.
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ICM_KEY_PCNT);   // Validate pcnt key length.
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ICM_KEY_OCNT);   // Validate ocnt key length.
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ICM_KEY_QDEP);   // Validate qdep key length.
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ICM_KEY_QDRP);   // Validate qdrp key length.
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ICM_KEY_OWAIT);  // Validate orchestration wait key length.
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ICM_KEY_OEVRG);  // Validate orchestration ring key length.
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ICM_KEY_OBCFM);  // Validate orchestration batch-confirm key length.
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ICM_KEY_ORFRC);  // Validate orchestration refresh-cache key length.
