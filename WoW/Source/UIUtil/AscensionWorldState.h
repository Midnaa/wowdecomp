#pragma once
#include "AscensionProtocol.h"
#include <array>

struct AscensionWorldState {
    uint32_t modes = 0, depth = 0, scene = 0;
    std::array<uint32_t, 7> arena{};
    std::map<uint32_t, uint32_t> challenges;
    bool ReadModes(const void* data, size_t size) {
        AscensionPacketReader packet(data, size);
        uint32_t next;
        if (!packet.Read(next) || !packet.Done()) { return false; }
        modes = next; return true;
    }
    bool ReadArena(const void* data, size_t size) {
        AscensionPacketReader packet(data, size);
        std::array<uint32_t, 7> next{};
        for (auto& value : next) { if (!packet.Read(value)) { return false; } }
        if (!packet.Done() || next[2] > 5) { return false; }
        arena = next; return true;
    }
    bool ReadChallenges(const void* data, size_t size) {
        AscensionPacketReader packet(data, size);
        uint32_t count;
        if (!packet.Read(count) || count > 4096 || size != 4 + size_t(count) * 26) { return false; }
        std::map<uint32_t, uint32_t> next;
        for (uint32_t i = 0; i < count; ++i) {
            uint32_t ignored, id, level;
            uint16_t flags;
            if (!packet.Read(ignored) || !packet.Read(id) || !id || !packet.Read(level) || !level
                || !packet.Read(ignored) || !packet.Read(ignored) || !packet.Read(ignored) || !packet.Read(flags)
                || !next.emplace(id, level).second) { return false; }
        }
        if (!packet.Done()) { return false; }
        challenges = std::move(next); return true;
    }
    bool ReadManastorm(const void* data, size_t size) {
        AscensionPacketReader packet(data, size);
        uint32_t nextDepth, nextScene, caches, item;
        float chance;
        std::string type;
        if (!packet.Read(nextDepth) || !packet.Read(nextScene) || !packet.Text(type, 64)
            || !packet.Read(caches) || !packet.Read(chance) || !std::isfinite(chance) || !packet.Read(item) || !packet.Done()) { return false; }
        depth = nextDepth; scene = nextScene; return true;
    }
};
