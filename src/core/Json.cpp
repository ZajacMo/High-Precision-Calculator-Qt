#include "Json.h"

#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace hpc {
namespace json {
namespace {

// 把 double 序列化为 JSON 数字。非有限值输出 null（JSON 无 NaN/Infinity）。
std::string numberToString(double value) {
    if (!std::isfinite(value)) return "null";
    // **先判范围，再转型**。
    //
    // 顺序反了就是未定义行为：`static_cast<std::int64_t>(1e300)` 超出了
    // int64 的可表示范围，C++ 标准规定这种转型是 UB —— MSVC 与 GCC
    // 在这里的实际结果并不一致（有的返回 INT64_MIN，有的给饱和值），
    // 同一个输入在不同编译器上会得到不同的 JSON。
    //
    // 9.0e15 < 2^53（约 9.007e15），所以这个区间内的 double 都能被
    // int64 精确表示，转型安全；转回 double 后再比较相等，就得到了
    // 「这个数是整数」的判断。
    if (std::fabs(value) < 9.0e15 &&
        value == static_cast<double>(static_cast<std::int64_t>(value))) {
        return std::to_string(static_cast<std::int64_t>(value));
    }
    std::ostringstream out;
    out.precision(15);
    out << value;
    return out.str();
}

void dumpValue(const Value& value, std::string& out, int indent, int depth);

void dumpIndent(std::string& out, int indent, int depth) {
    if (indent <= 0) return;
    out.append(static_cast<size_t>(indent * depth), ' ');
}

void dumpValue(const Value& value, std::string& out, int indent, int depth) {
    switch (value.type()) {
        case Value::Type::Null:
            out += "null";
            break;
        case Value::Type::Bool:
            out += value.asBool() ? "true" : "false";
            break;
        case Value::Type::Number:
            out += numberToString(value.asNumber());
            break;
        case Value::Type::String:
            out += '"';
            out += escape(value.asString());
            out += '"';
            break;
        case Value::Type::Array: {
            const Array& items = value.asArray();
            if (items.empty()) {
                out += "[]";
                break;
            }
            out += '[';
            for (size_t i = 0; i < items.size(); ++i) {
                if (i > 0) out += ',';
                if (indent > 0) {
                    out += '\n';
                    dumpIndent(out, indent, depth + 1);
                }
                dumpValue(items[i], out, indent, depth + 1);
            }
            if (indent > 0) {
                out += '\n';
                dumpIndent(out, indent, depth);
            }
            out += ']';
            break;
        }
        case Value::Type::Object: {
            const Object& members = value.asObject();
            if (members.empty()) {
                out += "{}";
                break;
            }
            out += '{';
            bool first = true;
            for (const auto& member : members) {
                if (!first) out += ',';
                first = false;
                if (indent > 0) {
                    out += '\n';
                    dumpIndent(out, indent, depth + 1);
                }
                out += '"';
                out += escape(member.first);
                out += "\":";
                if (indent > 0) out += ' ';
                dumpValue(member.second, out, indent, depth + 1);
            }
            if (indent > 0) {
                out += '\n';
                dumpIndent(out, indent, depth);
            }
            out += '}';
            break;
        }
    }
}

// 递归下降解析器。
class Parser {
public:
    Parser(const std::string& text) : m_text(text) {}

    bool parse(Value& out, std::string& error) {
        skipWhitespace();
        if (!parseValue(out)) {
            error = m_error;
            return false;
        }
        skipWhitespace();
        if (m_position != m_text.size()) {
            error = "JSON 结尾存在多余字符（位置 " + std::to_string(m_position) + "）。";
            return false;
        }
        return true;
    }

private:
    void skipWhitespace() {
        while (m_position < m_text.size()) {
            const char c = m_text[m_position];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++m_position;
            } else {
                break;
            }
        }
    }

    bool fail(const std::string& message) {
        if (m_error.empty()) {
            m_error = message + "（位置 " + std::to_string(m_position) + "）";
        }
        return false;
    }

    bool parseValue(Value& out) {
        skipWhitespace();
        if (m_position >= m_text.size()) return fail("JSON 内容意外结束");

        const char c = m_text[m_position];
        switch (c) {
            case '{': return parseObject(out);
            case '[': return parseArray(out);
            case '"': {
                std::string text;
                if (!parseString(text)) return false;
                out = Value(text);
                return true;
            }
            case 't':
                if (m_text.compare(m_position, 4, "true") == 0) {
                    m_position += 4;
                    out = Value(true);
                    return true;
                }
                return fail("无效的字面量");
            case 'f':
                if (m_text.compare(m_position, 5, "false") == 0) {
                    m_position += 5;
                    out = Value(false);
                    return true;
                }
                return fail("无效的字面量");
            case 'n':
                if (m_text.compare(m_position, 4, "null") == 0) {
                    m_position += 4;
                    out = Value();
                    return true;
                }
                return fail("无效的字面量");
            default:
                return parseNumber(out);
        }
    }

