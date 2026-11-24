#include "app.h"
#include "discord.h"
#include "update.h"
#include <shellscalingapi.h>

App app;
Stats S;

void addNoDrag(const Rect& r) { app.noDrag.push_back(r); }

static std::wstring prefsFile() { return g_eng.dataDir + L"\\settings.ini"; }
void Prefs::load() {
    std::string s = readFileBytes(prefsFile()); size_t p = 0;
    while (p < s.size()) {
        size_t e = s.find('\n', p); if (e == std::string::npos) e = s.size();
        std::string l = s.substr(p, e - p); p = e + 1;
        size_t q = l.find('='); if (q == std::string::npos) continue;
        std::string k = l.substr(0, q), v = l.substr(q + 1);
        if (k == "seed") seed = (int)strtoul(v.c_str(), nullptr, 16);
        else if (k == "theme") theme = atoi(v.c_str());
        else if (k == "transparency") transparency = (float)atof(v.c_str());
        else if (k == "motion") motion = v == "1";
        else if (k == "restore") restorePoint = v == "1";
        else if (k == "offline") offline = v == "1";
        else if (k == "lastrp") lastRestore = _atoi64(v.c_str());
        else if (k == "lastbk") lastBackup = _atoi64(v.c_str());
        else if (k == "bkdir") backupDir = u8to16(v);
    }
    transparency = clampf(transparency, 0, 0.35f);
}
void Prefs::save() {
    char b[512];
    snprintf(b, sizeof(b), "seed=%06X\ntheme=%d\ntransparency=%.3f\nmotion=%d\nrestore=%d\noffline=%d\nlastrp=%lld\nlastbk=%lld\n", seed, theme, transparency, motion ? 1 : 0, restorePoint ? 1 : 0, offline ? 1 : 0, lastRestore, lastBackup);
    writeFileUtf8(prefsFile(), std::string(b) + "bkdir=" + u16to8(backupDir) + "\n");
}

static bool windowsDark() { DWORD v = regDword(regParse(L"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize"), L"AppsUseLightTheme", 0); return v == 0; }
void App::applyTheme() {
    dark = pf.theme == 0 ? true : pf.theme == 1 ? false : windowsDark();
    th = makeTheme(dark, pf.seed, pf.transparency);
    ui.th = th;
    if (hwnd) { BOOL d = dark; DwmSetWindowAttribute(hwnd, 20, &d, sizeof(d)); }
    g_eng.restorePoint = pf.restorePoint;
}
void App::go(Page p, int t) {
    if (t >= 0) tab = t;
    page = p; detail = -1; planOpen = false; search.clear();
}
void App::confirm(const std::wstring& title, const std::wstring& body, const std::wstring& ok, int risk, std::function<void()> fn) {
    confirmOpen = true; confirmTitle = title; confirmBody = body; confirmOk = ok; confirmRisk = risk; confirmFn = fn;
}

const char* tabIcon(const std::string& id) { for (auto& t : g_cat.tabs) if (t.id == id) return t.icon.c_str(); return "tune"; }
Col riskColor(int r) { return r >= 3 ? ui.th.danger : r == 2 ? ui.th.warn : ui.th.good; }
const wchar_t* riskName(int r) { return r >= 3 ? L"Advanced" : r == 2 ? L"Caution" : L"Safe"; }
std::wstring fmtBytes(double b) {
    const wchar_t* u[] = { L"B", L"KB", L"MB", L"GB", L"TB" }; int i = 0; while (b >= 1024 && i < 4) { b /= 1024; i++; }
    wchar_t s[32]; swprintf(s, 32, b < 10 && i > 0 ? L"%.1f %s" : L"%.0f %s", b, u[i]); return s;
}
std::wstring fmtDur(u64 s) {
    u64 d = s / 86400, h = (s % 86400) / 3600, m = (s % 3600) / 60; wchar_t b[48];
    if (d) swprintf(b, 48, L"%llud %lluh", d, h); else if (h) swprintf(b, 48, L"%lluh %llum", h, m); else swprintf(b, 48, L"%llum", m);
    return b;
}
bool tweakOn(const Tweak& t) {
    if (t.kind == TK_TOGGLE) return t.state == 1;
    if (t.kind == TK_CHOICE) return t.state >= 0 && g_eng.journaled(t);
    if (t.kind == TK_PICK) { for (auto p : t.picked) if (p) return true; return false; }
    return false;
}
bool tweakVisible(const Tweak& t) { return g_eng.hardwareFits(t); }
int appliedCount(const Tab& tb, int* total) {
    int n = 0, all = 0;
    for (auto& s : tb.subs) for (int i : s.tweaks) { const Tweak& t = g_cat.tweaks[i]; if (t.kind == TK_ACTION || !tweakVisible(t)) continue; all++; if (tweakOn(t)) n++; }
    if (total) *total = all; return n;
}

