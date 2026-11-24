#include "catalog.h"
#ifndef COMPRESS_ALGORITHM_LZMS
#define COMPRESS_ALGORITHM_LZMS 5
#endif
#include <compressapi.h>

Catalog g_cat;

std::string loadResource(const wchar_t* name) {
    HRSRC h = FindResourceW(nullptr, name, MAKEINTRESOURCEW(10));
    if (!h) return {};
    HGLOBAL gl = LoadResource(nullptr, h);
    DWORD n = SizeofResource(nullptr, h);
    const char* p = (const char*)LockResource(gl);
    return p ? std::string(p, p + n) : std::string();
}

static void deobf(std::string& d) {
    static const u8 KEY[16] = { 0x9E,0x37,0x79,0xB9,0x7F,0x4A,0x7C,0x15,0xC2,0xB3,0x56,0x88,0x3D,0x11,0xE6,0xA5 };
    u32 k = 0x12345678;
    for (size_t i = 0; i < d.size(); i++) { k = k * 1103515245u + 12345u; d[i] = (char)((u8)d[i] ^ KEY[i & 15] ^ (u8)((k >> 16) & 0xFF)); }
}

static std::wstring trim(const std::wstring& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == L' ' || s[a] == L'\t' || s[a] == L'\r')) a++;
    while (b > a && (s[b - 1] == L' ' || s[b - 1] == L'\t' || s[b - 1] == L'\r')) b--;
    return s.substr(a, b - a);
}
static std::vector<std::wstring> splitBar(const std::wstring& s) {
    std::vector<std::wstring> out; size_t p = 0;
    for (;;) {
        size_t q = s.find(L" | ", p);
        if (q == std::wstring::npos) { out.push_back(trim(s.substr(p))); break; }
        out.push_back(trim(s.substr(p, q - p))); p = q + 3;
    }
    if (!out.empty() && out.back().size() >= 2 && out.back().substr(out.back().size() - 2) == L" |") out.back().resize(out.back().size() - 2);
    return out;
}
static std::vector<std::wstring> splitWs(const std::wstring& s) {
    std::vector<std::wstring> out; std::wstring cur;
    for (wchar_t c : s) { if (c == L' ' || c == L'\t') { if (!cur.empty()) out.push_back(cur), cur.clear(); } else cur += c; }
    if (!cur.empty()) out.push_back(cur);
    return out;
}
static std::vector<std::wstring> splitCh(const std::wstring& s, wchar_t ch) {
    std::vector<std::wstring> out; size_t p = 0;
    for (;;) { size_t q = s.find(ch, p); out.push_back(trim(s.substr(p, q == std::wstring::npos ? std::wstring::npos : q - p))); if (q == std::wstring::npos) break; p = q + 1; }
    return out;
}

