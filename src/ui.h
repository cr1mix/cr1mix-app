#pragma once
#include "gfx.h"

inline u64 HID(const char* s, u64 n = 0) {
    u64 h = 1469598103934665603ull;
    while (*s) { h ^= (u8)*s++; h *= 1099511628211ull; }
    h ^= n + 0x9E3779B97F4A7C15ull; h *= 1099511628211ull;
    return h;
}
inline u64 HID(const std::string& s, u64 n = 0) { return HID(s.c_str(), n); }

struct Input {
    float mx = -1, my = -1;
    bool down = false, pressed = false, released = false, rpressed = false;
    float wheel = 0;
    bool dbl = false;
    std::vector<wchar_t> chars;
    std::vector<int> keys;
};

struct Ripple { u64 id; float x, y; float t; bool hold; };

struct UI {
    Input in;
    Theme th;
    float dt = 0.016f, time = 0;
    u64 hot = 0, active = 0, lastHot = 0;
    bool blocked = false;
    bool anyAnim = false;
    bool wantHand = false;
    std::unordered_map<u64, float> anims;
    std::vector<Ripple> ripples;
    std::vector<Rect> clipStack;
    u64 focus = 0;
    std::wstring tooltip; float tipX = 0, tipY = 0; u64 tipId = 0; float tipT = 0;

    void beginFrame(float dt_);
    void endFrame();

    float anim(u64 id, float target, float speed = 14.f, float init = -1e9f);
    float animSet(u64 id, float v) { anims[id] = v; return v; }

    bool inClip(float x, float y) const;
    bool mouseIn(const Rect& r) const { return !blocked && r.hit(in.mx, in.my) && inClip(in.mx, in.my); }
    bool behave(u64 id, const Rect& r, bool* hovered = nullptr, bool* held = nullptr);
    void pushClip(const Rect& r) { clipStack.push_back(r); g.pushClip(r); }
    void popClip() { if (!clipStack.empty()) clipStack.pop_back(); g.popClip(); }

    void stateLayer(u64 id, const Rect& r, float rad, const Col& onColor, float hoverAmt = 0.08f);
    void drawRipples(u64 id, const Rect& r, float rad, const Col& c);
    void tip(u64 id, const std::wstring& s);

    bool btnFilled(u64 id, const Rect& r, const std::wstring& label, const char* icon = nullptr, bool enabled = true);
    bool btnTonal(u64 id, const Rect& r, const std::wstring& label, const char* icon = nullptr, bool enabled = true);
    bool btnOutlined(u64 id, const Rect& r, const std::wstring& label, const char* icon = nullptr, bool enabled = true);
    bool btnText(u64 id, const Rect& r, const std::wstring& label, const char* icon = nullptr, const Col* col = nullptr);
    bool btnDanger(u64 id, const Rect& r, const std::wstring& label, const char* icon = nullptr, bool enabled = true);
    bool iconBtn(u64 id, float cx, float cy, const char* icon, float size = 40, const Col* col = nullptr, bool filled = false, const Col* bg = nullptr);
    bool toggle(u64 id, float x, float y, bool on, bool enabled = true, bool busy = false);
    bool checkbox(u64 id, float x, float y, bool on, bool enabled = true);
    bool radio(u64 id, float x, float y, bool on);
    float chipW(const std::wstring& label, const char* icon);
    bool chip(u64 id, float x, float y, const std::wstring& label, bool selected, const char* icon = nullptr);
    int segmented(u64 id, const Rect& r, const std::vector<std::wstring>& labels, int sel);
    void progress(const Rect& r, float v, bool wavy, const Col* c = nullptr);
    void spinner(float cx, float cy, float rad, float w, const Col& c);
    void wavyRing(float cx, float cy, float rad, float w, float v, const Col& c, const Col& track, float phase, float amp = 2.2f, int waves = 10);
    bool textField(u64 id, const Rect& r, std::wstring& text, const wchar_t* hint, const char* icon = nullptr);
    void card(const Rect& r, float rad = 24, const Col* c = nullptr, bool shadow = false);
    void badge(float x, float y, const std::wstring& s, const Col& bg, const Col& fg, float* outW = nullptr);
    float slider(u64 id, const Rect& r, float v, bool* changed = nullptr);
    void blob(float cx, float cy, float rad, int shape, const Col& c, float rot = 0);
    void areaGraph(const Rect& r, const float* v, int n, float maxv, const Col& c, bool fill = true);
};
extern UI ui;

struct Scroller {
    float off = 0, target = 0, content = 0, view = 0, barA = 0, lastWheel = -10;
    void begin(const Rect& r);
    float y0() const { return -off; }
    void end(const Rect& r, float contentH);
    void scrollTo(float t) { target = t; }
};
