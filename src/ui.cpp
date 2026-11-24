#include "ui.h"

UI ui;

void UI::beginFrame(float dt_) {
    dt = std::min(dt_, 0.05f); time += dt;
    lastHot = hot; hot = 0; wantHand = false; anyAnim = false;
    tooltip.clear();
    for (auto& r : ripples) { r.t += dt; if (!in.down || active != r.id) r.hold = false; }
    ripples.erase(std::remove_if(ripples.begin(), ripples.end(), [](const Ripple& r) { return !r.hold && r.t > 0.85f; }), ripples.end());
    if (!ripples.empty()) anyAnim = true;
}

void UI::endFrame() {
    if (in.released) active = 0;
    in.pressed = in.released = in.rpressed = in.dbl = false;
    in.wheel = 0; in.chars.clear(); in.keys.clear();
}

float UI::anim(u64 id, float target, float speed, float init) {
    auto it = anims.find(id);
    if (it == anims.end()) { anims[id] = (init > -1e8f) ? init : target; it = anims.find(id); }
    float& v = it->second;
    float k = 1.f - expf(-speed * dt);
    v += (target - v) * k;
    if (fabsf(target - v) < 0.0015f) v = target; else anyAnim = true;
    return v;
}

bool UI::inClip(float x, float y) const {
    for (auto& c : clipStack) if (!c.hit(x, y)) return false;
    return true;
}

bool UI::behave(u64 id, const Rect& r, bool* hovered, bool* held) {
    bool hov = mouseIn(r);
    if (hov) { hot = id; wantHand = true; }
    if (hov && in.pressed) {
        active = id;
        ripples.push_back({ id, in.mx, in.my, 0, true });
    }
    bool h = (active == id && in.down);
    if (hovered) *hovered = hov;
    if (held) *held = h;
    return active == id && in.released && hov;
}

void UI::stateLayer(u64 id, const Rect& r, float rad, const Col& on, float hoverAmt) {
    float a = anim(id ^ 0x51ull, (hot == id ? hoverAmt : 0) + (active == id && in.down ? 0.05f : 0), 18);
    if (a > 0.002f) g.fill(r, withA(on, a), rad);
}

void UI::drawRipples(u64 id, const Rect& r, float rad, const Col& c) {
    bool any = false;
    for (auto& rp : ripples) if (rp.id == id) { any = true; break; }
    if (!any) return;
    g.pushLayer(1.f, &r, rad);
    for (auto& rp : ripples) {
        if (rp.id != id) continue;
        float maxR = sqrtf(r.w * r.w + r.h * r.h);
        float rr = maxR * easeOut(std::min(rp.t / 0.5f, 1.f));
        float a = rp.hold ? 0.14f : 0.14f * clampf(1.f - (rp.t - 0.35f) / 0.5f, 0, 1);
        if (rp.hold && rp.t > 0.5f) a = 0.14f;
        g.circle(rp.x, rp.y, rr, withA(c, a));
    }
    g.popLayer();
}

void UI::tip(u64 id, const std::wstring& s) {
    if (hot == id) { tooltip = s; if (tipId != id) { tipId = id; tipT = 0; } tipX = in.mx; tipY = in.my; }
}

static void btnBody(u64 id, const Rect& r, const std::wstring& label, const char* icon, const Col& fg, float size = 14) {
    float iw = icon ? 20.f : 0.f, gap = icon && !label.empty() ? 8.f : 0.f;
    float tw = label.empty() ? 0 : g.measure(label, size, TW_MED).width;
    float x = r.x + (r.w - (iw + gap + tw)) / 2;
    if (icon) g.icon(icon, x + 10, r.y + r.h / 2, 20, fg);
    if (!label.empty()) g.text(label, x + iw + gap, r.y + (r.h - size * 1.3f) / 2 - 0.5f, size, fg, TW_MED);
}