static void submit(const std::wstring& title, const std::vector<JobItem>& items) {
    Job j; j.title = title; j.items = items;
    for (auto& it : items) g_cat.tweaks[it.tweak].busy = true;
    g_eng.submit(j);
}
static bool gate(int ti, std::function<void()> go) {
    Tweak& t = g_cat.tweaks[ti];
    std::wstring why;
    if (!g_eng.hardwareFits(t)) { g_eng.requirementsMet(t, &why); app.showToast(why + L", so this tweak is not available", 2); return false; }
    if (!g_eng.requirementsMet(t, &why)) {
        app.confirm(t.name, why + L". It may not help on this PC. Apply anyway?", L"Apply anyway", 2, go);
        return false;
    }
    if (t.risk >= 3) {
        app.confirm(t.name, t.desc + L"\n\nThis is an advanced change. Everything cr1mix app changes is saved, so you can switch it back later.", L"Apply", 3, go);
        return false;
    }
    go(); return true;
}
void requestToggle(int ti, bool on) {
    Tweak& t = g_cat.tweaks[ti]; if (t.busy || g_eng.running) { app.showToast(L"Wait for the current change to finish", 2); return; }
    auto go = [ti, on] { submit(g_cat.tweaks[ti].name, { JobItem{ ti, on ? J_APPLY : J_REVERT, -1, {} } }); };
    if (!on) { go(); return; }
    gate(ti, go);
}
void requestChoice(int ti, int opt) {
    Tweak& t = g_cat.tweaks[ti]; if (t.busy || g_eng.running) { app.showToast(L"Wait for the current change to finish", 2); return; }
    gate(ti, [ti, opt] { submit(g_cat.tweaks[ti].name + L": " + g_cat.tweaks[ti].opts[opt].label, { JobItem{ ti, J_CHOOSE, opt, {} } }); });
}
void requestPick(int ti, const std::vector<int>& items) {
    if (items.empty() || g_eng.running) return;
    Tweak& t = g_cat.tweaks[ti];
    std::wstring list; for (int k : items) list += L"\n  •  " + t.opts[k].label;
    app.confirm(t.name, L"These will be removed for every user:" + list + L"\n\nApps can be installed again from the Microsoft Store.", L"Remove", 2,
        [ti, items] { submit(g_cat.tweaks[ti].name, { JobItem{ ti, J_PICK, -1, items } }); });
}
void requestRun(int ti, int opt) {
    Tweak& t = g_cat.tweaks[ti]; if (g_eng.running) { app.showToast(L"Wait for the current change to finish", 2); return; }
    if (!t.opts.empty() && opt < 0) { app.detail = ti; return; }
    std::wstring title = t.name + (opt >= 0 ? L": " + t.opts[opt].label : L"");
    auto go = [ti, opt, title] { submit(title, { JobItem{ ti, J_RUN, opt, {} } }); };
    if (t.risk >= 2) app.confirm(t.name, t.desc, L"Run", t.risk, go); else go();
}
void runTask(const std::wstring& title, std::function<void(SayFn)> fn) {
    if (g_eng.running) { app.showToast(L"Wait for the current change to finish", 2); return; }
    Job j; j.title = title; j.fn = fn; g_eng.submit(j);
}
void applyMany(const std::wstring& title, const std::vector<JobItem>& items) {
    if (items.empty()) { app.showToast(L"Everything here is already applied", 1); return; }
    if (g_eng.running) { app.showToast(L"Wait for the current change to finish", 2); return; }
    int adv = 0; for (auto& it : items) if (g_cat.tweaks[it.tweak].risk >= 3) adv++;
    std::wstring body = std::to_wstring(items.size()) + L" tweak" + (items.size() == 1 ? L"" : L"s") + L" will be applied.";
    if (adv) body += L" " + std::to_wstring(adv) + L" of them are advanced.";
    body += L"\nEvery change is saved, so each one can be switched back on its own later.";
    if (app.pf.restorePoint) body += L"\nA restore point is created first.";
    app.confirm(title, body, L"Apply", adv ? 3 : 1, [title, items] { submit(title, items); });
}

