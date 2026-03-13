#pragma once

#include "profile_catalog/shared/key_policy.hpp"

// Lock profile identity defaults.
#define PCAT_LOCK_DEV_TYPE "LOCK"
#define PCAT_LOCK_HW_VER "LOCK-HW1"
#define PCAT_LOCK_SW_VER "0.1.0"
#define PCAT_LOCK_BUILD_TAG "lok1"
#define PCAT_LOCK_BUILD_ID BUILD_ID
#define PCAT_LOCK_DEF_NAME "LOCK-Node"

// Lock slave profile NVS keys.
// Extend this list as soon as lock profile settings are added.
#define PCAT_LOCK_KEY_DNAME "dname"
#define PCAT_LOCK_KEY_CHAN "chan"

PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_DNAME);
PCAT_ASSERT_NVS_KEY_LEN(PCAT_LOCK_KEY_CHAN);
