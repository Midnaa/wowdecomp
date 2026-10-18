#pragma once
#include "AscensionProtocol.h"

struct AscensionAdvancementState {
    struct Entry { uint32_t rank, spellRank, order; bool locked; };
    std::map<uint32_t, Entry> known;
    uint32_t active = 0, count = 1;
    bool received = false;
    bool ReadKnown(const void* data, size_t size) {
        AscensionPacketReader packet(data, size);
        uint32_t total;
        if (!packet.Read(total) || total > 16384 || size != 4 + size_t(total) * 21) { return false; }
        std::map<uint32_t, Entry> next;
        for (uint32_t i = 0; i < total; ++i) {
            uint32_t id, unused;
            Entry entry{};
            uint8_t locked;
            if (!packet.Read(id) || !id || !packet.Read(entry.rank) || entry.rank > 9
                || !packet.Read(entry.spellRank) || !packet.Read(locked) || locked > 1
                || !packet.Read(entry.order) || !packet.Read(unused) || !next.emplace(id, entry).second) { return false; }
            next[id].locked = locked != 0;
        }
        if (!packet.Done()) { return false; }
        known = std::move(next);
        return true;
    }
    bool ReadSpec(const void* data, size_t size) {
        AscensionPacketReader packet(data, size);
        uint32_t nextActive, nextCount;
        if (!packet.Read(nextActive) || !packet.Read(nextCount) || !packet.Done()
            || !nextCount || nextCount > 32 || nextActive >= nextCount) { return false; }
        active = nextActive;
        count = nextCount;
        received = true;
        return true;
    }
};
