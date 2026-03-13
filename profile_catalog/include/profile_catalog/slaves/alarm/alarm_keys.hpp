#pragma once

#include "profile_catalog/shared/key_policy.hpp"

// Alarm profile identity defaults.
#define PCAT_ALARM_DEV_TYPE "ALARM"
#define PCAT_ALARM_HW_VER "ALARM-HW1"
#define PCAT_ALARM_SW_VER "0.1.0"
#define PCAT_ALARM_BUILD_TAG "alm1"
#define PCAT_ALARM_BUILD_ID BUILD_ID
#define PCAT_ALARM_DEF_NAME "ALARM-Node"

// Alarm slave profile NVS keys.
// Extend this list as soon as alarm profile settings are added.
#define PCAT_ALARM_KEY_DNAME "dname"
#define PCAT_ALARM_KEY_CHAN "chan"

PCAT_ASSERT_NVS_KEY_LEN(PCAT_ALARM_KEY_DNAME);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_ALARM_KEY_CHAN);
