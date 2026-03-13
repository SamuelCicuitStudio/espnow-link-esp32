#pragma once

#include <string>

#include "espnow_link/types.hpp"

namespace espnow_link {

/**
 * @brief Parse one hexadecimal nibble.
 * @param c Input character.
 * @param out Parsed value (0..15).
 * @return true when character is valid hex.
 */
bool parseHexNibble(char c, uint8_t& out);

/**
 * @brief Parse MAC string (`AA:BB:CC:DD:EE:FF`) into `MacAddress`.
 * @param text Input C-string.
 * @param out Parsed MAC output.
 * @return true on valid format.
 */
bool parseMac(const char* text, MacAddress& out);

/**
 * @brief Parse MAC string (`AA:BB:CC:DD:EE:FF`) into `MacAddress`.
 * @param text Input string.
 * @param out Parsed MAC output.
 * @return true on valid format.
 */
bool parseMac(const std::string& text, MacAddress& out);

/**
 * @brief Convert MAC bytes to uppercase colon-separated string.
 * @param mac Input MAC.
 * @return Formatted MAC string.
 */
std::string macToString(const MacAddress& mac);

}  // namespace espnow_link
