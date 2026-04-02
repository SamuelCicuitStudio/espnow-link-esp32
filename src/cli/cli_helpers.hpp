/**************************************************************
 *  Author      : Tshibangu Samuel
 *  Role        : Freelance Embedded Systems Engineer
 *  Expertise   : Secure IoT Systems, Embedded C++, RTOS, Control Logic
 *  Contact     : tshibsamuel47@gmail.com
 *  Portfolio   : https://www.freelancer.com/u/tshibsamuel477
 *  Phone       : +216 54 429 793
 *  File Purpose: Shared CLI helper utility declarations used across split CLI translation units.
 **************************************************************/#pragma once

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

