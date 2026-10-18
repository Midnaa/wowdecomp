#ifndef WOW_SOURCE_UIUTIL_ASCENSIONAURAAMOUNTS_H
#define WOW_SOURCE_UIUTIL_ASCENSIONAURAAMOUNTS_H
#include "AscensionProtocol.h"
#include <array>

struct AscensionAuraAmounts {
    struct Aura {
        uint64_t caster = 0;
        uint32_t spell = 0;
        std::array<int32_t, 3> amounts{};
    };
    std::map<uint64_t, std::map<int32_t, Aura>> units;
    bool Read(const void* data, size_t size, uint64_t& target) {
        AscensionPacketReader packet(data, size);
        uint64_t guid;
        int32_t slot;
        Aura aura;
        if (!packet.Read(guid) || !guid || !packet.Read(slot) || slot < -1 || slot >= 256
            || !packet.Read(aura.caster) || !packet.Read(aura.spell) || !aura.spell) { return false; }
        if (slot >= 0) {
            for (auto& amount : aura.amounts) { if (!packet.Read(amount)) { return false; } }
        }
        if (!packet.Done()) { return false; }
        auto found = units.find(guid);
        if (slot < 0) {
            if (found != units.end()) {
                for (auto entry = found->second.begin(); entry != found->second.end();) {
                    if (entry->second.caster == aura.caster && entry->second.spell == aura.spell) { entry = found->second.erase(entry); }
                    else { ++entry; }
                }
                if (found->second.empty()) { units.erase(found); }
            }
        } else {
            if (found == units.end() && units.size() >= 4096) { return false; }
            units[guid][slot] = aura;
        }
        target = guid;
        return true;
    }
};
#endif