bool UI::btnFilled(u64 id, const Rect& r, const std::wstring& label, const char* icon, bool enabled) {
    bool hov = false; bool c = enabled ? behave(id, r, &hov) : false;
    float press = anim(id ^ 7, (active == id && in.down) ? 1.f : 0.f, 20);
    Rect q = r; float rad = lerpf(r.h / 2, 12, press);
    Col bg = enabled ? th.accent : withA(th.text, 0.12f);
    Col fg = enabled ? th.onAccent : withA(th.text, 0.38f);
    if (enabled) { float s = anim(id ^ 9, hov ? 1.f : 0.f, 16); if (s > 0.01f) g.shadow(q, rad, s * 0.6f); }
    g.fill(q, bg, rad);
    if (enabled) { stateLayer(id, q, rad, th.onAccent); drawRipples(id, q, rad, th.onAccent); }
    btnBody(id, q, label, icon, fg);
    return c;
}
bool UI::btnTonal(u64 id, const Rect& r, const std::wstring& label, const char* icon, bool enabled) {
    bool c = enabled ? behave(id, r) : false;
    float press = anim(id ^ 7, (active == id && in.down) ? 1.f : 0.f, 20);
    float rad = lerpf(r.h / 2, 12, press);
    Col bg = enabled ? th.surfHi : withA(th.text, 0.08f);
    Col fg = enabled ? th.text : withA(th.text, 0.38f);
    g.fill(r, bg, rad);
    if (enabled) { stateLayer(id, r, rad, th.text); drawRipples(id, r, rad, th.text); }
    btnBody(id, r, label, icon, fg);
    return c;
}
bool UI::btnOutlined(u64 id, const Rect& r, const std::wstring& label, const char* icon, bool enabled) {
    bool c = enabled ? behave(id, r) : false;
    float press = anim(id ^ 7, (active == id && in.down) ? 1.f : 0.f, 20);
    float rad = lerpf(r.h / 2, 12, press);
    g.stroke(r, enabled ? th.outline : withA(th.text, 0.12f), rad, 1);
    Col fg = enabled ? th.text : withA(th.text, 0.38f);
    if (enabled) { stateLayer(id, r, rad, th.text); drawRipples(id, r, rad, th.text); }
    btnBody(id, r, label, icon, fg);
    return c;
}
bool UI::btnText(u64 id, const Rect& r, const std::wstring& label, const char* icon, const Col* col) {
    bool c = behave(id, r);
    Col fg = col ? *col : th.accent;
    stateLayer(id, r, r.h / 2, fg); drawRipples(id, r, r.h / 2, fg);
    btnBody(id, r, label, icon, fg);
    return c;
}
bool UI::btnDanger(u64 id, const Rect& r, const std::wstring& label, const char* icon, bool enabled) {
    bool c = enabled ? behave(id, r) : false;
    float press = anim(id ^ 7, (active == id && in.down) ? 1.f : 0.f, 20);
    float rad = lerpf(r.h / 2, 12, press);
    Col bg = enabled ? th.danger : withA(th.text, 0.12f);
    g.fill(r, bg, rad);
    if (enabled) { stateLayer(id, r, rad, rgb(0xFFFFFF)); drawRipples(id, r, rad, rgb(0xFFFFFF)); }
    btnBody(id, r, label, icon, enabled ? rgb(0x1A0507) : withA(th.text, 0.38f));
    return c;
}
bool UI::iconBtn(u64 id, float cx, float cy, const char* icon, float size, const Col* col, bool filled, const Col* bg) {
    Rect r{ cx - size / 2, cy - size / 2, size, size };
    bool c = behave(id, r);
    Col fg = col ? *col : th.textDim;
    if (bg) g.fill(r, *bg, size / 2);
    stateLayer(id, r, size / 2, fg, 0.10f); drawRipples(id, r, size / 2, fg);
    float press = anim(id ^ 3, (active == id && in.down) ? 1.f : 0.f, 22);
    g.icon(icon, cx, cy, size * 0.55f * (1 - press * 0.08f), fg, filled);
    return c;
}

