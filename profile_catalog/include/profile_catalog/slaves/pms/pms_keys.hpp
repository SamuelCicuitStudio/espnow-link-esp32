#pragma once  // Prevent multiple inclusion of this header.

#include "profile_catalog/shared/key_policy.hpp"  // Provides 6-char NVS key length policy checks.

// PMS profile identity defaults.
#define PCAT_PMS_DEV_TYPE "PMS"            // Logical device type reported in descriptor.
#define PCAT_PMS_HW_VER "PMS-HW1"          // Default hardware revision string.
#define PCAT_PMS_SW_VER "0.1.0"            // Default software version string.
#define PCAT_PMS_BUILD_TAG "pms1"          // Short profile tag used by build-id helper.
#define PCAT_PMS_BUILD_ID BUILD_ID         // External override build-id (from build flags) or empty.
#define PCAT_PMS_DEF_NAME "PMS-Node"       // Default human-readable device name.

// PMS NVS setting keys (all must remain <= 6 chars).
#define PCAT_PMS_KEY_DNAME "dname"         // Persisted device display name.
#define PCAT_PMS_KEY_CHAN "chan"           // Persisted ESP-NOW channel.
#define PCAT_PMS_KEY_PMOD "pmod"           // Persisted power mode token.
#define PCAT_PMS_KEY_LOOPA "loopa"         // Persisted LoopAuto flag.
#define PCAT_PMS_KEY_FANMD "fanmd"         // Persisted fan mode (0:auto,1:eco,2:forced,3:stopped).
#define PCAT_PMS_KEY_BUZEN "buzen"         // Persisted buzzer enable flag.
#define PCAT_PMS_KEY_CLIBD "cbaud"         // Persisted local CLI/USB serial baud (applied at boot).
#define PCAT_PMS_KEY_LEDFB "ledfb"         // Persisted LED feedback enable flag.
#define PCAT_PMS_KEY_RGBIDL "rgbidl"       // Persisted RGB idle color.
#define PCAT_PMS_KEY_RGBALT "rgbalt"       // Persisted RGB alert color.
#define PCAT_PMS_KEY_RGBBRT "rgbbrt"       // Persisted RGB brightness (0..255).
#define PCAT_PMS_KEY_C48EN "c48en"         // Persisted 48V chain power enable flag.
#define PCAT_PMS_KEY_CHGEN "chgen"         // Persisted charger enable flag.
#define PCAT_PMS_KEY_TRIPI "tripi"         // Persisted trip current limit (A).
#define PCAT_PMS_KEY_VCAL "vcal"           // Persisted voltage calibration factor.
#define PCAT_PMS_KEY_ICAL "ical"           // Persisted current calibration factor.
#define PCAT_PMS_KEY_VBOVP "vbovp"         // Persisted bus over-voltage threshold (mV).
#define PCAT_PMS_KEY_VBUVP "vbuvp"         // Persisted bus under-voltage threshold (mV).
#define PCAT_PMS_KEY_IBOCP "ibocp"         // Persisted bus over-current threshold (mA).
#define PCAT_PMS_KEY_BAOVP "baovp"         // Persisted battery over-voltage threshold (mV).
#define PCAT_PMS_KEY_BAUVP "bauvp"         // Persisted battery under-voltage threshold (mV).
#define PCAT_PMS_KEY_BIOCP "biocp"         // Persisted battery over-current threshold (mA).

