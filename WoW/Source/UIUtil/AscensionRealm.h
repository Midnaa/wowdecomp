#ifndef WOW_SOURCE_UIUTIL_ASCENSIONREALM_H
#define WOW_SOURCE_UIUTIL_ASCENSIONREALM_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

struct AscensionRealm {
    uint32_t id = 0;
    uint32_t ruleset = 0;
    std::array<uint8_t, 8> flags{};
    bool received = false;

    bool Read(const void* buffer, size_t size) {
        if (!buffer || size < 47) {
            return false;
        }
        const auto* bytes = static_cast<const uint8_t*>(buffer);
        size_t pos = 44;
        for (int i = 0; i < 2; ++i) {
            const auto* end = static_cast<const uint8_t*>(std::memchr(bytes + pos, 0, size - pos));
            if (!end) {
                return false;
            }
            pos = static_cast<size_t>(end - bytes) + 1;
        }
        if (pos >= size) {
            return false;
        }
        std::memcpy(&id, bytes, sizeof(id));
        std::memcpy(&ruleset, bytes + 4, sizeof(ruleset));
        std::memcpy(flags.data(), bytes + 36, flags.size());
        received = true;
        return true;
    }

    bool CanCreateClass(uint32_t id) const {
        if (!received || id == 0 || id > 32) {
            return false;
        }
        if (id == 10) {
            return !flags[6] && !flags[7];
        }
        if (id >= 12) {
            return flags[6] != 0;
        }
        return id != 6 && flags[7] != 0;
    }
};

#endif