void runReboot() {
    enablePrivilege(SE_SHUTDOWN_NAME);
    ExitWindowsEx(EWX_REBOOT | EWX_RESTARTAPPS, SHTDN_REASON_MAJOR_OTHER | SHTDN_REASON_FLAG_PLANNED);
}

static ID2D1Bitmap* s_avatar = nullptr; static size_t s_avatarLen = 0;
ID2D1Bitmap* avatarBitmap() {
    if (g_dc.avatarReady) {
        std::lock_guard<std::mutex> lk(g_dc.mx);
        if (g_dc.avatarPng.size() != s_avatarLen && !g_dc.avatarPng.empty()) { Rel(s_avatar); s_avatar = g.bitmapFromMemory(g_dc.avatarPng.data(), g_dc.avatarPng.size()); s_avatarLen = g_dc.avatarPng.size(); }
    }
    return s_avatar;
}

static LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp);
static void frame(bool force);
static void drawFrame();
static double s_lastFrame = 0, s_lastInput = 0;

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE, LPWSTR cmd, int) {
    int argc = 0; LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; i + 1 < argc; i++) {
        if (!wcscmp(argv[i], L"--timer")) return Engine::runTimerMode(_wtoi(argv[i + 1]));
        if (!wcscmp(argv[i], L"--clean")) return Engine::runCleanMode(argv[i + 1]);
    }
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    for (int i = 1; i < argc; i++) if (!wcscmp(argv[i], L"--uninstall")) return runUninstall();
    bool setup = setupMode();
    if (setup) for (int i = 1; i + 1 < argc; i++) if (!wcscmp(argv[i], L"--update")) setUpdateTarget(argv[i + 1]);
    HANDLE single = setup ? nullptr : CreateMutexW(nullptr, TRUE, L"cr1mix.single");
    if (!setup && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND hw = FindWindowW(L"cr1mixWnd", nullptr); if (hw) { ShowWindow(hw, SW_RESTORE); SetForegroundWindow(hw); }
        return 0;
    }
    (void)single;
    if (!setup) { g_upd.cleanup(); refreshInstallInfo(); }
    if (!g_cat.load()) { MessageBoxW(nullptr, L"The built-in tweak catalog could not be loaded.", L"cr1mix app", MB_ICONERROR); return 1; }

    g_eng.start();
    app.pf.load();
    g_dc.init(g_eng.dataDir);

    WNDCLASSEXW wc{ sizeof(wc) };
    wc.style = CS_DBLCLKS; wc.lpfnWndProc = WndProc; wc.hInstance = hi; wc.lpszClassName = L"cr1mixWnd";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.hIcon = LoadIconW(hi, MAKEINTRESOURCEW(1)); wc.hIconSm = wc.hIcon;
    RegisterClassExW(&wc);

    POINT cur; GetCursorPos(&cur);
    HMONITOR mon = MonitorFromPoint(cur, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi{ sizeof(mi) }; GetMonitorInfoW(mon, &mi);
    UINT dx = 96, dy = 96; GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &dx, &dy);
    float sc = dx / 96.f;
    int aw = mi.rcWork.right - mi.rcWork.left, ah = mi.rcWork.bottom - mi.rcWork.top;
    int ww = std::min((int)(1400 * sc), (int)(aw * 0.92f)), wh = std::min((int)(880 * sc), (int)(ah * 0.92f));
    int wx = mi.rcWork.left + (aw - ww) / 2, wy = mi.rcWork.top + (ah - wh) / 2;

    app.hwnd = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP, L"cr1mixWnd", L"cr1mix app",
        WS_POPUP | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_CLIPCHILDREN, wx, wy, ww, wh, nullptr, nullptr, hi, nullptr);
    app.applyTheme();
    int corner = 2; DwmSetWindowAttribute(app.hwnd, 33, &corner, sizeof(corner));
    int backdrop = 3; DwmSetWindowAttribute(app.hwnd, 38, &backdrop, sizeof(backdrop));
    MARGINS m{ -1, -1, -1, -1 }; DwmExtendFrameIntoClientArea(app.hwnd, &m);
    SetWindowPos(app.hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    if (!g.init(app.hwnd)) { MessageBoxW(nullptr, L"Direct2D / DirectComposition could not start.", L"cr1mix app", MB_ICONERROR); return 1; }

    g_stats.start();
    app.startTime = nowSec();
    ShowWindow(app.hwnd, SW_SHOW);
    if (!setup) g_upd.check();
    SetTimer(app.hwnd, 1, 8, nullptr);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    return 0;
}