bool UI::toggle(u64 id, float x, float y, bool on, bool enabled, bool busy) {
    Rect r{ x, y, 52, 32 };
    bool hov = false, held = false;
    bool c = enabled && !busy ? behave(id, Rect{ x - 6, y - 6, 64, 44 }, &hov, &held) : false;
    float t = anim(id, on ? 1.f : 0.f, 15);
    float vel = anim(id ^ 0x99, on ? 1.f : 0.f, 9);
    float spring = clampf(t + (t - vel) * 0.35f, -0.08f, 1.08f);
    float pr = anim(id ^ 5, held ? 1.f : 0.f, 22);
    Col track = mix(th.surfHighest, th.accent, t);
    if (!enabled) track = withA(th.text, 0.10f);
    g.fill(r, track, 16);
    if (t < 0.99f) g.stroke(r, withA(enabled ? th.outline : th.textFaint, 1 - t), 16, 2);
    float th0 = lerpf(16, 24, t); th0 = lerpf(th0, 28, pr);
    float cx = lerpf(x + 16, x + 36, spring), cy = y + 16;
    if (hov && enabled) g.circle(cx, cy, 20, withA(on ? th.accent : th.text, 0.08f));
    Col thumb = mix(th.outline, th.onAccent, t);
    if (!enabled) thumb = withA(th.text, 0.38f);
    g.circle(cx, cy, th0 / 2, thumb);
    if (busy) spinner(cx, cy, 6.5f, 2, on ? th.accent : th.surf);
    else if (t > 0.5f) g.icon("check", cx, cy, 16 * clampf((t - 0.5f) * 2, 0, 1), withA(th.accent, clampf((t - 0.5f) * 2, 0, 1)));
    return c;
}
bool UI::checkbox(u64 id, float x, float y, bool on, bool enabled) {
    Rect hitR{ x - 11, y - 11, 40, 40 };
    bool c = enabled ? behave(id, hitR) : false;
    float t = anim(id, on ? 1.f : 0.f, 20);
    Rect r{ x, y, 18, 18 };
    if (hot == id) g.circle(x + 9, y + 9, 20, withA(th.text, 0.08f));
    drawRipples(id, Rect{ x - 11, y - 11, 40, 40 }, 20, th.accent);
    if (t < 0.99f) g.stroke(r, withA(th.textDim, 1 - t), 3, 2);
    if (t > 0.01f) {
        g.fill(r, withA(enabled ? th.accent : th.textFaint, t), 3);
        g.icon("check", x + 9, y + 9, 16 * t, th.onAccent);
    }
    return c;
}
bool UI::radio(u64 id, float x, float y, bool on) {
    bool c = behave(id, Rect{ x - 10, y - 10, 40, 40 });
    float t = anim(id, on ? 1.f : 0.f, 20);
    if (hot == id) g.circle(x + 10, y + 10, 20, withA(th.text, 0.08f));
    g.ring(x + 10, y + 10, 9, mix(th.textDim, th.accent, t), 2);
    if (t > 0.01f) g.circle(x + 10, y + 10, 5 * t, th.accent);
    return c;
}
float UI::chipW(const std::wstring& label, const char* icon) {
    return g.measure(label, 13.5f, TW_MED).width + 32 + (icon ? 26 : 0);
}
bool UI::chip(u64 id, float x, float y, const std::wstring& label, bool selected, const char* icon) {
    float t = anim(id, selected ? 1.f : 0.f, 18);
    const char* ic = selected ? "check" : icon;
    float w = g.measure(label, 13.5f, TW_MED).width + 32 + ((ic || icon) ? 26 * (icon ? 1 : t) : 0);
    Rect r{ x, y, w, 32 };
    bool c = behave(id, r);
    g.fill(r, withA(th.accentDim, t), 10);
    if (t < 0.99f) g.stroke(r, withA(th.outline, 1 - t), 10, 1);
    stateLayer(id, r, 10, th.text); drawRipples(id, r, 10, th.text);
    float tx = x + 16;
    if (ic) { g.icon(ic, x + 22, y + 16, 18, selected ? th.text : th.textDim); tx = x + 38; }
    g.text(label, tx, y + 7, 13.5f, selected ? th.text : th.textDim, TW_MED);
    return c;
}
int UI::segmented(u64 id, const Rect& r, const std::vector<std::wstring>& labels, int sel) {
    int n = (int)labels.size(); if (!n) return sel;
    float sw = r.w / n;
    g.fill(r, th.surf2, r.h / 2);
    g.stroke(r, th.outlineV, r.h / 2, 1);
    float px = anim(id, r.x + sw * sel, 16);
    Rect pill{ px + 3, r.y + 3, sw - 6, r.h - 6 };
    g.fill(pill, th.accentDim, (r.h - 6) / 2);
    int out = sel;
    for (int i = 0; i < n; i++) {
        Rect s{ r.x + sw * i, r.y, sw, r.h };
        u64 sid = HID("seg", id + i);
        if (behave(sid, s)) out = i;
        stateLayer(sid, Rect{ s.x + 3, s.y + 3, s.w - 6, s.h - 6 }, (r.h - 6) / 2, th.text, 0.05f);
        bool on = i == sel;
        float tw = g.measure(labels[i], 13.5f, TW_MED).width;
        float ix = on ? 22.f : 0.f;
        float x0 = s.x + (s.w - tw - ix) / 2;
        if (on) g.icon("check", x0 + 8, s.y + s.h / 2, 17, th.text);
        g.text(labels[i], x0 + ix, s.y + (s.h - 17.5f) / 2, 13.5f, on ? th.text : th.textDim, TW_MED, s.w - 8);
    }
    return out;
}

