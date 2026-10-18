#ifndef WOW_SOURCE_UIUTIL_ASCENSIONSPELLCHARGES_H
#define WOW_SOURCE_UIUTIL_ASCENSIONSPELLCHARGES_H
#include "AscensionProtocol.h"

struct AscensionSpellCharges {
    struct State {
        uint32_t received, remaining, missing;
        uint32_t Restored(uint32_t now, uint32_t recovery) const {
            uint32_t elapsed = now - received;
            return !recovery || elapsed < remaining ? 0 : 1 + (elapsed - remaining) / recovery;
        }
    };
    std::map<uint32_t, State> entries;
    bool Read(uint16_t opcode, const void* data, size_t size, uint32_t now) {
        if (opcode == 0x09C2) {
            if (size) { return false; }
            entries.clear();
            return true;
        }
        if (opcode != 0x09C4 && opcode != 0x09C5) { return false; }
        AscensionPacketReader packet(data, size);
        uint32_t count = 1;
        if (opcode == 0x09C4 && (!packet.Read(count) || count > 4096)) { return false; }
        auto next = opcode == 0x09C4 ? std::map<uint32_t, State>{} : entries;
        for (uint32_t i = 0; i < count; ++i) {
            uint32_t category, remaining, missing;
            uint8_t byte;
            if (!packet.Read(category) || !category || !packet.Read(remaining)) { return false; }
            if (opcode == 0x09C4) { if (!packet.Read(byte)) { return false; } missing = byte; }
            else if (!packet.Read(missing) || missing > 255) { return false; }
            if (!missing || !remaining) { next.erase(category); }
            else { next[category] = { now, remaining, missing }; }
            if (next.size() > 4096) { return false; }
        }
        if (!packet.Done()) { return false; }
        entries = std::move(next);
        return true;
    }
};
#endif
