#include "common.h"

std::wstring u8to16(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0); MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n); return w;
}
std::string u16to8(const std::wstring& s) {
    if (s.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, 0, 0);
    std::string o(n, 0); WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), &o[0], n, 0, 0); return o;
}

Col hsl(float h, float s, float l, float a) {
    h = fmodf(h, 360.f); if (h < 0) h += 360.f;
    float c = (1 - fabsf(2 * l - 1)) * s, x = c * (1 - fabsf(fmodf(h / 60.f, 2) - 1)), m = l - c / 2;
    float r = 0, g = 0, b = 0;
    if (h < 60) { r = c; g = x; } else if (h < 120) { r = x; g = c; } else if (h < 180) { g = c; b = x; }
    else if (h < 240) { g = x; b = c; } else if (h < 300) { r = x; b = c; } else { r = c; b = x; }
    return { r + m, g + m, b + m, a };
}
void toHsl(const Col& c, float& h, float& s, float& l) {
    float mx = std::max(c.r, std::max(c.g, c.b)), mn = std::min(c.r, std::min(c.g, c.b)), d = mx - mn;
    l = (mx + mn) / 2; s = d < 1e-5f ? 0 : d / (1 - fabsf(2 * l - 1));
    if (d < 1e-5f) h = 0;
    else if (mx == c.r) h = 60 * fmodf((c.g - c.b) / d, 6);
    else if (mx == c.g) h = 60 * ((c.b - c.r) / d + 2);
    else h = 60 * ((c.r - c.g) / d + 4);
    if (h < 0) h += 360;
}

Theme makeTheme(bool dark, int seedHex, float transparency) {
    Theme t; t.dark = dark;
    float h, s, l; toHsl(rgb(seedHex), h, s, l);
    float ps = clampf(s, 0.28f, 0.72f);
    float ns = clampf(s * 0.22f, 0.04f, 0.12f);
    float op = clampf(1.f - transparency, 0.55f, 1.f);
    if (dark) {
        t.bg = hsl(h, ns, 0.070f, op);
        t.surf = hsl(h, ns, 0.085f, 0.0f + std::min(1.f, op + 0.06f));
        t.surfLow = hsl(h, ns * 1.1f, 0.105f, std::min(1.f, op + 0.08f));
        t.surf2 = hsl(h, ns * 1.2f, 0.130f, std::min(1.f, op + 0.10f));
        t.surfHi = hsl(h, ns * 1.2f, 0.165f);
        t.surfHighest = hsl(h, ns * 1.2f, 0.205f);
        t.card = hsl(h, ns * 1.2f, 0.125f, std::min(1.f, op + 0.06f));
        t.cardHi = hsl(h, ns * 1.3f, 0.165f, std::min(1.f, op + 0.10f));
        t.outline = hsl(h, ns, 0.46f); t.outlineV = hsl(h, ns, 0.24f);
        t.text = hsl(h, ns * 0.6f, 0.91f); t.textDim = hsl(h, ns * 0.8f, 0.74f); t.textFaint = hsl(h, ns * 0.8f, 0.55f);
        t.accent = hsl(h, ps, 0.76f); t.onAccent = hsl(h, ps * 0.8f, 0.17f);
        t.primaryC = hsl(h, ps * 0.55f, 0.27f); t.onPrimaryC = hsl(h, ps * 0.7f, 0.90f);
        t.accentDim = hsl(h, ps * 0.30f, 0.25f);
        t.tertiary = hsl(h + 70, ps * 0.8f, 0.78f); t.tertiaryC = hsl(h + 70, ps * 0.45f, 0.26f); t.onTertiaryC = hsl(h + 70, ps * 0.7f, 0.90f);
        t.good = hsl(148, 0.42f, 0.66f); t.warn = hsl(42, 0.75f, 0.66f); t.danger = hsl(356, 0.70f, 0.72f); t.dangerC = hsl(356, 0.45f, 0.24f);
        t.track = hsl(h, ns * 1.4f, 0.24f);
    } else {
        t.bg = hsl(h, ns * 2.0f, 0.955f, op);
        t.surf = hsl(h, ns * 2.0f, 0.975f, std::min(1.f, op + 0.06f));
        t.surfLow = hsl(h, ns * 2.2f, 0.960f, std::min(1.f, op + 0.08f));
        t.surf2 = hsl(h, ns * 2.4f, 0.935f, std::min(1.f, op + 0.10f));
        t.surfHi = hsl(h, ns * 2.4f, 0.905f);
        t.surfHighest = hsl(h, ns * 2.4f, 0.875f);
        t.card = hsl(h, ns * 2.2f, 0.985f, std::min(1.f, op + 0.08f));
        t.cardHi = hsl(h, ns * 2.4f, 0.945f, std::min(1.f, op + 0.10f));
        t.outline = hsl(h, ns, 0.50f); t.outlineV = hsl(h, ns * 2, 0.82f);
        t.text = hsl(h, ns * 2, 0.11f); t.textDim = hsl(h, ns * 2, 0.32f); t.textFaint = hsl(h, ns * 2, 0.50f);
        t.accent = hsl(h, ps, 0.38f); t.onAccent = rgb(0xFFFFFF);
        t.primaryC = hsl(h, ps * 0.8f, 0.88f); t.onPrimaryC = hsl(h, ps, 0.14f);
        t.accentDim = hsl(h, ps * 0.45f, 0.87f);
        t.tertiary = hsl(h + 70, ps, 0.38f); t.tertiaryC = hsl(h + 70, ps * 0.8f, 0.88f); t.onTertiaryC = hsl(h + 70, ps, 0.14f);
        t.good = hsl(150, 0.55f, 0.34f); t.warn = hsl(38, 0.85f, 0.40f); t.danger = hsl(356, 0.62f, 0.46f); t.dangerC = hsl(356, 0.70f, 0.92f);
        t.track = hsl(h, ns * 2.4f, 0.86f);
    }
    return t;
}
