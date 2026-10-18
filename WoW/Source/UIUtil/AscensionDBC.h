#ifndef WOW_SOURCE_UIUTIL_ASCENSIONDBC_H
#define WOW_SOURCE_UIUTIL_ASCENSIONDBC_H

#include <cstdint>
#include <cstring>
#include <vector>
#include <string_view>

// Ascension's additional WDBC tables keep string offsets on disk, also on x64.
class AscensionDBC {
public:
    // The archive packs byte fields and stores each localized field as 16
    // offsets plus flags. The native table layout aligns DWORDs and keeps only
    // the selected locale offset. Other compatibility tables still use Assign.
    bool AssignFormatted(const void* data, size_t size, std::string_view format, uint32_t locale = 0) {
        *this = {};
        if (!data || size < 20 || std::memcmp(data, "WDBC", 4) || locale >= 16) { return false; }
        uint32_t header[4];
        std::memcpy(header, static_cast<const uint8_t*>(data) + 4, sizeof(header));
        uint32_t diskStride = 0, stride = 0;
        for (char type : format) {
            if (type != 'd' && type != 'L' && type != 'b') { return false; }
            if (type != 'b') { stride = (stride + 3) & ~3u; }
            diskStride += type == 'L' ? 68 : type == 'b' ? 1 : 4;
            stride += type == 'b' ? 1 : 4;
        }
        stride = (stride + 3) & ~3u;
        if (!stride || header[2] != diskStride || uint64_t(header[0]) * diskStride + header[3] + 20 != size) { return false; }
        uint64_t outputSize = uint64_t(header[0]) * stride + header[3] + 20;
        if (outputSize > SIZE_MAX || outputSize > 128 * 1024 * 1024) { return false; }
        std::vector<uint8_t> decoded(static_cast<size_t>(outputSize), 0);
        std::memcpy(decoded.data(), "WDBC", 4);
        uint32_t decodedHeader[] = { header[0], stride / 4, stride, header[3] };
        std::memcpy(decoded.data() + 4, decodedHeader, sizeof(decodedHeader));
        const auto* bytes = static_cast<const uint8_t*>(data);
        const uint8_t* strings = bytes + 20 + size_t(header[0]) * diskStride;
        for (uint32_t row = 0; row < header[0]; ++row) {
            uint32_t disk = 0, memory = 0;
            for (char type : format) {
                if (type != 'b') { memory = (memory + 3) & ~3u; }
                const uint8_t* source = bytes + 20 + size_t(row) * diskStride + disk;
                if (type == 'L') {
                    uint32_t offset;
                    std::memcpy(&offset, source + locale * 4, 4);
                    if (offset < header[3] && strings[offset]) { source += locale * 4; }
                }
                std::memcpy(decoded.data() + 20 + size_t(row) * stride + memory, source, type == 'b' ? 1 : 4);
                disk += type == 'L' ? 68 : type == 'b' ? 1 : 4;
                memory += type == 'b' ? 1 : 4;
            }
        }
        std::memcpy(decoded.data() + 20 + size_t(header[0]) * stride, strings, header[3]);
        return Assign(decoded.data(), decoded.size(), stride / 4);
    }

    bool Assign(const void* data, size_t size, uint32_t expectedFields) {
        m_bytes.clear();
        m_count = m_fields = m_strings = 0;
        if (!data || size < 20 || std::memcmp(data, "WDBC", 4)) {
            return false;
        }
        uint32_t header[4];
        std::memcpy(header, static_cast<const uint8_t*>(data) + 4, sizeof(header));
        if (header[1] != expectedFields || header[2] != uint64_t(expectedFields) * 4
            || uint64_t(header[0]) * header[2] + header[3] + 20 != size) {
            return false;
        }
        m_count = header[0];
        m_fields = header[1];
        m_strings = header[3];
        const auto* bytes = static_cast<const uint8_t*>(data);
        m_bytes.assign(bytes, bytes + size);
        return true;
    }

    uint32_t Count() const { return m_count; }

    uint32_t Value(uint32_t row, uint32_t column) const {
        uint32_t value = 0;
        if (row < m_count && column < m_fields) {
            std::memcpy(&value, m_bytes.data() + 20 + (size_t(row) * m_fields + column) * 4, 4);
        }
        return value;
    }

    const char* String(uint32_t row, uint32_t column) const {
        if (row >= m_count || column >= m_fields) {
            return "";
        }
        uint32_t offset = Value(row, column);
        if (offset >= m_strings) {
            return "";
        }
        const char* value = reinterpret_cast<const char*>(m_bytes.data() + 20 + size_t(m_count) * m_fields * 4 + offset);
        return std::memchr(value, 0, m_strings - offset) ? value : "";
    }

private:
    std::vector<uint8_t> m_bytes;
    uint32_t m_count = 0;
    uint32_t m_fields = 0;
    uint32_t m_strings = 0;
};

#endif
