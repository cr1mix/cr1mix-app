#include "pages.h"
#include "discord.h"

Rect contentColumn(const Rect& area, float maxW) {
    float pad = area.w < 1100 ? 22.f : 32.f;
    float w = std::min(maxW, area.w - pad * 2);
    return { area.x + (area.w - w) / 2, area.y, w, area.h };
}
int columnsFor(float w) { return w >= 1060 ? 3 : w >= 700 ? 2 : 1; }

void surfaceCard(const Rect& r, float rad, const Col& c, bool hover) {
    g.fill(r, c, rad);
    g.stroke(r, withA(app.th.text, app.dark ? 0.045f : 0.06f), rad, 1);
    if (hover) g.fill(r, withA(app.th.text, 0.035f), rad);
}
void sectionTitle(float x, float y, const std::wstring& title, const std::wstring& sub) {
    g.text(title, x, y, 19, app.th.text, TW_BOLD);
    if (!sub.empty()) g.text(sub, x + g.measure(title, 19, TW_BOLD).width + 12, y + 4.5f, 13, app.th.textFaint, TW_MED);
}
void iconBlob(float cx, float cy, float size, const char* icon, const Col& bg, const Col& fg, int shape, float rot) {
    ui.blob(cx, cy, size / 2, shape, bg, rot);
    g.icon(icon, cx, cy, size * 0.48f, fg);
}

static void cardHeader(const Rect& r, const char* icon, const std::wstring& title, const Col& c) {
    iconBlob(r.x + 36, r.y + 36, 40, icon, withA(c, app.dark ? 0.18f : 0.16f), c, 1, ui.time * 0.15f);
    g.text(title, r.x + 64, r.y + 26, 14.5f, app.th.textDim, TW_MED, r.w - 90);
}
void graphCard(const Rect& r, const char* icon, const std::wstring& title, const std::wstring& big, const std::wstring& sub, const Hist& h, float maxv, const Col& c, float ring) {
    surfaceCard(r, 24, app.th.card);
    cardHeader(r, icon, title, c);
    g.text(big, r.x + 22, r.y + 66, 34, app.th.text, TW_BOLD);
    g.text(sub, r.x + 22, r.y + 110, 12.5f, app.th.textFaint, TW_MED, r.w - 44);
    if (ring >= 0) ui.wavyRing(r.r() - 52, r.y + 74, 30, 5, ring, c, app.th.track, ui.time * 2.5f, 1.8f, 9);
    float gh = std::max(36.f, r.h - 150);
    Rect gr{ r.x + 1, r.b() - gh - 2, r.w - 2, gh - 2 };
    g.pushLayer(1.f, &r, 24);
    int n = std::max(2, h.n);
    ui.areaGraph(gr, h.v + (Hist::N - n), n, maxv, c, true);
    g.popLayer();
}
void ringCard(const Rect& r, const char* icon, const std::wstring& title, float pct, const std::wstring& big, const std::wstring& sub, const Col& c) {
    surfaceCard(r, 24, app.th.card);
    cardHeader(r, icon, title, c);
    float rad = std::min(r.h - 96, r.w * 0.36f) / 2;
    float v = ui.anim(HID("ring", (u64)(uintptr_t)icon + (u64)r.x), clampf(pct, 0, 1), 6);
    ui.wavyRing(r.r() - rad - 30, r.y + 64 + rad, rad, 8, v, c, app.th.track, ui.time * 1.8f, 2.6f, 10);
    g.textR(big, { r.r() - rad * 2 - 30, r.y + 64 + rad - 16, rad * 2, 32 }, 22, app.th.text, TW_BOLD, AL_CENTER);
    g.text(sub, r.x + 22, r.y + 74, 13.5f, app.th.textDim, TW_REG, r.w - rad * 2 - 70, AL_LEFT, true);
}
void barsCard(const Rect& r, const char* icon, const std::wstring& title, const std::vector<float>& v, const std::vector<std::wstring>& labels, const std::wstring& sub, const Col& c) {
    surfaceCard(r, 24, app.th.card);
    cardHeader(r, icon, title, c);
    g.text(sub, r.x + 22, r.y + 62, 12.5f, app.th.textFaint, TW_MED, r.w - 44);
    int n = (int)v.size(); if (!n) return;
    Rect area{ r.x + 22, r.y + 92, r.w - 44, r.h - 92 - (labels.empty() ? 18 : 36) };
    float gap = n > 16 ? 3.f : 6.f, bw = (area.w - gap * (n - 1)) / n;
    for (int i = 0; i < n; i++) {
        float val = ui.anim(HID("bar", (u64)i * 131 + (u64)(uintptr_t)icon), clampf(v[i] / 100.f, 0, 1), 10);
        Rect b{ area.x + i * (bw + gap), area.y, bw, area.h };
        g.fill(b, app.th.track, std::min(bw / 2, 8.f));
        float hh = std::max(std::min(bw, 8.f), b.h * val);
        g.fill({ b.x, b.b() - hh, bw, hh }, mix(withA(c, 0.55f), c, val), std::min(bw / 2, 8.f));
        if (!labels.empty() && i < (int)labels.size()) g.textR(labels[i], { b.x - 2, area.b() + 8, bw + 4, 18 }, 11, app.th.textFaint, TW_MED, AL_CENTER);
    }
}
void infoCard(const Rect& r, const char* icon, const std::wstring& title, const std::vector<std::pair<std::wstring, std::wstring>>& rows, const Col& c) {
    surfaceCard(r, 24, app.th.card);
    cardHeader(r, icon, title, c);
    float y = r.y + 70;
    for (auto& kv : rows) {
        if (y + 22 > r.b() - 10) break;
        g.text(kv.first, r.x + 22, y, 13, app.th.textFaint, TW_MED, r.w * 0.42f);
        g.textR(kv.second, { r.x + r.w * 0.40f, y - 1, r.w * 0.60f - 22, 20 }, 13.5f, app.th.text, TW_MED, AL_RIGHT);
        y += 27;
    }
}
void netCard(const Rect& r) {
    Col c = app.th.tertiary, c2 = app.th.accent;
    surfaceCard(r, 24, app.th.card);
    cardHeader(r, "lan", L"Network", c);
    g.text(L"↓ " + fmtBytes(S.netDown) + L"/s", r.x + 22, r.y + 66, 24, app.th.text, TW_BOLD);
    g.text(L"↑ " + fmtBytes(S.netUp) + L"/s", r.x + 22, r.y + 100, 15, app.th.textDim, TW_MED);
    float mx = std::max(S.hDown.maxv(), S.hUp.maxv()) * 1.15f + 1024;
    float gh = std::max(36.f, r.h - 150);
    Rect gr{ r.x + 1, r.b() - gh - 2, r.w - 2, gh - 2 };
    g.pushLayer(1.f, &r, 24);
    ui.areaGraph(gr, S.hDown.v + (Hist::N - std::max(2, S.hDown.n)), std::max(2, S.hDown.n), mx, c, true);
    ui.areaGraph(gr, S.hUp.v + (Hist::N - std::max(2, S.hUp.n)), std::max(2, S.hUp.n), mx, c2, false);
    g.popLayer();
}