    bool parseNumber(Value& out) {
        const size_t start = m_position;
        if (m_position < m_text.size() && (m_text[m_position] == '-' || m_text[m_position] == '+')) {
            ++m_position;
        }
        bool hasDigits = false;
        while (m_position < m_text.size() && std::isdigit(static_cast<unsigned char>(m_text[m_position]))) {
            ++m_position;
            hasDigits = true;
        }
        if (m_position < m_text.size() && m_text[m_position] == '.') {
            ++m_position;
            while (m_position < m_text.size() && std::isdigit(static_cast<unsigned char>(m_text[m_position]))) {
                ++m_position;
                hasDigits = true;
            }
        }
        if (hasDigits && m_position < m_text.size() &&
            (m_text[m_position] == 'e' || m_text[m_position] == 'E')) {
            ++m_position;
            if (m_position < m_text.size() && (m_text[m_position] == '-' || m_text[m_position] == '+')) {
                ++m_position;
            }
            while (m_position < m_text.size() && std::isdigit(static_cast<unsigned char>(m_text[m_position]))) {
                ++m_position;
            }
        }
        if (!hasDigits) return fail("无效的数字");

        const std::string literal = m_text.substr(start, m_position - start);
        out = Value(std::strtod(literal.c_str(), nullptr));
        return true;
    }

    bool parseString(std::string& out) {
        if (m_position >= m_text.size() || m_text[m_position] != '"') {
            return fail("期望字符串");
        }
        ++m_position;
        out.clear();
        while (m_position < m_text.size()) {
            const char c = m_text[m_position];
            if (c == '"') {
                ++m_position;
                return true;
            }
            if (c == '\\') {
                ++m_position;
                if (m_position >= m_text.size()) return fail("转义序列未结束");
                const char escapeChar = m_text[m_position];
                switch (escapeChar) {
                    case '"': out += '"'; break;
                    case '\\': out += '\\'; break;
                    case '/': out += '/'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case 'n': out += '\n'; break;
                    case 'r': out += '\r'; break;
                    case 't': out += '\t'; break;
                    case 'u': {
                        // 解析 \uXXXX。仅支持 BMP 范围内的码点。
                        if (m_position + 4 >= m_text.size()) return fail("\\u 转义不完整");
                        unsigned int codePoint = 0;
                        for (int i = 1; i <= 4; ++i) {
                            const char digit = m_text[m_position + static_cast<size_t>(i)];
                            codePoint <<= 4;
                            if (digit >= '0' && digit <= '9') {
                                codePoint |= static_cast<unsigned int>(digit - '0');
                            } else if (digit >= 'a' && digit <= 'f') {
                                codePoint |= static_cast<unsigned int>(digit - 'a' + 10);
                            } else if (digit >= 'A' && digit <= 'F') {
                                codePoint |= static_cast<unsigned int>(digit - 'A' + 10);
                            } else {
                                return fail("\\u 转义包含非法字符");
                            }
                        }
                        m_position += 4;
                        // 代理对：高位 0xD800-0xDBFF 需搭配低位代理。
                        if (codePoint >= 0xD800 && codePoint <= 0xDBFF &&
                            m_position + 6 < m_text.size() && m_text[m_position + 1] == '\\' &&
                            m_text[m_position + 2] == 'u') {
                            unsigned int low = 0;
                            bool lowOk = true;
                            for (int i = 3; i <= 6; ++i) {
                                const char digit = m_text[m_position + static_cast<size_t>(i)];
                                low <<= 4;
                                if (digit >= '0' && digit <= '9') {
                                    low |= static_cast<unsigned int>(digit - '0');
                                } else if (digit >= 'a' && digit <= 'f') {
                                    low |= static_cast<unsigned int>(digit - 'a' + 10);
                                } else if (digit >= 'A' && digit <= 'F') {
                                    low |= static_cast<unsigned int>(digit - 'A' + 10);
                                } else {
                                    lowOk = false;
                                    break;
                                }
                            }
                            if (lowOk && low >= 0xDC00 && low <= 0xDFFF) {
                                codePoint = 0x10000 + ((codePoint - 0xD800) << 10) + (low - 0xDC00);
                                m_position += 6;
                            }
                        }
                        // 编码为 UTF-8。
                        if (codePoint < 0x80) {
                            out += static_cast<char>(codePoint);
                        } else if (codePoint < 0x800) {
                            out += static_cast<char>(0xC0 | (codePoint >> 6));
                            out += static_cast<char>(0x80 | (codePoint & 0x3F));
                        } else if (codePoint < 0x10000) {
                            out += static_cast<char>(0xE0 | (codePoint >> 12));
                            out += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
                            out += static_cast<char>(0x80 | (codePoint & 0x3F));
                        } else {
                            out += static_cast<char>(0xF0 | (codePoint >> 18));
                            out += static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F));
                            out += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
                            out += static_cast<char>(0x80 | (codePoint & 0x3F));
                        }
                        break;
                    }
                    default:
                        return fail("未知的转义字符");
                }
                ++m_position;
                continue;
            }
            out += c;
            ++m_position;
        }
        return fail("字符串未闭合");
    }

    bool parseArray(Value& out) {
        ++m_position;  // 跳过 '['
        Array items;
        skipWhitespace();
        if (m_position < m_text.size() && m_text[m_position] == ']') {
            ++m_position;
            out = Value(items);
            return true;
        }
        while (true) {
            Value item;
            if (!parseValue(item)) return false;
            items.push_back(item);
            skipWhitespace();
            if (m_position >= m_text.size()) return fail("数组未闭合");
            if (m_text[m_position] == ',') {
                ++m_position;
                continue;
            }
            if (m_text[m_position] == ']') {
                ++m_position;
                out = Value(items);
                return true;
            }
            return fail("数组中期望 ',' 或 ']'");
        }
    }

    bool parseObject(Value& out) {
        ++m_position;  // 跳过 '{'
        Object members;
        skipWhitespace();
        if (m_position < m_text.size() && m_text[m_position] == '}') {
            ++m_position;
            out = Value(members);
            return true;
        }
        while (true) {
            skipWhitespace();
            std::string key;
            if (!parseString(key)) return false;
            skipWhitespace();
            if (m_position >= m_text.size() || m_text[m_position] != ':') {
                return fail("对象中期望 ':'");
            }
            ++m_position;
            Value item;
            if (!parseValue(item)) return false;
            members[key] = item;
            skipWhitespace();
            if (m_position >= m_text.size()) return fail("对象未闭合");
            if (m_text[m_position] == ',') {
                ++m_position;
                continue;
            }
            if (m_text[m_position] == '}') {
                ++m_position;
                out = Value(members);
                return true;
            }
            return fail("对象中期望 ',' 或 '}'");
        }
    }

    const std::string& m_text;
    size_t m_position = 0;
    std::string m_error;
};

}  // namespace

