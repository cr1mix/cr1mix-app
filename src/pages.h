#pragma once
#include "app.h"

struct Grid { float x, w, gap; int cols; float colW() const { return (w - gap * (cols - 1)) / cols; } Rect cell(int i, float y, float h) const { return { x + (colW() + gap) * (i % cols), y, colW(), h }; } };
Rect contentColumn(const Rect& area, float maxW = 1320);
int columnsFor(float w);

void surfaceCard(const Rect& r, float rad, const Col& c, bool hover = false);
void sectionTitle(float x, float y, const std::wstring& title, const std::wstring& sub = L"");
void iconBlob(float cx, float cy, float size, const char* icon, const Col& bg, const Col& fg, int shape = 1, float rot = 0);

void graphCard(const Rect& r, const char* icon, const std::wstring& title, const std::wstring& big, const std::wstring& sub, const Hist& h, float maxv, const Col& c, float ring = -1);
void ringCard(const Rect& r, const char* icon, const std::wstring& title, float pct, const std::wstring& big, const std::wstring& sub, const Col& c);
void barsCard(const Rect& r, const char* icon, const std::wstring& title, const std::vector<float>& v, const std::vector<std::wstring>& labels, const std::wstring& sub, const Col& c);
void infoCard(const Rect& r, const char* icon, const std::wstring& title, const std::vector<std::pair<std::wstring, std::wstring>>& rows, const Col& c);
void netCard(const Rect& r);
void servicesCard(const Rect& r);
void tabHero(int tab, const Rect& row);
float automationCard(int tab, const Rect& r);
std::vector<JobItem> recommendedFor(int tab, bool includeAdvanced);
int recOption(const Tweak& t);