void UI::progress(const Rect& r, float v, bool wavy, const Col* c) {
    Col ac = c ? *c : th.accent;
    float h = r.h;
    if (v < 0) {
        float p = fmodf(time * 0.7f, 1.4f) - 0.4f;
        g.fill(r, th.track, h / 2);
        float a = clampf(p, 0, 1), b = clampf(p + 0.4f, 0, 1);
        if (b > a) g.fill(Rect{ r.x + r.w * a, r.y, r.w * (b - a), h }, ac, h / 2);
        anyAnim = true; return;
    }
    v = clampf(v, 0, 1);
    float fx = r.w * v;
    float gap = v > 0.001f && v < 0.999f ? 6.f : 0.f;
    if (fx + gap < r.w) g.fill(Rect{ r.x + fx + gap, r.y, r.w - fx - gap, h }, th.track, h / 2);
    if (fx <= 0.5f) return;
    if (!wavy) { g.fill(Rect{ r.x, r.y, fx, h }, ac, h / 2); return; }
    ID2D1PathGeometry* p = g.path(); ID2D1GeometrySink* s = nullptr; p->Open(&s);
    float cy = r.y + h / 2, amp = 3.f, wl = 22.f, ph = time * 6.f;
    s->BeginFigure(D2D1::Point2F(r.x + h / 2, cy + sinf(ph) * amp), D2D1_FIGURE_BEGIN_HOLLOW);
    for (float x = r.x + h / 2 + 2; x < r.x + fx - h / 2; x += 2)
        s->AddLine(D2D1::Point2F(x, cy + sinf((x - r.x) / wl * 6.2831f - ph) * amp));
    s->EndFigure(D2D1_FIGURE_END_OPEN); s->Close(); s->Release();
    g.strokeGeo(p, ac, h, true); p->Release();
    anyAnim = true;
}
static void arc(float cx, float cy, float rad, float a0, float a1, const Col& c, float w) {
    ID2D1PathGeometry* p = g.path(); ID2D1GeometrySink* s = nullptr; p->Open(&s);
    int n = std::max(6, (int)(fabsf(a1 - a0) * rad / 3));
    s->BeginFigure(D2D1::Point2F(cx + cosf(a0) * rad, cy + sinf(a0) * rad), D2D1_FIGURE_BEGIN_HOLLOW);
    for (int i = 1; i <= n; i++) { float a = a0 + (a1 - a0) * i / n; s->AddLine(D2D1::Point2F(cx + cosf(a) * rad, cy + sinf(a) * rad)); }
    s->EndFigure(D2D1_FIGURE_END_OPEN); s->Close(); s->Release();
    g.strokeGeo(p, c, w, true); p->Release();
}
void UI::spinner(float cx, float cy, float rad, float w, const Col& c) {
    float t = time;
    float head = t * 5.5f, len = 1.2f + sinf(t * 2.6f) * 0.9f + 0.9f;
    arc(cx, cy, rad, head, head + len, c, w);
    anyAnim = true;
}
void UI::wavyRing(float cx, float cy, float rad, float w, float v, const Col& c, const Col& track, float phase, float amp, int waves) {
    v = clampf(v, 0, 1);
    float a0 = -1.5708f, gap = v > 0.01f && v < 0.99f ? 0.22f : 0.f;
    float aEnd = a0 + 6.2831f * v;
    if (v < 0.999f) arc(cx, cy, rad, aEnd + gap, a0 + 6.2831f - gap * (v > 0.01f ? 1 : 0), track, w);
    if (v <= 0.002f) return;
    ID2D1PathGeometry* p = g.path(); ID2D1GeometrySink* s = nullptr; p->Open(&s);
    int n = std::max(12, (int)(6.2831f * v * rad / 2.2f));
    for (int i = 0; i <= n; i++) {
        float a = a0 + (aEnd - a0) * i / n;
        float rr = rad + sinf((a - a0) * waves + phase) * amp;
        D2D1_POINT_2F pt = D2D1::Point2F(cx + cosf(a) * rr, cy + sinf(a) * rr);
        if (i == 0) s->BeginFigure(pt, D2D1_FIGURE_BEGIN_HOLLOW); else s->AddLine(pt);
    }
    s->EndFigure(D2D1_FIGURE_END_OPEN); s->Close(); s->Release();
    g.strokeGeo(p, c, w, true); p->Release();
}