int recOption(const Tweak& t) {
    for (size_t i = 0; i < t.opts.size(); i++) if (t.opts[i].rec) return (int)i;
    if (t.id == "cpu.plan" || t.id == "gpu.nvprofile") return g_eng.mc.laptop ? 1 : 0;
    if (t.id == "win.svcpreset") return 2;
    for (size_t i = 0; i < t.opts.size(); i++) if (!t.opts[i].hidden) return (int)i;
    return -1;
}
std::vector<JobItem> recommendedFor(int tab, bool adv) {
    std::vector<JobItem> out;
    for (auto& t : g_cat.tweaks) {
        if (tab >= 0 && t.tab != g_cat.tabs[tab].id) continue;
        if (t.tab == "look") continue;
        if (!(t.fl & TF_REC) || t.kind == TK_ACTION) continue;
        if (!g_eng.requirementsMet(t)) continue;
        if (t.risk >= 3 && !adv) continue;
        if (t.kind == TK_TOGGLE) { if (t.state != 1) out.push_back({ t.idx, J_APPLY, -1, {} }); }
        else if (t.kind == TK_CHOICE) { int o = recOption(t); if (o >= 0 && t.state != o) out.push_back({ t.idx, J_CHOOSE, o, {} }); }
        else if (t.kind == TK_PICK) { std::vector<int> it; for (size_t i = 0; i < t.opts.size(); i++) if (t.opts[i].rec && !t.picked[i]) it.push_back((int)i); if (!it.empty()) out.push_back({ t.idx, J_PICK, -1, it }); }
    }
    return out;
}
float automationCard(int tab, const Rect& r) {
    auto rec = recommendedFor(tab, false);
    int total = 0, on = appliedCount(g_cat.tabs[tab], &total);
    u64 id = HID("auto", tab);
    surfaceCard(r, 28, app.th.primaryC);
    g.pushLayer(1.f, &r, 28);
    ui.blob(r.r() - 70, r.y + 30, 90, 1, withA(app.th.onPrimaryC, 0.06f), ui.time * 0.12f);
    ui.blob(r.r() - 170, r.b() - 10, 46, 3, withA(app.th.onPrimaryC, 0.05f), -ui.time * 0.2f);
    g.popLayer();
    iconBlob(r.x + 46, r.y + r.h / 2, 52, "auto_awesome", app.th.onPrimaryC, app.th.primaryC, 3, ui.time * 0.3f);
    std::wstring title = L"Safe " + g_cat.tabs[tab].title + L" tweaks";
    g.text(title, r.x + 86, r.y + 22, 18, app.th.onPrimaryC, TW_BOLD, r.w - 380);
    std::wstring sub = rec.empty() ? L"Everything safe in " + g_cat.tabs[tab].title + L" is already applied."
        : std::to_wstring(rec.size()) + L" safe tweak" + (rec.size() == 1 ? L"" : L"s") + L" for this PC. Advanced ones are never included. Open the list to check each one before anything changes.";
    (void)on; (void)total;
    g.text(sub, r.x + 86, r.y + 50, 13.5f, withA(app.th.onPrimaryC, 0.85f), TW_REG, r.w - 380, AL_LEFT, true);
    Rect b{ r.r() - 210, r.y + (r.h - 48) / 2, 186, 48 };
    Col sav = ui.th.accent, savOn = ui.th.onAccent;
    ui.th.accent = app.th.onPrimaryC; ui.th.onAccent = app.th.primaryC;
    if (ui.btnFilled(id, b, rec.empty() ? L"All set" : L"See " + std::to_wstring(rec.size()) + L" tweaks", rec.empty() ? "check" : "view_list", !rec.empty()))
        app.openPlan(L"Safe " + g_cat.tabs[tab].title + L" tweaks", rec);
    ui.th.accent = sav; ui.th.onAccent = savOn;
    return r.h;
}

