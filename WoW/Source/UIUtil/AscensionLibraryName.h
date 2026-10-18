#pragma once

#include <string_view>

inline bool AscensionLibraryName(std::string_view name) {
    if (name.empty() || name.size() > 120 || name.find("..") != std::string_view::npos) { return false; }
    for (unsigned char c : name) {
        if (!(c >= 'a' && c <= 'z') && !(c >= 'A' && c <= 'Z') && !(c >= '0' && c <= '9')
            && c != '-' && c != '_' && c != '.') { return false; }
    }
    return true;
}
