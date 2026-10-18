#ifndef WOW_SOURCE_UIUTIL_ASCENSIONJSON_H
#define WOW_SOURCE_UIUTIL_ASCENSIONJSON_H
#include <cmath>
#include <charconv>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

struct AscensionJSONValue {
    enum Type { Null, Boolean, Number, String, Array, Object } type = Null;
    double number = 0;
    std::string string;
    std::vector<AscensionJSONValue> array;
    std::vector<std::pair<std::string, AscensionJSONValue>> object;
};

class AscensionJSON {
public:
    bool Parse(const char* text, size_t length, AscensionJSONValue& value) {
        if (!text || length > 16 * 1024 * 1024) { return false; }
        m_text = text;
        m_size = length;
        m_pos = m_nodes = 0;
        AscensionJSONValue parsed;
        if (!Value(parsed, 0)) { return false; }
        Space();
        if (m_pos != m_size) { return false; }
        value = std::move(parsed);
        return true;
    }
private:
    const char* m_text = nullptr;
    size_t m_size = 0, m_pos = 0, m_nodes = 0;
    void Space() {
        while (m_pos < m_size && (m_text[m_pos] == ' ' || m_text[m_pos] == '\r' || m_text[m_pos] == '\n' || m_text[m_pos] == '\t')) { ++m_pos; }
    }
    bool Take(char c) {
        Space();
        if (m_pos == m_size || m_text[m_pos] != c) { return false; }
        ++m_pos;
        return true;
    }
    bool Hex(uint32_t& value) {
        value = 0;
        for (int i = 0; i < 4; ++i) {
            if (m_pos == m_size) { return false; }
            char c = m_text[m_pos++];
            int digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
            if (digit < 0) { return false; }
            value = value * 16 + digit;
        }
        return true;
    }
    bool String(std::string& result) {
        if (!Take('"')) { return false; }
        while (m_pos < m_size) {
            unsigned char c = static_cast<unsigned char>(m_text[m_pos++]);
            if (c == '"') { return true; }
            if (c < 32) { return false; }
            if (c != '\\') { result.push_back(c); continue; }
            if (m_pos == m_size) { return false; }
            char escape = m_text[m_pos++];
            switch (escape) {
                case '"': case '\\': case '/': result.push_back(escape); break;
                case 'b': result.push_back('\b'); break;
                case 'f': result.push_back('\f'); break;
                case 'n': result.push_back('\n'); break;
                case 'r': result.push_back('\r'); break;
                case 't': result.push_back('\t'); break;
                case 'u': {
                    uint32_t cp;
                    if (!Hex(cp)) { return false; }
                    if (cp >= 0xD800 && cp <= 0xDBFF) {
                        if (m_size - m_pos < 2 || m_text[m_pos] != '\\' || m_text[m_pos + 1] != 'u') { return false; }
                        m_pos += 2;
                        uint32_t low;
                        if (!Hex(low) || low < 0xDC00 || low > 0xDFFF) { return false; }
                        cp = 0x10000 + ((cp - 0xD800) << 10) + low - 0xDC00;
                    } else if (cp >= 0xDC00 && cp <= 0xDFFF) { return false; }
                    if (cp < 0x80) { result.push_back(static_cast<char>(cp)); }
                    else if (cp < 0x800) {
                        result.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                        result.push_back(static_cast<char>(0x80 | (cp & 63)));
                    } else if (cp < 0x10000) {
                        result.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                        result.push_back(static_cast<char>(0x80 | ((cp >> 6) & 63)));
                        result.push_back(static_cast<char>(0x80 | (cp & 63)));
                    } else {
                        result.push_back(static_cast<char>(0xF0 | (cp >> 18)));
                        result.push_back(static_cast<char>(0x80 | ((cp >> 12) & 63)));
                        result.push_back(static_cast<char>(0x80 | ((cp >> 6) & 63)));
                        result.push_back(static_cast<char>(0x80 | (cp & 63)));
                    }
                    break;
                }
                default: return false;
            }
        }
        return false;
    }
    bool Digit() const { return m_pos < m_size && m_text[m_pos] >= '0' && m_text[m_pos] <= '9'; }
    bool Number(AscensionJSONValue& value) {
        size_t start = m_pos;
        if (m_text[m_pos] == '-') { ++m_pos; }
        if (!Digit()) { return false; }
        if (m_text[m_pos] == '0') { ++m_pos; }
        else { while (Digit()) { ++m_pos; } }
        if (m_pos < m_size && m_text[m_pos] == '.') {
            ++m_pos;
            if (!Digit()) { return false; }
            while (Digit()) { ++m_pos; }
        }
        if (m_pos < m_size && (m_text[m_pos] == 'e' || m_text[m_pos] == 'E')) {
            ++m_pos;
            if (m_pos < m_size && (m_text[m_pos] == '+' || m_text[m_pos] == '-')) { ++m_pos; }
            if (!Digit()) { return false; }
            while (Digit()) { ++m_pos; }
        }
        auto result = std::from_chars(m_text + start, m_text + m_pos, value.number);
        value.type = AscensionJSONValue::Number;
        return std::isfinite(value.number) && result.ec == std::errc() && result.ptr == m_text + m_pos;
    }
    bool Value(AscensionJSONValue& value, unsigned depth) {
        Space();
        if (depth > 64 || ++m_nodes > 1000000 || m_pos == m_size) { return false; }
        char c = m_text[m_pos];
        if (c == '"') { value.type = AscensionJSONValue::String; return String(value.string); }
        if (c == '{' || c == '[') {
            bool object = c == '{';
            value.type = object ? AscensionJSONValue::Object : AscensionJSONValue::Array;
            ++m_pos;
            if (Take(object ? '}' : ']')) { return true; }
            do {
                std::string key;
                if (object && (!String(key) || !Take(':'))) { return false; }
                AscensionJSONValue child;
                if (!Value(child, depth + 1)) { return false; }
                if (object) { value.object.emplace_back(std::move(key), std::move(child)); }
                else { value.array.push_back(std::move(child)); }
                if (Take(object ? '}' : ']')) { return true; }
            } while (Take(','));
            return false;
        }
        for (const auto& literal : { std::make_pair("null", 0), std::make_pair("false", 1), std::make_pair("true", 2) }) {
            size_t length = std::char_traits<char>::length(literal.first);
            if (length <= m_size - m_pos && !std::char_traits<char>::compare(m_text + m_pos, literal.first, length)) {
                m_pos += length;
                value.type = literal.second ? AscensionJSONValue::Boolean : AscensionJSONValue::Null;
                value.number = literal.second == 2;
                return true;
            }
        }
        return Number(value);
    }
};
#endif
