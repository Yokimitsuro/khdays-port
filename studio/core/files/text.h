// Small text helpers the studio's file readers share.
#pragma once

#include <string>

namespace studio {

// `s` without its leading and trailing spaces, tabs and carriage returns.
inline std::string trimmed(const std::string& s) {
    const auto a = s.find_first_not_of(" \t\r");
    if (a == std::string::npos)
        return {};
    return s.substr(a, s.find_last_not_of(" \t\r") - a + 1);
}

}  // namespace studio
