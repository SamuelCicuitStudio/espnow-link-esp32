#pragma once

// NVS key policy for app-owned profile catalog.
// ESP Preferences key names must stay compact for portability.
#define PCAT_NVS_KEY_MAX_LEN 6

#define PCAT_ASSERT_NVS_KEY_LEN(key_literal) \
  static_assert((sizeof(key_literal) - 1) <= PCAT_NVS_KEY_MAX_LEN, "profile_catalog nvs key too long: " #key_literal)

// Global default build stamp for app-owned profiles.
// Can be overridden from build flags with -DBUILD_ID="...".
// Empty means "auto-generate short build id from profile tag + sw + compile timestamp".
#ifndef BUILD_ID
#define BUILD_ID ""
#endif
