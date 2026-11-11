// 轻量 JSON：只实现本项目需要的能力（对象、数组、字符串、数字、布尔、null）。
// 零外部依赖，保证任何 C++17 工具链下都能编译。
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace hpc {
namespace json {

class Value;

using Object = std::map<std::string, Value>;
using Array = std::vector<Value>;

// JSON 值。内部用 shared_ptr 存储数组与对象，避免拷贝开销与不完整类型问题。
class Value {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Value() = default;
    Value(bool value);
    Value(double value);
    Value(int value);
    Value(std::int64_t value);
    Value(const char* value);
    Value(std::string value);
    Value(Array value);
    Value(Object value);

    Type type() const { return m_type; }
    bool isNull() const { return m_type == Type::Null; }
    bool isBool() const { return m_type == Type::Bool; }
    bool isNumber() const { return m_type == Type::Number; }
    bool isString() const { return m_type == Type::String; }
    bool isArray() const { return m_type == Type::Array; }
    bool isObject() const { return m_type == Type::Object; }

    // 取值。类型不匹配时返回默认值。
    bool asBool(bool fallback = false) const;
    double asNumber(double fallback = 0.0) const;
    std::string asString(const std::string& fallback = std::string()) const;
    const Array& asArray() const;
    const Object& asObject() const;

    // 访问对象成员，不存在时返回 nullptr。
    const Value* find(const std::string& key) const;

    // 访问数组元素，越界时返回 nullptr。
    const Value* at(size_t index) const;

    // 序列化为紧凑 JSON 字符串。
    std::string dump() const;

    // 序列化为带缩进的 JSON 字符串，便于调试。
    std::string dump(int indent) const;

private:
    Type m_type = Type::Null;
    bool m_bool = false;
    double m_number = 0.0;
    std::string m_string;
    std::shared_ptr<Array> m_array;
    std::shared_ptr<Object> m_object;
};

// 解析 JSON 字符串。失败时返回 false 并填写 error。
bool parse(const std::string& text, Value& out, std::string& error);

// 转义字符串内容（不含引号）。
std::string escape(const std::string& text);

}  // namespace json
}  // namespace hpc