bool UI::textField(u64 id, const Rect& r, std::wstring& text, const wchar_t* hint, const char* icon) {
    bool hov = false;
    if (behave(id, r, &hov)) focus = id;
    if (in.pressed && !hov && focus == id) focus = 0;
    bool f = focus == id;
    bool changed = false;
    if (f) {
        for (wchar_t ch : in.chars) { if (ch >= 32 && ch != 127) { text += ch; changed = true; } }
        for (int k : in.keys) {
            if (k == VK_BACK && !text.empty()) { text.pop_back(); changed = true; }
            if (k == VK_ESCAPE) { if (!text.empty()) { text.clear(); changed = true; } else focus = 0; }
        }
    }
    float ft = anim(id, f ? 1.f : 0.f, 18);
    g.fill(r, mix(th.surf2, th.surfHi, ft * 0.6f + (hov ? 0.2f : 0)), r.h / 2);
    if (ft > 0.01f) g.stroke(r, withA(th.accent, ft * 0.6f), r.h / 2, 1.5f);
    float x = r.x + 18;
    if (icon) { g.icon(icon, r.x + 26, r.y + r.h / 2, 22, th.textDim); x = r.x + 46; }
    float tw = 0;
    if (text.empty()) g.text(hint, x, r.y + (r.h - 19) / 2, 14.5f, th.textFaint, TW_REG, r.w - (x - r.x) - 16);
    else { g.text(text, x, r.y + (r.h - 19) / 2, 14.5f, th.text, TW_REG, r.w - (x - r.x) - 16); tw = g.measure(text, 14.5f, TW_REG).width; }
    if (f && fmodf(time, 1.0f) < 0.55f) g.fill(Rect{ x + tw + 1, r.y + r.h / 2 - 9, 1.6f, 18 }, th.accent);
    if (f) anyAnim = true;
    if (!text.empty()) {
        if (iconBtn(HID("tfclear", id), r.r() - 22, r.y + r.h / 2, "close", 32)) { text.clear(); changed = true; }
    }
    return changed;
}

