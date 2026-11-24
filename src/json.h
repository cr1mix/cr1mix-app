#pragma once
#include <string>
#include <vector>
#include <memory>
#include <cstdlib>

struct JV {
    enum T { Null, Bool, Num, Str, Arr, Obj } t = Null;
    bool b = false; double n = 0; std::string s;
    std::vector<JV> a;
    std::vector<std::pair<std::string, JV>> o;
    const JV& operator[](const char* k) const {
        static JV nil;
        if (t != Obj) return nil;
        for (auto& p : o) if (p.first == k) return p.second;
        return nil;
    }
    const JV& operator[](size_t i) const { static JV nil; return (t == Arr && i < a.size()) ? a[i] : nil; }
    const JV& operator[](int i) const { return (*this)[(size_t)i]; }
    size_t size() const { return t == Arr ? a.size() : (t == Obj ? o.size() : 0); }
    std::string str(const char* d = "") const { return t == Str ? s : std::string(d); }
    double num(double d = 0) const { return t == Num ? n : (t == Bool ? (b ? 1 : 0) : d); }
    bool boolean(bool d = false) const { return t == Bool ? b : (t == Num ? n != 0 : d); }
    bool has(const char* k) const { if (t != Obj) return false; for (auto& p : o) if (p.first == k) return true; return false; }
};

struct JParser {
    const char* p; const char* e;
    void ws() { while (p < e && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) p++; }
    static void put(std::string& o, unsigned cp) {
        if (cp < 0x80) o += (char)cp;
        else if (cp < 0x800) { o += (char)(0xC0 | (cp >> 6)); o += (char)(0x80 | (cp & 63)); }
        else if (cp < 0x10000) { o += (char)(0xE0 | (cp >> 12)); o += (char)(0x80 | ((cp >> 6) & 63)); o += (char)(0x80 | (cp & 63)); }
        else { o += (char)(0xF0 | (cp >> 18)); o += (char)(0x80 | ((cp >> 12) & 63)); o += (char)(0x80 | ((cp >> 6) & 63)); o += (char)(0x80 | (cp & 63)); }
    }
    unsigned hex4() { unsigned v = 0; for (int i = 0; i < 4 && p < e; i++, p++) { char c = *p; v <<= 4; v |= (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : 0; } return v; }
    std::string str() {
        std::string o; p++;
        while (p < e && *p != '"') {
            if (*p == '\\' && p + 1 < e) {
                p++; char c = *p++;
                switch (c) {
                case 'n': o += '\n'; break; case 't': o += '\t'; break; case 'r': o += '\r'; break;
                case 'b': o += '\b'; break; case 'f': o += '\f'; break;
                case 'u': { unsigned cp = hex4();
                    if (cp >= 0xD800 && cp < 0xDC00 && p + 1 < e && p[0] == '\\' && p[1] == 'u') { p += 2; unsigned lo = hex4(); cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00); }
                    put(o, cp); break; }
                default: o += c;
                }
            } else o += *p++;
        }
        p++; return o;
    }
    JV val() {
        ws(); JV v; if (p >= e) return v;
        char c = *p;
        if (c == '{') { v.t = JV::Obj; p++; ws(); if (*p == '}') { p++; return v; }
            for (;;) { ws(); std::string k = str(); ws(); p++; v.o.emplace_back(std::move(k), val()); ws(); if (*p == ',') { p++; continue; } p++; break; } }
        else if (c == '[') { v.t = JV::Arr; p++; ws(); if (*p == ']') { p++; return v; }
            for (;;) { v.a.push_back(val()); ws(); if (*p == ',') { p++; continue; } p++; break; } }
        else if (c == '"') { v.t = JV::Str; v.s = str(); }
        else if (c == 't') { v.t = JV::Bool; v.b = true; p += 4; }
        else if (c == 'f') { v.t = JV::Bool; v.b = false; p += 5; }
        else if (c == 'n') { p += 4; }
        else { v.t = JV::Num; char* end; v.n = strtod(p, &end); p = end; }
        return v;
    }
};
inline JV jparse(const std::string& s) { JParser P{ s.data(), s.data() + s.size() }; return P.val(); }

inline void jesc(std::string& o, const std::string& s) {
    o += '"';
    for (unsigned char c : s) {
        if (c == '"') o += "\\\""; else if (c == '\\') o += "\\\\"; else if (c == '\n') o += "\\n";
        else if (c == '\r') o += "\\r"; else if (c == '\t') o += "\\t";
        else if (c < 0x20) { char b[8]; snprintf(b, 8, "\\u%04x", c); o += b; }
        else o += (char)c;
    }
    o += '"';
}
