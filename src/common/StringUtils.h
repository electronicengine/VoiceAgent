#pragma once

#include <string>
#include <string_view>

namespace voice_agent {

std::string Trim(const std::string& input);
std::string ToLower(std::string value);
std::string EscapeXml(std::string_view input);

}  // namespace voice_agent