float UI::slider(u64 id, const Rect& r, float v, bool* changed) {
    Rect hitR{ r.x - 6, r.y - 10, r.w + 12, r.h + 20 };
    bool hov = false, held = false;
    behave(id, hitR, &hov, &held);
    float nv = v;
    if (held) { nv = clampf((in.mx - r.x) / r.w, 0, 1); if (changed) *changed = fabsf(nv - v) > 1e-4f; }
    float t = anim(id, nv, 24);
    float th2 = r.h, cy = r.y + r.h / 2, hx = r.x + r.w * t;
    float hw = anim(id ^ 0x77, held ? 2.f : 4.f, 20);
    float gap = 6;
    if (hx - gap > r.x) g.fill(Rect{ r.x, r.y, std::max(th2, hx - gap - r.x), th2 }, th.accent, th2 / 2);
    if (hx + gap < r.r()) g.fill(Rect{ hx + gap, r.y, r.r() - hx - gap, th2 }, th.accentDim, th2 / 2);
    g.circle(r.r() - th2 / 2, cy, 2, th.accent);
    g.fill(Rect{ hx - hw / 2, cy - th2 * 1.4f, hw, th2 * 2.8f }, th.accent, hw / 2);
    if (hov || held) g.circle(hx, cy, th2 * 1.6f, withA(th.accent, held ? 0.10f : 0.06f));
    return nv;
}

void UI::blob(float cx, float cy, float rad, int shape, const Col& c, float rot) {
    if (shape <= 0) { g.circle(cx, cy, rad, c); return; }
    ID2D1PathGeometry* p = g.path(); ID2D1GeometrySink* s = nullptr; p->Open(&s);
    int n = 96;
    for (int i = 0; i <= n; i++) {
        float a = (float)i / n * 6.2831853f, k = 1;
        switch (shape) {
        case 1: k = 0.92f + 0.08f * cosf(9 * (a + rot)); break;
        case 2: k = 0.80f + 0.20f * powf(fabsf(cosf(2 * (a + rot))), 0.6f); break;
        case 3: k = 0.88f + 0.12f * cosf(8 * (a + rot)); break;
        case 4: k = 0.90f + 0.10f * cosf(12 * (a + rot)); break;
        case 5: { float c4 = powf(fabsf(cosf(a + rot)), 4) + powf(fabsf(sinf(a + rot)), 4); k = 0.86f / powf(c4, 0.25f) * 0.98f; } break;
        case 6: k = 0.90f + 0.10f * cosf(5 * (a + rot)); break;
        }
        D2D1_POINT_2F pt = D2D1::Point2F(cx + cosf(a) * rad * k, cy + sinf(a) * rad * k);
        if (i == 0) s->BeginFigure(pt, D2D1_FIGURE_BEGIN_FILLED); else s->AddLine(pt);
    }
    s->EndFigure(D2D1_FIGURE_END_CLOSED); s->Close(); s->Release();
    g.fillGeo(p, c); p->Release();
}

