#pragma once
#include "common.h"

std::string loadResource(const wchar_t* name);

enum Verb : u8 {
    V_REG, V_REGDEL, V_KEYADD, V_KEYDEL, V_RBIT, V_RSCAN, V_MITOFF, V_SPV, V_EXTDEL, V_ASETUP, V_RUNDEL, V_KMATCH, V_CONSENT, V_MSI,
    V_SVC, V_SVCFIX, V_SVCCOPY, V_SVCDEL, V_DEFSVC, V_START, V_STOP, V_STOPDIS, V_RESET,
    V_TASK, V_TIMER, V_SCHED,
    V_APPX, V_AREG, V_ARESET, V_CAP, V_FEAT, V_MMAGENT, V_BIND, V_DEVWAKE, V_DEVOFF, V_DEVON,
    V_KILL, V_CMD, V_BCD, V_FSU, V_RUNGLOB, V_RUNREG, V_MSIX, V_WINGET, V_FWGROUP,
    V_FILE, V_DELDIR, V_DELFILE, V_DELCONTENT, V_DELGLOB, V_DELEMPTY, V_FILEOFF, V_KFRESTORE, V_LNKCLEAN, V_EVTCLEAR, V_CLEANMGR, V_HOSTSCLEAN, V_EFIHIDE,
    V_PW, V_PLAN, V_PLANSET,
    V_VERIFY, V_REFRESH,
    V_NVDRS, V_GPUPREF, V_STARTUPOFF,
    V_COUNT
};

enum : u32 {
    AF_EXISTS = 1,
    AF_NIC = 2,
    AF_WIRED = 4,
    AF_NV = 8,
    AF_AMD = 16,
    AF_GEFORCE = 32,
    AF_SLOW = 64,
    AF_TI = 128,
    AF_DEEP = 256,
    AF_FORCE = 512,
    AF_X64 = 1024,
    AF_HV = 2048,
};

struct Act {
    Verb v;
    u8 type = 0;
    int n = 0;
    int n2 = -1;
    u32 f = 0;
    std::wstring a, b, c, d;
};

struct Opt { std::string key; std::wstring label; bool rec = false; bool hidden = false; std::vector<Act> acts; std::vector<std::string> uses; };

enum TweakKind : u8 { TK_TOGGLE, TK_CHOICE, TK_PICK, TK_ACTION };
enum : u32 {
    TF_REC = 1, TF_REBOOT = 2, TF_LOGOFF = 4, TF_DESKTOP = 8, TF_LAPTOP = 16, TF_RAM12 = 32, TF_RAM4 = 64, TF_TI = 128,
    TF_INV = 256,
    TF_NVIDIA = 512,
    TF_AMD = 1024,
};

struct Tweak {
    std::string id, tab, sub, icon;
    std::wstring name, desc, src;
    TweakKind kind = TK_TOGGLE;
    u32 fl = 0;
    int risk = 1;
    std::vector<Act> on, off;
    std::vector<Opt> opts;
    bool hasOff = false;
    int state = -1;
    std::vector<u8> picked;
    bool busy = false;
    int idx = 0;
};

struct Sub { std::string id; std::wstring title; std::vector<int> tweaks; };
struct Tab { std::string id; std::wstring title; std::string icon; std::vector<Sub> subs; int count = 0; };

struct Catalog {
    std::vector<Tab> tabs;
    std::vector<Tweak> tweaks;
    std::unordered_map<std::string, int> byId;
    bool load();
    int find(const std::string& id) const { auto it = byId.find(id); return it == byId.end() ? -1 : it->second; }
    Tab* tab(const std::string& id) { for (auto& t : tabs) if (t.id == id) return &t; return nullptr; }
    int tabIndex(const std::string& id) const { for (size_t i = 0; i < tabs.size(); i++) if (tabs[i].id == id) return (int)i; return -1; }
};
extern Catalog g_cat;

std::wstring describeAct(const Act& a);
