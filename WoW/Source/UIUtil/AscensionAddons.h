#ifndef WOW_SOURCE_UIUTIL_ASCENSIONADDONS_H
#define WOW_SOURCE_UIUTIL_ASCENSIONADDONS_H

#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

struct AscensionAddons {
    std::vector<std::pair<std::string, bool>> entries;

    bool Read(const void* buffer, size_t size) {
        if (!buffer || size < 4) {
            return false;
        }
        const auto* bytes = static_cast<const uint8_t*>(buffer);
        uint32_t count;
        std::memcpy(&count, bytes, 4);
        if (count > 4096 || count > (size - 4) / 3) {
            return false;
        }
        std::vector<std::pair<std::string, bool>> result;
        size_t pos = 4;
        for (uint32_t i = 0; i < count; ++i) {
            if (pos >= size) {
                return false;
            }
            const auto* end = static_cast<const uint8_t*>(std::memchr(bytes + pos, 0, size - pos));
            if (!end) {
                return false;
            }
            size_t length = static_cast<size_t>(end - bytes) - pos;
            if (!length || length > 260 || pos + length + 1 >= size || end[1] > 1) {
                return false;
            }
            result.emplace_back(std::string(reinterpret_cast<const char*>(bytes + pos), length), end[1] != 0);
            pos += length + 2;
        }
        if (pos != size) {
            return false;
        }
        entries.swap(result);
        return true;
    }
};

#endif
