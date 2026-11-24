#include "gfx.h"
#include "catalog.h"
#include "gen_icons.h"
#include <cstring>

Gfx g;

int iconCodepoint(const char* name) {
    int lo = 0, hi = ICONS_N - 1;
    while (lo <= hi) {
        int m = (lo + hi) / 2; int c = strcmp(ICONS[m].n, name);
        if (c == 0) return ICONS[m].cp;
        if (c < 0) lo = m + 1; else hi = m - 1;
    }
    return 0;
}

static IDWriteInMemoryFontFileLoader* s_memLoader = nullptr;
static std::wstring s_famText = L"Google Sans", s_famIcon0 = L"Material Symbols Rounded", s_famIcon1 = L"Material Symbols Rounded";

static IDWriteFontFile* fontFileFromWoff(const std::string& blob) {
    if (blob.empty()) return nullptr;
    IDWriteFontFile* file = nullptr;
    DWRITE_CONTAINER_TYPE ct = g.dw->AnalyzeContainerType(blob.data(), (UINT32)blob.size());
    if (ct == DWRITE_CONTAINER_TYPE_UNKNOWN) {
        s_memLoader->CreateInMemoryFontFileReference(g.dw, blob.data(), (UINT32)blob.size(), nullptr, &file);
        return file;
    }
    IDWriteFontFileStream* st = nullptr;
    if (FAILED(g.dw->UnpackFontFile(ct, blob.data(), (UINT32)blob.size(), &st)) || !st) return nullptr;
    UINT64 sz = 0; st->GetFileSize(&sz);
    const void* frag = nullptr; void* ctx = nullptr;
    if (SUCCEEDED(st->ReadFileFragment(&frag, 0, sz, &ctx))) {
        s_memLoader->CreateInMemoryFontFileReference(g.dw, frag, (UINT32)sz, nullptr, &file);
        st->ReleaseFileFragment(ctx);
    }
    st->Release();
    return file;
}

static IDWriteFontCollection1* makeCollection(std::initializer_list<const wchar_t*> resNames, std::wstring* famOut) {
    IDWriteFontSetBuilder1* b = nullptr;
    if (FAILED(g.dw->CreateFontSetBuilder(&b))) return nullptr;
    for (auto n : resNames) {
        IDWriteFontFile* f = fontFileFromWoff(loadResource(n));
        if (f) { b->AddFontFile(f); f->Release(); }
    }
    IDWriteFontSet* set = nullptr; IDWriteFontCollection1* coll = nullptr;
    b->CreateFontSet(&set);
    if (set) { g.dw->CreateFontCollectionFromFontSet(set, &coll); set->Release(); }
    b->Release();
    if (coll && famOut && coll->GetFontFamilyCount() > 0) {
        IDWriteFontFamily1* fam = nullptr;
        if (SUCCEEDED(coll->GetFontFamily(0, &fam)) && fam) {
            IDWriteLocalizedStrings* ns = nullptr;
            if (SUCCEEDED(fam->GetFamilyNames(&ns)) && ns) {
                UINT32 len = 0; ns->GetStringLength(0, &len);
                std::wstring w(len + 1, 0); ns->GetString(0, &w[0], len + 1); w.resize(len);
                *famOut = w; ns->Release();
            }
            fam->Release();
        }
    }
    return coll;
}

