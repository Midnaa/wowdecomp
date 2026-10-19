#pragma once
#include "AscensionProtocol.h"

// SMSG_UPDATE_OBJECT_ADDON: GUID followed by field/value pairs.
struct AscensionObjectAddonState {
    std::map<uint64_t, std::map<uint32_t, uint32_t>> units;
    bool Read(const void* data, size_t size) {
        if (size < 8 || (size - 8) % 8 || size > 8 + 4096 * 8) return false;
        AscensionPacketReader packet(data, size);
        uint64_t guid;
        if (!packet.Read(guid) || !guid) return false;
        auto found = units.find(guid);
        if (found == units.end() && units.size() >= 4096) return false;
        auto fields = found == units.end() ? std::map<uint32_t, uint32_t>{} : found->second;
        while (!packet.Done()) {
            uint32_t index, value;
            if (!packet.Read(index) || !packet.Read(value) || index >= 4096) return false;
            fields[index] = value;
        }
        units[guid] = std::move(fields);
        return true;
    }
    uint32_t Get(uint64_t guid, uint32_t index) const {
        auto unit = units.find(guid);
        if (unit == units.end()) return 0;
        auto field = unit->second.find(index);
        return field == unit->second.end() ? 0 : field->second;
    }
};