// PMS schema setting keys (CLI/descriptor-facing names).
#define PCAT_PMS_SET_DNAME PCAT_PMS_KEY_DNAME   // Setting key for device name.
#define PCAT_PMS_SET_CHAN PCAT_PMS_KEY_CHAN     // Setting key for channel.
#define PCAT_PMS_SET_PMOD PCAT_PMS_KEY_PMOD     // Setting key for power mode.
#define PCAT_PMS_SET_LOOPAUTO "LoopAuto"        // Public setting key for LoopAuto (kept camel case intentionally).
#define PCAT_PMS_SET_FANMD "fan_mode"           // Public setting key for fan mode.
#define PCAT_PMS_SET_BUZEN "buzzer_enable"      // Public setting key for buzzer enable.
#define PCAT_PMS_SET_CLIBD "cli_baud"           // Public setting key for local CLI serial baud.
#define PCAT_PMS_SET_LEDFB "led_feedback_enable"  // Public setting key for LED feedback enable.
#define PCAT_PMS_SET_RGBIDL "rgb_idle_color"    // Public setting key for RGB idle color.
#define PCAT_PMS_SET_RGBALT "rgb_alert_color"   // Public setting key for RGB alert color.
#define PCAT_PMS_SET_RGBBRT "rgb_brightness"    // Public setting key for RGB brightness.
#define PCAT_PMS_SET_C48EN "chain_48v_enable"   // Public setting key for 48V chain on/off.
#define PCAT_PMS_SET_CHGEN "charger_enable"     // Public setting key for charger on/off.
#define PCAT_PMS_SET_TRIPI PCAT_PMS_KEY_TRIPI   // Setting key for trip current.
#define PCAT_PMS_SET_VCAL PCAT_PMS_KEY_VCAL     // Setting key for voltage calibration.
#define PCAT_PMS_SET_ICAL PCAT_PMS_KEY_ICAL     // Setting key for current calibration.
#define PCAT_PMS_SET_VBOVP PCAT_PMS_KEY_VBOVP   // Setting key for bus OVP.
#define PCAT_PMS_SET_VBUVP PCAT_PMS_KEY_VBUVP   // Setting key for bus UVP.
#define PCAT_PMS_SET_IBOCP PCAT_PMS_KEY_IBOCP   // Setting key for bus OCP.
#define PCAT_PMS_SET_BAOVP PCAT_PMS_KEY_BAOVP   // Setting key for battery OVP.
#define PCAT_PMS_SET_BAUVP PCAT_PMS_KEY_BAUVP   // Setting key for battery UVP.
#define PCAT_PMS_SET_BIOCP PCAT_PMS_KEY_BIOCP   // Setting key for battery OCP.

// PMS setting defaults and ranges (single source of truth for provider + descriptors).
#define PCAT_PMS_SET_DNAME_DEF PCAT_PMS_DEF_NAME  // Default device name.
#define PCAT_PMS_SET_DNAME_MIN_LEN 1              // Minimum allowed device-name length.
#define PCAT_PMS_SET_DNAME_MAX_LEN 31             // Maximum allowed device-name length.

#define PCAT_PMS_SET_CHAN_DEF 1U                  // Default ESP-NOW channel.
#define PCAT_PMS_SET_CHAN_MIN 1U                  // Minimum channel accepted by provider.
#define PCAT_PMS_SET_CHAN_MAX 14U                 // Maximum channel accepted by provider.

#define PCAT_PMS_SET_PMOD_DEF "normal"            // Default power mode.
#define PCAT_PMS_SET_PMOD_ENUM "normal|eco|safe"  // Allowed power mode values.

#define PCAT_PMS_SET_LOOPAUTO_DEF 0               // Default LoopAuto disabled.
#define PCAT_PMS_SET_FANMD_DEF 0U                 // Default fan mode: auto.
#define PCAT_PMS_SET_FANMD_MIN 0U                 // Minimum fan mode value.
#define PCAT_PMS_SET_FANMD_MAX 3U                 // Maximum fan mode value.
#define PCAT_PMS_SET_BUZEN_DEF 1                  // Default buzzer feedback enabled.
#define PCAT_PMS_SET_CLIBD_DEF 115200U            // Default CLI baud rate.
#define PCAT_PMS_SET_CLIBD_MIN 9600U              // Minimum allowed CLI baud rate.
#define PCAT_PMS_SET_CLIBD_MAX 921600U            // Maximum allowed CLI baud rate.
#define PCAT_PMS_SET_LEDFB_DEF 1                  // Default LED feedback enabled.
#define PCAT_PMS_SET_RGBIDL_DEF "#00ffaa"         // Default RGB idle color.
#define PCAT_PMS_SET_RGBALT_DEF "#ff3366"         // Default RGB alert color.
#define PCAT_PMS_SET_RGBBRT_DEF 180U              // Default RGB brightness.
#define PCAT_PMS_SET_RGBBRT_MIN 0U                // Minimum RGB brightness.
#define PCAT_PMS_SET_RGBBRT_MAX 255U              // Maximum RGB brightness.
#define PCAT_PMS_SET_C48EN_DEF 0                  // Default 48V chain power disabled.
#define PCAT_PMS_SET_CHGEN_DEF 0                  // Default charger disabled.

#define PCAT_PMS_SET_TRIPI_DEF 15.0f              // Default trip current (A).
#define PCAT_PMS_SET_TRIPI_MIN 1.0f               // Minimum trip current (A).
#define PCAT_PMS_SET_TRIPI_MAX 200.0f             // Maximum trip current (A).