bool Gfx::init(HWND h) {
    hwnd = h;
    dpi = (float)GetDpiForWindow(h); scale = dpi / 96.f;
    RECT rc; GetClientRect(h, &rc);
    pw = std::max<int>(1, rc.right - rc.left); ph = std::max<int>(1, rc.bottom - rc.top);
    W = pw / scale; H = ph / scale;

    UINT fl = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    D3D_FEATURE_LEVEL lv[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, fl, lv, 4, D3D11_SDK_VERSION, &d3d, nullptr, nullptr);
    if (FAILED(hr)) hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, fl, lv, 4, D3D11_SDK_VERSION, &d3d, nullptr, nullptr);
    if (FAILED(hr)) return false;
    d3d->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgi);

    IDXGIAdapter* ad = nullptr; dxgi->GetAdapter(&ad);
    IDXGIFactory2* fac = nullptr; ad->GetParent(__uuidof(IDXGIFactory2), (void**)&fac); ad->Release();
    DXGI_SWAP_CHAIN_DESC1 sd{};
    sd.Width = pw; sd.Height = ph; sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM; sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; sd.BufferCount = 2;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL; sd.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
    hr = fac->CreateSwapChainForComposition(d3d, &sd, nullptr, &swap); fac->Release();
    if (FAILED(hr)) return false;

    D2D1_FACTORY_OPTIONS fo{};
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &fo, (void**)&d2f))) return false;
    if (FAILED(d2f->CreateDevice(dxgi, &d2dev))) return false;
    if (FAILED(d2dev->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &dc))) return false;
    dc->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);

    if (FAILED(DCompositionCreateDevice(dxgi, __uuidof(IDCompositionDevice), (void**)&dcomp))) return false;
    dcomp->CreateTargetForHwnd(hwnd, TRUE, &dtarget);
    dcomp->CreateVisual(&dvisual);
    dvisual->SetContent(swap);
    dtarget->SetRoot(dvisual);
    dcomp->Commit();

    dc->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1), &brush);
    resize();

    if (!dw) {
        DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory5), (IUnknown**)&dw);
        dw->CreateInMemoryFontFileLoader(&s_memLoader);
        dw->RegisterFontFileLoader(s_memLoader);
        fcText = makeCollection({ L"FONTGS4", L"FONTGS5", L"FONTGS7" }, &s_famText);
        fcIcon0 = makeCollection({ L"FONTIC0" }, &s_famIcon0);
        fcIcon1 = makeCollection({ L"FONTIC1" }, &s_famIcon1);
    }
    if (!wic) CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic));
    std::string lp = loadResource(L"LOGO");
    Rel(logo); logo = bitmapFromMemory(lp.data(), lp.size());
    std::string ap = loadResource(L"AUTHOR");
    Rel(author); author = bitmapFromMemory(ap.data(), ap.size());
    return true;
}

void Gfx::resize() {
    if (!swap) return;
    RECT rc; GetClientRect(hwnd, &rc);
    int nw = std::max<int>(1, rc.right - rc.left), nh = std::max<int>(1, rc.bottom - rc.top);
    dpi = (float)GetDpiForWindow(hwnd); scale = dpi / 96.f;
    if (target && nw == pw && nh == ph) { W = pw / scale; H = ph / scale; return; }
    pw = nw; ph = nh; W = pw / scale; H = ph / scale;
    dc->SetTarget(nullptr); Rel(target);
    swap->ResizeBuffers(2, pw, ph, DXGI_FORMAT_B8G8R8A8_UNORM, 0);
    IDXGISurface* surf = nullptr; swap->GetBuffer(0, __uuidof(IDXGISurface), (void**)&surf);
    D2D1_BITMAP_PROPERTIES1 bp = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), dpi, dpi);
    dc->CreateBitmapFromDxgiSurface(surf, &bp, &target);
    surf->Release();
    dc->SetTarget(target);
    dc->SetDpi(dpi, dpi);
}

bool Gfx::begin() {
    if (!dc || !target) return false;
    dc->BeginDraw();
    dc->SetTransform(D2D1::Matrix3x2F::Identity());
    dc->Clear(D2D1::ColorF(0, 0, 0, 0));
    frame++;
    return true;
}

void Gfx::end() {
    while (!clips.empty()) popClip();
    HRESULT hr = dc->EndDraw();
    DXGI_PRESENT_PARAMETERS pp{};
    HRESULT ph2 = swap->Present1(1, 0, &pp);
    if (hr == (HRESULT)D2DERR_RECREATE_TARGET || ph2 == DXGI_ERROR_DEVICE_REMOVED || ph2 == DXGI_ERROR_DEVICE_RESET) {
        HWND h = hwnd; release(); init(h);
    }
    if ((frame & 255) == 0) trimCache();
}

void Gfx::release() {
    Rel(logo); Rel(author); Rel(brush); Rel(target); Rel(dc); Rel(d2dev); Rel(d2f);
    Rel(dvisual); Rel(dtarget); Rel(dcomp); Rel(swap); Rel(dxgi); Rel(d3d);
}

static inline Col op(const Col& c) { return { c.r, c.g, c.b, c.a * g.opacity }; }