static void serviceCounts(int& running, int& total) {
    static double at = -100; static int r = 0, n = 0;
    if (nowSec() - at > 4) {
        at = nowSec(); r = n = 0;
        SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ENUMERATE_SERVICE);
        if (scm) {
            DWORD need = 0, cnt = 0, resume = 0;
            EnumServicesStatusExW(scm, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_STATE_ALL, nullptr, 0, &need, &cnt, &resume, nullptr);
            std::vector<BYTE> buf(need + 4096); resume = 0;
            if (EnumServicesStatusExW(scm, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_STATE_ALL, buf.data(), (DWORD)buf.size(), &need, &cnt, &resume, nullptr)) {
                auto* e = (ENUM_SERVICE_STATUS_PROCESSW*)buf.data();
                for (DWORD i = 0; i < cnt; i++) { n++; if (e[i].ServiceStatusProcess.dwCurrentState == SERVICE_RUNNING) r++; }
            }
            CloseServiceHandle(scm);
        }
    }
    running = r; total = n;
}
void servicesCard(const Rect& r) {
    const Theme& T = app.th;
    surfaceCard(r, 24, T.card);
    cardHeader(r, "dns", L"Services", T.tertiary);
    int run = 0, all = 0; serviceCounts(run, all);
    g.text(std::to_wstring(run), r.x + 22, r.y + 66, 34, T.text, TW_BOLD);
    float nw = g.measure(std::to_wstring(run), 34, TW_BOLD).width;
    g.text(L"running", r.x + 30 + nw, r.y + 82, 14, T.textDim, TW_MED);
    int pi = g_cat.find("win.svcpreset");
    std::wstring preset = pi >= 0 && g_cat.tweaks[pi].state >= 0 ? g_cat.tweaks[pi].opts[g_cat.tweaks[pi].state].label : L"none";
    g.text(L"of " + std::to_wstring(all) + L" installed · preset: " + preset, r.x + 22, r.y + 114, 12.5f, T.textFaint, TW_MED, r.w - 44);
    if (pi >= 0 && ui.btnTonal(HID("svcCust"), { r.x + 20, r.b() - 58, std::min(220.f, r.w - 40), 42 }, L"Customize presets", "tune")) app.detail = pi;
}