static void frame(bool force) {
    double t = nowSec();
    bool busy = force || ui.anyAnim || g_eng.running || (t - s_lastInput) < 0.6 || app.page == P_SPLASH;
    if (!busy && t - s_lastFrame < 0.083) return;
    float dt = (float)(s_lastFrame ? t - s_lastFrame : 0.016); s_lastFrame = t;
    if (!app.pf.motion) dt = 1.f;
    g_stats.snapshot(S);
    ui.th = app.th; ui.beginFrame(dt);
    app.noDrag.clear();
    if (!g.begin()) return;
    drawFrame();
    g.end();
    if (ui.wantHand) SetCursor(LoadCursor(nullptr, IDC_HAND));
    ui.endFrame();
}

static LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    auto mpos = [&](LPARAM l) { ui.in.mx = GET_X_LPARAM(l) / g.scale; ui.in.my = GET_Y_LPARAM(l) / g.scale; s_lastInput = nowSec(); };
    switch (msg) {
    case WM_TIMER: frame(false); return 0;
    case WM_MOUSEMOVE: {
        mpos(lp);
        TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, h, 0 }; TrackMouseEvent(&tme);
        return 0;
    }
    case WM_MOUSELEAVE: ui.in.mx = ui.in.my = -1000; return 0;
    case WM_LBUTTONDOWN: SetCapture(h); mpos(lp); ui.in.down = true; ui.in.pressed = true; frame(true); return 0;
    case WM_LBUTTONUP: ReleaseCapture(); mpos(lp); ui.in.down = false; ui.in.released = true; frame(true); return 0;
    case WM_LBUTTONDBLCLK: mpos(lp); ui.in.dbl = true; ui.in.pressed = true; ui.in.down = true; return 0;
    case WM_RBUTTONDOWN: ui.in.rpressed = true; return 0;
    case WM_MOUSEWHEEL: ui.in.wheel += (float)GET_WHEEL_DELTA_WPARAM(wp) / 120.f * 90.f; s_lastInput = nowSec(); return 0;
    case WM_CHAR: ui.in.chars.push_back((wchar_t)wp); s_lastInput = nowSec(); return 0;
    case WM_KEYDOWN:
        ui.in.keys.push_back((int)wp); s_lastInput = nowSec();
        if (wp == VK_ESCAPE) { if (app.confirmOpen) app.confirmOpen = false; else if (app.detail >= 0) app.detail = -1; else if (app.planOpen) app.planOpen = false; else if (app.sheet) app.sheet = false; }
        return 0;
    case WM_SETCURSOR: if (LOWORD(lp) == HTCLIENT) { SetCursor(LoadCursor(nullptr, IDC_ARROW)); return TRUE; } break;
    case WM_DPICHANGED: { RECT* r = (RECT*)lp; SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE); g.resize(); frame(true); return 0; }
    case WM_SIZE: if (g.dc) { g.resize(); frame(true); } return 0;
    case WM_SETTINGCHANGE: if (app.pf.theme == 2) app.applyTheme(); break;
    case WM_GETMINMAXINFO: { MINMAXINFO* mi = (MINMAXINFO*)lp; float s = GetDpiForWindow(h) / 96.f; mi->ptMinTrackSize.x = (LONG)(1000 * s); mi->ptMinTrackSize.y = (LONG)(660 * s); return 0; }
    case WM_ERASEBKGND: return 1;
    case WM_NCCALCSIZE:
        if (wp) {
            if (IsZoomed(h)) {
                NCCALCSIZE_PARAMS* p = (NCCALCSIZE_PARAMS*)lp;
                UINT dpi = GetDpiForWindow(h);
                int fx = GetSystemMetricsForDpi(SM_CXFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
                int fy = GetSystemMetricsForDpi(SM_CYFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
                p->rgrc[0].left += fx; p->rgrc[0].right -= fx; p->rgrc[0].top += fy; p->rgrc[0].bottom -= fy;
            }
            return 0;
        }
        break;
    case WM_NCHITTEST: {
        POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) }; ScreenToClient(h, &pt);
        float x = pt.x / g.scale, y = pt.y / g.scale, B = 7;
        if (!IsZoomed(h)) {
            bool l = x < B, r = x > g.W - B, t = y < B, b = y > g.H - B;
            if (t && l) return HTTOPLEFT; if (t && r) return HTTOPRIGHT; if (b && l) return HTBOTTOMLEFT; if (b && r) return HTBOTTOMRIGHT;
            if (l) return HTLEFT; if (r) return HTRIGHT; if (t) return HTTOP; if (b) return HTBOTTOM;
        }
        if (y < app.barH) { for (auto& nd : app.noDrag) if (nd.hit(x, y)) return HTCLIENT; return HTCAPTION; }
        return HTCLIENT;
    }
    case WM_CLOSE: if (g_eng.running) { app.showToast(L"Please wait, changes are still being applied", 2); return 0; } break;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