void Gfx::fill(const Rect& r, const Col& c, float rad) {
    if (r.w <= 0 || r.h <= 0) return;
    brush->SetColor(C(op(c)));
    if (rad > 0) dc->FillRoundedRectangle(RR(r, std::min(rad, std::min(r.w, r.h) / 2)), brush);
    else dc->FillRectangle(RF(r), brush);
}
void Gfx::stroke(const Rect& r, const Col& c, float rad, float w) {
    brush->SetColor(C(op(c)));
    Rect q{ r.x + w / 2, r.y + w / 2, r.w - w, r.h - w };
    if (rad > 0) dc->DrawRoundedRectangle(RR(q, std::min(rad, std::min(q.w, q.h) / 2)), brush, w);
    else dc->DrawRectangle(RF(q), brush, w);
}
void Gfx::circle(float cx, float cy, float rad, const Col& c) {
    brush->SetColor(C(op(c)));
    dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), rad, rad), brush);
}
void Gfx::ring(float cx, float cy, float rad, const Col& c, float w) {
    brush->SetColor(C(op(c)));
    dc->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), rad, rad), brush, w);
}
void Gfx::line(float x1, float y1, float x2, float y2, const Col& c, float w) {
    brush->SetColor(C(op(c)));
    dc->DrawLine(D2D1::Point2F(x1, y1), D2D1::Point2F(x2, y2), brush, w);
}
void Gfx::shadow(const Rect& r, float rad, float strength) {
    for (int i = 6; i >= 1; i--) {
        float d = i * 1.6f;
        Rect q{ r.x - d * 0.6f, r.y - d * 0.3f + i * 0.9f, r.w + d * 1.2f, r.h + d * 1.2f };
        fill(q, { 0, 0, 0, 0.028f * strength * (1.f - i / 7.f) + 0.006f * strength }, rad + d);
    }
}
void Gfx::gradientV(const Rect& r, const Col& top, const Col& bot, float rad) {
    D2D1_GRADIENT_STOP st[2] = { {0, C(op(top))}, {1, C(op(bot))} };
    ID2D1GradientStopCollection* sc = nullptr; dc->CreateGradientStopCollection(st, 2, &sc);
    ID2D1LinearGradientBrush* lb = nullptr;
    dc->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(r.x, r.y), D2D1::Point2F(r.x, r.b())), sc, &lb);
    if (lb) { if (rad > 0) dc->FillRoundedRectangle(RR(r, rad), lb); else dc->FillRectangle(RF(r), lb); lb->Release(); }
    if (sc) sc->Release();
}
void Gfx::pushClip(const Rect& r) {
    D2D1_RECT_F c = RF(r);
    dc->PushAxisAlignedClip(c, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    clips.push_back(c);
}
void Gfx::popClip() { if (clips.empty()) return; dc->PopAxisAlignedClip(); clips.pop_back(); }
void Gfx::pushLayer(float alpha, const Rect* clipRR, float rad) {
    ID2D1Geometry* geo = nullptr;
    if (clipRR) { ID2D1RoundedRectangleGeometry* rg = nullptr; d2f->CreateRoundedRectangleGeometry(RR(*clipRR, rad), &rg); geo = rg; }
    D2D1_LAYER_PARAMETERS1 lp = D2D1::LayerParameters1(D2D1::InfiniteRect(), geo, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
        D2D1::IdentityMatrix(), alpha, nullptr, D2D1_LAYER_OPTIONS1_NONE);
    dc->PushLayer(lp, nullptr);
    if (geo) geo->Release();
}
void Gfx::popLayer() { dc->PopLayer(); }
void Gfx::setTransform(float dx, float dy, float s, float cx, float cy) {
    dc->SetTransform(D2D1::Matrix3x2F::Scale(s, s, D2D1::Point2F(cx, cy)) * D2D1::Matrix3x2F::Translation(dx, dy));
}
void Gfx::resetTransform() { dc->SetTransform(D2D1::Matrix3x2F::Identity()); }

struct FmtKey { int size10, weight, kind; bool operator==(const FmtKey& o) const { return size10 == o.size10 && weight == o.weight && kind == o.kind; } };
static std::vector<std::pair<FmtKey, IDWriteTextFormat*>> s_fmts;
IDWriteTextFormat* Gfx::fmt(float size, int weight, bool icon, bool filled) {
    FmtKey k{ (int)(size * 10), weight, icon ? (filled ? 2 : 1) : 0 };
    for (auto& p : s_fmts) if (p.first == k) return p.second;
    IDWriteTextFormat* f = nullptr;
    IDWriteFontCollection1* fc = icon ? (filled ? fcIcon1 : fcIcon0) : fcText;
    const std::wstring& fam = icon ? (filled ? s_famIcon1 : s_famIcon0) : s_famText;
    HRESULT hr = dw->CreateTextFormat(fam.c_str(), fc, (DWRITE_FONT_WEIGHT)weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"en-us", &f);
    if (FAILED(hr)) dw->CreateTextFormat(L"Segoe UI Variable Text", nullptr, (DWRITE_FONT_WEIGHT)weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"en-us", &f);
    if (f) { f->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP); }
    s_fmts.push_back({ k, f });
    return f;
}