void tabHero(int tab, const Rect& row) {
    const std::string& id = g_cat.tabs[tab].id;
    Grid gr{ row.x, row.w, 16, columnsFor(row.w) };
    Rect a = gr.cell(0, row.y, row.h), b = gr.cell(1, row.y, row.h), c = gr.cell(2, row.y, row.h);
    if (gr.cols == 2) c = { row.x, row.y + row.h + 16, row.w, row.h };
    if (gr.cols == 1) { b = { row.x, row.y + row.h + 16, row.w, row.h }; c = { row.x, row.y + (row.h + 16) * 2, row.w, row.h }; }
    wchar_t bf[96];
    const Theme& T = app.th;
    if (id == "cpu") {
        swprintf(bf, 96, L"%.0f%%", S.cpu); graphCard(a, "memory", L"CPU load", bf, S.cpuName, S.hCpu, 100, T.accent, S.cpu / 100.f);
        std::vector<float> pc = S.perCore; if (pc.size() > 32) pc.resize(32);
        barsCard(b, "grid_view", L"Per-core usage", pc, {}, std::to_wstring(S.cores) + L" cores · " + std::to_wstring(S.threads) + L" threads", T.tertiary);
        swprintf(bf, 96, L"%.2f GHz", S.clockGHz);
        infoCard(c, "speed", L"Clock & power", { {L"Current clock", bf}, {L"Base clock", std::to_wstring(S.baseMhz) + L" MHz"}, {L"Processes", std::to_wstring(S.procs)}, {L"Threads", std::to_wstring(S.thrds)}, {L"Uptime", fmtDur(S.uptimeSec)} }, T.accent);
    } else if (id == "gpu") {
        swprintf(bf, 96, L"%.0f%%", S.gpu); graphCard(a, "developer_board", L"GPU load", bf, S.gpuName, S.hGpu, 100, T.accent, S.gpu / 100.f);
        swprintf(bf, 96, L"%.1f GB", S.vramUsedGB);
        ringCard(b, "memory_alt", L"Video memory", S.vramTotalGB > 0 ? S.vramUsedGB / (float)S.vramTotalGB : 0, bf, L"of " + fmtBytes(S.vramTotalGB * 1073741824.0) + L" dedicated memory in use", T.tertiary);
        barsCard(c, "equalizer", L"GPU engines", { S.gpu3d, S.gpuCompute, S.gpuCopy, S.gpuVideo }, { L"3D", L"Compute", L"Copy", L"Video" }, L"Load per engine", T.accent);
    } else if (id == "mem") {
        swprintf(bf, 96, L"%.0f%%", S.ram); graphCard(a, "memory_alt", L"Memory in use", bf, L"of " + fmtBytes(S.ramTotalGB * 1073741824.0), S.hRam, 100, T.accent, S.ram / 100.f);
        swprintf(bf, 96, L"%.1f GB", S.ramUsedGB);
        ringCard(b, "data_usage", L"Used now", S.ram / 100.f, bf, L"Free: " + fmtBytes((S.ramTotalGB - S.ramUsedGB) * 1073741824.0), T.tertiary);
        infoCard(c, "insights", L"System", { {L"Processes", std::to_wstring(S.procs)}, {L"Threads", std::to_wstring(S.thrds)}, {L"Handles", std::to_wstring(S.handles)}, {L"Installed", fmtBytes(S.ramTotalGB * 1073741824.0)} }, T.accent);
    } else if (id == "sto") {
        swprintf(bf, 96, L"%.0f%%", S.diskAct); graphCard(a, "hard_drive", L"Disk activity", bf, L"System drive", S.hDisk, 100, T.accent, S.diskAct / 100.f);
        swprintf(bf, 96, L"%.0f%%", S.diskPct);
        ringCard(b, "storage", L"System drive", S.diskPct / 100.f, bf, fmtBytes(S.diskFreeGB * 1073741824.0) + L" free of " + fmtBytes(S.diskTotalGB * 1073741824.0), T.tertiary);
        infoCard(c, "cleaning_services", L"Space", { {L"Free", fmtBytes(S.diskFreeGB * 1073741824.0)}, {L"Total", fmtBytes(S.diskTotalGB * 1073741824.0)}, {L"Used", fmtBytes((S.diskTotalGB - S.diskFreeGB) * 1073741824.0)} }, T.accent);
    } else if (id == "net") {
        netCard(a);
        graphCard(b, "download", L"Download", fmtBytes(S.netDown) + L"/s", L"Last 90 seconds", S.hDown, S.hDown.maxv() * 1.15f + 1024, T.accent);
        graphCard(c, "upload", L"Upload", fmtBytes(S.netUp) + L"/s", L"Last 90 seconds", S.hUp, S.hUp.maxv() * 1.15f + 1024, T.tertiary);
    } else if (id == "game") {
        swprintf(bf, 96, L"%.0f%%", S.cpu); graphCard(a, "memory", L"CPU", bf, L"Load while you play", S.hCpu, 100, T.accent, S.cpu / 100.f);
        swprintf(bf, 96, L"%.0f%%", S.gpu); graphCard(b, "developer_board", L"GPU", bf, L"Load while you play", S.hGpu, 100, T.tertiary, S.gpu / 100.f);
        int t = g_cat.find("cpu.timerres"); std::wstring tr = t >= 0 && g_cat.tweaks[t].state > 0 ? g_cat.tweaks[t].opts[g_cat.tweaks[t].state].label : L"Windows default";
        int gm = g_cat.find("game.mode"), dvr = g_cat.find("game.dvr"), fso = g_cat.find("game.fso");
        auto onoff = [](int i, const wchar_t* a2, const wchar_t* b2) { return i >= 0 && g_cat.tweaks[i].state == 1 ? std::wstring(a2) : std::wstring(b2); };
        infoCard(c, "sports_esports", L"Game setup", { {L"Timer resolution", tr}, {L"Game Mode", onoff(gm, L"On", L"Windows default")}, {L"Background recording", onoff(dvr, L"Off", L"On")}, {L"Fullscreen optimizations", onoff(fso, L"Off", L"On")} }, T.accent);
    } else if (id == "apps") {
        int total = 0, on = appliedCount(g_cat.tabs[tab], &total);
        swprintf(bf, 96, L"%d / %d", on, total);
        ringCard(a, g_cat.tabs[tab].icon.c_str(), L"Apps tweaks", total ? (float)on / total : 0, bf, L"Applied in this category. Each one can be switched back on its own.", T.accent);
        servicesCard(b);
        infoCard(c, "computer", L"This PC", { {L"Windows build", std::to_wstring(g_eng.mc.build)}, {L"Type", g_eng.mc.laptop ? L"Laptop" : L"Desktop"}, {L"Memory", fmtBytes(S.ramTotalGB * 1073741824.0)}, {L"Uptime", fmtDur(S.uptimeSec)} }, T.accent);
    } else {
        int total = 0, on = appliedCount(g_cat.tabs[tab], &total);
        swprintf(bf, 96, L"%d / %d", on, total);
        ringCard(a, g_cat.tabs[tab].icon.c_str(), g_cat.tabs[tab].title + L" tweaks", total ? (float)on / total : 0, bf, L"Applied in this category. Each one can be switched back on its own.", T.accent);
        int sb = 0; for (auto& s : g_cat.tabs[tab].subs) sb += !s.tweaks.empty();
        std::vector<float> per; std::vector<std::wstring> lab;
        for (auto& s : g_cat.tabs[tab].subs) { if (s.tweaks.empty()) continue; int n = 0, m = 0; for (int i : s.tweaks) { if (g_cat.tweaks[i].kind == TK_ACTION) continue; n++; m += tweakOn(g_cat.tweaks[i]); } per.push_back(n ? 100.f * m / n : 0); std::wstring l = s.title; size_t sp = l.find_first_of(L" &"); if (sp != std::wstring::npos) l = l.substr(0, sp); lab.push_back(l); if (per.size() >= 8) break; }
        barsCard(b, "bar_chart", L"By section", per, lab, (int)per.size() < sb ? L"First " + std::to_wstring(per.size()) + L" of " + std::to_wstring(sb) + L" sections" : std::to_wstring(sb) + L" sections", T.tertiary);
        infoCard(c, "computer", L"This PC", { {L"Windows build", std::to_wstring(g_eng.mc.build)}, {L"Type", g_eng.mc.laptop ? L"Laptop" : L"Desktop"}, {L"Memory", fmtBytes(S.ramTotalGB * 1073741824.0)}, {L"Uptime", fmtDur(S.uptimeSec)}, {L"Virtual machine", g_eng.mc.vm ? L"Yes" : L"No"} }, T.accent);
    }
}