#define PCAT_PMS_SET_VCAL_DEF 1.0f                // Default voltage calibration factor.
#define PCAT_PMS_SET_VCAL_MIN 0.5f                // Minimum voltage calibration factor.
#define PCAT_PMS_SET_VCAL_MAX 1.5f                // Maximum voltage calibration factor.

#define PCAT_PMS_SET_ICAL_DEF 1.0f                // Default current calibration factor.
#define PCAT_PMS_SET_ICAL_MIN 0.5f                // Minimum current calibration factor.
#define PCAT_PMS_SET_ICAL_MAX 1.5f                // Maximum current calibration factor.

#define PCAT_PMS_SET_VBOVP_DEF 15000U             // Default bus OVP threshold (mV).
#define PCAT_PMS_SET_VBOVP_MIN 6000U              // Minimum bus OVP threshold (mV).
#define PCAT_PMS_SET_VBOVP_MAX 30000U             // Maximum bus OVP threshold (mV).

#define PCAT_PMS_SET_VBUVP_DEF 9000U              // Default bus UVP threshold (mV).
#define PCAT_PMS_SET_VBUVP_MIN 4000U              // Minimum bus UVP threshold (mV).
#define PCAT_PMS_SET_VBUVP_MAX 25000U             // Maximum bus UVP threshold (mV).

#define PCAT_PMS_SET_IBOCP_DEF 10000U             // Default bus OCP threshold (mA).
#define PCAT_PMS_SET_IBOCP_MIN 500U               // Minimum bus OCP threshold (mA).
#define PCAT_PMS_SET_IBOCP_MAX 60000U             // Maximum bus OCP threshold (mA).

#define PCAT_PMS_SET_BAOVP_DEF 14600U             // Default battery OVP threshold (mV).
#define PCAT_PMS_SET_BAOVP_MIN 6000U              // Minimum battery OVP threshold (mV).
#define PCAT_PMS_SET_BAOVP_MAX 30000U             // Maximum battery OVP threshold (mV).

#define PCAT_PMS_SET_BAUVP_DEF 10000U             // Default battery UVP threshold (mV).
#define PCAT_PMS_SET_BAUVP_MIN 4000U              // Minimum battery UVP threshold (mV).
#define PCAT_PMS_SET_BAUVP_MAX 25000U             // Maximum battery UVP threshold (mV).

#define PCAT_PMS_SET_BIOCP_DEF 10000U             // Default battery OCP threshold (mA).
#define PCAT_PMS_SET_BIOCP_MIN 500U               // Minimum battery OCP threshold (mA).
#define PCAT_PMS_SET_BIOCP_MAX 60000U             // Maximum battery OCP threshold (mA).

// PMS schema telemetry keys.
#define PCAT_PMS_MET_WALLV "wallv"          // Telemetry key: wall voltage.
#define PCAT_PMS_MET_BATTV "battv"          // Telemetry key: battery voltage.
#define PCAT_PMS_MET_WALLI "walli"          // Telemetry key: wall current.
#define PCAT_PMS_MET_BATTI "batti"          // Telemetry key: battery current.
#define PCAT_PMS_MET_PSRC "psrc"            // Telemetry key: current power source.
#define PCAT_PMS_MET_TRIP "trip"            // Telemetry key: trip status flag.
#define PCAT_PMS_MET_RCUT "rcut"            // Telemetry key: relay cut status flag.

// PMS telemetry ranges.
#define PCAT_PMS_MET_WALLV_MIN 0.0f         // Minimum expected wall voltage telemetry (V).
#define PCAT_PMS_MET_WALLV_MAX 60.0f        // Maximum expected wall voltage telemetry (V).

#define PCAT_PMS_MET_BATTV_MIN 0.0f         // Minimum expected battery voltage telemetry (V).
#define PCAT_PMS_MET_BATTV_MAX 60.0f        // Maximum expected battery voltage telemetry (V).

#define PCAT_PMS_MET_WALLI_MIN 0.0f       // Minimum expected wall current telemetry (A).
#define PCAT_PMS_MET_WALLI_MAX 0.0f        // Maximum expected wall current telemetry (A).

#define PCAT_PMS_MET_BATTI_MIN 0.0f       // Minimum expected battery current telemetry (A).
#define PCAT_PMS_MET_BATTI_MAX 0.0f        // Maximum expected battery current telemetry (A).

#define PCAT_PMS_MET_PSRC_ENUM "wall|battery"  // Allowed power-source telemetry strings.
#define PCAT_PMS_MET_TRIP_MIN 0                // Minimum trip flag value.
#define PCAT_PMS_MET_TRIP_MAX 1                // Maximum trip flag value.
#define PCAT_PMS_MET_RCUT_MIN 0                // Minimum relay-cut flag value.
#define PCAT_PMS_MET_RCUT_MAX 1                // Maximum relay-cut flag value.