struct LayEntry { IDWriteTextLayout* l; u64 last; };
static std::unordered_map<std::wstring, LayEntry> s_lay;
IDWriteTextLayout* Gfx::layout(const std::wstring& s, float size, int weight, float maxW, bool wrap, int align, float maxH) {
    wchar_t kb[96];
    swprintf(kb, 96, L"\x1%d|%d|%d|%d|%d|", (int)(size * 10), weight, (int)maxW, wrap ? 1 : 0, align);
    std::wstring key = kb; key += s;
    auto it = s_lay.find(key);
    if (it != s_lay.end()) { it->second.last = frame; return it->second.l; }
    IDWriteTextLayout* l = nullptr;
    dw->CreateTextLayout(s.c_str(), (UINT32)s.size(), fmt(size, weight), maxW, maxH, &l);
    if (!l) return nullptr;
    l->SetWordWrapping(wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
    l->SetTextAlignment(align == AL_CENTER ? DWRITE_TEXT_ALIGNMENT_CENTER : align == AL_RIGHT ? DWRITE_TEXT_ALIGNMENT_TRAILING : DWRITE_TEXT_ALIGNMENT_LEADING);
    if (!wrap) {
        DWRITE_TRIMMING tr{ DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
        IDWriteInlineObject* ell = nullptr; dw->CreateEllipsisTrimmingSign(l, &ell);
        l->SetTrimming(&tr, ell); if (ell) ell->Release();
    }
    s_lay[key] = { l, frame };
    return l;
}
void Gfx::trimCache() {
    for (auto it = s_lay.begin(); it != s_lay.end();) {
        if (frame - it->second.last > 600) { it->second.l->Release(); it = s_lay.erase(it); } else ++it;
    }
}
D2D1_SIZE_F Gfx::measure(const std::wstring& s, float size, int weight, float maxW, bool wrap) {
    IDWriteTextLayout* l = layout(s, size, weight, maxW, wrap);
    if (!l) return { 0, 0 };
    DWRITE_TEXT_METRICS m; l->GetMetrics(&m);
    return { m.widthIncludingTrailingWhitespace, m.height };
}
float Gfx::text(const std::wstring& s, float x, float y, float size, const Col& c, int weight, float maxW, int align, bool wrap) {
    if (s.empty()) return 0;
    IDWriteTextLayout* l = layout(s, size, weight, maxW, wrap, align);
    if (!l) return 0;
    brush->SetColor(C(op(c)));
    dc->DrawTextLayout(D2D1::Point2F(x, y), l, brush, D2D1_DRAW_TEXT_OPTIONS_NONE);
    DWRITE_TEXT_METRICS m; l->GetMetrics(&m);
    return m.height;
}
float Gfx::textR(const std::wstring& s, const Rect& r, float size, const Col& c, int weight, int align, bool vcenter, bool wrap) {
    if (s.empty()) return 0;
    IDWriteTextLayout* l = layout(s, size, weight, std::max(1.f, r.w), wrap, align);
    if (!l) return 0;
    DWRITE_TEXT_METRICS m; l->GetMetrics(&m);
    float y = vcenter ? r.y + (r.h - m.height) / 2 : r.y;
    brush->SetColor(C(op(c)));
    dc->DrawTextLayout(D2D1::Point2F(r.x, y), l, brush, D2D1_DRAW_TEXT_OPTIONS_NONE);
    return m.height;
}
void Gfx::icon(const char* name, float cx, float cy, float size, const Col& c, bool filled) {
    int cp = iconCodepoint(name);
    if (!cp) return;
    wchar_t buf[3]; int n = 0;
    if (cp >= 0x10000) { cp -= 0x10000; buf[0] = (wchar_t)(0xD800 + (cp >> 10)); buf[1] = (wchar_t)(0xDC00 + (cp & 0x3FF)); n = 2; }
    else { buf[0] = (wchar_t)cp; n = 1; }
    IDWriteTextFormat* f = fmt(size, 400, true, filled);
    if (!f) return;
    brush->SetColor(C(op(c)));
    D2D1_RECT_F r = D2D1::RectF(cx - size, cy - size, cx + size, cy + size);
    f->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    dc->DrawText(buf, n, f, r, brush, D2D1_DRAW_TEXT_OPTIONS_NONE);
}

ID2D1Bitmap* Gfx::bitmapFromMemory(const void* data, size_t n) {
    if (!wic || !data || !n) return nullptr;
    IWICStream* st = nullptr; IWICBitmapDecoder* dec = nullptr; IWICBitmapFrameDecode* fr = nullptr; IWICFormatConverter* cv = nullptr;
    ID2D1Bitmap* bmp = nullptr;
    if (SUCCEEDED(wic->CreateStream(&st)) && SUCCEEDED(st->InitializeFromMemory((BYTE*)data, (DWORD)n)) &&
        SUCCEEDED(wic->CreateDecoderFromStream(st, nullptr, WICDecodeMetadataCacheOnLoad, &dec)) &&
        SUCCEEDED(dec->GetFrame(0, &fr)) && SUCCEEDED(wic->CreateFormatConverter(&cv)) &&
        SUCCEEDED(cv->Initialize(fr, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) {
        dc->CreateBitmapFromWicBitmap(cv, nullptr, &bmp);
    }
    if (cv) cv->Release(); if (fr) fr->Release(); if (dec) dec->Release(); if (st) st->Release();
    return bmp;
}
void Gfx::image(ID2D1Bitmap* b, const Rect& r, float alpha) {
    if (!b) return;
    dc->DrawBitmap(b, RF(r), alpha * opacity, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
}
void Gfx::imageTint(ID2D1Bitmap* b, const Rect& r, const Col& c) {
    if (!b) return;
    brush->SetColor(C(op(c)));
    D2D1_ANTIALIAS_MODE am = dc->GetAntialiasMode();
    dc->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
    dc->FillOpacityMask(b, brush, RF(r), nullptr);
    dc->SetAntialiasMode(am);
}
void Gfx::imageCircle(ID2D1Bitmap* b, float cx, float cy, float rad, float alpha) {
    if (!b) return;
    ID2D1EllipseGeometry* eg = nullptr;
    d2f->CreateEllipseGeometry(D2D1::Ellipse(D2D1::Point2F(cx, cy), rad, rad), &eg);
    D2D1_LAYER_PARAMETERS1 lp = D2D1::LayerParameters1(D2D1::InfiniteRect(), eg, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
        D2D1::IdentityMatrix(), 1.f, nullptr, D2D1_LAYER_OPTIONS1_NONE);
    dc->PushLayer(lp, nullptr);
    dc->DrawBitmap(b, D2D1::RectF(cx - rad, cy - rad, cx + rad, cy + rad), alpha * opacity, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    dc->PopLayer();
    if (eg) eg->Release();
}

ID2D1PathGeometry* Gfx::path() { ID2D1PathGeometry* p = nullptr; d2f->CreatePathGeometry(&p); return p; }
void Gfx::fillGeo(ID2D1Geometry* geo, const Col& c) { brush->SetColor(C(op(c))); dc->FillGeometry(geo, brush); }
void Gfx::strokeGeo(ID2D1Geometry* geo, const Col& c, float w, bool round) {
    static ID2D1StrokeStyle* rs = nullptr;
    if (!rs) {
        D2D1_STROKE_STYLE_PROPERTIES sp = D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND);
        d2f->CreateStrokeStyle(sp, nullptr, 0, &rs);
    }
    brush->SetColor(C(op(c)));
    dc->DrawGeometry(geo, brush, w, round ? rs : nullptr);
}