static std::wstring greeting() {
    SYSTEMTIME st; GetLocalTime(&st);
    return st.wHour < 5 ? L"Good night" : st.wHour < 12 ? L"Good morning" : st.wHour < 18 ? L"Good afternoon" : L"Good evening";
}
static std::wstring agoText(long long t) {
    if (t <= 0) return L"Never";
    long long d = (long long)time(nullptr) - t;
    if (d < 90) return L"Just now";
    if (d < 3600) return std::to_wstring(d / 60) + L" min ago";
    if (d < 86400) return std::to_wstring(d / 3600) + L" h ago";
    return std::to_wstring(d / 86400) + (d / 86400 == 1 ? L" day ago" : L" days ago");
}
static float infoChip(float x, float y, float h, const char* ic, const std::wstring& s, const Col& fg, const Col& bg, float maxX) {
    float w = g.measure(s, 12.5f, TW_MED).width + 44;
    if (x + w > maxX) return x;
    Rect cr{ x, y, w, h };
    g.fill(cr, bg, h / 2);
    g.icon(ic, cr.x + 18, cr.y + h / 2, 16, fg);
    g.textR(s, { cr.x + 32, cr.y, w - 36, h }, 12.5f, fg, TW_MED, AL_LEFT, true);
    return x + w + 8;
}
static void helloCard(const Rect& r) {
    const Theme& T = app.th;
    surfaceCard(r, 32, T.primaryC);
    g.pushLayer(1.f, &r, 32);
    float t = ui.time;
    ui.blob(r.r() - 110, r.y + 50, 110 + sinf(t * 0.7f) * 6, 1, withA(T.onPrimaryC, 0.07f), t * 0.10f);
    ui.blob(r.r() - 290, r.b() - 2, 62, 3, withA(T.onPrimaryC, 0.06f), -t * 0.16f);
    ui.blob(r.r() - 36, r.b() - 26, 40, 2, withA(T.onPrimaryC, 0.08f), t * 0.22f);
    g.popLayer();
    float ls = std::min(96.f, r.h - 36);
    if (g.logo) g.imageTint(g.logo, { r.r() - ls - 44, r.y + (r.h - ls) / 2, ls, ls }, withA(T.onPrimaryC, 0.22f));
    ID2D1Bitmap* av = avatarBitmap();
    float rad = std::min(38.f, r.h / 2 - 22);
    float ax = r.x + 30 + rad, ay = r.y + r.h / 2;
    if (av) { g.circle(ax, ay, rad + 4, withA(T.onPrimaryC, 0.15f)); g.imageCircle(av, ax, ay, rad); }
    else iconBlob(ax, ay, rad * 2 + 4, "person", T.onPrimaryC, T.primaryC, 1, t * 0.2f);
    float tx = ax + rad + 24;
    std::wstring name;
    { std::lock_guard<std::mutex> lk(g_dc.mx); name = u8to16(!g_dc.display.empty() ? g_dc.display : g_dc.username); }
    if (name.empty()) name = S.userName;
    float blockH = 16 + 38 + 12 + 30, by = r.y + (r.h - blockH) / 2;
    g.text(L"WELCOME BACK", tx, by, 11.5f, withA(T.onPrimaryC, 0.7f), TW_BOLD);
    g.text(greeting() + L", " + name, tx, by + 16, 28, T.onPrimaryC, TW_BOLD, r.r() - tx - ls - 60);
    float cx = tx, cy = by + 16 + 38 + 12, mx = r.r() - ls - 60;
    Col cb = withA(T.onPrimaryC, 0.10f);
    cx = infoChip(cx, cy, 30, "window", L"Windows build " + std::to_wstring(g_eng.mc.build), T.onPrimaryC, cb, mx);
    cx = infoChip(cx, cy, 30, g_eng.mc.laptop ? "laptop_windows" : "desktop_windows", g_eng.mc.laptop ? L"Laptop" : L"Desktop", T.onPrimaryC, cb, mx);
    cx = infoChip(cx, cy, 30, "memory_alt", fmtBytes(S.ramTotalGB * 1073741824.0) + L" RAM", T.onPrimaryC, cb, mx);
    cx = infoChip(cx, cy, 30, "schedule", L"Up " + fmtDur(S.uptimeSec), T.onPrimaryC, cb, mx);
}