static void windowControls() {
    float w = 46, hh = 34, x = g.W - w * 3;
    addNoDrag({ x, 0, w * 3, hh });
    auto one = [&](float bx, const char* ic, bool close) {
        Rect r{ bx, 0, w, hh }; u64 id = HID(ic, 77);
        bool c = ui.behave(id, r);
        float a = ui.anim(id ^ 1, ui.hot == id ? 1.f : 0.f, 20);
        if (a > 0.01f) g.fill(r, close ? withA(rgb(0xD83B3B), 0.92f * a) : withA(ui.th.text, 0.08f * a), 0);
        g.icon(ic, r.x + w / 2, r.y + hh / 2, 17, close && a > 0.5f ? rgb(0xFFFFFF) : ui.th.textDim);
        return c;
    };
    if (one(x, "remove", false)) ShowWindow(app.hwnd, SW_MINIMIZE);
    if (one(x + w, IsZoomed(app.hwnd) ? "filter_none" : "crop_square", false)) ShowWindow(app.hwnd, IsZoomed(app.hwnd) ? SW_RESTORE : SW_MAXIMIZE);
    if (one(x + w * 2, "close", true)) PostMessageW(app.hwnd, WM_CLOSE, 0, 0);
}

static void drawSplash() {
    float t = (float)(nowSec() - app.startTime);
    float cx = g.W / 2, cy = g.H / 2 - 18;
    float in = easeOut(clampf(t / 0.55f, 0, 1));
    float box = lerpf(130, 176, in);
    Rect r{ cx - box / 2, cy - box / 2, box, box };
    g.opacity = clampf(t / 0.35f, 0, 1);
    g.shadow(r, 44, 0.9f);
    g.fill(r, app.th.surf2, 44);
    g.stroke(r, withA(app.th.text, 0.06f), 44, 1);
    float lw = box * 0.54f * (1 + sinf(t * 2.6f) * 0.012f);
    if (g.logo) g.imageTint(g.logo, { cx - lw / 2, cy - lw / 2, lw, lw }, app.th.text);
    float prog = clampf((t - 0.25f) / 1.5f, 0, 1);
    ui.wavyRing(cx, cy, box * 0.66f, 4, easeInOut(prog), app.th.accent, app.th.track, t * 4, 2.4f, 12);
    float tw = g.measure(L"cr1mix app", 24, TW_BOLD).width;
    g.text(L"cr1mix app", cx - tw / 2, r.b() + 44, 24, app.th.text, TW_BOLD);
    const wchar_t* sub = g_eng.detecting ? L"Reading your current settings" : L"Ready";
    g.text(sub, cx - g.measure(sub, 13, TW_MED).width / 2, r.b() + 78, 13, app.th.textDim, TW_MED);
    g.opacity = 1; ui.anyAnim = true;
    if (t > 1.9f && !g_eng.detecting && !app.splashDone) {
        app.splashDone = true;
        app.go(setupMode() ? P_INSTALL : g_dc.loggedIn() ? P_HOME : P_LOGIN);
    }
}

