#pragma once

namespace espnow_link {

// Single source of truth for app/runtime/profile persistence namespace.
inline constexpr const char* kSharedNvsPartition = "nvs";
inline constexpr const char* kSharedNvsNamespace = "cfg";

}  // namespace espnow_link
