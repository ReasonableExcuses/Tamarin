// ============================================================================
// json.h - Lightweight JSON DOM for Language Server Protocol (LSP)
// ============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <string_view>
#include <optional>
#include <initializer_list>

namespace tamarin::lsp {

enum class JsonType {
    Null,
    Bool,
    Int,
    Float,
    String,
    Array,
    Object
};

class JsonValue {
public:
    using Array = std::vector<JsonValue>;
    using Object = std::vector<std::pair<std::string, JsonValue>>;

    // Constructors
    JsonValue() : type_(JsonType::Null) {}
    JsonValue(std::nullptr_t) : type_(JsonType::Null) {}
    JsonValue(bool b) : type_(JsonType::Bool), boolVal_(b) {}
    JsonValue(int i) : type_(JsonType::Int), intVal_(i) {}
    JsonValue(int64_t i) : type_(JsonType::Int), intVal_(i) {}
    JsonValue(uint64_t u) : type_(JsonType::Int), intVal_(static_cast<int64_t>(u)) {}
    JsonValue(double d) : type_(JsonType::Float), floatVal_(d) {}
    JsonValue(const char* s) : type_(JsonType::String), strVal_(s ? s : "") {}
    JsonValue(std::string s) : type_(JsonType::String), strVal_(std::move(s)) {}
    JsonValue(std::string_view sv) : type_(JsonType::String), strVal_(sv) {}
    JsonValue(Array arr) : type_(JsonType::Array), arrVal_(std::move(arr)) {}
    JsonValue(Object obj) : type_(JsonType::Object), objVal_(std::move(obj)) {}
    JsonValue(std::initializer_list<std::pair<std::string, JsonValue>> init)
        : type_(JsonType::Object), objVal_(init.begin(), init.end()) {}

    // Type queries
    [[nodiscard]] JsonType type() const noexcept { return type_; }
    [[nodiscard]] bool isNull() const noexcept { return type_ == JsonType::Null; }
    [[nodiscard]] bool isBool() const noexcept { return type_ == JsonType::Bool; }
    [[nodiscard]] bool isInt() const noexcept { return type_ == JsonType::Int; }
    [[nodiscard]] bool isFloat() const noexcept { return type_ == JsonType::Float; }
    [[nodiscard]] bool isNumber() const noexcept { return type_ == JsonType::Int || type_ == JsonType::Float; }
    [[nodiscard]] bool isString() const noexcept { return type_ == JsonType::String; }
    [[nodiscard]] bool isArray() const noexcept { return type_ == JsonType::Array; }
    [[nodiscard]] bool isObject() const noexcept { return type_ == JsonType::Object; }

    // Value extractors
    [[nodiscard]] bool asBool(bool def = false) const noexcept { return isBool() ? boolVal_ : def; }
    [[nodiscard]] int64_t asInt(int64_t def = 0) const noexcept {
        if (isInt()) return intVal_;
        if (isFloat()) return static_cast<int64_t>(floatVal_);
        return def;
    }
    [[nodiscard]] double asFloat(double def = 0.0) const noexcept {
        if (isFloat()) return floatVal_;
        if (isInt()) return static_cast<double>(intVal_);
        return def;
    }
    [[nodiscard]] const std::string& asString() const {
        static const std::string empty;
        return isString() ? strVal_ : empty;
    }
    [[nodiscard]] const Array& asArray() const {
        static const Array empty;
        return isArray() ? arrVal_ : empty;
    }
    [[nodiscard]] Array& asArray() {
        if (!isArray()) { type_ = JsonType::Array; arrVal_.clear(); }
        return arrVal_;
    }
    [[nodiscard]] const Object& asObject() const {
        static const Object empty;
        return isObject() ? objVal_ : empty;
    }
    [[nodiscard]] Object& asObject() {
        if (!isObject()) { type_ = JsonType::Object; objVal_.clear(); }
        return objVal_;
    }

    // Accessors
    [[nodiscard]] bool contains(std::string_view key) const;
    [[nodiscard]] const JsonValue* get(std::string_view key) const;
    [[nodiscard]] JsonValue* get(std::string_view key);
    [[nodiscard]] JsonValue getOrDefault(std::string_view key, JsonValue def) const;

    JsonValue& operator[](std::string_view key);
    const JsonValue& operator[](std::string_view key) const;

    JsonValue& operator[](size_t index);
    const JsonValue& operator[](size_t index) const;

    void push_back(JsonValue val);

    // Serialization & Parsing
    [[nodiscard]] std::string serialize(bool pretty = false) const;
    static std::optional<JsonValue> parse(std::string_view json, std::string* error = nullptr);

    // Factory helpers
    static JsonValue array() { return JsonValue(Array{}); }
    static JsonValue object() { return JsonValue(Object{}); }

private:
    JsonType type_ = JsonType::Null;
    bool boolVal_ = false;
    int64_t intVal_ = 0;
    double floatVal_ = 0.0;
    std::string strVal_;
    Array arrVal_;
    Object objVal_;
};

} // namespace tamarin::lsp