static void drawLogin() {
    float cx = g.W / 2, cy = g.H / 2;
    float a = ui.anim(HID("loginIn"), 1, 9, 0);
    Rect card{ cx - 230, cy - 250 + (1 - a) * 18, 460, 470 };
    g.opacity = a;
    g.shadow(card, 36, 0.9f);
    g.fill(card, app.th.surf2, 36);
    g.pushLayer(1.f, &card, 36);
    ui.blob(card.r() - 40, card.y + 40, 80, 1, withA(app.th.primaryC, 0.9f), ui.time * 0.25f);
    ui.blob(card.x + 20, card.b() - 10, 54, 2, withA(app.th.tertiaryC, 0.8f), -ui.time * 0.2f);
    g.popLayer();
    float lw = 88;
    g.fill({ cx - 60, card.y + 54, 120, 120 }, app.th.surfHi, 40);
    if (g.logo) g.imageTint(g.logo, { cx - lw / 2, card.y + 54 + (120 - lw) / 2, lw, lw }, app.th.text);
    g.textR(L"Welcome to cr1mix app", { card.x, card.y + 196, card.w, 36 }, 26, app.th.text, TW_BOLD, AL_CENTER);
    g.textR(L"Sign in with Discord to show your profile on Home", { card.x, card.y + 236, card.w, 22 }, 14, app.th.textDim, TW_REG, AL_CENTER);
    int st = g_dc.state;
    Rect btn{ cx - 160, card.y + 300, 320, 56 };
    addNoDrag(btn);
    if (st == Discord::Waiting || st == Discord::Fetching) {
        g.fill(btn, app.th.surfHi, 28);
        ui.spinner(btn.x + 34, btn.y + 28, 10, 3, app.th.accent);
        g.text(st == Discord::Fetching ? L"Loading your profile…" : g_dc.via == 2 ? L"Finish signing in in your browser…" : L"Looking for the Discord app…", btn.x + 58, btn.y + 18, 14.5f, app.th.textDim, TW_MED);
        if (st == Discord::Waiting && g_dc.via == 2) { Rect cb{ cx - 50, btn.b() + 8, 100, 34 }; addNoDrag(cb); if (ui.btnText(HID("dccancel"), cb, L"Cancel")) g_dc.cancel(); }
    } else {
        u64 id = HID("dclogin");
        bool c = ui.behave(id, btn);
        float press = ui.anim(id ^ 7, ui.active == id && ui.in.down ? 1.f : 0.f, 22);
        float rad = lerpf(28, 16, press);
        g.fill(btn, rgb(0x5865F2), rad);
        ui.stateLayer(id, btn, rad, rgb(0xFFFFFF)); ui.drawRipples(id, btn, rad, rgb(0xFFFFFF));
        float tw = g.measure(L"Continue with Discord", 15.5f, TW_MED).width;
        float x0 = btn.x + (btn.w - tw - 32) / 2;
        g.icon("forum", x0 + 11, btn.y + 28, 22, rgb(0xFFFFFF));
        g.text(L"Continue with Discord", x0 + 32, btn.y + 18, 15.5f, rgb(0xFFFFFF), TW_MED);
        if (c) g_dc.login();
    }
    if (st == Discord::Error) g.textR(u8to16(g_dc.err), { card.x + 20, btn.b() + 8, card.w - 40, 40 }, 12.5f, app.th.danger, TW_REG, AL_CENTER, true, true);
    Rect skip{ cx - 110, card.b() - 74, 220, 44 }; addNoDrag(skip);
    if (ui.btnText(HID("offline"), skip, L"Continue without signing in")) { app.pf.offline = true; app.pf.save(); app.go(P_HOME); }
    g.opacity = 1;
    if (g_dc.loggedIn()) app.go(P_HOME);
}

