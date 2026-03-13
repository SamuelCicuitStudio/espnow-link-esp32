#pragma once

#include <string>

#include "espnow_link/control.hpp"

namespace espnow_link {
namespace cli_helpers {

std::string trim(const std::string& in);
std::string lowerCopy(const std::string& in);
bool startsWith(const std::string& s, const std::string& prefix);
std::string macToPrintable(const MacAddress& mac);
uint32_t nowMs();

}  // namespace cli_helpers
}  // namespace espnow_link