Value::Value(bool value) : m_type(Type::Bool), m_bool(value) {}
Value::Value(double value) : m_type(Type::Number), m_number(value) {}
Value::Value(int value) : m_type(Type::Number), m_number(value) {}
Value::Value(std::int64_t value) : m_type(Type::Number), m_number(static_cast<double>(value)) {}
Value::Value(const char* value) : m_type(Type::String), m_string(value ? value : "") {}
Value::Value(std::string value) : m_type(Type::String), m_string(std::move(value)) {}
Value::Value(Array value) : m_type(Type::Array), m_array(std::make_shared<Array>(std::move(value))) {}
Value::Value(Object value) : m_type(Type::Object), m_object(std::make_shared<Object>(std::move(value))) {}

bool Value::asBool(bool fallback) const {
    return m_type == Type::Bool ? m_bool : fallback;
}

double Value::asNumber(double fallback) const {
    return m_type == Type::Number ? m_number : fallback;
}

std::string Value::asString(const std::string& fallback) const {
    return m_type == Type::String ? m_string : fallback;
}

const Array& Value::asArray() const {
    static const Array empty;
    return m_array ? *m_array : empty;
}

const Object& Value::asObject() const {
    static const Object empty;
    return m_object ? *m_object : empty;
}

const Value* Value::find(const std::string& key) const {
    if (!m_object) return nullptr;
    const auto it = m_object->find(key);
    return it == m_object->end() ? nullptr : &it->second;
}

const Value* Value::at(size_t index) const {
    if (!m_array || index >= m_array->size()) return nullptr;
    return &(*m_array)[index];
}

std::string Value::dump() const {
    std::string out;
    dumpValue(*this, out, 0, 0);
    return out;
}

std::string Value::dump(int indent) const {
    std::string out;
    dumpValue(*this, out, indent, 0);
    return out;
}

std::string escape(const std::string& text) {
    std::string out;
    out.reserve(text.size() + 8);
    for (unsigned char c : text) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buffer[8];
                    std::snprintf(buffer, sizeof(buffer), "\\u%04x", c);
                    out += buffer;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    return out;
}

bool parse(const std::string& text, Value& out, std::string& error) {
    Parser parser(text);
    return parser.parse(out, error);
}

}  // namespace json
}  // namespace hpc