static void miniGraph(const Rect& r, const char* icon, const std::wstring& title, const std::wstring& big, const std::wstring& sub, const Hist& h, float maxv, const Col& c) {
    const Theme& T = app.th;
    surfaceCard(r, 24, T.card);
    iconBlob(r.x + 34, r.y + 34, 38, icon, withA(c, app.dark ? 0.18f : 0.16f), c, 1, ui.time * 0.15f);
    g.textR(title, { r.x + 62, r.y + 18, r.w - 170, 32 }, 14.5f, T.textDim, TW_MED);
    g.textR(big, { r.r() - 140, r.y + 14, 120, 40 }, 28, T.text, TW_BOLD, AL_RIGHT);
    g.textR(sub, { r.x + 20, r.y + 58, r.w - 40, 20 }, 12.5f, T.textFaint, TW_MED);
    float top = r.y + 88;
    if (r.b() - top < 30) return;
    Rect gr{ r.x + 1, top, r.w - 2, r.b() - top - 2 };
    g.pushLayer(1.f, &r, 24);
    int n = std::max(2, h.n);
    ui.areaGraph(gr, h.v + (Hist::N - n), n, maxv, c, true);
    g.popLayer();
}

static bool actionTile(const Rect& r, u64 id, const char* icon, const Col& bg, const Col& fg, int shape, const std::wstring& title, const std::wstring& sub, const std::wstring& btn, const char* btnIcon, bool filled, bool enabled = true) {
    const Theme& T = app.th;
    surfaceCard(r, 24, T.card);
    iconBlob(r.x + 36, r.y + 36, 44, icon, bg, fg, shape, ui.time * 0.15f);
    g.textR(title, { r.x + 70, r.y + 14, r.w - 84, 26 }, 15.5f, T.text, TW_BOLD);
    g.textR(sub, { r.x + 70, r.y + 38, r.w - 84, 20 }, 12.5f, T.textFaint, TW_MED);
    float bw = std::min(r.w - 40, g.measure(btn, 13.5f, TW_MED).width + 64);
    Rect b{ r.x + 20, r.b() - 56, bw, 40 };
    return filled ? ui.btnFilled(id, b, btn, btnIcon, enabled) : ui.btnTonal(id, b, btn, btnIcon, enabled);
}

