#include "pages.h"
#include "discord.h"
#include <shobjidl.h>
#include <shlguid.h>
#include <tlhelp32.h>
#include <thread>
#include <atomic>

static std::wstring s_updateTarget;
void setUpdateTarget(const std::wstring& path) { s_updateTarget = path; }

static const wchar_t* kUninstKey = L"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\cr1mix app";
static const wchar_t* kExeName = L"cr1mix app.exe";

bool setupMode() {
    std::wstring p = selfPath(); std::wstring f = p.substr(p.find_last_of(L'\\') + 1);
    return icontains(f, L"setup");
}
std::wstring installDir() { return expandEnv(L"%ProgramFiles%\\cr1mix app"); }
static std::wstring installedExe() { return installDir() + L"\\" + kExeName; }
bool isInstalled() { return fileExists(installedExe()); }

static void putStr(const RegPath& k, const wchar_t* name, const std::wstring& v) { DWORD t; std::string d; if (encodeValue('s', v, t, d)) regWriteRaw(k, name, t, d, false); }
static void putDword(const RegPath& k, const wchar_t* name, DWORD v) { regWriteRaw(k, name, REG_DWORD, std::string((char*)&v, 4), false); }

static bool makeShortcut(const std::wstring& lnk, const std::wstring& target) {
    IShellLinkW* sl = nullptr; IPersistFile* pf = nullptr; bool ok = false;
    if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (void**)&sl))) {
        sl->SetPath(target.c_str());
        sl->SetWorkingDirectory(installDir().c_str());
        sl->SetIconLocation(target.c_str(), 0);
        sl->SetDescription(L"cr1mix app");
        if (SUCCEEDED(sl->QueryInterface(IID_IPersistFile, (void**)&pf))) { ok = SUCCEEDED(pf->Save(lnk.c_str(), TRUE)); pf->Release(); }
        sl->Release();
    }
    return ok;
}
static std::wstring startLnk() { return expandEnv(L"%ProgramData%\\Microsoft\\Windows\\Start Menu\\Programs\\cr1mix app.lnk"); }
static std::wstring deskLnk() { return expandEnv(L"%PUBLIC%\\Desktop\\cr1mix app.lnk"); }

static BOOL CALLBACK closeOthers(HWND w, LPARAM) {
    wchar_t cls[64]; DWORD pid = 0;
    if (GetClassNameW(w, cls, 64) && !wcscmp(cls, L"cr1mixWnd") && GetWindowThreadProcessId(w, &pid) && pid != GetCurrentProcessId())
        PostMessageW(w, WM_CLOSE, 0, 0);
    return TRUE;
}
static void closeRunningApp() {
    EnumWindows(closeOthers, 0);
    if (HANDLE m = OpenMutexW(SYNCHRONIZE, FALSE, L"cr1mix.single")) {
        DWORD w = WaitForSingleObject(m, 8000);
        if (w == WAIT_OBJECT_0 || w == WAIT_ABANDONED) ReleaseMutex(m);
        CloseHandle(m);
    }
    killProcesses(L"cr1mix app");
    Sleep(300);
}

