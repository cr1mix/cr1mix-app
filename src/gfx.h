#pragma once
#include "common.h"

enum FontW { TW_REG = 400, TW_MED = 500, TW_BOLD = 700 };
enum Align { AL_LEFT = 0, AL_CENTER = 1, AL_RIGHT = 2 };

struct Gfx {
    HWND hwnd = nullptr;
    float dpi = 96, scale = 1;
    int pw = 0, ph = 0;
    float W = 0, H = 0;
    ID3D11Device* d3d = nullptr;
    IDXGIDevice* dxgi = nullptr;
    IDXGISwapChain1* swap = nullptr;
    ID2D1Factory1* d2f = nullptr;
    ID2D1Device* d2dev = nullptr;
    ID2D1DeviceContext* dc = nullptr;
    ID2D1Bitmap1* target = nullptr;
    IDCompositionDevice* dcomp = nullptr;
    IDCompositionTarget* dtarget = nullptr;
    IDCompositionVisual* dvisual = nullptr;
    ID2D1SolidColorBrush* brush = nullptr;
    IDWriteFactory5* dw = nullptr;
    IDWriteFontCollection1* fcText = nullptr;
    IDWriteFontCollection1* fcIcon0 = nullptr;
    IDWriteFontCollection1* fcIcon1 = nullptr;
    IWICImagingFactory* wic = nullptr;
    ID2D1Bitmap* logo = nullptr;
    ID2D1Bitmap* author = nullptr;
    float opacity = 1;
    std::vector<D2D1_RECT_F> clips;

    bool init(HWND h);
    void resize();
    bool begin();
    void end();
    void release();

    void fill(const Rect& r, const Col& c, float rad = 0);
    void stroke(const Rect& r, const Col& c, float rad = 0, float w = 1);
    void circle(float cx, float cy, float rad, const Col& c);
    void ring(float cx, float cy, float rad, const Col& c, float w);
    void line(float x1, float y1, float x2, float y2, const Col& c, float w = 1);
    void shadow(const Rect& r, float rad, float strength = 1);
    void gradientV(const Rect& r, const Col& top, const Col& bot, float rad = 0);
    void pushClip(const Rect& r);
    void popClip();
    void pushLayer(float alpha, const Rect* clipRR = nullptr, float rad = 0);
    void popLayer();
    void setTransform(float dx, float dy, float s = 1, float cx = 0, float cy = 0);
    void resetTransform();

    IDWriteTextFormat* fmt(float size, int weight, bool icon = false, bool filled = false);
    IDWriteTextLayout* layout(const std::wstring& s, float size, int weight, float maxW, bool wrap, int align = AL_LEFT, float maxH = 10000);
    float text(const std::wstring& s, float x, float y, float size, const Col& c, int weight = TW_REG, float maxW = 10000, int align = AL_LEFT, bool wrap = false);
    float textR(const std::wstring& s, const Rect& r, float size, const Col& c, int weight = TW_REG, int align = AL_LEFT, bool vcenter = true, bool wrap = false);
    D2D1_SIZE_F measure(const std::wstring& s, float size, int weight, float maxW = 10000, bool wrap = false);
    void icon(const char* name, float cx, float cy, float size, const Col& c, bool filled = false);

    ID2D1Bitmap* bitmapFromMemory(const void* data, size_t n);
    void image(ID2D1Bitmap* b, const Rect& r, float alpha = 1);
    void imageTint(ID2D1Bitmap* b, const Rect& r, const Col& c);
    void imageCircle(ID2D1Bitmap* b, float cx, float cy, float rad, float alpha = 1);

    ID2D1PathGeometry* path();
    void fillGeo(ID2D1Geometry* g, const Col& c);
    void strokeGeo(ID2D1Geometry* g, const Col& c, float w, bool round = true);

    void trimCache();
    u64 frame = 0;
};
extern Gfx g;
int iconCodepoint(const char* name);