static void topBar() {
    float lx = 22;
    float wm = g.measure(L"cr1mix app", 18, TW_BOLD).width;
    int navN = 1 + (int)g_cat.tabs.size();
    float minNav = navN * 44.f + 8;
    bool upd = g_upd.state == Updater::Available || g_upd.state == Updater::Downloading || g_upd.state == Updater::Handover;
    bool updBusy = upd && g_upd.state != Updater::Available;
    float rightFixed = g.W - 46 * 3 - 12 - 40 - 48 - 16;
    float updFull = upd ? g.measure(updBusy ? L"Updating…" : L"Update", 13.5f, TW_MED).width + 52 + 8 : 0;
    bool showWord = true, updCompact = false;
    if (rightFixed - updFull - (lx + 40 + wm + 24) < minNav) { updCompact = upd; }
    float updW = upd ? (updCompact ? 40.f + 8 : updFull) : 0;
    if (rightFixed - updW - (lx + 40 + wm + 24) < minNav) showWord = false;
    if (g.logo) g.imageTint(g.logo, { lx, app.barH / 2 - 15, 30, 30 }, app.th.text);
    if (showWord) g.text(L"cr1mix app", lx + 40, app.barH / 2 - 12, 18, app.th.text, TW_BOLD);
    float leftEnd = showWord ? lx + 40 + wm + 24 : lx + 30 + 16;

    float rx = g.W - 46 * 3 - 12;
    Rect av{ rx - 40, app.barH / 2 - 18, 36, 36 }; addNoDrag(av);
    {
        u64 id = HID("avatar"); bool c = ui.behave(id, av);
        ID2D1Bitmap* b = avatarBitmap();
        if (ui.hot == id) g.circle(av.x + 18, av.y + 18, 21, withA(app.th.text, 0.08f));
        if (b) g.imageCircle(b, av.x + 18, av.y + 18, 17);
        else { g.circle(av.x + 18, av.y + 18, 17, app.th.primaryC); g.icon("person", av.x + 18, av.y + 18, 20, app.th.onPrimaryC); }
        if (c) app.go(P_SETTINGS);
    }
    Rect setR{ av.x - 48, app.barH / 2 - 20, 40, 40 }; addNoDrag(setR);
    Col setBg = app.th.accentDim;
    if (ui.iconBtn(HID("settingsBtn"), setR.x + 20, setR.y + 20, "settings", 40, nullptr, false, app.page == P_SETTINGS ? &setBg : nullptr)) app.go(P_SETTINGS);
    float rightStart = setR.x - 16;
    if (upd) {
        std::wstring lbl = updBusy ? L"Updating…" : L"Update";
        float uw = updCompact ? 36.f : g.measure(lbl, 13.5f, TW_MED).width + 52;
        Rect ur{ setR.x - uw - 8, app.barH / 2 - 18, uw, 36 }; addNoDrag(ur);
        bool clicked;
        if (updCompact) {
            Col bg = app.th.accent, fg = app.th.onAccent;
            clicked = ui.iconBtn(HID("updPill"), ur.x + 18, ur.y + 18, "system_update_alt", 36, &fg, false, &bg);
            if (ui.mouseIn(ur)) ui.tooltip = updBusy ? L"Updating…" : L"Update available";
        } else clicked = ui.btnFilled(HID("updPill"), ur, lbl, "system_update_alt", !updBusy);
        if (clicked && !updBusy) {
            std::wstring v, n; { std::lock_guard<std::mutex> lk(g_upd.mx); v = g_upd.latest; n = g_upd.notes; }
            app.confirm(L"Update to " + v + L"?", (n.empty() ? L"" : n + L"\n\n") + kUpdateHow, L"Update", 1, [] { g_upd.install(); });
        }
        rightStart = ur.x - 16;
    }

    int n = 1 + (int)g_cat.tabs.size();
    std::vector<std::wstring> labels{ L"Home" }; std::vector<const char*> icons{ "home" };
    for (auto& t : g_cat.tabs) { labels.push_back(t.title); icons.push_back(t.icon.c_str()); }
    int cur = app.page == P_HOME ? 0 : app.page == P_TAB ? app.tab + 1 : -1;
    std::vector<float> full(n);
    float sumFull = 0;
    for (int i = 0; i < n; i++) { full[i] = 44 + g.measure(labels[i], 13.5f, TW_MED).width + 16; sumFull += full[i]; }
    float avail = rightStart - leftEnd;
    bool allLabels = sumFull + 8 <= avail;
    std::vector<float> w(n);
    float total = 0;
    for (int i = 0; i < n; i++) {
        float target = (allLabels || i == cur) ? full[i] : 44;
        w[i] = ui.anim(HID("navw", i), target, 16); total += w[i];
    }
    float nx = std::max(leftEnd, (g.W - total) / 2);
    if (nx + total > rightStart) nx = std::max(leftEnd, rightStart - total);
    Rect pill{ nx - 4, app.barH / 2 - 24, total + 8, 48 };
    g.fill(pill, withA(app.th.surf2, 0.85f), 24);
    addNoDrag(pill);
    float x = nx;
    float selX = nx; for (int i = 0; i < std::max(0, cur); i++) selX += w[i];
    float ax = ui.anim(HID("navsel"), selX, 18), aw = ui.anim(HID("navselw"), cur >= 0 ? w[cur] : 0, 18);
    if (cur >= 0) g.fill({ ax, pill.y + 4, aw, 40 }, app.th.primaryC, 20);
    for (int i = 0; i < n; i++) {
        Rect r{ x, pill.y + 4, w[i], 40 }; u64 id = HID("nav", i);
        bool c = ui.behave(id, r);
        bool on = i == cur;
        if (!on) ui.stateLayer(id, r, 20, app.th.text, 0.07f);
        ui.drawRipples(id, r, 20, app.th.text);
        Col fg = on ? app.th.onPrimaryC : app.th.textDim;
        g.icon(icons[i], r.x + 22, r.y + 20, 20, fg);
        float la = clampf((w[i] - 50) / std::max(1.f, full[i] - 50), 0, 1);
        if (la > 0.02f) { g.pushClip(r); g.text(labels[i], r.x + 40, r.y + 11, 13.5f, withA(fg, la), TW_MED); g.popClip(); }
        else ui.tip(id, labels[i]);
        if (c) { if (i == 0) app.go(P_HOME); else app.go(P_TAB, i - 1); }
        x += w[i];
    }
}