static bool installApp(bool desktop, std::wstring& err) {
    std::wstring dir = installDir(), exe = installedExe();
    if (!ensureDir(dir)) { err = L"Could not create " + dir; return false; }
    closeRunningApp();
    bool copied = false;
    for (int i = 0; i < 10 && !copied; i++) { copied = CopyFileW(selfPath().c_str(), exe.c_str(), FALSE) != 0; if (!copied) Sleep(300); }
    if (!copied) { err = L"Could not copy the app to " + dir + L". Close cr1mix app and try again."; return false; }
    makeShortcut(startLnk(), exe);
    if (desktop) makeShortcut(deskLnk(), exe); else DeleteFileW(deskLnk().c_str());
    RegPath k = regParse(kUninstKey);
    regCreateKey(k, false);
    putStr(k, L"DisplayName", L"cr1mix app");
    putStr(k, L"DisplayVersion", CR1MIX_VERSION);
    putStr(k, L"Publisher", L"cr1mix");
    putStr(k, L"DisplayIcon", exe + L",0");
    putStr(k, L"InstallLocation", dir);
    putStr(k, L"UninstallString", L"\"" + exe + L"\" --uninstall");
    putStr(k, L"URLInfoAbout", L"https://discord.com/invite/NjkgT7vXBb");
    putDword(k, L"NoModify", 1); putDword(k, L"NoRepair", 1);
    WIN32_FILE_ATTRIBUTE_DATA fa{}; if (GetFileAttributesExW(exe.c_str(), GetFileExInfoStandard, &fa)) putDword(k, L"EstimatedSize", fa.nFileSizeLow / 1024 + 1);
    return true;
}

void refreshInstallInfo() {
    if (_wcsicmp(selfPath().c_str(), installedExe().c_str())) return;
    RegPath k = regParse(kUninstKey);
    if (!regKeyExists(k)) return;
    if (regString(k, L"DisplayVersion") != CR1MIX_VERSION) putStr(k, L"DisplayVersion", CR1MIX_VERSION);
    WIN32_FILE_ATTRIBUTE_DATA fa{}; if (GetFileAttributesExW(installedExe().c_str(), GetFileExInfoStandard, &fa)) putDword(k, L"EstimatedSize", fa.nFileSizeLow / 1024 + 1);
}

int runUninstall() {
    int r = MessageBoxW(nullptr, L"Remove cr1mix app from this PC?\n\nTweaks you applied stay as they are. To put Windows back first, open the app and use Settings > Undo everything.",
        L"Uninstall cr1mix app", MB_YESNO | MB_ICONQUESTION);
    if (r != IDYES) return 0;
    DWORD me = GetCurrentProcessId();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe{ sizeof(pe) };
        if (Process32FirstW(snap, &pe)) do {
            if (pe.th32ProcessID != me && !_wcsicmp(pe.szExeFile, kExeName)) { HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID); if (h) { TerminateProcess(h, 0); CloseHandle(h); } }
        } while (Process32NextW(snap, &pe));
        CloseHandle(snap);
    }
    DeleteFileW(startLnk().c_str());
    DeleteFileW(deskLnk().c_str());
    regDeleteTree(regParse(kUninstKey), false);
    std::wstring exe = installedExe(), dir = installDir();
    bool self = !_wcsicmp(selfPath().c_str(), exe.c_str());
    if (!self && DeleteFileW(exe.c_str())) RemoveDirectoryW(dir.c_str());
    else { MoveFileExW(exe.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT); MoveFileExW(dir.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT); }
    MessageBoxW(nullptr, self ? L"cr1mix app was removed. Its folder in Program Files goes away the next time you restart." : L"cr1mix app was removed.", L"cr1mix app", MB_ICONINFORMATION);
    return 0;
}