static void optimizationTile(const Rect& r) {
    const Theme& T = app.th;
    surfaceCard(r, 24, T.primaryC);
    int on = 0, total = 0; for (auto& tb : g_cat.tabs) { int tt = 0; on += appliedCount(tb, &tt); total += tt; }
    auto rec = recommendedFor(-1, false);
    float rad = std::min(30.f, (r.h - 80) / 2);
    float cx = r.r() - rad - 26, cy = r.y + rad + 22;
    float v = ui.anim(HID("homeOptRing"), total ? (float)on / total : 0.f, 6);
    ui.wavyRing(cx, cy, rad, 6, v, T.onPrimaryC, withA(T.onPrimaryC, 0.18f), ui.time * 1.8f, 2.2f, 9);
    g.textR(std::to_wstring(on), { cx - rad, cy - 14, rad * 2, 28 }, 17, T.onPrimaryC, TW_BOLD, AL_CENTER);
    g.textR(L"Safe tweaks", { r.x + 22, r.y + 14, r.w - rad * 2 - 60, 26 }, 15.5f, T.onPrimaryC, TW_BOLD);
    g.textR(std::to_wstring(on) + (on == 1 ? L" tweak applied" : L" tweaks applied"), { r.x + 22, r.y + 38, r.w - rad * 2 - 60, 20 }, 12.5f, withA(T.onPrimaryC, 0.8f), TW_MED);
    Col sav = ui.th.accent, savOn = ui.th.onAccent;
    ui.th.accent = T.onPrimaryC; ui.th.onAccent = T.primaryC;
    std::wstring lbl = rec.empty() ? L"All set" : L"See safe tweaks";
    float bw = std::min(r.w - 40, g.measure(lbl, 13.5f, TW_MED).width + 64);
    if (ui.btnFilled(HID("homeOpt"), { r.x + 20, r.b() - 56, bw, 40 }, lbl, rec.empty() ? "check" : "view_list", !rec.empty()))
        app.openPlan(L"Safe tweaks", rec);
    ui.th.accent = sav; ui.th.onAccent = savOn;
}

