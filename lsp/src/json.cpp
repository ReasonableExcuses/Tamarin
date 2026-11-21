// ============================================================================
// json.cpp - Lightweight JSON DOM implementation for Language Server Protocol
// ============================================================================

#include "lsp/json.h"

#include <cctype>
#include <charconv>
#include <cmath>
#include <sstream>
#include <iomanip>

namespace tamarin::lsp {

bool JsonValue::contains(std::string_view key) const {
    if (!isObject()) return false;
    for (const auto& kv : objVal_) {
        if (kv.first == key) return true;
    }
    return false;
}

const JsonValue* JsonValue::get(std::string_view key) const {
    if (!isObject()) return nullptr;
    for (const auto& kv : objVal_) {
        if (kv.first == key) return &kv.second;
    }
    return nullptr;
}

JsonValue* JsonValue::get(std::string_view key) {
    if (!isObject()) return nullptr;
    for (auto& kv : objVal_) {
        if (kv.first == key) return &kv.second;
    }
    return nullptr;
}

JsonValue JsonValue::getOrDefault(std::string_view key, JsonValue def) const {
    const auto* p = get(key);
    return p ? *p : def;
}

JsonValue& JsonValue::operator[](std::string_view key) {
    if (!isObject()) {
        type_ = JsonType::Object;
        objVal_.clear();
    }
    for (auto& kv : objVal_) {
        if (kv.first == key) return kv.second;
    }
    objVal_.emplace_back(std::string(key), JsonValue());
    return objVal_.back().second;
}

const JsonValue& JsonValue::operator[](std::string_view key) const {
    static const JsonValue nullVal;
    const auto* p = get(key);
    return p ? *p : nullVal;
}

JsonValue& JsonValue::operator[](size_t index) {
    if (!isArray()) {
        type_ = JsonType::Array;
        arrVal_.clear();
    }
    if (index >= arrVal_.size()) {
        arrVal_.resize(index + 1);
    }
    return arrVal_[index];
}

const JsonValue& JsonValue::operator[](size_t index) const {
    static const JsonValue nullVal;
    if (!isArray() || index >= arrVal_.size()) return nullVal;
    return arrVal_[index];
}

void JsonValue::push_back(JsonValue val) {
    if (!isArray()) {
        type_ = JsonType::Array;
        arrVal_.clear();
    }
    arrVal_.push_back(std::move(val));
}

// ── Serialization ───────────────────────────────────────────────────────────

static void escapeString(std::string_view s, std::string& out) {
    out.push_back('"');
    for (char c : s) {
        switch (c) {
            case '"':  out.append("\\\""); break;
            case '\\': out.append("\\\\"); break;
            case '\b': out.append("\\b"); break;
            case '\f': out.append("\\f"); break;
            case '\n': out.append("\\n"); break;
            case '\r': out.append("\\r"); break;
            case '\t': out.append("\\t"); break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                    out.append(buf);
                } else {
                    out.push_back(c);
                }
                break;
        }
    }
    out.push_back('"');
}

static void serializeHelper(const JsonValue& val, std::string& out, bool pretty, int indent) {
    switch (val.type()) {
        case JsonType::Null:
            out.append("null");
            break;
        case JsonType::Bool:
            out.append(val.asBool() ? "true" : "false");
            break;
        case JsonType::Int:
            out.append(std::to_string(val.asInt()));
            break;
        case JsonType::Float: {
            double d = val.asFloat();
            if (std::isnan(d) || std::isinf(d)) {
                out.append("null");
            } else {
                char buf[64];
                snprintf(buf, sizeof(buf), "%.17g", d);
                out.append(buf);
            }
            break;
        }
        case JsonType::String:
            escapeString(val.asString(), out);
            break;
        case JsonType::Array: {
            const auto& arr = val.asArray();
            if (arr.empty()) {
                out.append("[]");
                return;
            }
            out.push_back('[');
            if (pretty) out.push_back('\n');
            for (size_t i = 0; i < arr.size(); ++i) {
                if (pretty) out.append(indent + 2, ' ');
                serializeHelper(arr[i], out, pretty, indent + 2);
                if (i + 1 < arr.size()) out.push_back(',');
                if (pretty) out.push_back('\n');
            }
            if (pretty) out.append(indent, ' ');
            out.push_back(']');
            break;
        }
        case JsonType::Object: {
            const auto& obj = val.asObject();
            if (obj.empty()) {
                out.append("{}");
                return;
            }
            out.push_back('{');
            if (pretty) out.push_back('\n');
            for (size_t i = 0; i < obj.size(); ++i) {
                if (pretty) out.append(indent + 2, ' ');
                escapeString(obj[i].first, out);
                out.append(pretty ? ": " : ":");
                serializeHelper(obj[i].second, out, pretty, indent + 2);
                if (i + 1 < obj.size()) out.push_back(',');
                if (pretty) out.push_back('\n');
            }
            if (pretty) out.append(indent, ' ');
            out.push_back('}');
            break;
        }
    }
}