// PMS descriptor text snippets derived from ranges.
#define PCAT_PMS_DESC_SET_DNAME "type=str;rw=1;min=1;max=31"                   // Descriptor metadata for device_name.
#define PCAT_PMS_DESC_SET_CHAN "type=u16;rw=1;min=1;max=14"                    // Descriptor metadata for channel.
#define PCAT_PMS_DESC_SET_PMOD "type=str;rw=1;enum=normal|eco|safe"            // Descriptor metadata for power mode.
#define PCAT_PMS_DESC_SET_LOOPAUTO "type=bool;rw=1"                             // Descriptor metadata for LoopAuto.
#define PCAT_PMS_DESC_SET_FANMD "type=u16;rw=1;min=0;max=3;enum=0:auto|1:eco|2:forced|3:stopped"  // Descriptor metadata for fan mode.
#define PCAT_PMS_DESC_SET_BUZEN "type=bool;rw=1"                                // Descriptor metadata for buzzer_enable.
#define PCAT_PMS_DESC_SET_CLIBD "type=u32;rw=1;enum=9600|19200|38400|57600|74880|115200|230400|250000|460800|921600"  // Descriptor metadata for cli_baud.
#define PCAT_PMS_DESC_SET_LEDFB "type=bool;rw=1"                                // Descriptor metadata for led_feedback_enable.
#define PCAT_PMS_DESC_SET_RGBIDL "type=str;rw=1"                                // Descriptor metadata for rgb_idle_color.
#define PCAT_PMS_DESC_SET_RGBALT "type=str;rw=1"                                // Descriptor metadata for rgb_alert_color.
#define PCAT_PMS_DESC_SET_RGBBRT "type=u16;rw=1;min=0;max=255"                  // Descriptor metadata for rgb_brightness.
#define PCAT_PMS_DESC_SET_C48EN "type=bool;rw=1"                                // Descriptor metadata for chain_48v_enable.
#define PCAT_PMS_DESC_SET_CHGEN "type=bool;rw=1"                                // Descriptor metadata for charger_enable.
#define PCAT_PMS_DESC_SET_TRIPI "type=f32;rw=1;min=1.0;max=200.0;unit=A"       // Descriptor metadata for trip current.
#define PCAT_PMS_DESC_SET_VCAL "type=f32;rw=1;min=0.5;max=1.5"                 // Descriptor metadata for V calibration.
#define PCAT_PMS_DESC_SET_ICAL "type=f32;rw=1;min=0.5;max=1.5"                 // Descriptor metadata for I calibration.
#define PCAT_PMS_DESC_SET_VBOVP "type=u16;rw=1;min=6000;max=30000;unit=mV"     // Descriptor metadata for VBUS OVP.
#define PCAT_PMS_DESC_SET_VBUVP "type=u16;rw=1;min=4000;max=25000;unit=mV"     // Descriptor metadata for VBUS UVP.
#define PCAT_PMS_DESC_SET_IBOCP "type=u16;rw=1;min=500;max=60000;unit=mA"      // Descriptor metadata for IBUS OCP.
#define PCAT_PMS_DESC_SET_BAOVP "type=u16;rw=1;min=6000;max=30000;unit=mV"     // Descriptor metadata for VBAT OVP.
#define PCAT_PMS_DESC_SET_BAUVP "type=u16;rw=1;min=4000;max=25000;unit=mV"     // Descriptor metadata for VBAT UVP.
#define PCAT_PMS_DESC_SET_BIOCP "type=u16;rw=1;min=500;max=60000;unit=mA"      // Descriptor metadata for IBAT OCP.

#define PCAT_PMS_DESC_MET_WALLV "type=f32;unit=V;min=0.0;max=60.0;pull=1;push=1"      // Descriptor metadata for wall voltage.
#define PCAT_PMS_DESC_MET_BATTV "type=f32;unit=V;min=0.0;max=60.0;pull=1;push=1"      // Descriptor metadata for battery voltage.
#define PCAT_PMS_DESC_MET_WALLI "type=f32;unit=A;min=0.0;max=60.0;pull=1;push=1"    // Descriptor metadata for wall current.
#define PCAT_PMS_DESC_MET_BATTI "type=f32;unit=A;min=0.0;max=60.0;pull=1;push=1"    // Descriptor metadata for battery current.
#define PCAT_PMS_DESC_MET_PSRC "type=str;enum=wall|battery;pull=1;push=1"              // Descriptor metadata for power source.
#define PCAT_PMS_DESC_MET_TRIP "type=bool;min=0;max=1;pull=1;push=1"                   // Descriptor metadata for trip flag.
#define PCAT_PMS_DESC_MET_RCUT "type=bool;min=0;max=1;pull=1;push=1"                   // Descriptor metadata for relay-cut flag.