struct Abbr { const wchar_t* k; const wchar_t* v; };
static const Abbr ABBR[] = {
    {L"UCV\\", L"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\"}, {L"UPW\\", L"HKCU\\Software\\Policies\\Microsoft\\Windows\\"},
    {L"UM\\", L"HKCU\\Software\\Microsoft\\"}, {L"CV\\", L"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\"},
    {L"PW\\", L"HKLM\\SOFTWARE\\Policies\\Microsoft\\Windows\\"}, {L"PM\\", L"HKLM\\SOFTWARE\\Policies\\Microsoft\\"},
    {L"CC\\", L"HKLM\\SYSTEM\\CurrentControlSet\\Control\\"}, {L"SV\\", L"HKLM\\SYSTEM\\CurrentControlSet\\Services\\"},
    {L"SYS\\", L"HKLM\\SYSTEM\\CurrentControlSet\\"}, {L"NT\\", L"HKLM\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\"},
    {L"M\\", L"HKLM\\SOFTWARE\\Microsoft\\"},
};
static std::wstring keyPath(const std::wstring& p) {
    for (auto& a : ABBR) { size_t n = wcslen(a.k); if (p.compare(0, n, a.k) == 0) return a.v + p.substr(n); }
    return p;
}

static bool parseData(const std::wstring& s, u8& type, std::wstring& data) {
    if (s.size() < 2 || s[1] != L':') return false;
    wchar_t t = s[0]; if (!wcschr(L"dqsxmb", t)) return false;
    type = (u8)t; data = s.substr(2); return true;
}

static u32 parseFilters(const std::wstring& s) {
    u32 f = 0;
    for (auto& t : splitWs(s)) {
        if (t == L"?exists") f |= AF_EXISTS; else if (t == L"?nic") f |= AF_NIC; else if (t == L"?wired") f |= AF_WIRED;
        else if (t == L"?nv") f |= AF_NV; else if (t == L"?amd") f |= AF_AMD; else if (t == L"?geforce") f |= AF_GEFORCE;
        else if (t == L"?slow") f |= AF_SLOW; else if (t == L"ti") f |= AF_TI; else if (t == L"?hv") f |= AF_HV; else if (t == L"x64") f |= AF_X64;
    }
    return f;
}

static const wchar_t* NIC_CLASS = L"HKLM\\SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e972-e325-11ce-bfc1-08002be10318}\\*";
static const wchar_t* GPU_CLASS = L"HKLM\\SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e968-e325-11ce-bfc1-08002be10318}\\*";

static Act reg(const std::wstring& path, const std::wstring& name, u8 type, const std::wstring& data, const std::wstring& def = L"", u32 f = 0) {
    Act a; a.v = V_REG; a.a = keyPath(path); a.b = name; a.type = type; a.c = data; a.d = def; a.f = f; return a;
}
static Act simple(Verb v, const std::wstring& a = L"", const std::wstring& b = L"", int n = 0) { Act x; x.v = v; x.a = a; x.b = b; x.n = n; return x; }

struct Parser {
    Catalog& c;
    Tab* tab = nullptr; Sub* sub = nullptr;
    Tweak* tw = nullptr; std::vector<Act>* dst = nullptr;
    std::vector<std::wstring> foldList, fwpList;
    explicit Parser(Catalog& cc) : c(cc) {}

    void push(const Act& a) { if (dst) dst->push_back(a); }

    void line(std::wstring s) {
        if (s.find(L"%F") != std::wstring::npos && !foldList.empty()) {
            for (auto& g : foldList) { std::wstring t = s; size_t p; while ((p = t.find(L"%F")) != std::wstring::npos) t.replace(p, 2, g); line1(t); }
            return;
        }
        if (s.find(L"%P") != std::wstring::npos && !fwpList.empty()) {
            for (auto& g : fwpList) { std::wstring t = s; size_t p; while ((p = t.find(L"%P")) != std::wstring::npos) t.replace(p, 2, g); line1(t); }
            return;
        }
        line1(s);
    }

    void line1(const std::wstring& raw) {
        std::wstring s = trim(raw);
        if (s.empty() || s[0] == L'#') return;
        size_t sp = s.find_first_of(L" \t");
        std::wstring v = sp == std::wstring::npos ? s : s.substr(0, sp);
        std::wstring rest = sp == std::wstring::npos ? L"" : trim(s.substr(sp));

        if (v == L"tab") { auto p = splitBar(rest); c.tabs.push_back({}); tab = &c.tabs.back(); tab->id = u16to8(p[0]); tab->title = p.size() > 1 ? p[1] : p[0]; tab->icon = p.size() > 2 ? u16to8(p[2]) : "tune"; sub = nullptr; tw = nullptr; return; }
        if (v == L"sub") { if (!tab) return; auto p = splitBar(rest); tab->subs.push_back({}); sub = &tab->subs.back(); sub->id = u16to8(p[0]); sub->title = p.size() > 1 ? p[1] : p[0]; tw = nullptr; return; }
        if (v == L"tweak") {
            auto p = splitBar(rest);
            c.tweaks.push_back({}); tw = &c.tweaks.back();
            tw->id = u16to8(p[0]); tw->icon = p.size() > 1 ? u16to8(p[1]) : "tune";
            tw->tab = tab ? tab->id : ""; tw->sub = sub ? sub->id : "";
            bool pick = false, action = false, choice = false;
            if (p.size() > 2) for (auto& f : splitWs(p[2])) {
                if (f == L"rec") tw->fl |= TF_REC; else if (f == L"reboot") tw->fl |= TF_REBOOT; else if (f == L"logoff") tw->fl |= TF_LOGOFF;
                else if (f == L"desktop") tw->fl |= TF_DESKTOP; else if (f == L"laptop") tw->fl |= TF_LAPTOP; else if (f == L"ram12") tw->fl |= TF_RAM12;
                else if (f == L"ram4") tw->fl |= TF_RAM4; else if (f == L"ti") tw->fl |= TF_TI;
                else if (f == L"inv") tw->fl |= TF_INV; else if (f == L"nvidia") tw->fl |= TF_NVIDIA; else if (f == L"amd") tw->fl |= TF_AMD;
                else if (f == L"risk1") tw->risk = 1; else if (f == L"risk2") tw->risk = 2; else if (f == L"risk3") tw->risk = 3;
                else if (f == L"pick") pick = true; else if (f == L"action") action = true; else if (f == L"choice") choice = true;
            }
            tw->kind = pick ? TK_PICK : action ? TK_ACTION : choice ? TK_CHOICE : TK_TOGGLE;
            dst = &tw->on; foldList.clear(); fwpList.clear();
            return;
        }
        if (!tw) return;
        if (v == L"name") { tw->name = rest; return; }
        if (v == L"desc") { tw->desc = rest; return; }
        if (v == L"src") { tw->src = rest; return; }
        if (v == L"off:") { tw->hasOff = true; dst = &tw->off; return; }
        if (v == L"opt" || v == L"item") {
            auto p = splitBar(rest);
            Opt o; o.key = u16to8(p[0]); o.label = p.size() > 1 ? p[1] : p[0];
            o.hidden = !o.key.empty() && o.key[0] == '_';
            if (p.size() > 2 && p[2].find(L"rec") != std::wstring::npos) o.rec = true;
            tw->opts.push_back(o); dst = &tw->opts.back().acts;
            if (tw->kind == TK_TOGGLE) tw->kind = TK_CHOICE;
            return;
        }
        if (v == L"USE") { Act m; m.v = V_COUNT; m.a = rest; push(m); return; }
        if (v == L"FOLD") { foldList = splitWs(rest); return; }
        if (v == L"FWP") { fwpList = splitWs(rest); return; }
        action(v, rest);
    }

    void action(const std::wstring& v, const std::wstring& rest) {
        if (v == L"R") {
            auto p = splitBar(rest);
            if (p.size() < 3) return;
            u8 t; std::wstring data;
            if (!parseData(p[2], t, data)) return;
            Act a = reg(p[0], p[1] == L"(default)" ? L"" : p[1], t, data, p.size() > 3 ? p[3] : L"", p.size() > 4 ? parseFilters(p[4]) : 0);
            if (p.size() > 3 && (p[3] == L"ti" || p[3].find(L"?") == 0)) { a.d.clear(); a.f |= parseFilters(p[3]); }
            if (a.a.find(L"\\Enum\\SCSI\\") != std::wstring::npos || a.a.find(L"Interfaces\\*") != std::wstring::npos) {}
            push(a); return;
        }
        if (v == L"R-") { auto p = splitBar(rest); Act a = simple(V_REGDEL, keyPath(p[0]), p.size() > 1 ? p[1] : L""); push(a); return; }
        if (v == L"K-") { push(simple(V_KEYDEL, keyPath(rest))); return; }
        if (v == L"K+") { push(simple(V_KEYADD, keyPath(rest))); return; }
        if (v == L"RBIT") { auto p = splitBar(rest); Act a = simple(V_RBIT, keyPath(p[0]), p[1]); a.n = (int)wcstoul(p[2].c_str(), nullptr, 0); push(a); return; }
        if (v == L"RS") {
            auto p = splitBar(rest); if (p.size() < 4) return;
            Act a; a.v = V_RSCAN; a.a = keyPath(p[0]); a.b = p[1]; a.c = p[2]; parseData(p[3], a.type, a.d); push(a); return;
        }
        if (v == L"NICZ" || v == L"NICS") {
            std::wstring val = L"0", list = rest; u32 extra = 0;
            if (v == L"NICS") { auto p = splitBar(rest); val = p[0]; list = p.size() > 1 ? p[1] : L""; }
            auto names = splitWs(list);
            for (auto& n : names) if (n == L"?slow") extra |= AF_SLOW;
            for (auto& n : names) if (n[0] != L'?') push(reg(NIC_CLASS, n, 's', val, L"", AF_EXISTS | AF_NIC | extra));
            return;
        }
        if (v == L"DEVZ") { for (auto& n : splitWs(rest)) push(reg(L"HKLM\\SYSTEM\\CurrentControlSet\\Enum", n, 'd', L"0", L"", AF_EXISTS | AF_DEEP)); return; }
        if (v == L"NVSET" || v == L"AMDSET") {
            u32 f = (v == L"NVSET" ? AF_NV : AF_AMD);
            auto toks = splitWs(rest);
            for (auto& t : toks) if (t == L"?geforce") f |= AF_GEFORCE;
            for (auto& t : toks) { size_t e = t.find(L'='); if (e == std::wstring::npos) continue; push(reg(GPU_CLASS, t.substr(0, e), 'd', t.substr(e + 1), L"", f)); }
            return;
        }
        if (v == L"POLICY" || v == L"OSHELL" || v == L"OOBE" || v == L"WTDS" || v == L"STARTF") {
            std::wstring base, list = rest;
            if (v == L"POLICY") { auto p = splitBar(rest); base = p[0]; list = p.size() > 1 ? p[1] : L""; }
            else if (v == L"OSHELL") base = L"HKCU\\Software\\OpenShell\\StartMenu\\Settings";
            for (auto& t : splitWs(list)) {
                size_t e = t.find(L'='); if (e == std::wstring::npos) continue;
                std::wstring k = t.substr(0, e), val = t.substr(e + 1);
                u8 ty = 'd'; if (val.size() >= 2 && val[1] == L':') { ty = (u8)val[0]; val = val.substr(2); }
                if (v == L"OOBE") { push(reg(L"CV\\OOBE", k, ty, val, L"-")); push(reg(L"PW\\OOBE", k, ty, val, L"-")); }
                else if (v == L"WTDS") { push(reg(L"CV\\WTDS\\Components", k, ty, val, L"-")); push(reg(L"PW\\WTDS\\Components", k, ty, val, L"-")); }
                else if (v == L"STARTF") { push(reg(L"M\\PolicyManager\\current\\device\\Start", L"AllowPinnedFolder" + k, 'd', val, L"-")); push(reg(L"M\\PolicyManager\\current\\device\\Start", L"AllowPinnedFolder" + k + L"_ProviderSet", 'd', L"0", L"-")); }
                else push(reg(base, k, ty, val, L"-"));
            }
            return;
        }
        if (v == L"CDM") { for (auto& n : splitWs(rest)) push(reg(L"UCV\\ContentDeliveryManager", n, 'd', L"0", L"-")); return; }
        if (v == L"AUXP") { for (auto& n : splitWs(rest)) push(reg(L"UCV\\Explorer\\Taskband\\AuxilliaryPins", n, 'd', L"0", L"-")); return; }
        if (v == L"SPLIT") { for (auto& n : splitWs(rest)) push(reg(L"SV\\" + n, L"SvcHostSplitDisable", 'd', L"1", L"-")); return; }
        if (v == L"BLOCK") {
            auto p = splitBar(rest);
            push(reg(L"CV\\Shell Extensions\\Blocked", p[0], 's', p.size() > 1 ? p[1] : L"", L"-"));
            push(reg(L"HKLM\\SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Shell Extensions\\Blocked", p[0], 's', p.size() > 1 ? p[1] : L"", L"-"));
            return;
        }
        if (v == L"QOS") {
            auto p = splitBar(rest); if (p.size() < 2) return;
            std::wstring k = L"PW\\QoS\\" + p[0];
            push(simple(V_KEYADD, keyPath(k)));
            const wchar_t* star[] = { L"Protocol", L"Local Port", L"Local IP", L"Local IP Prefix Length", L"Remote Port", L"Remote IP", L"Remote IP Prefix Length" };
            push(reg(k, L"Version", 's', L"1.0")); push(reg(k, L"Application Name", 's', p[1]));
            for (auto s : star) push(reg(k, s, 's', L"*"));
            push(reg(k, L"DSCP Value", 's', L"46")); push(reg(k, L"Throttle Rate", 's', L"-1"));
            return;
        }
        if (v == L"PRINTV") {
            for (auto& t : splitWs(rest)) {
                std::wstring k = t == L"image" ? L"HKCR\\SystemFileAssociations\\image\\shell\\print" : L"HKCR\\" + t + L"\\shell\\print";
                push(reg(k, L"ProgrammaticAccessOnly", 's', L"", L"-"));
            }
            return;
        }
        if (v == L"SVCS") {
            auto p = splitBar(rest); int start = _wtoi(p[0].c_str());
            for (auto& t : splitWs(p.size() > 1 ? p[1] : L"")) {
                Act a; a.v = V_SVC; a.n = start; size_t col = t.find(L':');
                a.a = col == std::wstring::npos ? t : t.substr(0, col); a.n2 = col == std::wstring::npos ? -1 : _wtoi(t.substr(col + 1).c_str());
                push(a);
            }
            return;
        }
        if (v == L"SVCFIX") {
            u32 f = 0;
            for (auto& t : splitWs(rest)) {
                if (t == L"?hv") { f |= AF_HV; continue; }
                size_t e = t.find(L'='); if (e == std::wstring::npos) continue;
                Act a; a.v = V_SVCFIX; a.a = t.substr(0, e); a.n = _wtoi(t.substr(e + 1).c_str()); a.f = f; push(a);
            }
            return;
        }
        if (v == L"SVCCOPY" || v == L"SVCDEL" || v == L"DEFSVC" || v == L"START" || v == L"STOP") {
            Verb vv = v == L"SVCCOPY" ? V_SVCCOPY : v == L"SVCDEL" ? V_SVCDEL : v == L"DEFSVC" ? V_DEFSVC : v == L"START" ? V_START : V_STOP;
            for (auto& t : splitWs(rest)) push(simple(vv, t));
            return;
        }
        if (v == L"STOPDISABLED") { push(simple(V_STOPDIS)); return; }
        if (v == L"RESET") { push(simple(V_RESET)); return; }
        if (v == L"T") {
            size_t sp = rest.find(L' '); std::wstring op = rest.substr(0, sp), path = trim(rest.substr(sp + 1));
            Act a = simple(V_TASK, path); a.n = op == L"disable" ? 0 : op == L"enable" ? 1 : 2; push(a); return;
        }
        if (v == L"TIMER") { Act a = simple(V_TIMER); a.n = _wtoi(rest.c_str()); push(a); return; }
        if (v == L"SCHED") { auto p = splitBar(rest); push(simple(V_SCHED, p[0], p.size() > 1 ? p[1] : L"")); return; }
        if (v == L"A" || v == L"A!") { for (auto& t : splitWs(rest)) { Act a = simple(V_APPX, t); if (v == L"A!") a.f |= AF_FORCE; push(a); } return; }
        if (v == L"AREG") { push(simple(V_AREG, rest)); return; }
        if (v == L"ARESET") { for (auto& t : splitWs(rest)) push(simple(V_ARESET, t)); return; }
        if (v == L"CAP") { push(simple(V_CAP, rest)); return; }
        if (v == L"FEAT") { for (auto& t : splitWs(rest)) push(simple(V_FEAT, t)); return; }
        if (v == L"MMAGENT") { push(simple(V_MMAGENT, rest)); return; }
        if (v == L"BIND") { auto p = splitBar(rest); push(simple(V_BIND, p.size() > 1 ? p[1] : L"", p[0])); return; }
        if (v == L"DEVWAKE") { push(simple(V_DEVWAKE, rest)); return; }
        if (v == L"DEVOFF" || v == L"DEVON") { push(simple(v == L"DEVOFF" ? V_DEVOFF : V_DEVON, rest)); return; }
        if (v == L"KILL") { push(simple(V_KILL, rest)); return; }
        if (v == L"C") { push(simple(V_CMD, rest)); return; }
        if (v == L"BCD" || v == L"BCD-") { push(simple(V_BCD, rest)); return; }
        if (v == L"FSU") { push(simple(V_FSU, rest)); return; }
        if (v == L"RUNGLOB") { auto p = splitBar(rest); push(simple(V_RUNGLOB, p[0], p.size() > 1 ? p[1] : L"")); return; }
        if (v == L"RUNREG") { auto p = splitBar(rest); push(simple(V_RUNREG, keyPath(p[0]), p.size() > 1 ? p[1] : L"")); return; }
        if (v == L"MSIX") { push(simple(V_MSIX, rest)); return; }
        if (v == L"WINGET") {
            auto p = splitBar(rest); Act a = simple(V_WINGET, p[0]);
            if (p.size() > 1) a.f |= parseFilters(p[1]);
            push(a); return;
        }
        if (v == L"FWGROUP") { auto p = splitBar(rest); push(simple(V_FWGROUP, p.size() > 1 ? p[1] : L"", p[0])); return; }
        if (v == L"FILE") { auto p = splitBar(rest); push(simple(V_FILE, p[0], p.size() > 1 ? p[1] : L"")); return; }
        if (v == L"DELDIR") { push(simple(V_DELDIR, rest)); return; }
        if (v == L"DELFILE") { push(simple(V_DELFILE, rest)); return; }
        if (v == L"DELCONTENT") { push(simple(V_DELCONTENT, rest)); return; }
        if (v == L"DELGLOB") { push(simple(V_DELGLOB, rest)); return; }
        if (v == L"DELEMPTY") { push(simple(V_DELEMPTY, rest)); return; }
        if (v == L"FILEOFF") { push(simple(V_FILEOFF, rest)); return; }
        if (v == L"KFRESTORE") { push(simple(V_KFRESTORE, rest)); return; }
        if (v == L"LNKCLEAN") { push(simple(V_LNKCLEAN)); return; }
        if (v == L"EVTCLEAR") { push(simple(V_EVTCLEAR)); return; }
        if (v == L"CLEANMGR") { push(simple(V_CLEANMGR, rest)); return; }
        if (v == L"HOSTSCLEAN") { push(simple(V_HOSTSCLEAN, rest)); return; }
        if (v == L"EFIHIDE") { push(simple(V_EFIHIDE)); return; }
        if (v == L"MITOFF") { push(simple(V_MITOFF)); return; }
        if (v == L"MSI") { push(simple(V_MSI)); return; }
        if (v == L"CONSENT") { auto p = splitBar(rest); Act a = simple(V_CONSENT, p[0]); for (size_t i = 1; i < p.size(); i++) { if (p[i].rfind(L"allow=", 0) == 0) a.b = p[i].substr(6); else if (p[i].rfind(L"skip=", 0) == 0) a.c = p[i].substr(5); } push(a); return; }
        if (v == L"SPV") { for (auto& t : splitWs(rest)) push(simple(V_SPV, t)); return; }
        if (v == L"EXTDEL") { auto p = splitBar(rest); for (auto& e : splitWs(p.size() > 1 ? p[1] : L"")) push(simple(V_EXTDEL, e, p[0])); return; }
        if (v == L"ASETUP") { push(simple(V_ASETUP, rest)); return; }
        if (v == L"RUNDEL") { push(simple(V_RUNDEL, rest)); return; }
        if (v == L"KMATCH") { auto p = splitBar(rest); push(simple(V_KMATCH, keyPath(p[0]), p.size() > 1 ? p[1] : L"")); return; }
        if (v == L"PW") {
            auto t = splitWs(rest); if (t.size() < 4) return;
            Act a; a.v = V_PW; a.a = t[0]; a.b = t[1];
            a.n = t[2] == L"-" ? INT32_MIN : _wtoi(t[2].c_str()); a.n2 = t[3] == L"-" ? INT32_MIN : _wtoi(t[3].c_str());
            push(a); return;
        }
        if (v == L"PLAN") { auto p = splitBar(rest); push(simple(V_PLAN, p[0], p.size() > 1 ? p[1] : L"")); return; }
        if (v == L"PLANSET") { push(simple(V_PLANSET, rest)); return; }
        if (v == L"VERIFY") {
            auto p = splitBar(rest); auto t = splitWs(p[0]);
            Act a = simple(V_VERIFY, t.size() ? t[0] : L"", p.size() > 1 ? p[1] : L""); a.n = t.size() > 1 ? _wtoi(t[1].c_str()) : 4; push(a); return;
        }
        if (v == L"REFRESH") { push(simple(V_REFRESH)); return; }
        if (v == L"NVDRS") {
            auto p = splitBar(rest); auto t = splitWs(p[0]); if (t.size() < 2) return;
            Act a = simple(V_NVDRS, p.size() > 1 ? p[1] : L"", t[1]); a.n = (int)wcstoul(t[0].c_str(), nullptr, 0); push(a); return;
        }
        if (v == L"STARTUPOFF") { push(simple(V_STARTUPOFF, rest)); return; }
        if (v == L"GPUPREF") {
            auto p = splitBar(rest); if (p.size() < 2) return;
            Act a = simple(V_GPUPREF, p[1]); a.n = _wtoi(p[0].c_str()); push(a); return;
        }
    }
};

bool Catalog::load() {
    std::string comp = loadResource(L"PACK");
    if (comp.size() < 8) return false;
    deobf(comp);
    std::string text;
    DECOMPRESSOR_HANDLE dh = nullptr;
    if (!CreateDecompressor(COMPRESS_ALGORITHM_LZMS, nullptr, &dh)) return false;
    SIZE_T need = 0;
    Decompress(dh, comp.data(), comp.size(), nullptr, 0, &need);
    text.resize(need);
    BOOL ok = Decompress(dh, comp.data(), comp.size(), &text[0], need, &need);
    CloseDecompressor(dh);
    if (!ok) return false;
    text.resize(need);

    std::wstring w = u8to16(text);
    tweaks.reserve(512); tabs.reserve(16);
    Parser p(*this);
    size_t pos = 0;
    while (pos < w.size()) {
        size_t e = w.find(L'\n', pos);
        if (e == std::wstring::npos) e = w.size();
        p.line(w.substr(pos, e - pos));
        pos = e + 1;
    }
    for (auto& t : tweaks) {
        for (int pass = 0; pass < 4; pass++)
            for (auto& o : t.opts) {
                std::vector<Act> out; out.reserve(o.acts.size());
                for (auto& a : o.acts) {
                    if (a.v != V_COUNT) { out.push_back(a); continue; }
                    const Opt* src = nullptr;
                    for (auto& o2 : t.opts) if (o2.key == u16to8(a.a) && &o2 != &o) src = &o2;
                    if (!src) continue;
                    bool nested = false; for (auto& b : src->acts) if (b.v == V_COUNT) nested = true;
                    if (nested && pass < 3) { out.push_back(a); continue; }
                    for (auto& b : src->acts) if (b.v != V_COUNT) out.push_back(b);
                }
                o.acts.swap(out);
            }
    }
    for (int i = 0; i < (int)tweaks.size(); i++) {
        Tweak& t = tweaks[i]; t.idx = i; byId[t.id] = i;
        if (t.kind == TK_PICK) t.picked.assign(t.opts.size(), 0);
        Tab* tb = tab(t.tab); if (!tb) continue; tb->count++;
        for (auto& s : tb->subs) if (s.id == t.sub) { s.tweaks.push_back(i); break; }
    }
    return !tweaks.empty();
}

static std::wstring shortKey(const std::wstring& k) {
    struct R { const wchar_t* a; const wchar_t* b; };
    static const R rs[] = { {L"HKLM\\SOFTWARE\\Policies\\Microsoft\\Windows\\", L"Policy\\"}, {L"HKLM\\SOFTWARE\\Policies\\Microsoft\\", L"Policy\\"},
        {L"HKLM\\SYSTEM\\CurrentControlSet\\Services\\", L"Services\\"}, {L"HKLM\\SYSTEM\\CurrentControlSet\\Control\\", L"Control\\"},
        {L"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\", L"User\\"}, {L"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\", L"Windows\\"},
        {L"HKLM\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\", L"WindowsNT\\"}, {L"HKLM\\SOFTWARE\\Microsoft\\", L"Microsoft\\"} };
    for (auto& r : rs) { size_t n = wcslen(r.a); if (k.compare(0, n, r.a) == 0) return r.b + k.substr(n); }
    return k;
}
static std::wstring powerName(const std::wstring& g) {
    struct P { const wchar_t* id; const wchar_t* n; };
    static const P ps[] = {
        {L"0cc5b647", L"Core parking: minimum cores"}, {L"ea062031", L"Core parking: maximum cores"}, {L"be337238", L"Processor boost mode"}, {L"5d76a2ca", L"Processor idle disable"},
        {L"893dee8e", L"Minimum processor state"}, {L"bc5038f7", L"Maximum processor state"}, {L"45bcc044", L"Processor boost policy"}, {L"36687f9e", L"Energy performance preference"},
        {L"06cadf0e", L"Processor performance increase policy"}, {L"40fbefc7", L"Processor performance decrease policy"}, {L"4d2b0152", L"Performance time check interval"},
        {L"3b04d4fd", L"Processor idle demote threshold"}, {L"7f2f5cfa", L"Idle threshold scaling"}, {L"c4581c31", L"Idle time check"}, {L"4b70f900", L"Parking concurrency threshold"},
        {L"619b7505", L"Parking distribution threshold"}, {L"616cdaa5", L"Parking over-utilization threshold"}, {L"94d3a615", L"Latency sensitivity hint"},
        {L"ee12f906", L"PCI Express link state power management"}, {L"6738e2c4", L"Hard disk turn off after"}, {L"0853a681", L"USB hub selective suspend timeout"},
        {L"48e6b7a6", L"USB selective suspend"}, {L"d4e98f31", L"USB 3 link power management"}, {L"12bbebe6", L"Wireless adapter power saving"},
        {L"3c0bc021", L"Display brightness / dimming"}, {L"17aaa29b", L"Adaptive brightness"}, {L"29f6c1db", L"Sleep after"}, {L"9d7815a6", L"Hibernate after"},
        {L"94ac6d29", L"Allow hybrid sleep"}, {L"bd3b718a", L"Allow wake timers"}, {L"3619c3f2", L"Integrated graphics power policy"},
    };
    for (auto& p : ps) if (g.compare(0, 8, p.id) == 0) return p.n;
    return L"Power setting " + g.substr(0, 8);
}
static const wchar_t* startName(int s) { switch (s) { case 0: return L"Boot"; case 1: return L"System"; case 2: return L"Automatic"; case 3: return L"Manual"; default: return L"Disabled"; } }

std::wstring describeAct(const Act& a) {
    switch (a.v) {
    case V_REG: { std::wstring d = a.c; if (a.type == 'b' && d.size() > 24) d = d.substr(0, 24) + L"…"; if (d.size() > 60) d = d.substr(0, 60) + L"…"; return shortKey(a.a) + L"  ›  " + (a.b.empty() ? L"(default)" : a.b) + L" = " + (d.empty() ? L"\"\"" : d); }
    case V_REGDEL: return L"Remove " + shortKey(a.a) + L"  ›  " + a.b;
    case V_KEYDEL: return L"Remove key " + shortKey(a.a);
    case V_KEYADD: return L"Create key " + shortKey(a.a);
    case V_RBIT: return L"Set bit flag on " + shortKey(a.a) + L"  ›  " + a.b;
    case V_RSCAN: return L"Set " + a.c + L" = " + a.d + L" on every matching key under " + shortKey(a.a);
    case V_SVC: return L"Service " + a.a + L"  →  " + startName(a.n);
    case V_SVCFIX: return L"Repair service " + a.a + L" (only if disabled)  →  " + startName(a.n);
    case V_SVCCOPY: return L"Restore missing service key " + a.a;
    case V_SVCDEL: case V_DEFSVC: return L"Delete service " + a.a + L" (backed up)";
    case V_START: return L"Start service " + a.a;
    case V_STOP: return L"Stop service " + a.a;
    case V_STOPDIS: return L"Stop running services that are now disabled";
    case V_RESET: return L"Put every service above back to its Windows default";
    case V_TASK: return (a.n == 0 ? L"Disable task " : a.n == 1 ? L"Enable task " : L"Delete task ") + a.a;
    case V_TIMER: { if (!a.n) return L"Remove the timer resolution task"; wchar_t b[32]; swprintf(b, 32, L"%.3g", a.n / 10000.0); return L"Hold timer resolution at " + std::wstring(b) + L" ms (hidden logon task)"; }
    case V_SCHED: return L"Schedule " + a.b + L" cleanup (" + a.a + L")";
    case V_APPX: return (a.f & AF_FORCE ? L"Force-remove app " : L"Remove app ") + a.a;
    case V_AREG: return L"Re-register app " + a.a;
    case V_ARESET: return L"Reset app data " + a.a;
    case V_CAP: return L"Remove capability " + a.a;
    case V_FEAT: return L"Turn off Windows feature " + a.a;
    case V_MMAGENT: return L"Memory compression off";
    case V_BIND: return L"Unbind " + a.a + L" from wired adapters";
    case V_DEVWAKE: return L"Stop Windows from powering devices down";
    case V_DEVOFF: return L"Disable devices: " + a.a;
    case V_DEVON: return L"Re-enable devices: " + a.a;
    case V_KILL: return L"End process " + a.a;
    case V_CMD: return L"Run  " + a.a;
    case V_BCD: return L"bcdedit " + a.a;
    case V_FSU: return L"fsutil " + a.a;
    case V_RUNGLOB: return L"Run " + a.a + L" " + a.b;
    case V_RUNREG: return L"Run the uninstaller in " + shortKey(a.a);
    case V_MSIX: return L"Uninstall: " + a.a;
    case V_WINGET: return L"Install " + a.a + L" with winget";
    case V_FWGROUP: return L"Firewall rules \"" + a.a + L"\" " + a.b;
    case V_FILE: return L"Write " + a.a;
    case V_DELDIR: return L"Delete folder " + a.a;
    case V_DELFILE: return L"Delete file " + a.a;
    case V_DELCONTENT: return L"Empty folder " + a.a;
    case V_DELGLOB: return L"Delete " + a.a;
    case V_DELEMPTY: return L"Delete " + a.a + L" if empty";
    case V_FILEOFF: return L"Disable program " + a.a;
    case V_KFRESTORE: return L"Move Desktop, Documents, Pictures, Music, Videos out of " + a.a;
    case V_LNKCLEAN: return L"Remove broken Start menu shortcuts";
    case V_EVTCLEAR: return L"Clear all event logs";
    case V_CLEANMGR: return L"Run Disk Cleanup";
    case V_HOSTSCLEAN: return L"Remove " + a.a + L" lines from the hosts file (backup kept)";
    case V_EFIHIDE: return L"Remove the EFI partition drive letter";
    case V_MITOFF: return L"System exploit mitigations off";
    case V_MSI: return L"MSI interrupts for GPU, network, NVMe, audio and USB";
    case V_CONSENT: return L"Privacy switches: " + a.a + L" (except " + a.b + L")";
    case V_SPV: return L"Hide the " + a.a + L" page in Settings";
    case V_EXTDEL: return L"Remove " + a.b + L" from " + a.a + L" open-with list";
    case V_ASETUP: return L"Remove Active Setup entries: " + a.a;
    case V_RUNDEL: return L"Remove startup entries: " + a.a;
    case V_KMATCH: return L"Remove " + a.b + L" keys under " + shortKey(a.a);
    case V_PW: return powerName(a.b) + L":  plugged in " + (a.n == INT32_MIN ? L"unchanged" : std::to_wstring(a.n)) + L", battery " + (a.n2 == INT32_MIN ? L"unchanged" : std::to_wstring(a.n2));
    case V_PLAN: return L"Create and activate power plan \"" + a.b + L"\"";
    case V_PLANSET: return L"Activate power plan " + a.a;
    case V_VERIFY: return L"Check that " + a.a + L" is " + startName(a.n);
    case V_REFRESH: return L"Restart Explorer";
    case V_NVDRS: return L"NVIDIA profile  ›  " + a.a;
    case V_STARTUPOFF: return L"Turn off every app that starts with Windows (Run keys and Startup folders), except: " + a.a;
    case V_GPUPREF: { std::wstring f = a.a; size_t q = f.find_last_of(L'\\'); if (q != std::wstring::npos) f = f.substr(q + 1); return f + L"  ›  " + (a.n == 2 ? L"High-performance GPU" : L"Power-saving GPU") + L" (if installed)"; }
    default: return L"";
    }
}
