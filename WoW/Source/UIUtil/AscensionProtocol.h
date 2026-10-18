#ifndef WOW_SOURCE_UIUTIL_ASCENSIONPROTOCOL_H
#define WOW_SOURCE_UIUTIL_ASCENSIONPROTOCOL_H

#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

class AscensionPacketReader {
public:
    AscensionPacketReader(const void* bytes, size_t size) : m_bytes(static_cast<const uint8_t*>(bytes)), m_size(bytes ? size : 0) {}
    template<class T> bool Read(T& value) {
        if (sizeof(T) > m_size - m_position) { return false; }
        std::memcpy(&value, m_bytes + m_position, sizeof(T));
        m_position += sizeof(T);
        return true;
    }
    bool Text(std::string& value, size_t limit) {
        if (m_position == m_size) { return false; }
        const auto* begin = reinterpret_cast<const char*>(m_bytes + m_position);
        const auto* end = static_cast<const char*>(std::memchr(begin, 0, m_size - m_position));
        if (!end || size_t(end - begin) > limit) { return false; }
        value.assign(begin, end);
        m_position += value.size() + 1;
        return true;
    }
    bool Key(std::string& value) {
        uint32_t length;
        if (!Read(length) || !length || length > 256 || length > m_size - m_position) { return false; }
        const auto* begin = reinterpret_cast<const char*>(m_bytes + m_position);
        if (std::memchr(begin, 0, length)) { return false; }
        value.assign(begin, length);
        m_position += length;
        return true;
    }
    bool Done() const { return m_position == m_size; }
private:
    const uint8_t* m_bytes;
    size_t m_size;
    size_t m_position = 0;
};

struct AscensionCharacterList {
    struct Entry {
        uint32_t guid = 0, zone = 0;
        uint8_t active = 0, online = 0, level = 0, race = 0, classID = 0, sex = 0;
        std::string name;
    };
    uint32_t maximum = 0, total = 0, active = 0, inactive = 0;
    bool received = false;
    std::vector<Entry> entries;

    bool Read(const void* bytes, size_t size) {
        AscensionPacketReader packet(bytes, size);
        AscensionCharacterList next;
        if (!packet.Read(next.maximum) || !packet.Read(next.total) || !packet.Read(next.active)
            || !packet.Read(next.inactive) || !next.maximum || next.maximum > 128 || next.total > 128
            || next.active > next.total || next.inactive != next.total - next.active) { return false; }
        uint32_t activeCount = 0;
        std::set<uint32_t> guids;
        for (uint32_t i = 0; i < next.total; ++i) {
            Entry entry;
            if (!packet.Read(entry.guid) || !entry.guid || !guids.insert(entry.guid).second
                || !packet.Read(entry.active) || entry.active > 1
                || !packet.Read(entry.online) || entry.online > 1 || !packet.Read(entry.level)
                || !packet.Read(entry.race) || !packet.Read(entry.classID) || !packet.Read(entry.sex)
                || entry.sex > 2 || !packet.Read(entry.zone) || !packet.Text(entry.name, 47) || entry.name.empty()) {
                return false;
            }
            activeCount += entry.active;
            next.entries.push_back(std::move(entry));
        }
        if (!packet.Done() || activeCount != next.active) { return false; }
        next.received = true;
        *this = std::move(next);
        return true;
    }
};

struct AscensionConfig {
    std::map<std::string, int32_t> integers;
    std::map<std::string, uint8_t> booleans;
    std::map<std::string, float> floats, rates;
    std::map<std::string, std::vector<int32_t>> integerVectors;
    std::map<std::string, std::vector<float>> floatVectors;

    template<class T> static bool Valid(T) { return true; }
    static bool Valid(float value) { return std::isfinite(value); }
    static bool Valid(uint8_t value) { return value <= 1; }

    template<class T> static bool Section(AscensionPacketReader& packet, std::map<std::string, T>& values) {
        uint32_t count;
        if (!packet.Read(count) || count > 4096) { return false; }
        for (uint32_t i = 0; i < count; ++i) {
            std::string key;
            T value;
            if (!packet.Key(key) || !packet.Read(value) || !Valid(value)) { return false; }
            values[key] = value;
        }
        return true;
    }
    template<class T> static bool Vectors(AscensionPacketReader& packet, std::map<std::string, std::vector<T>>& values) {
        uint32_t count;
        if (!packet.Read(count) || count > 4096) { return false; }
        for (uint32_t i = 0; i < count; ++i) {
            std::string key;
            uint32_t length;
            if (!packet.Key(key) || !packet.Read(length) || length > 4096) { return false; }
            std::vector<T> vector;
            for (uint32_t j = 0; j < length; ++j) {
                T value;
                if (!packet.Read(value) || !Valid(value)) { return false; }
                vector.push_back(value);
            }
            values[key] = std::move(vector);
        }
        return true;
    }
    bool Read(const void* bytes, size_t size) {
        if (size > 1024 * 1024) { return false; }
        AscensionPacketReader packet(bytes, size);
        AscensionConfig next;
        if (!Section(packet, next.integers) || !Section(packet, next.booleans)
            || !Section(packet, next.floats) || !Section(packet, next.rates)
            || !Vectors(packet, next.integerVectors) || !Vectors(packet, next.floatVectors) || !packet.Done()) {
            return false;
        }
        *this = std::move(next);
        return true;
    }
};

#endif