static void drawUpdate() {
    static std::atomic<int> st{ 0 };
    static std::wstring err;
    static bool started = false;
    static double doneAt = 0;
    if (!started) {
        started = true;
        std::thread([] {
            for (int i = 0; i < 60; i++) {
                HANDLE m = OpenMutexW(SYNCHRONIZE, FALSE, L"cr1mix.single");
                if (!m) break;
                DWORD w = WaitForSingleObject(m, 500);
                if (w == WAIT_OBJECT_0 || w == WAIT_ABANDONED) { ReleaseMutex(m); CloseHandle(m); break; }
                CloseHandle(m);
            }
            st = 1;
            Sleep(400);
            std::wstring e; bool ok = false;
            if (!_wcsicmp(s_updateTarget.c_str(), installedExe().c_str())) ok = installApp(fileExists(deskLnk()), e);
            else {
                for (int i = 0; i < 20 && !ok; i++) { ok = CopyFileW(selfPath().c_str(), s_updateTarget.c_str(), FALSE) != 0; if (!ok) Sleep(250); }
                if (!ok) e = L"Could not write " + s_updateTarget + L". Close cr1mix app and try again.";
            }
            if (ok) st = 2; else { err = e; st = 3; }
        }).detach();
    }
    const Theme& T = app.th;
    float cx = g.W / 2, cy = g.H / 2;
    float a = ui.anim(HID("updIn"), 1, 9, 0);
    Rect card{ cx - 250, cy - 230 + (1 - a) * 18, 500, 440 };
    g.opacity = a;
    g.shadow(card, 36, 0.9f);
    g.fill(card, T.surf2, 36);
    g.pushLayer(1.f, &card, 36);
    ui.blob(card.r() - 40, card.y + 40, 80, 1, withA(T.primaryC, 0.9f), ui.time * 0.25f);
    ui.blob(card.x + 20, card.b() - 10, 54, 2, withA(T.tertiaryC, 0.8f), -ui.time * 0.2f);
    g.popLayer();
    g.fill({ cx - 56, card.y + 46, 112, 112 }, T.surfHi, 38);
    if (g.logo) g.imageTint(g.logo, { cx - 41, card.y + 61, 82, 82 }, T.text);
    int s = st;
    g.textR(s == 2 ? L"cr1mix app is up to date" : s == 3 ? L"The update did not finish" : L"Updating cr1mix app", { card.x, card.y + 180, card.w, 36 }, 26, T.text, TW_BOLD, AL_CENTER);
    g.textR(L"Version " CR1MIX_VERSION, { card.x + 20, card.y + 220, card.w - 40, 22 }, 13, T.textDim, TW_REG, AL_CENTER);
    float y = card.y + 268;
    if (s < 2) {
        Rect track{ card.x + 70, y + 20, card.w - 140, 6 };
        g.fill(track, T.surfHi, 3);
        float t = (float)fmod(ui.time * 0.7, 1.0), w = track.w * 0.35f;
        float x0 = track.x + (track.w + w) * t - w;
        float l = std::max(track.x, x0), r = std::min(track.r(), x0 + w);
        if (r > l) g.fill({ l, track.y, r - l, track.h }, T.primaryC, 3);
        g.textR(s == 0 ? L"Waiting for cr1mix app to close…" : L"Installing…", { card.x, y + 44, card.w, 24 }, 14, T.textDim, TW_MED, AL_CENTER);
        ui.anyAnim = true;
    } else if (s == 2) {
        if (doneAt == 0) doneAt = nowSec();
        iconBlob(cx, y + 30, 56, "check", T.primaryC, T.onPrimaryC, 1, ui.time * 0.3f);
        g.textR(L"Opening cr1mix app…", { card.x, y + 74, card.w, 24 }, 14, T.textDim, TW_MED, AL_CENTER);
        ui.anyAnim = true;
        if (doneAt > 0 && nowSec() - doneAt > 1.2) {
            std::wstring dir = s_updateTarget.substr(0, s_updateTarget.find_last_of(L'\\'));
            ShellExecuteW(nullptr, L"open", s_updateTarget.c_str(), nullptr, dir.c_str(), SW_SHOWNORMAL);
            PostMessageW(app.hwnd, WM_CLOSE, 0, 0);
            doneAt = -1;
        }
    } else {
        g.textR(err, { card.x + 30, y, card.w - 60, 44 }, 13, T.danger, TW_REG, AL_CENTER, true, true);
        Rect btn{ cx - 110, y + 64, 220, 48 }; addNoDrag(btn);
        if (ui.btnFilled(HID("updClose"), btn, L"Close", "close")) PostMessageW(app.hwnd, WM_CLOSE, 0, 0);
    }
    g.opacity = 1;
}