std::string JsonValue::serialize(bool pretty) const {
    std::string out;
    out.reserve(128);
    serializeHelper(*this, out, pretty, 0);
    return out;
}

// ── Parser ──────────────────────────────────────────────────────────────────

namespace {

class JsonParser {
public:
    explicit JsonParser(std::string_view src) : src_(src) {}

    std::optional<JsonValue> parse(std::string* error) {
        skipWhitespace();
        if (pos_ >= src_.size()) {
            if (error) *error = "Empty input";
            return std::nullopt;
        }
        auto res = parseValue(error);
        if (!res) return std::nullopt;
        skipWhitespace();
        if (pos_ < src_.size()) {
            if (error) *error = "Unexpected trailing characters at offset " + std::to_string(pos_);
            return std::nullopt;
        }
        return res;
    }

private:
    std::string_view src_;
    size_t pos_ = 0;

    void skipWhitespace() {
        while (pos_ < src_.size()) {
            char c = src_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                pos_++;
            } else {
                break;
            }
        }
    }

    char peek() const {
        return pos_ < src_.size() ? src_[pos_] : '\0';
    }

    char get() {
        return pos_ < src_.size() ? src_[pos_++] : '\0';
    }

    std::optional<JsonValue> parseValue(std::string* error) {
        skipWhitespace();
        if (pos_ >= src_.size()) {
            if (error) *error = "Unexpected end of input";
            return std::nullopt;
        }

        char c = peek();
        if (c == 'n') return parseNull(error);
        if (c == 't' || c == 'f') return parseBool(error);
        if (c == '"') return parseString(error);
        if (c == '[') return parseArray(error);
        if (c == '{') return parseObject(error);
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) return parseNumber(error);