void UI::areaGraph(const Rect& r, const float* v, int n, float maxv, const Col& c, bool fill) {
    if (n < 2) return;
    maxv = std::max(maxv, 1e-3f);
    auto px = [&](int i) { return r.x + r.w * i / (n - 1); };
    auto py = [&](int i) { return r.b() - r.h * clampf(v[i] / maxv, 0, 1); };
    ID2D1PathGeometry* line = g.path(); ID2D1GeometrySink* s = nullptr; line->Open(&s);
    ID2D1PathGeometry* area = fill ? g.path() : nullptr; ID2D1GeometrySink* sa = nullptr; if (area) area->Open(&sa);
    s->BeginFigure(D2D1::Point2F(px(0), py(0)), D2D1_FIGURE_BEGIN_HOLLOW);
    if (sa) { sa->BeginFigure(D2D1::Point2F(px(0), r.b()), D2D1_FIGURE_BEGIN_FILLED); sa->AddLine(D2D1::Point2F(px(0), py(0))); }
    for (int i = 0; i < n - 1; i++) {
        int i0 = std::max(0, i - 1), i3 = std::min(n - 1, i + 2);
        D2D1_BEZIER_SEGMENT b;
        b.point1 = D2D1::Point2F(px(i) + (px(i + 1) - px(i0)) / 6, py(i) + (py(i + 1) - py(i0)) / 6);
        b.point2 = D2D1::Point2F(px(i + 1) - (px(i3) - px(i)) / 6, py(i + 1) - (py(i3) - py(i)) / 6);
        b.point3 = D2D1::Point2F(px(i + 1), py(i + 1));
        s->AddBezier(b); if (sa) sa->AddBezier(b);
    }
    s->EndFigure(D2D1_FIGURE_END_OPEN); s->Close(); s->Release();
    if (sa) { sa->AddLine(D2D1::Point2F(r.r(), r.b())); sa->EndFigure(D2D1_FIGURE_END_CLOSED); sa->Close(); sa->Release(); }
    if (area) {
        D2D1_GRADIENT_STOP st[2] = { {0, C(withA(c, 0.30f * g.opacity))}, {1, C(withA(c, 0.0f))} };
        ID2D1GradientStopCollection* sc = nullptr; g.dc->CreateGradientStopCollection(st, 2, &sc);
        ID2D1LinearGradientBrush* lb = nullptr;
        if (sc) g.dc->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(r.x, r.y), D2D1::Point2F(r.x, r.b())), sc, &lb);
        if (lb) { g.dc->FillGeometry(area, lb); lb->Release(); }
        if (sc) sc->Release();
        area->Release();
    }
    g.strokeGeo(line, c, 2.2f, true); line->Release();
}

void UI::card(const Rect& r, float rad, const Col* c, bool sh) {
    if (sh) g.shadow(r, rad, 0.7f);
    g.fill(r, c ? *c : th.card, rad);
}
void UI::badge(float x, float y, const std::wstring& s, const Col& bg, const Col& fg, float* outW) {
    float w = g.measure(s, 11.5f, TW_MED).width + 16;
    g.fill(Rect{ x, y, w, 22 }, bg, 11);
    g.text(s, x + 8, y + 3.5f, 11.5f, fg, TW_MED);
    if (outW) *outW = w;
}

void Scroller::begin(const Rect& r) {
    view = r.h;
    if (!ui.blocked && r.hit(ui.in.mx, ui.in.my) && ui.in.wheel != 0) {
        target -= ui.in.wheel; lastWheel = ui.time;
    }
    float mx = std::max(0.f, content - view);
    if (target < 0) target = 0; if (target > mx) target = mx;
    float k = 1.f - expf(-16.f * ui.dt);
    off += (target - off) * k;
    if (fabsf(target - off) < 0.3f) off = target; else ui.anyAnim = true;
    ui.pushClip(r);
}
void Scroller::end(const Rect& r, float contentH) {
    ui.popClip();
    content = contentH;
    if (content > view + 1) {
        bool nearBar = r.hit(ui.in.mx, ui.in.my) && ui.in.mx > r.r() - 18;
        float want = (ui.time - lastWheel < 0.9f || nearBar || fabsf(target - off) > 1) ? 1.f : 0.f;
        barA += (want - barA) * (1.f - expf(-10.f * ui.dt));
        if (fabsf(want - barA) > 0.01f) ui.anyAnim = true;
        float bh = std::max(40.f, view * view / content);
        float by = r.y + (view - bh) * (off / std::max(1.f, content - view));
        if (barA > 0.01f) g.fill(Rect{ r.r() - 7, by + 4, 4, bh - 8 }, withA(ui.th.textDim, 0.45f * barA), 2);
    }
}
