#pragma once
#include "ui.h"
#include "catalog.h"
#include "engine.h"
#include "stats.h"

enum Page { P_SPLASH, P_LOGIN, P_HOME, P_TAB, P_SETUP, P_SETTINGS, P_INSTALL };

bool setupMode();
std::wstring installDir();
bool isInstalled();
int runUninstall();
void setUpdateTarget(const std::wstring& path);
void refreshInstallInfo();
void drawInstall();

struct Prefs {
    int seed = 0x9EC9BD;
    int theme = 0;
    float transparency = 0.14f;
    bool motion = true;
    bool restorePoint = false;
    bool offline = false;
    long long lastRestore = 0, lastBackup = 0;
    std::wstring backupDir;
    void load(); void save();
};

struct App {
    HWND hwnd = nullptr;
    Prefs pf;
    Page page = P_SPLASH;
    int tab = 0;
    Theme th;
    bool dark = true;
    double startTime = 0;
    bool splashDone = false;

    float barH = 64;
    std::vector<Rect> noDrag;

    int detail = -1;
    bool sheet = false;
    bool confirmOpen = false; std::wstring confirmTitle, confirmBody, confirmOk; int confirmRisk = 0; std::function<void()> confirmFn;
    std::wstring toast; float toastT = 0; int toastKind = 0;
    std::wstring search;
    bool planOpen = false; std::wstring planTitle; std::vector<JobItem> planItems; std::vector<u8> planSel;
    void openPlan(const std::wstring& title, const std::vector<JobItem>& items) { planTitle = title; planItems = items; planSel.assign(items.size(), 1); planOpen = true; detail = -1; }

    Scroller scHome, scTab[16], scSetup, scSettings, scDetail, scSheet;

    void go(Page p, int t = -1);
    void confirm(const std::wstring& title, const std::wstring& body, const std::wstring& ok, int risk, std::function<void()> fn);
    void showToast(const std::wstring& s, int kind = 1) { toast = s; toastT = 0; toastKind = kind; }
    void applyTheme();
};
extern App app;
extern Stats S;

void addNoDrag(const Rect& r);
void runReboot();
ID2D1Bitmap* avatarBitmap();

void drawHome(const Rect& area);
void drawTab(const Rect& area);
void drawSetup(const Rect& area);
void drawSettings(const Rect& area);
void drawDetailSheet();
void drawPlanSheet();
void drawProgress();
void drawConfirm();

void requestToggle(int ti, bool on);
void requestChoice(int ti, int opt);
void requestPick(int ti, const std::vector<int>& items);
void requestRun(int ti, int opt = -1);
void runTask(const std::wstring& title, std::function<void(SayFn)> fn);
void applyMany(const std::wstring& title, const std::vector<JobItem>& items);

const char* tabIcon(const std::string& id);
Col riskColor(int r);
const wchar_t* riskName(int r);
std::wstring fmtBytes(double b);
std::wstring fmtDur(u64 s);
int appliedCount(const Tab& t, int* total = nullptr);
bool tweakVisible(const Tweak& t);
bool tweakOn(const Tweak& t);