        if (error) *error = std::string("Unexpected character '") + c + "' at offset " + std::to_string(pos_);
        return std::nullopt;
    }

    std::optional<JsonValue> parseNull(std::string* error) {
        if (src_.substr(pos_, 4) == "null") {
            pos_ += 4;
            return JsonValue(nullptr);
        }
        if (error) *error = "Expected 'null' at offset " + std::to_string(pos_);
        return std::nullopt;
    }

    std::optional<JsonValue> parseBool(std::string* error) {
        if (src_.substr(pos_, 4) == "true") {
            pos_ += 4;
            return JsonValue(true);
        }
        if (src_.substr(pos_, 5) == "false") {
            pos_ += 5;
            return JsonValue(false);
        }
        if (error) *error = "Expected boolean at offset " + std::to_string(pos_);
        return std::nullopt;
    }

    std::optional<JsonValue> parseString(std::string* error) {
        if (get() != '"') {
            if (error) *error = "Expected '\"'";
            return std::nullopt;
        }

        std::string s;
        while (pos_ < src_.size()) {
            char c = get();
            if (c == '"') {
                return JsonValue(std::move(s));
            }
            if (c == '\\') {
                if (pos_ >= src_.size()) {
                    if (error) *error = "Unexpected end of string escape";
                    return std::nullopt;
                }
                char esc = get();
                switch (esc) {
                    case '"':  s.push_back('"'); break;
                    case '\\': s.push_back('\\'); break;
                    case '/':  s.push_back('/'); break;
                    case 'b':  s.push_back('\b'); break;
                    case 'f':  s.push_back('\f'); break;
                    case 'n':  s.push_back('\n'); break;
                    case 'r':  s.push_back('\r'); break;
                    case 't':  s.push_back('\t'); break;
                    case 'u': {
                        if (pos_ + 4 > src_.size()) {
                            if (error) *error = "Incomplete \\u hex escape";
                            return std::nullopt;
                        }
                        unsigned int codepoint = 0;
                        for (int i = 0; i < 4; ++i) {
                            char h = get();
                            codepoint <<= 4;
                            if (h >= '0' && h <= '9') codepoint |= (h - '0');
                            else if (h >= 'a' && h <= 'f') codepoint |= (h - 'a' + 10);
                            else if (h >= 'A' && h <= 'F') codepoint |= (h - 'A' + 10);
                            else {
                                if (error) *error = "Invalid hex in \\u escape";
                                return std::nullopt;
                            }
                        }
                        // Encode UTF-8
                        if (codepoint <= 0x7F) {
                            s.push_back(static_cast<char>(codepoint));
                        } else if (codepoint <= 0x7FF) {
                            s.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
                            s.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
                        } else {
                            s.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
                            s.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
                            s.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
                        }
                        break;
                    }
                    default:
                        s.push_back(esc);
                        break;
                }
            } else {
                s.push_back(c);
            }
        }

        if (error) *error = "Unterminated string literal";
        return std::nullopt;
    }

    std::optional<JsonValue> parseNumber(std::string* error) {
        size_t start = pos_;
        if (peek() == '-') pos_++;
        if (pos_ >= src_.size() || !std::isdigit(static_cast<unsigned char>(src_[pos_]))) {
            if (error) *error = "Invalid number format at offset " + std::to_string(start);
            return std::nullopt;
        }

        while (pos_ < src_.size() && std::isdigit(static_cast<unsigned char>(src_[pos_]))) {
            pos_++;
        }

        bool isFloating = false;
        if (pos_ < src_.size() && src_[pos_] == '.') {
            isFloating = true;
            pos_++;
            if (pos_ >= src_.size() || !std::isdigit(static_cast<unsigned char>(src_[pos_]))) {
                if (error) *error = "Expected digits after decimal point";
                return std::nullopt;
            }
            while (pos_ < src_.size() && std::isdigit(static_cast<unsigned char>(src_[pos_]))) {
                pos_++;
            }
        }

        if (pos_ < src_.size() && (src_[pos_] == 'e' || src_[pos_] == 'E')) {
            isFloating = true;
            pos_++;
            if (pos_ < src_.size() && (src_[pos_] == '+' || src_[pos_] == '-')) {
                pos_++;
            }
            if (pos_ >= src_.size() || !std::isdigit(static_cast<unsigned char>(src_[pos_]))) {
                if (error) *error = "Expected exponent digits";
                return std::nullopt;
            }
            while (pos_ < src_.size() && std::isdigit(static_cast<unsigned char>(src_[pos_]))) {
                pos_++;
            }
        }

        std::string_view numStr = src_.substr(start, pos_ - start);
        if (isFloating) {
            std::string temp(numStr);
            char* end = nullptr;
            double d = std::strtod(temp.c_str(), &end);
            return JsonValue(d);
        } else {
            int64_t v = 0;
            auto [ptr, ec] = std::from_chars(numStr.data(), numStr.data() + numStr.size(), v);
            if (ec == std::errc()) {
                return JsonValue(v);
            }
            // Fallback to double on overflow
            std::string temp(numStr);
            return JsonValue(std::strtod(temp.c_str(), nullptr));
        }
    }

    std::optional<JsonValue> parseArray(std::string* error) {
        if (get() != '[') {
            if (error) *error = "Expected '['";
            return std::nullopt;
        }
        skipWhitespace();
        if (peek() == ']') {
            get();
            return JsonValue(JsonValue::Array{});
        }

        JsonValue::Array arr;
        while (true) {
            auto elem = parseValue(error);
            if (!elem) return std::nullopt;
            arr.push_back(std::move(*elem));

            skipWhitespace();
            char c = peek();
            if (c == ']') {
                get();
                return JsonValue(std::move(arr));
            }
            if (c == ',') {
                get();
                skipWhitespace();
            } else {
                if (error) *error = "Expected ',' or ']' in array";
                return std::nullopt;
            }
        }
    }

    std::optional<JsonValue> parseObject(std::string* error) {
        if (get() != '{') {
            if (error) *error = "Expected '{'";
            return std::nullopt;
        }
        skipWhitespace();
        if (peek() == '}') {
            get();
            return JsonValue(JsonValue::Object{});
        }

        JsonValue::Object obj;
        while (true) {
            skipWhitespace();
            if (peek() != '"') {
                if (error) *error = "Expected string key in object";
                return std::nullopt;
            }
            auto keyVal = parseString(error);
            if (!keyVal) return std::nullopt;
            std::string key = keyVal->asString();

            skipWhitespace();
            if (get() != ':') {
                if (error) *error = "Expected ':' after key in object";
                return std::nullopt;
            }

            auto val = parseValue(error);
            if (!val) return std::nullopt;
            obj.emplace_back(std::move(key), std::move(*val));

            skipWhitespace();
            char c = peek();
            if (c == '}') {
                get();
                return JsonValue(std::move(obj));
            }
            if (c == ',') {
                get();
                skipWhitespace();
            } else {
                if (error) *error = "Expected ',' or '}' in object";
                return std::nullopt;
            }
        }
    }
};

} // namespace

std::optional<JsonValue> JsonValue::parse(std::string_view json, std::string* error) {
    JsonParser parser(json);
    return parser.parse(error);
}

} // namespace tamarin::lsp
