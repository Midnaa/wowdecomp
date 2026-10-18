#pragma once
#include "AscensionProtocol.h"
#include <array>

struct AscensionTutorialState {
    struct Patch { std::array<uint32_t, 45> fields{}; std::array<std::string, 5> strings; };
    std::map<uint32_t, Patch> patches;
    std::set<uint32_t> rewarded;
    bool ReadPatch(const void* data, size_t size) {
        AscensionPacketReader packet(data, size);
        Patch next;
        for (auto& value : next.fields) { if (!packet.Read(value)) { return false; } }
        for (auto& value : next.strings) { if (!packet.Text(value, 1024 * 1024)) { return false; } }
        if (!packet.Done() || !next.fields[0] || patches.size() >= 4096 && !patches.count(next.fields[0])) { return false; }
        patches[next.fields[0]] = std::move(next);
        return true;
    }
    bool ReadRewards(const void* data, size_t size, bool single) {
        AscensionPacketReader packet(data, size);
        uint32_t count = 1;
        if (!single && (!packet.Read(count) || count > 4096)) { return false; }
        std::set<uint32_t> next;
        for (uint32_t i = 0; i < count; ++i) {
            uint32_t id;
            if (!packet.Read(id) || !id || !next.insert(id).second) { return false; }
        }
        if (!packet.Done()) { return false; }
        if (single) { rewarded.insert(next.begin(), next.end()); }
        else { rewarded = std::move(next); }
        return true;
    }
};
