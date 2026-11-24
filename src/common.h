#pragma once
#define CR1MIX_VERSION L"0.1v"
#define CR1MIX_BUILD 1
#define _WIN32_WINNT 0x0A00
#define WINVER 0x0A00
#define NTDDI_VERSION 0x0A00000C
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define COBJMACROS
#include <windows.h>
#include <windowsx.h>
#include <d3d11.h>
#include <dxgi1_3.h>
#include <d2d1_1.h>
#include <d2d1_1helper.h>
#include <dwrite_3.h>
#include <dcomp.h>
#include <wincodec.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <functional>
#include <algorithm>
#include <set>
#include <cmath>
#include <cstdint>

#pragma comment(lib,"d3d11")
#pragma comment(lib,"dxgi")
#pragma comment(lib,"d2d1")
#pragma comment(lib,"dwrite")
#pragma comment(lib,"dcomp")
#pragma comment(lib,"windowscodecs")
#pragma comment(lib,"dwmapi")

typedef uint8_t  u8;  typedef uint32_t u32; typedef int32_t i32; typedef uint64_t u64;

template<class T> inline void SafeRelease(T** p){ if(*p){ (*p)->lpVtbl->Release((IUnknown*)*p); *p=nullptr; } }
template<class T> inline void Rel(T*& p){ if(p){ p->Release(); p=nullptr; } }

inline float clampf(float v,float a,float b){ return v<a?a:(v>b?b:v); }
inline float lerpf(float a,float b,float t){ return a+(b-a)*t; }
inline float easeOut(float t){ t=clampf(t,0,1); return 1-powf(1-t,3); }
inline float easeInOut(float t){ t=clampf(t,0,1); return t<0.5f?4*t*t*t:1-powf(-2*t+2,3)/2; }

struct Rect { float x,y,w,h;
    float r()const{return x+w;} float b()const{return y+h;}
    bool hit(float px,float py)const{ return px>=x&&py>=y&&px<x+w&&py<y+h; }
    Rect inset(float d)const{ return {x+d,y+d,w-2*d,h-2*d}; }
};
inline D2D1_RECT_F RF(const Rect&r){ return D2D1::RectF(r.x,r.y,r.r(),r.b()); }
inline D2D1_ROUNDED_RECT RR(const Rect&r,float rad){ return D2D1::RoundedRect(RF(r),rad,rad); }

struct Col { float r,g,b,a; };
inline D2D1_COLOR_F C(const Col&c){ return D2D1::ColorF(c.r,c.g,c.b,c.a); }
inline Col rgb(int hex,float a=1){ return { ((hex>>16)&255)/255.f,((hex>>8)&255)/255.f,(hex&255)/255.f,a }; }
inline Col mix(const Col&a,const Col&b,float t){ return {lerpf(a.r,b.r,t),lerpf(a.g,b.g,t),lerpf(a.b,b.b,t),lerpf(a.a,b.a,t)}; }
inline Col withA(const Col&c,float a){ return {c.r,c.g,c.b,a}; }

struct Theme {
    bool dark=true;
    Col bg;
    Col surf, surfLow, surf2, surfHi, surfHighest;
    Col card, cardHi;
    Col outline, outlineV;
    Col text, textDim, textFaint;
    Col accent, onAccent, accentDim, primaryC, onPrimaryC;
    Col tertiary, tertiaryC, onTertiaryC;
    Col good, warn, danger, dangerC, track;
};
Theme makeTheme(bool dark, int seedHex, float transparency);
Col hsl(float h, float s, float l, float a = 1);
void toHsl(const Col& c, float& h, float& s, float& l);

std::wstring u8to16(const std::string& s);
std::string  u16to8(const std::wstring& s);
