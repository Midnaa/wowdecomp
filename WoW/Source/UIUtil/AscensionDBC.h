#ifndef WOW_SOURCE_UIUTIL_ASCENSIONDBC_H
#define WOW_SOURCE_UIUTIL_ASCENSIONDBC_H

#include <cstdint>
#include <cstring>
#include <vector>

// Ascension's additional WDBC tables keep string offsets on disk, also on x64.
class AscensionDBC {
public:
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