// PMS schema event keys.
#define PCAT_PMS_EVT_TRIPCH "tripch"      // Event key: trip state changed.
#define PCAT_PMS_EVT_PWRFLT "pwrflt"      // Event key: power fault.
#define PCAT_PMS_EVT_RESET "reset"        // Event key: reset requested.

// PMS schema map strings.
#define PCAT_PMS_SETMAP "dname,chan,pmod,LoopAuto,fan_mode,buzzer_enable,cli_baud,led_feedback_enable,rgb_idle_color,rgb_alert_color,rgb_brightness,chain_48v_enable,charger_enable,tripi,vcal,ical,vbovp,vbuvp,ibocp,baovp,bauvp,biocp"  // Ordered setting key map for capabilities.
#define PCAT_PMS_METMAP "wallv,battv,walli,batti,psrc,trip,rcut"                                           // Ordered telemetry key map for capabilities.
#define PCAT_PMS_EVMAP "tripch,pwrflt,reset"                                                                // Ordered event key map for capabilities.

// Compile-time key-length policy enforcement (<= 6 chars each).
PCAT_ASSERT_NVS_KEY_LEN(PCAT_PMS_KEY_DNAME);  // Validate dname key length.
PCAT_ASSERT_NVS_KEY_LEN(PCAT_PMS_KEY_CHAN);   // Validate chan key length.
PCAT_ASSERT_NVS_KEY_LEN(PCAT_PMS_KEY_PMOD);   // Validate pmod key length.
PCAT_ASSERT_NVS_KEY_LEN(PCAT_PMS_KEY_LOOPA);  // Validate loopa key length.
PCAT_ASSERT_NVS_KEY_LEN(PCAT_PMS_KEY_FANMD);  // Validate fanmd key length.
PCAT_ASSERT_NVS_KEY_LEN(PCAT_PMS_KEY_BUZEN);  // Validate buzen key length.
PCAT_ASSERT_NVS_KEY_LEN(PCAT_PMS_KEY_CLIBD);  // Validate cbaud key length.
PCAT_ASSERT_NVS_KEY_LEN(PCAT_PMS_KEY_LEDFB);  // Validate ledfb key length.
PCAT_ASSERT_NVS_KEY_LEN(PCAT_PMS_KEY_RGBIDL); // Validate rgbidl key length.
PCAT_ASSERT_NVS_KEY_LEN(PCAT_PMS_KEY_RGBALT); // Validate rgbalt key length.
PCAT_ASSERT_NVS_KEY_LEN(PCAT_PMS_KEY_RGBBRT); // Validate rgbbrt key length.
PCAT_ASSERT_NVS_KEY_LEN(PCAT_PMS_KEY_C48EN);  // Validate c48en key length.
PCAT_ASSERT_NVS_KEY_LEN(PCAT_PMS_KEY_CHGEN);  // Validate chgen key length.
PCAT_ASSERT_NVS_KEY_LEN(PCAT_PMS_KEY_TRIPI);  // Validate tripi key length.
PCAT_ASSERT_NVS_KEY_LEN(PCAT_PMS_KEY_VCAL);   // Validate vcal key length.
PCAT_ASSERT_NVS_KEY_LEN(PCAT_PMS_KEY_ICAL);   // Validate ical key length.
PCAT_ASSERT_NVS_KEY_LEN(PCAT_PMS_KEY_VBOVP);  // Validate vbovp key length.
PCAT_ASSERT_NVS_KEY_LEN(PCAT_PMS_KEY_VBUVP);  // Validate vbuvp key length.
PCAT_ASSERT_NVS_KEY_LEN(PCAT_PMS_KEY_IBOCP);  // Validate ibocp key length.
PCAT_ASSERT_NVS_KEY_LEN(PCAT_PMS_KEY_BAOVP);  // Validate baovp key length.
PCAT_ASSERT_NVS_KEY_LEN(PCAT_PMS_KEY_BAUVP);  // Validate bauvp key length.
PCAT_ASSERT_NVS_KEY_LEN(PCAT_PMS_KEY_BIOCP);  // Validate biocp key length.