void drawInstall() {
    if (!s_updateTarget.empty()) { drawUpdate(); return; }
    static bool desktop = !isInstalled() || fileExists(deskLnk());
    static int state = 0;
    static std::wstring err;
    static double doneAt = 0;
    const Theme& T = app.th;
    float cx = g.W / 2, cy = g.H / 2;
    float a = ui.anim(HID("instIn"), 1, 9, 0);
    Rect card{ cx - 250, cy - 270 + (1 - a) * 18, 500, 520 };
    g.opacity = a;
    g.shadow(card, 36, 0.9f);
    g.fill(card, T.surf2, 36);
    g.pushLayer(1.f, &card, 36);
    ui.blob(card.r() - 40, card.y + 40, 80, 1, withA(T.primaryC, 0.9f), ui.time * 0.25f);
    ui.blob(card.x + 20, card.b() - 10, 54, 2, withA(T.tertiaryC, 0.8f), -ui.time * 0.2f);
    g.popLayer();
    g.fill({ cx - 56, card.y + 46, 112, 112 }, T.surfHi, 38);
    if (g.logo) g.imageTint(g.logo, { cx - 41, card.y + 61, 82, 82 }, T.text);
    bool inst = isInstalled();
    g.textR(state == 1 ? L"cr1mix app is installed" : inst ? L"Update cr1mix app" : L"Install cr1mix app", { card.x, card.y + 180, card.w, 36 }, 26, T.text, TW_BOLD, AL_CENTER);
    g.textR(L"Version " CR1MIX_VERSION L" · " + installDir(), { card.x + 20, card.y + 220, card.w - 40, 22 }, 13, T.textDim, TW_REG, AL_CENTER);

    float y = card.y + 262;
    if (state != 1) {
        Rect row{ card.x + 40, y, card.w - 80, 56 };
        g.fill(row, T.surfHi, 18);
        g.icon("desktop_windows", row.x + 28, row.y + 28, 20, T.accent);
        g.textR(L"Desktop shortcut", { row.x + 52, row.y, row.w - 130, row.h }, 14.5f, T.text, TW_MED);
        if (ui.toggle(HID("instDesk"), row.r() - 70, row.y + 12, desktop)) desktop = !desktop;
        y += 70;
        g.textR(L"A Start menu shortcut and an Apps & features entry are added too, so you can uninstall it like any app.", { card.x + 40, y, card.w - 80, 40 }, 12.5f, T.textFaint, TW_REG, AL_CENTER, true, true);
        y += 52;
        Rect btn{ cx - 150, y, 300, 54 }; addNoDrag(btn);
        if (ui.btnFilled(HID("instGo"), btn, inst ? L"Update" : L"Install", "download")) {
            if (installApp(desktop, err)) { state = 1; doneAt = nowSec(); } else state = 2;
        }
        y += 66;
        Rect skip{ cx - 110, y, 220, 40 }; addNoDrag(skip);
        if (ui.btnText(HID("instSkip"), skip, L"Run without installing")) app.go(g_dc.loggedIn() ? P_HOME : P_LOGIN);
        if (state == 2) g.textR(err, { card.x + 30, card.b() - 34, card.w - 60, 22 }, 12.5f, T.danger, TW_REG, AL_CENTER);
    } else {
        iconBlob(cx, y + 40, 64, "check", T.primaryC, T.onPrimaryC, 1, ui.time * 0.3f);
        g.textR(L"Opening cr1mix app…", { card.x, y + 90, card.w, 24 }, 14, T.textDim, TW_MED, AL_CENTER);
        ui.anyAnim = true;
        if (nowSec() - doneAt > 1.2) {
            ShellExecuteW(nullptr, L"open", installedExe().c_str(), nullptr, installDir().c_str(), SW_SHOWNORMAL);
            PostMessageW(app.hwnd, WM_CLOSE, 0, 0);
            state = 3;
        }
    }
    g.opacity = 1;
}
