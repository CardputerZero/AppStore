#pragma once

#include <string>
#include <vector>

namespace appstore {

std::string tsv_unescape(const std::string &value);
std::vector<std::string> split_tab(const std::string &line);

} // namespace appstore
