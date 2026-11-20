#pragma once
/* A small JSON reader (no dependencies), enough for Mosaic's state messages.
   Used off the audio thread: the parsed tree is turned into plain C++ structs before the audio thread sees it. */
#include <cmath>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace mosaic::json
{
struct Value
{
    enum Type { Null, Bool, Number, String, Array, Object } type = Null;
    double num = 0; bool b = false; std::string str;
    std::vector<Value> arr; std::map<std::string, Value> obj;

    bool isNull() const { return type == Null; }
    bool isObject() const { return type == Object; }
    bool isArray() const { return type == Array; }
    const Value& operator[] (const std::string& k) const
    {
        static const Value none;
        if (type != Object) return none;
        auto it = obj.find (k); return it == obj.end() ? none : it->second;
    }
    const Value& operator[] (size_t i) const { static const Value none; return type == Array && i < arr.size() ? arr[i] : none; }
    size_t size() const { return type == Array ? arr.size() : 0; }
    // JavaScript truthiness, and the two ways the prototype reads a number:
    bool truthy() const { return type == Bool ? b : type == Number ? (num != 0 && num == num) : type == String ? ! str.empty() : type == Null ? false : true; }
    double numOr (double d) const { return type == Number ? num : type == Bool ? (b ? 1 : 0) : d; }   // x == null ? d : x  (also for missing)
    double orNum (double d) const { const double v = numOr (0); return (type == Number || type == Bool) && v != 0 && v == v ? v : d; }   // x || d
    std::string strOr (const std::string& d) const { return type == String && ! str.empty() ? str : d; }      // x || d for strings
};

class Parser
{
public:
    explicit Parser (const std::string& s) : t (s) {}
    Value parse() { Value v; ws(); value (v); return v; }
private:
    const std::string& t; size_t i = 0;
    void ws() { while (i < t.size() && (t[i] == ' ' || t[i] == '\n' || t[i] == '\r' || t[i] == '\t')) ++i; }
    void value (Value& v)
    {
        ws(); if (i >= t.size()) return;
        const char c = t[i];
        if (c == '{')
        {
            v.type = Value::Object; ++i; ws();
            if (i < t.size() && t[i] == '}') { ++i; return; }
            while (i < t.size())
            {
                ws(); std::string k; str (k); ws(); if (i < t.size() && t[i] == ':') ++i;
                value (v.obj[k]); ws();
                if (i < t.size() && t[i] == ',') { ++i; continue; }
                if (i < t.size() && t[i] == '}') { ++i; break; }
                break;
            }
        }
        else if (c == '[')
        {
            v.type = Value::Array; ++i; ws();
            if (i < t.size() && t[i] == ']') { ++i; return; }
            while (i < t.size())
            {
                v.arr.emplace_back(); value (v.arr.back()); ws();
                if (i < t.size() && t[i] == ',') { ++i; continue; }
                if (i < t.size() && t[i] == ']') { ++i; break; }
                break;
            }
        }
        else if (c == '"') { v.type = Value::String; str (v.str); }
        else if (t.compare (i, 4, "true") == 0) { v.type = Value::Bool; v.b = true; i += 4; }
        else if (t.compare (i, 5, "false") == 0) { v.type = Value::Bool; v.b = false; i += 5; }
        else if (t.compare (i, 4, "null") == 0) { v.type = Value::Null; i += 4; }
        else { v.type = Value::Number; char* e = nullptr; v.num = std::strtod (t.c_str() + i, &e); i = (size_t) (e - t.c_str()); if (e == t.c_str()) ++i; }
    }
    void str (std::string& out)
    {
        if (i >= t.size() || t[i] != '"') return;
        ++i;
        while (i < t.size() && t[i] != '"')
        {
            if (t[i] == '\\' && i + 1 < t.size())
            {
                const char e = t[++i];
                if (e == 'n') out += '\n'; else if (e == 't') out += '\t'; else if (e == 'r') out += '\r'; else if (e == 'b') out += '\b'; else if (e == 'f') out += '\f';
                else if (e == 'u' && i + 4 < t.size())
                {
                    unsigned cp = (unsigned) std::strtoul (t.substr (i + 1, 4).c_str(), nullptr, 16); i += 4;
                    if (cp < 0x80) out += (char) cp; else if (cp < 0x800) { out += (char) (0xC0 | (cp >> 6)); out += (char) (0x80 | (cp & 63)); }
                    else { out += (char) (0xE0 | (cp >> 12)); out += (char) (0x80 | ((cp >> 6) & 63)); out += (char) (0x80 | (cp & 63)); }
                }
                else out += e;
                ++i;
            }
            else out += t[i++];
        }
        ++i;
    }
};

inline Value parse (const std::string& s) { return Parser (s).parse(); }
} // namespace mosaic::json