void drawHome(const Rect& area) {
    const Theme& T = app.th;
    Rect col = contentColumn(area);
    app.scHome.begin(area);
    float top = area.y + 20 - app.scHome.off;
    float avail = area.h - 40;
    int cols = columnsFor(col.w);
    Grid gr{ col.x, col.w, 16, cols };
    int acols = col.w >= 1060 ? 4 : col.w >= 640 ? 2 : 1;
    Grid ag{ col.x, col.w, 16, acols };
    float helloH = clampf(avail * 0.21f, 128, 152);
    float actH = 136;
    int gRows = (3 + cols - 1) / cols, aRows = (4 + acols - 1) / acols;
    float graphH = (avail - helloH - actH * aRows - 16 * (aRows + 1) - 16 * (gRows - 1)) / gRows;
    graphH = std::max(graphH, 150.f);
    float y = top;

    helloCard({ col.x, y, col.w, helloH }); y += helloH + 16;

    wchar_t bf[64];
    Rect g0 = gr.cell(0, y, graphH), g1 = gr.cell(1, y + (cols < 2 ? graphH + 16 : 0), graphH), g2 = gr.cell(2, y + (cols < 3 ? (cols == 2 ? graphH + 16 : (graphH + 16) * 2) : 0), graphH);
    if (cols == 2) g2 = { col.x, y + graphH + 16, col.w, graphH };
    if (cols == 1) { g1 = { col.x, y + graphH + 16, col.w, graphH }; g2 = { col.x, y + (graphH + 16) * 2, col.w, graphH }; }
    auto scale = [](const Hist& h, const char* key) { return ui.anim(HID("homeScale", (u64)(uintptr_t)key), clampf(h.maxv() * 1.35f, 25.f, 100.f), 3); };
    swprintf(bf, 64, L"%.0f%%", S.cpu); miniGraph(g0, "memory", L"Processor", bf, S.cpuName, S.hCpu, scale(S.hCpu, "c"), T.accent);
    swprintf(bf, 64, L"%.0f%%", S.gpu); miniGraph(g1, "developer_board", L"Graphics", bf, S.gpuName, S.hGpu, scale(S.hGpu, "g"), T.tertiary);
    swprintf(bf, 64, L"%.0f%%", S.ram); miniGraph(g2, "memory_alt", L"Memory", bf, fmtBytes(S.ramUsedGB * 1073741824.0) + L" of " + fmtBytes(S.ramTotalGB * 1073741824.0), S.hRam, 100, T.accent);
    y = std::max(g0.b(), std::max(g1.b(), g2.b())) + 16;

    Rect a0 = ag.cell(0, y, actH), a1 = ag.cell(1, y + (acols == 1 ? actH + 16 : 0), actH);
    Rect a2 = ag.cell(2, y + (acols == 2 ? actH + 16 : acols == 1 ? (actH + 16) * 2 : 0), actH);
    Rect a3 = ag.cell(3, y + (acols == 2 ? actH + 16 : acols == 1 ? (actH + 16) * 3 : 0), actH);
    if (acols == 2) { a2 = ag.cell(0, y + actH + 16, actH); a3 = ag.cell(1, y + actH + 16, actH); }
    if (acols == 1) { a1 = { col.x, y + actH + 16, col.w, actH }; a2 = { col.x, y + (actH + 16) * 2, col.w, actH }; a3 = { col.x, y + (actH + 16) * 3, col.w, actH }; }

    optimizationTile(a0);

    bool busy = g_eng.running;
    if (actionTile(a1, HID("homeRp"), "backup", T.tertiaryC, T.onTertiaryC, 2, L"Restore point", L"Last: " + agoText(app.pf.lastRestore), L"Create", "backup", false, !busy))
        runTask(L"Restore point", [](SayFn say) {
            bool ok = createRestorePointNow(L"cr1mix app restore point");
            if (ok) { app.pf.lastRestore = (long long)time(nullptr); app.pf.save(); say(L"Restore point created", 1); }
            else say(L"Windows did not create a restore point. System Restore may be off for this drive.", 2);
        });
    {
        float tx = a1.r() - 70, ty = a1.b() - 52;
        g.textR(L"Auto", { tx - 64, ty, 58, 32 }, 12.5f, T.textDim, TW_MED, AL_RIGHT);
        if (ui.toggle(HID("rpToggle"), tx, ty, app.pf.restorePoint)) { app.pf.restorePoint = !app.pf.restorePoint; g_eng.restorePoint = app.pf.restorePoint; app.pf.save(); app.showToast(app.pf.restorePoint ? L"A restore point is made before every change" : L"Automatic restore points off"); }
        if (ui.mouseIn({ tx - 64, ty, 120, 32 })) ui.tooltip = L"Create a restore point automatically before every change";
    }

    if (actionTile(a2, HID("homeBk"), "save", T.accentDim, T.accent, 4, L"Registry backup", L"Last: " + agoText(app.pf.lastBackup), L"Back up", "save", false, !busy))
        runTask(L"Registry backup", [](SayFn say) {
            std::wstring dir;
            if (backupRegistry(dir, say)) { app.pf.lastBackup = (long long)time(nullptr); app.pf.backupDir = dir; app.pf.save(); say(L"Saved in " + dir, 1); }
        });
    if (!app.pf.backupDir.empty() && dirExists(app.pf.backupDir)) {
        if (ui.iconBtn(HID("homeBkOpen"), a2.r() - 40, a2.b() - 36, "folder_open", 40)) ShellExecuteW(nullptr, L"open", app.pf.backupDir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        if (ui.mouseIn({ a2.r() - 60, a2.b() - 56, 40, 40 })) ui.tooltip = L"Open the backup folder";
    }

    if (actionTile(a3, HID("homeQs"), "magic_button", T.primaryC, T.onPrimaryC, 3, L"Quick setup", L"Pick what to keep, then apply", L"Start", "arrow_forward", true))
        app.go(P_SETUP);

    y = std::max(a0.b(), std::max(a1.b(), std::max(a2.b(), a3.b()))) + 20;
    app.scHome.end(area, y + app.scHome.off - area.y);
}