static void drawToast() {
    if (app.toast.empty()) return;
    app.toastT += ui.dt;
    float a = clampf(app.toastT * 5, 0, 1) * clampf((3.2f - app.toastT) * 3, 0, 1);
    if (app.toastT > 3.2f) { app.toast.clear(); return; }
    ui.anyAnim = true;
    float w = g.measure(app.toast, 14, TW_MED).width + 70;
    Rect r{ g.W / 2 - w / 2, g.H - 96 + (1 - a) * 12, w, 48 };
    g.opacity = a;
    g.shadow(r, 24, 0.8f);
    g.fill(r, app.dark ? hsl(0, 0, 0.88f) : hsl(0, 0, 0.18f), 14);
    const char* ic = app.toastKind == 3 ? "error" : app.toastKind == 2 ? "info" : "check_circle";
    Col fg = app.dark ? hsl(0, 0, 0.12f) : hsl(0, 0, 0.95f);
    g.icon(ic, r.x + 28, r.y + 24, 20, fg);
    g.text(app.toast, r.x + 48, r.y + 14.5f, 14, fg, TW_MED);
    g.opacity = 1;
}

static void drawTooltip() {
    if (ui.tooltip.empty()) return;
    ui.tipT += ui.dt; if (ui.tipT < 0.45f) { ui.anyAnim = true; return; }
    float a = clampf((ui.tipT - 0.45f) * 8, 0, 1); if (a < 1) ui.anyAnim = true;
    float w = g.measure(ui.tooltip, 12.5f, TW_MED).width + 20;
    Rect r{ clampf(ui.tipX - w / 2, 8, g.W - w - 8), ui.tipY + 22, w, 28 };
    g.opacity = a; g.fill(r, app.dark ? hsl(0, 0, 0.86f) : hsl(0, 0, 0.22f), 6);
    g.text(ui.tooltip, r.x + 10, r.y + 6, 12.5f, app.dark ? hsl(0, 0, 0.1f) : hsl(0, 0, 0.96f), TW_MED); g.opacity = 1;
}

static void drawFrame() {
    g.fill({ 0, 0, g.W, g.H }, app.th.bg, 0);
    if (g_dc.dirty.exchange(false)) {}
    if (app.page == P_SPLASH) { drawSplash(); windowControls(); return; }
    if (app.page == P_LOGIN) { drawLogin(); windowControls(); return; }
    if (app.page == P_INSTALL) { drawInstall(); windowControls(); return; }

    bool modal = app.confirmOpen || app.detail >= 0 || app.planOpen;
    ui.blocked = modal;
    topBar();
    Rect content{ 0, app.barH, g.W, g.H - app.barH };
    u64 key = HID("page", app.page * 64 + app.tab);
    float pa = ui.anim(key, 1.f, 13, 0.f);
    g.opacity = 0.35f + 0.65f * pa;
    g.setTransform(0, (1 - pa) * 22);
    switch (app.page) {
    case P_HOME: drawHome(content); break;
    case P_TAB: drawTab(content); break;
    case P_SETUP: drawSetup(content); break;
    case P_SETTINGS: drawSettings(content); break;
    default: break;
    }
    g.resetTransform(); g.opacity = 1;
    ui.blocked = app.confirmOpen;
    drawProgress();
    if (app.detail >= 0) { ui.blocked = app.confirmOpen; drawDetailSheet(); }
    else if (app.planOpen) { ui.blocked = app.confirmOpen; drawPlanSheet(); }
    ui.blocked = false;
    if (app.confirmOpen) drawConfirm();
    windowControls();
    drawToast();
    drawTooltip();
}
