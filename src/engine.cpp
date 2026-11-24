#include "engine.h"
#include <taskschd.h>
#include <powrprof.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <winevt.h>
#include <shlobj.h>
#include <winioctl.h>
#include <tlhelp32.h>
#include <memory>

Engine g_eng;

static const GUID kCLSID_TaskScheduler = { 0x0f87369f, 0xa4e5, 0x4cfc, { 0xbd, 0x3e, 0x73, 0xe6, 0x15, 0x45, 0x72, 0xdd } };
static const GUID kIID_ITaskService = { 0x2faba4c7, 0x4da9, 0x4013, { 0x96, 0x97, 0x20, 0xcc, 0x3f, 0xd4, 0x0f, 0x85 } };

struct Journal {
    std::wstring file;
    std::vector<std::vector<std::wstring>> rows;
    std::set<std::wstring> seen;
    bool dirty = false;
    void load(const std::wstring& f) {
        file = f; rows.clear(); seen.clear();
        std::wstring all = u8to16(readFileBytes(f));
        size_t p = 0;
        while (p < all.size()) {
            size_t e = all.find(L'\n', p); if (e == std::wstring::npos) e = all.size();
            std::wstring ln = all.substr(p, e - p); p = e + 1;
            if (!ln.empty() && ln.back() == L'\r') ln.pop_back();
            if (ln.empty()) continue;
            std::vector<std::wstring> r; size_t q = 0;
            for (;;) { size_t t = ln.find(L'\t', q); r.push_back(ln.substr(q, t == std::wstring::npos ? std::wstring::npos : t - q)); if (t == std::wstring::npos) break; q = t + 1; }
            if (r.size() >= 2) seen.insert(r[0] + L"\t" + r[1] + (r.size() > 2 ? L"\t" + r[2] : L""));
            rows.push_back(r);
        }
    }
    bool has(const std::wstring& k) const { return seen.count(k) != 0; }
    void add(const std::vector<std::wstring>& r) {
        std::wstring k = r[0] + L"\t" + (r.size() > 1 ? r[1] : L"") + (r.size() > 2 ? L"\t" + r[2] : L"");
        if (r[0] != L"OPT" && seen.count(k)) return;
        seen.insert(k); rows.push_back(r); dirty = true;
    }
    void save() {
        if (!dirty) return;
        std::wstring s;
        for (auto& r : rows) { for (size_t i = 0; i < r.size(); i++) { if (i) s += L'\t'; s += r[i]; } s += L'\n'; }
        writeFileUtf8(file, u16to8(s)); dirty = false;
    }
};

struct Ctx {
    Journal* j = nullptr;
    const Tweak* t = nullptr;
    bool ti = false;
    bool inPlan = false; GUID plan{};
    bool refresh = false;
    int fails = 0;
    std::vector<std::wstring> warn;
};

static std::wstring guidStr(const GUID& g) { wchar_t b[64]; StringFromGUID2(g, b, 64); std::wstring s = b; return s.substr(1, s.size() - 2); }
static bool parseGuid(const std::wstring& s, GUID& g) { std::wstring b = s; if (b.empty()) return false; if (b[0] != L'{') b = L"{" + b + L"}"; return CLSIDFromString(b.c_str(), &g) == S_OK; }
static std::wstring hashName(const std::wstring& s) { u64 h = 1469598103934665603ull; for (wchar_t c : s) { h ^= c; h *= 1099511628211ull; } wchar_t b[32]; swprintf(b, 32, L"%016llx", h); return b; }

static void recordValue(Ctx& c, const RegPath& p, const std::wstring& name) {
    if (!c.j) return;
    std::wstring key = L"V\t" + regJoin(p) + L"\t" + name;
    if (c.j->has(key)) return;
    RegVal v = regRead(p, name);
    c.j->add({ L"V", regJoin(p), name, v.exists ? L"1" : L"0", std::to_wstring(v.type), u8to16(toHex(v.data)) });
}
static bool setValue(Ctx& c, const RegPath& p, const std::wstring& name, DWORD type, const std::string& data) {
    recordValue(c, p, name);
    bool ok = regWriteRaw(p, name, type, data, c.ti);
    if (!ok) c.fails++;
    return ok;
}
static bool delValue(Ctx& c, const RegPath& p, const std::wstring& name) {
    if (!regRead(p, name).exists) return true;
    recordValue(c, p, name);
    return regDeleteValue(p, name, c.ti);
}
static bool delKey(Ctx& c, const RegPath& p) {
    if (!regKeyExists(p)) return true;
    if (c.j && !c.j->has(L"K\t" + regJoin(p))) {
        std::wstring f = g_eng.stateDir + L"\\hive_" + hashName(regJoin(p)) + L".hiv";
        if (regSaveKey(p, f)) c.j->add({ L"K", regJoin(p), L"1", f });
    }
    return regDeleteTree(p, c.ti);
}
static bool addKey(Ctx& c, const RegPath& p) {
    if (regKeyExists(p)) return true;
    if (c.j) c.j->add({ L"K", regJoin(p), L"0", L"" });
    return regCreateKey(p, c.ti);
}

static bool isClassInstance(const std::wstring& name) { if (name.size() != 4) return false; for (wchar_t ch : name) if (!iswdigit(ch)) return false; return true; }
static bool nicWired(const RegPath& k) {
    std::wstring desc = regString(k, L"DriverDesc"), comp = regString(k, L"ComponentId");
    if (desc.empty()) return false;
    std::wstring t = lower(desc + L" " + comp);
    static const wchar_t* skip[] = { L"wlan", L"802.11", L"wi-fi", L"wifi", L"wireless", L"wan miniport", L"virtual", L"bluetooth", L"root\\", L"swd\\", L"tap-",
        L"hyper-v", L"vpn", L"loopback", L"kernel debug", L"teredo", L"isatap", L"6to4", L"wi-fi direct", L"npcap", L"wintun", L"wireguard" };
    for (auto s : skip) if (t.find(s) != std::wstring::npos) return false;
    for (auto& v : regValueNames(k)) if (v.size() > 7 && lower(v).find(L"phytype") != std::wstring::npos) return false;
    return true;
}
static bool nicFast(const RegPath& k) {
    std::wstring d = lower(regString(k, L"DriverDesc"));
    static const wchar_t* fast[] = { L"10g", L"25g", L"40g", L"100g", L"2.5g", L"5gbase", L"10gbase", L"nbase", L"aquantia", L"aqtion", L"x710", L"x550", L"x540", L"e810", L"bcm57", L"qlogic", L"mellanox", L"connectx" };
    for (auto f : fast) if (d.find(f) != std::wstring::npos) return true;
    return false;
}
static std::set<std::wstring> wiredGuids() {
    std::set<std::wstring> out;
    RegPath cls = regParse(L"HKLM\\SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e972-e325-11ce-bfc1-08002be10318}");
    for (auto& s : regSubkeys(cls)) {
        if (!isClassInstance(s)) continue;
        RegPath k = cls; k.sub += L"\\" + s;
        if (nicWired(k)) { std::wstring g = lower(regString(k, L"NetCfgInstanceId")); if (!g.empty()) out.insert(g); }
    }
    return out;
}
static bool gpuMatch(const RegPath& k, u32 f) {
    std::wstring mid = lower(regString(k, L"MatchingDeviceId"));
    if ((f & AF_NV) && mid.find(L"ven_10de") == std::wstring::npos) return false;
    if ((f & AF_AMD) && mid.find(L"ven_1002") == std::wstring::npos) return false;
    if (f & AF_GEFORCE) {
        std::wstring d = lower(regString(k, L"DriverDesc"));
        static const wchar_t* ws[] = { L"quadro", L"tesla", L"rtx a", L"t1000", L"t400", L"a2000", L"a4000", L"a5000", L"a6000", L"h100", L"l40", L"a100" };
        for (auto w : ws) if (d.find(w) != std::wstring::npos) return false;
    }
    return true;
}

static void expandKeys(const std::wstring& pattern, u32 f, std::vector<RegPath>& out) {
    RegPath base = regParse(pattern);
    if (base.sub.find(L'*') == std::wstring::npos) { out.push_back(base); return; }
    std::vector<std::wstring> segs; size_t p = 0;
    for (;;) { size_t q = base.sub.find(L'\\', p); segs.push_back(base.sub.substr(p, q == std::wstring::npos ? std::wstring::npos : q - p)); if (q == std::wstring::npos) break; p = q + 1; }
    std::vector<RegPath> cur{ base }; cur[0].sub.clear();
    std::set<std::wstring> wired; bool wiredLoaded = false;
    for (size_t i = 0; i < segs.size(); i++) {
        std::vector<RegPath> next;
        for (auto& c : cur) {
            if (segs[i].find(L'*') == std::wstring::npos) { RegPath n = c; n.sub += (n.sub.empty() ? L"" : L"\\") + segs[i]; next.push_back(n); continue; }
            bool classDir = icontains(c.sub, L"\\Control\\Class\\{");
            for (auto& s : regSubkeys(c)) {
                if (!wildMatch(segs[i], s)) continue;
                RegPath n = c; n.sub += L"\\" + s;
                if (classDir && !isClassInstance(s)) continue;
                if (classDir && (f & AF_NIC) && !nicWired(n)) continue;
                if (classDir && (f & AF_SLOW) && nicFast(n)) continue;
                if (classDir && (f & (AF_NV | AF_AMD)) && !gpuMatch(n, f)) continue;
                if (f & AF_WIRED) { if (!wiredLoaded) { wired = wiredGuids(); wiredLoaded = true; } if (!wired.count(lower(s))) continue; }
                next.push_back(n);
            }
        }
        cur.swap(next);
    }
    for (auto& c : cur) if (regKeyExists(c) || segs.back().find(L'*') == std::wstring::npos) out.push_back(c);
}

static void deepWalk(Ctx& c, const RegPath& k, const std::wstring& name, DWORD type, const std::string& data, int depth) {
    if (depth > 8) return;
    RegVal v = regRead(k, name);
    if (v.exists && !sameValue(v, type, data)) {
        std::string d = data;
        if (v.type == REG_SZ) { std::wstring w = L"0"; d.assign((const char*)w.c_str(), 4); setValue(c, k, name, REG_SZ, d); }
        else setValue(c, k, name, v.type == REG_DWORD ? REG_DWORD : type, d);
    }
    for (auto& s : regSubkeys(k)) { RegPath n = k; n.sub += L"\\" + s; deepWalk(c, n, name, type, data, depth + 1); }
}

static bool applyReg(Ctx& c, const Act& a) {
    DWORD type; std::string data;
    if (!encodeValue(a.type, a.c, type, data)) return false;
    if (a.f & AF_DEEP) { deepWalk(c, regParse(a.a), a.b, type, data, 0); return true; }
    std::vector<RegPath> keys; expandKeys(a.a, a.f, keys);
    bool tiSave = c.ti; if (a.f & AF_TI) c.ti = true;
    for (auto& k : keys) {
        if (a.f & AF_EXISTS) {
            RegVal cur = regRead(k, a.b);
            if (!cur.exists) continue;
            if (cur.type == REG_DWORD) encodeValue('d', a.c, type, data);
            else { encodeValue('s', a.c, type, data); type = cur.type == REG_EXPAND_SZ ? REG_EXPAND_SZ : REG_SZ; }
        }
        setValue(c, k, a.b, type, data);
    }
    c.ti = tiSave;
    return true;
}

static const wchar_t* SVC_ROOT = L"HKLM\\SYSTEM\\CurrentControlSet\\Services";
static std::vector<std::wstring> serviceNames(const std::wstring& pat) {
    std::vector<std::wstring> out;
    if (pat.find_first_of(L"*?") == std::wstring::npos) { if (regKeyExists(regParse(std::wstring(SVC_ROOT) + L"\\" + pat))) out.push_back(pat); return out; }
    for (auto& s : regSubkeys(regParse(SVC_ROOT))) if (wildMatch(pat, s)) out.push_back(s);
    return out;
}
static void setStart(Ctx& c, const std::wstring& svc, DWORD start) {
    RegPath p = regParse(std::wstring(SVC_ROOT) + L"\\" + svc);
    if (regDword(p, L"Start", 99) == start) { recordValue(c, p, L"Start"); return; }
    std::string d((char*)&start, 4);
    setValue(c, p, L"Start", REG_DWORD, d);
}
static bool svcControl(const std::wstring& name, bool start) {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT); if (!scm) return false;
    SC_HANDLE s = OpenServiceW(scm, name.c_str(), start ? SERVICE_START : SERVICE_STOP | SERVICE_QUERY_STATUS);
    bool ok = false;
    if (s) {
        if (start) ok = StartServiceW(s, 0, nullptr) || GetLastError() == ERROR_SERVICE_ALREADY_RUNNING;
        else { SERVICE_STATUS st; ok = ControlService(s, SERVICE_CONTROL_STOP, &st) != 0; }
        CloseServiceHandle(s);
    }
    CloseServiceHandle(scm); return ok;
}
static void stopDisabled() {
    static const wchar_t* keep[] = { L"RpcSs", L"DcomLaunch", L"RpcEptMapper", L"LSM", L"BrokerInfrastructure", L"CoreMessagingRegistrar", L"SystemEventsBroker", L"Power",
        L"EventLog", L"SamSs", L"CryptSvc", L"gpsvc", L"ProfSvc", L"UserManager", L"Appinfo", L"Schedule", L"Winmgmt", L"PlugPlay", L"KeyIso", L"BFE", L"MpsSvc",
        L"Audiosrv", L"AudioEndpointBuilder", L"Dhcp", L"nsi", L"NlaSvc", L"netprofm", L"Netman", L"WlanSvc", L"Wcmsvc", L"TrustedInstaller", L"sppsvc", L"AppXSvc",
        L"ClipSVC", L"InstallService", L"StateRepository", L"DoSvc" };
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ENUMERATE_SERVICE); if (!scm) return;
    DWORD need = 0, cnt = 0, resume = 0;
    EnumServicesStatusExW(scm, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_ACTIVE, nullptr, 0, &need, &cnt, &resume, nullptr);
    std::vector<BYTE> buf(need + 1024);
    if (EnumServicesStatusExW(scm, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_ACTIVE, buf.data(), (DWORD)buf.size(), &need, &cnt, &resume, nullptr)) {
        auto* e = (ENUM_SERVICE_STATUS_PROCESSW*)buf.data();
        for (DWORD i = 0; i < cnt; i++) {
            std::wstring n = e[i].lpServiceName; bool k = false;
            for (auto x : keep) if (!_wcsicmp(x, n.c_str())) k = true;
            if (k) continue;
            if (regDword(regParse(std::wstring(SVC_ROOT) + L"\\" + n), L"Start", 0) == 4) svcControl(n, false);
        }
    }
    CloseServiceHandle(scm);
}

struct TaskSvc {
    ITaskService* svc = nullptr;
    bool init() {
        if (svc) return true;
        if (FAILED(CoCreateInstance(kCLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER, kIID_ITaskService, (void**)&svc))) return false;
        VARIANT e; VariantInit(&e);
        if (FAILED(svc->Connect(e, e, e, e))) { svc->Release(); svc = nullptr; return false; }
        return true;
    }
    ITaskFolder* folder(const std::wstring& path) {
        ITaskFolder* f = nullptr; BSTR b = SysAllocString(path.empty() ? L"\\" : path.c_str());
        svc->GetFolder(b, &f); SysFreeString(b); return f;
    }
    void collect(ITaskFolder* f, const std::wstring& namePat, bool recurse, std::vector<std::wstring>& out) {
        IRegisteredTaskCollection* tc = nullptr;
        if (SUCCEEDED(f->GetTasks(TASK_ENUM_HIDDEN, &tc)) && tc) {
            LONG n = 0; tc->get_Count(&n);
            for (LONG i = 1; i <= n; i++) {
                VARIANT v; VariantInit(&v); v.vt = VT_I4; v.lVal = i;
                IRegisteredTask* t = nullptr;
                if (SUCCEEDED(tc->get_Item(v, &t)) && t) {
                    BSTR nm = nullptr, pth = nullptr; t->get_Name(&nm); t->get_Path(&pth);
                    if (nm && pth && (namePat.empty() || wildMatch(namePat, nm))) out.push_back(pth);
                    SysFreeString(nm); SysFreeString(pth); t->Release();
                }
            }
            tc->Release();
        }
        if (!recurse) return;
        ITaskFolderCollection* fc = nullptr;
        if (SUCCEEDED(f->GetFolders(0, &fc)) && fc) {
            LONG n = 0; fc->get_Count(&n);
            for (LONG i = 1; i <= n; i++) {
                VARIANT v; VariantInit(&v); v.vt = VT_I4; v.lVal = i;
                ITaskFolder* sub = nullptr;
                if (SUCCEEDED(fc->get_Item(v, &sub)) && sub) { collect(sub, L"", true, out); sub->Release(); }
            }
            fc->Release();
        }
    }
    std::vector<std::wstring> match(const std::wstring& pat) {
        std::vector<std::wstring> out;
        if (!init()) return out;
        size_t s = pat.find_last_of(L'\\');
        std::wstring fold = s == 0 ? L"\\" : pat.substr(0, s), name = pat.substr(s + 1);
        bool recurse = name.empty();
        if (!recurse && name.find_first_of(L"*?") == std::wstring::npos) {
            ITaskFolder* f = folder(fold); if (!f) return out;
            IRegisteredTask* t = nullptr; BSTR b = SysAllocString(name.c_str());
            if (SUCCEEDED(f->GetTask(b, &t)) && t) { out.push_back(pat); t->Release(); }
            SysFreeString(b); f->Release(); return out;
        }
        ITaskFolder* f = folder(fold); if (!f) return out;
        collect(f, name, recurse, out); f->Release();
        return out;
    }
    IRegisteredTask* get(const std::wstring& path) {
        if (!init()) return nullptr;
        size_t s = path.find_last_of(L'\\');
        ITaskFolder* f = folder(s == 0 ? L"\\" : path.substr(0, s)); if (!f) return nullptr;
        IRegisteredTask* t = nullptr; BSTR b = SysAllocString(path.substr(s + 1).c_str());
        f->GetTask(b, &t); SysFreeString(b); f->Release(); return t;
    }
    int enabled(const std::wstring& path) {
        IRegisteredTask* t = get(path); if (!t) return -1;
        VARIANT_BOOL e = VARIANT_FALSE; t->get_Enabled(&e); t->Release(); return e ? 1 : 0;
    }
    bool setEnabled(const std::wstring& path, bool on) {
        IRegisteredTask* t = get(path); if (!t) return false;
        HRESULT hr = t->put_Enabled(on ? VARIANT_TRUE : VARIANT_FALSE); t->Release();
        if (hr == E_ACCESSDENIED) {
            TIScope ti; IRegisteredTask* t2 = get(path);
            if (t2) { hr = t2->put_Enabled(on ? VARIANT_TRUE : VARIANT_FALSE); t2->Release(); }
            if (FAILED(hr)) { ProcResult r = runHidden(L"schtasks.exe /Change /TN \"" + path + L"\" " + (on ? L"/ENABLE" : L"/DISABLE")); return r.code == 0; }
        }
        return SUCCEEDED(hr);
    }
    std::wstring xml(const std::wstring& path) {
        IRegisteredTask* t = get(path); if (!t) return L"";
        BSTR b = nullptr; t->get_Xml(&b); std::wstring s = b ? b : L""; SysFreeString(b); t->Release(); return s;
    }
    bool remove(const std::wstring& path) {
        if (!init()) return false;
        size_t s = path.find_last_of(L'\\');
        ITaskFolder* f = folder(s == 0 ? L"\\" : path.substr(0, s)); if (!f) return false;
        BSTR b = SysAllocString(path.substr(s + 1).c_str());
        HRESULT hr = f->DeleteTask(b, 0);
        if (hr == E_ACCESSDENIED) { TIScope ti; hr = f->DeleteTask(b, 0); }
        SysFreeString(b); f->Release(); return SUCCEEDED(hr);
    }
    bool restore(const std::wstring& path, const std::wstring& xmlText) {
        if (!init() || xmlText.empty()) return false;
        size_t s = path.find_last_of(L'\\');
        ITaskFolder* f = folder(s == 0 ? L"\\" : path.substr(0, s));
        if (!f) { ITaskFolder* root = folder(L"\\"); if (root) { BSTR fp = SysAllocString(path.substr(0, s).c_str()); VARIANT e; VariantInit(&e); root->CreateFolder(fp, e, &f); SysFreeString(fp); root->Release(); } }
        if (!f) return false;
        BSTR n = SysAllocString(path.substr(s + 1).c_str()), x = SysAllocString(xmlText.c_str());
        VARIANT e; VariantInit(&e); IRegisteredTask* out = nullptr;
        HRESULT hr = f->RegisterTask(n, x, TASK_CREATE_OR_UPDATE, e, e, TASK_LOGON_NONE, e, &out);
        if (out) out->Release(); SysFreeString(n); SysFreeString(x); f->Release();
        return SUCCEEDED(hr);
    }
};
static TaskSvc s_tasks;

static void taskAction(Ctx& c, const Act& a) {
    for (auto& path : s_tasks.match(a.a)) {
        if (a.n == 2) {
            std::wstring x = s_tasks.xml(path);
            if (c.j && !x.empty()) { std::wstring f = g_eng.stateDir + L"\\task_" + hashName(path) + L".xml"; writeFileUtf8(f, u16to8(x)); c.j->add({ L"TD", path, f }); }
            if (!s_tasks.remove(path)) c.fails++;
        } else {
            int cur = s_tasks.enabled(path);
            if (cur < 0) continue;
            if (c.j) c.j->add({ L"T", path, std::to_wstring(cur) });
            if (cur != a.n && !s_tasks.setEnabled(path, a.n == 1)) c.fails++;
        }
    }
}

static GUID powerSub(const std::wstring& alias) {
    struct A { const wchar_t* n; const wchar_t* g; };
    static const A al[] = { {L"proc", L"54533251-82be-4824-96c1-47b60b740d00"}, {L"pcie", L"501a4d13-42af-4429-9fd1-a8218c268e20"}, {L"disk", L"0012ee47-9041-4b5d-9b77-535fba8b1442"},
        {L"usb", L"2a737441-1930-4402-8d77-b2bebba308a3"}, {L"wifi", L"19cbb8fa-5279-450e-9fac-8a3d5fedd0c1"}, {L"disp", L"7516b95f-f776-4464-8c53-06167f40cc99"},
        {L"sleep", L"238c9fa8-0aad-41ed-83f4-97be242c8f20"}, {L"battery", L"e73a048d-bf27-4f12-9731-8b2076e8891f"}, {L"igpu", L"44f3beca-a7c0-460e-9df2-bb8b99e0cba6"} };
    GUID g{};
    for (auto& x : al) if (alias == x.n) { parseGuid(x.g, g); return g; }
    parseGuid(alias, g); return g;
}
static GUID activeScheme() { GUID* p = nullptr; GUID g{}; if (PowerGetActiveScheme(nullptr, &p) == ERROR_SUCCESS && p) { g = *p; LocalFree(p); } return g; }
static bool schemeExists(const GUID& g) { DWORD sz = 0; return PowerReadFriendlyName(nullptr, &g, nullptr, nullptr, nullptr, &sz) == ERROR_SUCCESS; }

static void powerSet(Ctx& c, const Act& a) {
    GUID scheme = c.inPlan ? c.plan : activeScheme();
    GUID sub = powerSub(a.a), set{}; parseGuid(a.b, set);
    if (!c.inPlan && c.j) {
        DWORD ac = 0, dc = 0;
        bool hasAc = PowerReadACValueIndex(nullptr, &scheme, &sub, &set, &ac) == ERROR_SUCCESS;
        bool hasDc = PowerReadDCValueIndex(nullptr, &scheme, &sub, &set, &dc) == ERROR_SUCCESS;
        c.j->add({ L"PWV", guidStr(scheme), guidStr(sub), guidStr(set), hasAc ? std::to_wstring(ac) : L"-", hasDc ? std::to_wstring(dc) : L"-" });
    }
    if (a.n != INT32_MIN) PowerWriteACValueIndex(nullptr, &scheme, &sub, &set, (DWORD)a.n);
    if (a.n2 != INT32_MIN) PowerWriteDCValueIndex(nullptr, &scheme, &sub, &set, (DWORD)a.n2);
    PowerSetActiveScheme(nullptr, &scheme);
}
static void powerPlan(Ctx& c, const Act& a) {
    GUID g{}; if (!parseGuid(a.a, g)) return;
    GUID prev = activeScheme();
    if (c.j && !c.j->has(L"PACT\t" + guidStr(prev))) c.j->add({ L"PACT", guidStr(prev) });
    if (!schemeExists(g)) {
        const wchar_t* bases[] = { L"e9a42b02-d5df-448d-aa00-03f14749eb61", L"8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c", L"381b4222-f694-41f0-9685-ff5bb260df2e" };
        for (auto b : bases) { runHidden(L"powercfg.exe /duplicatescheme " + std::wstring(b) + L" " + a.a); if (schemeExists(g)) break; }
        if (!schemeExists(g)) { c.fails++; return; }
        if (c.j) c.j->add({ L"PNEW", a.a });
    }
    std::wstring nm = a.b; PowerWriteFriendlyName(nullptr, &g, nullptr, nullptr, (UCHAR*)nm.c_str(), (DWORD)((nm.size() + 1) * 2));
    PowerSetActiveScheme(nullptr, &g);
    c.inPlan = true; c.plan = g;
}

static void devicesByName(const std::wstring& patterns, std::function<void(HDEVINFO, SP_DEVINFO_DATA&, const std::wstring&)> fn) {
    std::vector<std::wstring> pats; size_t p = 0;
    for (;;) { size_t q = patterns.find(L'|', p); pats.push_back(patterns.substr(p, q == std::wstring::npos ? std::wstring::npos : q - p)); if (q == std::wstring::npos) break; p = q + 1; }
    HDEVINFO h = SetupDiGetClassDevsW(nullptr, nullptr, nullptr, DIGCF_ALLCLASSES);
    if (h == INVALID_HANDLE_VALUE) return;
    SP_DEVINFO_DATA d{ sizeof(d) };
    for (DWORD i = 0; SetupDiEnumDeviceInfo(h, i, &d); i++) {
        wchar_t nm[512] = {}; DWORD t;
        if (!SetupDiGetDeviceRegistryPropertyW(h, &d, SPDRP_FRIENDLYNAME, &t, (BYTE*)nm, sizeof(nm), nullptr))
            SetupDiGetDeviceRegistryPropertyW(h, &d, SPDRP_DEVICEDESC, &t, (BYTE*)nm, sizeof(nm), nullptr);
        for (auto& pt : pats) if (!pt.empty() && wildMatch(pt, nm)) { wchar_t id[512] = {}; SetupDiGetDeviceInstanceIdW(h, &d, id, 512, nullptr); fn(h, d, id); break; }
    }
    SetupDiDestroyDeviceInfoList(h);
}
static bool deviceState(HDEVINFO h, SP_DEVINFO_DATA& d, bool enable) {
    SP_PROPCHANGE_PARAMS pc{}; pc.ClassInstallHeader.cbSize = sizeof(SP_CLASSINSTALL_HEADER); pc.ClassInstallHeader.InstallFunction = DIF_PROPERTYCHANGE;
    pc.StateChange = enable ? DICS_ENABLE : DICS_DISABLE; pc.Scope = DICS_FLAG_GLOBAL;
    return SetupDiSetClassInstallParamsW(h, &d, &pc.ClassInstallHeader, sizeof(pc)) && SetupDiCallClassInstaller(DIF_PROPERTYCHANGE, h, &d);
}
static bool deviceById(const std::wstring& id, bool enable) {
    HDEVINFO h = SetupDiCreateDeviceInfoList(nullptr, nullptr); if (h == INVALID_HANDLE_VALUE) return false;
    SP_DEVINFO_DATA d{ sizeof(d) }; bool ok = false;
    if (SetupDiOpenDeviceInfoW(h, id.c_str(), nullptr, 0, &d)) ok = deviceState(h, d, enable);
    SetupDiDestroyDeviceInfoList(h); return ok;
}

static std::wstring psq(const std::wstring& s) { std::wstring o = L"'"; for (wchar_t c : s) { if (c == L'\'') o += L"''"; else o += c; } return o + L"'"; }
static void lines(const std::string& out, std::function<void(const std::wstring&)> fn) {
    std::wstring w = u8to16(out); size_t p = 0;
    while (p < w.size()) { size_t e = w.find(L'\n', p); if (e == std::wstring::npos) e = w.size(); std::wstring l = w.substr(p, e - p); if (!l.empty() && l.back() == L'\r') l.pop_back(); if (!l.empty()) fn(l); p = e + 1; }
}
static void appxRemove(Ctx& c, const std::vector<const Act*>& acts) {
    std::wstring pats, force;
    for (auto a : acts) { pats += (pats.empty() ? L"" : L",") + psq(a->a); if (a->f & AF_FORCE) force += (force.empty() ? L"" : L",") + psq(a->a); }
    std::wstring s =
        L"$store='HKLM:\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Appx\\AppxAllUserStore';$force=@(" + force + L");"
        L"$sids=@('S-1-5-18')+@(Get-ChildItem $store | ? { $_.PSChildName -like 'S-1-5-21-*' } | % { $_.PSChildName });"
        L"foreach($p in @(" + pats + L")){"
        L" $pk=@(Get-AppxPackage -AllUsers -Name $p);"
        L" if($force -contains $p){ foreach($x in $pk){ foreach($s in $sids){ New-Item ($store+'\\EndOfLife\\'+$s+'\\'+$x.PackageFullName) -Force | Out-Null } } }"
        L" foreach($x in $pk){ Remove-AppxPackage -Package $x.PackageFullName -AllUsers; New-Item ($store+'\\Deprovisioned\\'+$x.PackageFamilyName) -Force | Out-Null; 'REMOVED|'+$x.Name }"
        L" Get-AppxProvisionedPackage -Online | ? { $_.DisplayName -like $p } | % { Remove-AppxProvisionedPackage -Online -PackageName $_.PackageName -AllUsers | Out-Null; 'DEPROV|'+$_.DisplayName }"
        L"}";
    ProcResult r = runPowerShell(s);
    lines(r.out, [&](const std::wstring& l) { if (l.rfind(L"REMOVED|", 0) == 0 && c.j) c.j->add({ L"APPX", l.substr(8) }); });
}
static void featuresOff(Ctx& c, const std::vector<const Act*>& acts) {
    std::wstring names; for (auto a : acts) names += (names.empty() ? L"" : L",") + psq(a->a);
    ProcResult r = runPowerShell(L"foreach($f in @(" + names + L")){ Get-WindowsOptionalFeature -Online -FeatureName $f | ? { $_.State -eq 'Enabled' } | % { Disable-WindowsOptionalFeature -Online -FeatureName $_.FeatureName -NoRestart | Out-Null; 'OFF|'+$_.FeatureName } }", 1800000);
    lines(r.out, [&](const std::wstring& l) { if (l.rfind(L"OFF|", 0) == 0 && c.j) c.j->add({ L"FEAT", l.substr(4) }); });
}

static void fileOff(Ctx& c, const std::wstring& pattern) {
    for (auto& f : globPaths(pattern)) {
        if (!fileExists(f)) continue;
        std::wstring exe = f.substr(f.find_last_of(L'\\') + 1); killProcesses(exe.substr(0, exe.find_last_of(L'.')));
        std::wstring to = f + L".cr1mixoff";
        bool ok = MoveFileExW(f.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
        if (!ok) { TIScope t; ok = MoveFileExW(f.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING) != 0; }
        if (ok && c.j) c.j->add({ L"F", to, f }); else if (!ok) c.fails++;
    }
}

static void restoreKnownFolders(const std::wstring& marker) {
    struct KF { const wchar_t* sub; const wchar_t* name; const wchar_t* guid; };
    static const KF kfs[] = { {L"Desktop", L"Desktop", L"{B4BFCC3A-DB2C-424C-B029-7FE99A87C641}"}, {L"Documents", L"Personal", L"{F42EE2D3-909F-4907-8871-4C22FC0BF756}"},
        {L"Pictures", L"My Pictures", L"{0DDD015D-B06C-45D5-8C4C-F59713854639}"}, {L"Music", L"My Music", L"{A0C69A99-21C8-4671-8703-7934162FCF1D}"}, {L"Videos", L"My Video", L"{35286A68-3C57-41A1-BBB1-0EAE73D76C95}"} };
    RegPath usf = regParse(L"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\User Shell Folders");
    RegPath shf = regParse(L"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Shell Folders");
    std::wstring prof = expandEnv(L"%USERPROFILE%");
    for (auto& k : kfs) {
        std::wstring raw = regString(usf, k.name);
        if (raw.empty()) raw = regString(usf, k.guid);
        if (!icontains(raw, marker)) continue;
        std::wstring src = expandEnv(raw), dst = prof + L"\\" + k.sub;
        ensureDir(dst);
        if (dirExists(src) && _wcsicmp(src.c_str(), dst.c_str())) {
            std::wstring from = src + L"\\*"; from.push_back(0);
            std::wstring to = dst; to.push_back(0);
            SHFILEOPSTRUCTW op{}; op.wFunc = FO_MOVE; op.pFrom = from.c_str(); op.pTo = to.c_str();
            op.fFlags = FOF_NO_UI | FOF_NOCONFIRMMKDIR | FOF_RENAMEONCOLLISION;
            SHFileOperationW(&op);
        }
        std::wstring v = L"%USERPROFILE%\\" + std::wstring(k.sub); DWORD t; std::string d;
        encodeValue('x', v, t, d); regWriteRaw(usf, k.name, t, d, false);
        if (!regString(usf, k.guid).empty()) regWriteRaw(usf, k.guid, t, d, false);
        encodeValue('s', dst, t, d); regWriteRaw(shf, k.name, t, d, false);
    }
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}

static int cleanLinks(const std::wstring& dir) {
    int n = 0;
    WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileExW((dir + L"\\*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, 0);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
        std::wstring p = dir + L"\\" + fd.cFileName;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { n += cleanLinks(p); RemoveDirectoryW(p.c_str()); continue; }
        if (!icontains(p, L".lnk")) continue;
        IShellLinkW* sl = nullptr;
        if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (void**)&sl))) {
            IPersistFile* pf = nullptr;
            if (SUCCEEDED(sl->QueryInterface(IID_IPersistFile, (void**)&pf))) {
                if (SUCCEEDED(pf->Load(p.c_str(), STGM_READ))) {
                    wchar_t tgt[MAX_PATH] = {}; sl->GetPath(tgt, MAX_PATH, nullptr, SLGP_RAWPATH);
                    std::wstring t = expandEnv(tgt);
                    if (t.size() > 3 && t[1] == L':' && GetFileAttributesW(t.c_str()) == INVALID_FILE_ATTRIBUTES && GetDriveTypeW(t.substr(0, 3).c_str()) == DRIVE_FIXED) { pf->Release(); pf = nullptr; if (DeleteFileW(p.c_str())) n++; }
                }
                if (pf) pf->Release();
            }
            sl->Release();
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h); return n;
}

static void hideEfi() {
    wchar_t vol[MAX_PATH]; HANDLE fv = FindFirstVolumeW(vol, MAX_PATH);
    if (fv == INVALID_HANDLE_VALUE) return;
    static const GUID efi = { 0xc12a7328, 0xf81f, 0x11d2, { 0xba, 0x4b, 0x00, 0xa0, 0xc9, 0x3e, 0xc9, 0x3b } };
    do {
        std::wstring v = vol; std::wstring dev = v; if (!dev.empty() && dev.back() == L'\\') dev.pop_back();
        HANDLE h = CreateFileW(dev.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) continue;
        PARTITION_INFORMATION_EX pi{}; DWORD br;
        bool isEfi = DeviceIoControl(h, IOCTL_DISK_GET_PARTITION_INFO_EX, nullptr, 0, &pi, sizeof(pi), &br, nullptr) && pi.PartitionStyle == PARTITION_STYLE_GPT && IsEqualGUID(pi.Gpt.PartitionType, efi);
        CloseHandle(h);
        if (!isEfi) continue;
        wchar_t names[512] = {}; DWORD len = 0;
        if (GetVolumePathNamesForVolumeNameW(v.c_str(), names, 512, &len))
            for (wchar_t* p = names; *p; p += wcslen(p) + 1) if (wcslen(p) == 3) DeleteVolumeMountPointW(p);
    } while (FindNextVolumeW(fv, vol, MAX_PATH));
    FindVolumeClose(fv);
}

static void clearEventLogs() {
    EVT_HANDLE en = EvtOpenChannelEnum(nullptr, 0); if (!en) return;
    wchar_t buf[512]; DWORD used;
    while (EvtNextChannelPath(en, 512, buf, &used)) EvtClearLog(nullptr, buf, nullptr, 0);
    EvtClose(en);
}

static void runOwnTask(const std::wstring& name, const std::wstring& args, const std::wstring& schedule) {
    std::wstring dir = expandEnv(L"%ProgramData%\\cr1mix"); ensureDir(dir);
    std::wstring exe = dir + L"\\cr1mix.exe";
    CopyFileW(selfPath().c_str(), exe.c_str(), FALSE);
    runHidden(L"schtasks.exe /Create /F /TN \"cr1mix\\" + name + L"\" /TR \"\\\"" + exe + L"\\\" " + args + L"\" " + schedule + L" /RU SYSTEM /RL HIGHEST");
}

namespace nvdrs {
typedef void* (__cdecl* QI_t)(unsigned);
typedef int(__cdecl* Init_t)();
typedef int(__cdecl* Sess_t)(void**);
typedef int(__cdecl* Sess1_t)(void*);
typedef int(__cdecl* Base_t)(void*, void**);
typedef int(__cdecl* Set_t)(void*, void*, void*);
typedef int(__cdecl* Get_t)(void*, void*, unsigned, void*);
typedef int(__cdecl* Id_t)(void*, void*, unsigned);
union Val { unsigned u32; unsigned char bin[4100]; unsigned short str[2048]; };
struct Setting { unsigned version; unsigned short name[2048]; unsigned id, type, location, isCurrentPredefined, isPredefinedValid; Val pre, cur; };
static_assert(sizeof(Setting) == 12320, "NVDRS_SETTING_V1 layout");
static const unsigned kVer = (unsigned)sizeof(Setting) | (1u << 16);
static const int kNotFound = -160;

struct Session {
    HMODULE dll = nullptr; void* s = nullptr; void* base = nullptr;
    Sess1_t destroy = nullptr, load = nullptr, save = nullptr;
    Set_t set = nullptr; Get_t get = nullptr; Id_t del = nullptr, restore = nullptr;
    bool open() {
        dll = LoadLibraryW(L"nvapi64.dll"); if (!dll) return false;
        auto qi = (QI_t)GetProcAddress(dll, "nvapi_QueryInterface"); if (!qi) return false;
        auto init = (Init_t)qi(0x0150E828); auto create = (Sess_t)qi(0x0694D52E); auto getBase = (Base_t)qi(0xDA8466A0);
        destroy = (Sess1_t)qi(0xDAD9CFF8); load = (Sess1_t)qi(0x375DBD6B); save = (Sess1_t)qi(0xFCBC7E14);
        set = (Set_t)qi(0x577DD202); get = (Get_t)qi(0x73BF8338); del = (Id_t)qi(0xE4A26362); restore = (Id_t)qi(0x53F0381E);
        if (!init || !create || !getBase || !destroy || !load || !save || !set || !get || !del || !restore) return false;
        if (init() != 0 || create(&s) != 0 || !s) return false;
        if (load(s) != 0 || getBase(s, &base) != 0) return false;
        return true;
    }
    ~Session() { if (s && destroy) destroy(s); if (dll) FreeLibrary(dll); }
    int read(unsigned id, unsigned& v) {
        auto st = std::make_unique<Setting>(); memset(st.get(), 0, sizeof(Setting)); st->version = kVer;
        int r = get(s, base, id, st.get());
        if (r != 0) return 0;
        v = st->cur.u32; return st->isCurrentPredefined ? 1 : 2;
    }
    bool write(unsigned id, unsigned v) {
        auto st = std::make_unique<Setting>(); memset(st.get(), 0, sizeof(Setting)); st->version = kVer;
        st->id = id; st->type = 0; st->cur.u32 = v;
        return set(s, base, st.get()) == 0;
    }
};
static Session* s_shared = nullptr;
}
struct NvScope {
    nvdrs::Session* own = nullptr;
    NvScope() { if (g_eng.mc.nvidia && !nvdrs::s_shared) { own = new nvdrs::Session(); if (own->open()) nvdrs::s_shared = own; else { delete own; own = nullptr; } } }
    ~NvScope() { if (own) { nvdrs::s_shared = nullptr; delete own; } }
};
static void nvApply(Ctx& c, const std::vector<const Act*>& acts) {
    if (acts.empty()) return;
    nvdrs::Session ses;
    if (!ses.open()) { c.warn.push_back(L"NVIDIA profile: no NVIDIA driver found"); return; }
    for (auto* a : acts) {
        unsigned id = (unsigned)a->n, v = (unsigned)wcstoul(a->b.c_str(), nullptr, 0), old = 0;
        int st = ses.read(id, old);
        if (c.j && !c.j->has(L"NV\t" + std::to_wstring(id))) c.j->add({ L"NV", std::to_wstring(id), std::to_wstring(st), std::to_wstring(old) });
        if (!ses.write(id, v)) c.fails++;
    }
    if (ses.save(ses.s) != 0) c.fails++;
}
static void nvUndo(const std::vector<std::vector<std::wstring>>& rows) {
    if (rows.empty()) return;
    nvdrs::Session ses; if (!ses.open()) return;
    for (auto& r : rows) {
        unsigned id = (unsigned)wcstoul(r[1].c_str(), nullptr, 10);
        if (r[2] == L"2") ses.write(id, (unsigned)wcstoul(r[3].c_str(), nullptr, 10));
        else if (r[2] == L"1") ses.restore(ses.s, ses.base, id);
        else { if (ses.del(ses.s, ses.base, id) != 0) ses.restore(ses.s, ses.base, id); }
    }
    ses.save(ses.s);
}
static int nvCheck(const Act& a) {
    if (!nvdrs::s_shared) return -1;
    unsigned v = 0; int st = nvdrs::s_shared->read((unsigned)a.n, v);
    if (st == 0) return 0;
    return v == (unsigned)wcstoul(a.b.c_str(), nullptr, 0) ? 1 : 0;
}

static std::vector<std::wstring> steamLibraries() {
    std::vector<std::wstring> out;
    std::wstring sp = regString(regParse(L"HKCU\\Software\\Valve\\Steam"), L"SteamPath");
    for (auto& ch : sp) if (ch == L'/') ch = L'\\';
    if (sp.empty()) sp = expandEnv(L"%ProgramFiles(x86)%\\Steam");
    out.push_back(sp);
    std::wstring vdf = u8to16(readFileBytes(sp + L"\\steamapps\\libraryfolders.vdf"));
    size_t p = 0;
    while ((p = vdf.find(L"\"path\"", p)) != std::wstring::npos) {
        size_t q1 = vdf.find(L'"', p + 6); if (q1 == std::wstring::npos) break;
        size_t q2 = vdf.find(L'"', q1 + 1); if (q2 == std::wstring::npos) break;
        std::wstring lib = vdf.substr(q1 + 1, q2 - q1 - 1), clean;
        for (size_t i = 0; i < lib.size(); i++) { if (lib[i] == L'\\' && i + 1 < lib.size() && lib[i + 1] == L'\\') i++; clean += lib[i]; }
        bool dup = false; for (auto& o : out) if (!_wcsicmp(o.c_str(), clean.c_str())) dup = true;
        if (!dup && !clean.empty()) out.push_back(clean);
        p = q2 + 1;
    }
    return out;
}
static std::vector<std::wstring> appPaths(const std::wstring& pattern) {
    std::vector<std::wstring> out;
    if (pattern.rfind(L"%STEAM%", 0) == 0) { for (auto& lib : steamLibraries()) for (auto& f : globPaths(lib + pattern.substr(7))) out.push_back(f); }
    else out = globPaths(pattern);
    return out;
}
static void gpuPref(Ctx& c, const Act& a) {
    RegPath k = regParse(L"HKCU\\SOFTWARE\\Microsoft\\DirectX\\UserGpuPreferences");
    DWORD ty; std::string d;
    if (!encodeValue('s', L"GpuPreference=" + std::to_wstring(a.n) + L";", ty, d)) return;
    for (auto& f : appPaths(a.a)) setValue(c, k, f, ty, d);
}

static void startupOff(Ctx& c, const std::wstring& keepList) {
    std::vector<std::wstring> keep; { size_t p = 0; while (p < keepList.size()) { size_t e = keepList.find(L' ', p); if (e == std::wstring::npos) e = keepList.size(); if (e > p) keep.push_back(keepList.substr(p, e - p)); p = e + 1; } }
    auto kept = [&](const std::wstring& n) { for (auto& k : keep) if (wildMatch(k, n)) return true; return icontains(n, L"cr1mix"); };
    std::string off = fromHex("030000000000000000000000");
    auto approve = [&](const RegPath& ak, const std::wstring& name) {
        if (name.empty() || kept(name)) return;
        RegVal cur = regRead(ak, name);
        if (cur.exists && !cur.data.empty() && ((u8)cur.data[0] & 1)) return;
        setValue(c, ak, name, REG_BINARY, off);
    };
    struct K { const wchar_t* run; const wchar_t* appr; };
    const K keys[] = {
        { L"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Run", L"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run" },
        { L"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", L"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run" },
        { L"HKLM\\SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Run", L"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run32" },
    };
    for (auto& k : keys) { RegPath ak = regParse(k.appr); for (auto& n : regValueNames(regParse(k.run))) approve(ak, n); }
    struct F { const wchar_t* dir; const wchar_t* appr; };
    const F folders[] = {
        { L"%APPDATA%\\Microsoft\\Windows\\Start Menu\\Programs\\Startup", L"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\StartupFolder" },
        { L"%ProgramData%\\Microsoft\\Windows\\Start Menu\\Programs\\Startup", L"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\StartupFolder" },
    };
    for (auto& f : folders) {
        RegPath ak = regParse(f.appr);
        for (auto& p : globPaths(std::wstring(f.dir) + L"\\*")) {
            std::wstring n = p.substr(p.find_last_of(L'\\') + 1);
            if (!_wcsicmp(n.c_str(), L"desktop.ini")) continue;
            approve(ak, n);
        }
    }
}

static void execute(Ctx& c, const std::vector<Act>& acts, std::function<void(const Act&)> tick) {
    std::vector<const Act*> nv;
    for (size_t i = 0; i < acts.size(); i++) if (acts[i].v == V_NVDRS) nv.push_back(&acts[i]);
    nvApply(c, nv);
    for (size_t i = 0; i < acts.size(); i++) {
        const Act& a = acts[i];
        if (tick) tick(a);
        switch (a.v) {
        case V_REG: applyReg(c, a); break;
        case V_REGDEL: { std::vector<RegPath> keys; expandKeys(a.a, 0, keys); for (auto& k : keys) delValue(c, k, a.b); } break;
        case V_KEYADD: addKey(c, regParse(a.a)); break;
        case V_KEYDEL: delKey(c, regParse(a.a)); break;
        case V_RBIT: { RegPath p = regParse(a.a); DWORD v = regDword(p, a.b, 0) | (DWORD)a.n; setValue(c, p, a.b, REG_DWORD, std::string((char*)&v, 4)); } break;
        case V_RSCAN: {
            DWORD t; std::string d; encodeValue(a.type, a.d, t, d);
            std::function<void(const RegPath&, int)> walk = [&](const RegPath& k, int depth) {
                if (depth > 7) return;
                for (auto& s : regSubkeys(k)) {
                    RegPath n = k; n.sub += L"\\" + s;
                    if (a.b == L"?" ? regRead(n, a.c).exists : !_wcsicmp(s.c_str(), a.b.c_str())) setValue(c, n, a.c, t, d);
                    walk(n, depth + 1);
                }
            };
            walk(regParse(a.a), 0);
        } break;
        case V_MITOFF: {
            RegPath p = regParse(L"HKLM\\SYSTEM\\CurrentControlSet\\Control\\Session Manager\\kernel");
            RegVal au = regRead(p, L"MitigationAuditOptions");
            size_t n = au.exists && au.data.size() ? au.data.size() : 24;
            std::string b(n, (char)0x22);
            setValue(c, p, L"MitigationOptions", REG_BINARY, b); setValue(c, p, L"MitigationAuditOptions", REG_BINARY, b);
        } break;
        case V_SPV: {
            RegPath p = regParse(L"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer");
            std::wstring v = regString(p, L"SettingsPageVisibility");
            if (v.empty()) v = L"hide:" + a.a; else if (!icontains(v, a.a)) v += L";" + a.a; else break;
            DWORD t; std::string d; encodeValue('s', v, t, d); setValue(c, p, L"SettingsPageVisibility", t, d);
        } break;
        case V_EXTDEL: delValue(c, regParse(L"HKCR\\" + a.a + L"\\OpenWithProgids"), a.b); delValue(c, regParse(L"HKCR\\" + a.a + L"\\OpenWithProgIds"), a.b); break;
        case V_ASETUP: {
            std::vector<std::wstring> pats; size_t p = 0; for (;;) { size_t q = a.a.find(L'|', p); pats.push_back(a.a.substr(p, q == std::wstring::npos ? std::wstring::npos : q - p)); if (q == std::wstring::npos) break; p = q + 1; }
            for (auto root : { L"HKLM\\SOFTWARE\\Microsoft\\Active Setup\\Installed Components", L"HKLM\\SOFTWARE\\WOW6432Node\\Microsoft\\Active Setup\\Installed Components" }) {
                RegPath rp = regParse(root);
                for (auto& s : regSubkeys(rp)) {
                    RegPath k = rp; k.sub += L"\\" + s;
                    std::wstring t = regString(k, L"") + L" " + regString(k, L"StubPath");
                    for (auto& pt : pats) if (!pt.empty() && icontains(t, pt)) { delKey(c, k); break; }
                }
            }
        } break;
        case V_RUNDEL:
            for (auto root : { L"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", L"HKLM\\SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Run", L"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Run" }) {
                RegPath rp = regParse(root);
                for (auto& v : regValueNames(rp)) if (wildMatch(a.a, v)) delValue(c, rp, v);
            }
            break;
        case V_KMATCH: { RegPath rp = regParse(a.a); for (auto& s : regSubkeys(rp)) if (icontains(s, a.b)) { RegPath k = rp; k.sub += L"\\" + s; delKey(c, k); } } break;
        case V_CONSENT: {
            auto inList = [](const std::wstring& list, const std::wstring& x) { size_t p = 0; for (;;) { size_t q = list.find(L',', p); if (!_wcsicmp(list.substr(p, q == std::wstring::npos ? std::wstring::npos : q - p).c_str(), x.c_str())) return true; if (q == std::wstring::npos) return false; p = q + 1; } };
            bool deny = a.a == L"deny";
            for (auto root : { L"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\CapabilityAccessManager\\ConsentStore", L"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\CapabilityAccessManager\\ConsentStore" }) {
                RegPath rp = regParse(root);
                for (auto& cap : regSubkeys(rp)) {
                    if (inList(a.c, cap)) continue;
                    std::wstring val = (!deny || inList(a.b, cap)) ? L"Allow" : L"Deny";
                    RegPath k = rp; k.sub += L"\\" + cap; DWORD t; std::string d; encodeValue('s', val, t, d);
                    setValue(c, k, L"Value", t, d);
                    RegPath np = k; np.sub += L"\\NonPackaged"; if (regKeyExists(np)) setValue(c, np, L"Value", t, d);
                }
            }
        } break;
        case V_MSI: {
            if (g_eng.mc.vm) { c.warn.push_back(L"Virtual machine detected, MSI mode skipped"); break; }
            RegPath pci = regParse(L"HKLM\\SYSTEM\\CurrentControlSet\\Enum\\PCI");
            for (auto& dev : regSubkeys(pci)) for (auto& inst : regSubkeys(regParse(regJoin(pci) + L"\\" + dev))) {
                RegPath k = regParse(regJoin(pci) + L"\\" + dev + L"\\" + inst);
                std::wstring cls = regString(k, L"Class"), desc = regString(k, L"DeviceDesc"); size_t sc = desc.find(L';'); if (sc != std::wstring::npos) desc = desc.substr(sc + 1);
                std::wstring dl = lower(desc);
                bool gpu = !_wcsicmp(cls.c_str(), L"Display") || dl.find(L"geforce") != std::wstring::npos || dl.find(L"radeon") != std::wstring::npos;
                bool per = false; for (auto w : { L"high definition audio", L"nvm", L"ahci", L"sata", L"ethernet", L"network", L"gigabit", L"gbe", L"killer", L"usb", L"xhci", L"host controller" }) if (dl.find(w) != std::wstring::npos) per = true;
                if (!gpu && !per) continue;
                RegPath msi = regParse(regJoin(k) + L"\\Device Parameters\\Interrupt Management\\MessageSignaledInterruptProperties");
                bool known = gpu || dl.find(L"ethernet") != std::wstring::npos || dl.find(L"network") != std::wstring::npos || dl.find(L"nvm") != std::wstring::npos || dl.find(L"high definition audio") != std::wstring::npos;
                if (!regKeyExists(msi) && !known) continue;
                DWORD one = 1; setValue(c, msi, L"MSISupported", REG_DWORD, std::string((char*)&one, 4));
                delValue(c, msi, L"MessageNumberLimit");
                if (!g_eng.mc.laptop) { DWORD pr = gpu ? 3 : 0; RegPath ap = regParse(regJoin(k) + L"\\Device Parameters\\Interrupt Management\\Affinity Policy"); setValue(c, ap, L"DevicePriority", REG_DWORD, std::string((char*)&pr, 4)); }
            }
        } break;

        case V_SVC:
            if (c.t && c.t->id == "win.svcpreset" && g_eng.keepsService(a.a)) break;
            for (auto& s : serviceNames(a.a)) { if (c.t && c.t->id == "win.svcpreset" && g_eng.keepsService(s)) continue; setStart(c, s, (DWORD)a.n); }
            break;
        case V_SVCFIX: {
            if ((a.f & AF_HV) && !g_eng.mc.hypervisor) break;
            RegPath p = regParse(std::wstring(SVC_ROOT) + L"\\" + a.a);
            if (regKeyExists(p) && regDword(p, L"Start", 0) == 4) setStart(c, a.a, (DWORD)a.n);
        } break;
        case V_SVCCOPY: {
            RegPath dst = regParse(std::wstring(SVC_ROOT) + L"\\" + a.a), src = regParse(L"HKLM\\SYSTEM\\ControlSet001\\Services\\" + a.a);
            if (!regKeyExists(dst) && regKeyExists(src)) { HKEY s, d; if (RegOpenKeyExW(src.root, src.sub.c_str(), 0, KEY_READ | KEY_WOW64_64KEY, &s) == ERROR_SUCCESS) { if (RegCreateKeyExW(dst.root, dst.sub.c_str(), 0, nullptr, 0, KEY_ALL_ACCESS | KEY_WOW64_64KEY, nullptr, &d, nullptr) == ERROR_SUCCESS) { RegCopyTreeW(s, nullptr, d); RegCloseKey(d); } RegCloseKey(s); } }
        } break;
        case V_SVCDEL: case V_DEFSVC:
            for (auto& s : serviceNames(a.a)) { svcControl(s, false); delKey(c, regParse(std::wstring(SVC_ROOT) + L"\\" + s)); }
            break;
        case V_START: svcControl(a.a, true); break;
        case V_STOP: svcControl(a.a, false); break;
        case V_STOPDIS: stopDisabled(); break;
        case V_RESET:
            if (c.t) for (auto& o : c.t->opts) for (auto& x : o.acts) if (x.v == V_SVC && x.n2 >= 0) for (auto& s : serviceNames(x.a)) setStart(c, s, (DWORD)x.n2);
            break;

        case V_TASK: taskAction(c, a); break;
        case V_TIMER:
            if (a.n == 0) { runHidden(L"schtasks.exe /Delete /F /TN \"cr1mix\\TimerResolution\""); killProcesses(L"cr1mix"); }
            else { runOwnTask(L"TimerResolution", L"--timer " + std::to_wstring(a.n), L"/SC ONLOGON"); runHidden(L"schtasks.exe /Run /TN \"cr1mix\\TimerResolution\""); if (c.j) c.j->add({ L"OWNTASK", L"TimerResolution" }); }
            break;
        case V_SCHED: runOwnTask(a.b == L"shader" ? L"ShaderCleanup" : a.b, L"--clean " + a.b, L"/SC WEEKLY /D SUN /ST 03:00"); if (c.j) c.j->add({ L"OWNTASK", a.b == L"shader" ? L"ShaderCleanup" : a.b }); break;

        case V_APPX: {
            std::vector<const Act*> batch{ &a };
            while (i + 1 < acts.size() && acts[i + 1].v == V_APPX) batch.push_back(&acts[++i]);
            appxRemove(c, batch);
        } break;
        case V_FEAT: {
            std::vector<const Act*> batch{ &a };
            while (i + 1 < acts.size() && acts[i + 1].v == V_FEAT) batch.push_back(&acts[++i]);
            featuresOff(c, batch);
        } break;
        case V_AREG: runPowerShell(L"Get-AppxPackage -AllUsers -Name " + psq(a.a) + L" | % { Add-AppxPackage -Register (Join-Path $_.InstallLocation 'AppXManifest.xml') -DisableDevelopmentMode }"); break;
        case V_ARESET: runPowerShell(L"Get-AppxPackage -AllUsers -Name " + psq(a.a) + L" | Reset-AppxPackage"); break;
        case V_CAP: { ProcResult r = runPowerShell(L"Get-WindowsCapability -Online -Name " + psq(a.a) + L" | ? { $_.State -eq 'Installed' } | % { Remove-WindowsCapability -Online -Name $_.Name | Out-Null; 'CAP|'+$_.Name }", 1800000); lines(r.out, [&](const std::wstring& l) { if (l.rfind(L"CAP|", 0) == 0 && c.j) c.j->add({ L"CAP", l.substr(4) }); }); } break;
        case V_MMAGENT: {
            if (g_eng.mc.ramGB < 3.5) { c.warn.push_back(L"Less than 4 GB of RAM, memory compression kept"); break; }
            runPowerShell(L"Disable-MMAgent -MemoryCompression"); if (c.j) c.j->add({ L"MMA", L"mc" });
        } break;
        case V_BIND: {
            std::wstring comps; for (auto& t : std::vector<std::wstring>{ L"ms_lldp", L"ms_lltdio", L"ms_rspndr" }) if (icontains(a.a, t)) comps += (comps.empty() ? L"" : L",") + psq(t);
            runPowerShell(L"Get-NetAdapter -Physical | ? { $_.InterfaceDescription -notmatch 'Wireless|Wi-?Fi|802\\.11|Bluetooth' } | % { foreach($c in @(" + comps + L")){ Disable-NetAdapterBinding -Name $_.Name -ComponentID $c } }");
            if (c.j) c.j->add({ L"BIND", comps });
        } break;
        case V_DEVWAKE: {
            ProcResult r = runPowerShell(L"Get-CimInstance -Namespace root\\wmi -ClassName MSPower_DeviceEnable | ? { $_.Enable } | % { Set-CimInstance -InputObject $_ -Property @{Enable=$false}; 'W|'+$_.InstanceName }");
            lines(r.out, [&](const std::wstring& l) { if (l.rfind(L"W|", 0) == 0 && c.j) c.j->add({ L"WAKE", l.substr(2) }); });
        } break;
        case V_DEVOFF: devicesByName(a.a, [&](HDEVINFO h, SP_DEVINFO_DATA& d, const std::wstring& id) { ULONG st = 0, pr = 0; if (CM_Get_DevNode_Status(&st, &pr, d.DevInst, 0) == CR_SUCCESS && (pr == CM_PROB_DISABLED)) return; if (deviceState(h, d, false) && c.j) c.j->add({ L"DEV", id }); }); break;
        case V_DEVON: devicesByName(a.a, [&](HDEVINFO h, SP_DEVINFO_DATA& d, const std::wstring&) { deviceState(h, d, true); }); break;

        case V_KILL: killProcesses(a.a); break;
        case V_CMD: { ProcResult r = runHidden(L"cmd.exe /d /c " + a.a, 1800000); if (!r.ok) c.fails++; } break;
        case V_BCD: runHidden(L"bcdedit.exe " + a.a); break;
        case V_FSU: runHidden(L"fsutil.exe " + a.a); break;
        case V_RUNGLOB: for (auto& p : globPaths(a.a)) runHidden(L"\"" + p + L"\" " + a.b, 900000, false); break;
        case V_RUNREG: {
            std::wstring cmd = expandEnv(regString(regParse(a.a), a.b));
            if (!cmd.empty()) runHidden(cmd, 900000, false);
        } break;
        case V_MSIX: {
            std::vector<std::wstring> pats; size_t p = 0; for (;;) { size_t q = a.a.find(L'|', p); pats.push_back(a.a.substr(p, q == std::wstring::npos ? std::wstring::npos : q - p)); if (q == std::wstring::npos) break; p = q + 1; }
            for (auto root : { L"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall", L"HKLM\\SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall", L"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall" }) {
                RegPath rp = regParse(root);
                for (auto& s : regSubkeys(rp)) {
                    std::wstring dn = regString(regParse(regJoin(rp) + L"\\" + s), L"DisplayName");
                    for (auto& pt : pats) if (!pt.empty() && icontains(dn, pt) && s.size() == 38 && s[0] == L'{') { runHidden(L"msiexec.exe /x " + s + L" /qn /norestart", 900000); break; }
                }
            }
        } break;
        case V_WINGET: {
            if ((a.f & AF_X64) && !g_eng.mc.x64) break;
            std::wstring wg = expandEnv(L"%LOCALAPPDATA%\\Microsoft\\WindowsApps\\winget.exe");
            if (!fileExists(wg)) {
                c.fails++;
                if (c.warn.empty() || c.warn.back().find(L"winget") == std::wstring::npos)
                    c.warn.push_back(L"winget is missing. Install App Installer from the Microsoft Store, then try again");
                break;
            }
            ProcResult r = runHidden(L"\"" + wg + L"\" install --id " + a.a + L" --exact --silent --accept-package-agreements --accept-source-agreements --disable-interactivity", 1800000);
            const DWORD kAlready = 0x8A15002B, kInstalled = 0x8A150061;
            if (!r.ok || (r.code != 0 && r.code != kAlready && r.code != kInstalled)) { c.fails++; c.warn.push_back(L"winget could not install " + a.a); }
        } break;
        case V_FWGROUP: runHidden(L"netsh.exe advfirewall firewall set rule group=\"" + a.a + L"\" new enable=" + (a.b == L"off" ? L"no" : L"yes")); if (c.j && a.b == L"off") c.j->add({ L"FWG", a.a }); break;
        case V_FILE: {
            std::wstring p = expandEnv(a.a); size_t s1 = p.find_last_of(L'\\'); size_t s2 = s1 == std::wstring::npos ? s1 : p.find_last_of(L'\\', s1 - 1);
            if (s2 != std::wstring::npos && dirExists(p.substr(0, s2))) writeFileUtf8(p, u16to8(a.b));
        } break;
        case V_DELDIR: case V_DELFILE: for (auto& p : globPaths(a.a)) deleteTree(p, false, nullptr); break;
        case V_DELGLOB: for (auto& p : globPaths(a.a)) deleteTree(p, false, nullptr); break;
        case V_DELCONTENT: for (auto& p : globPaths(a.a)) if (dirExists(p)) deleteTree(p, true, nullptr); break;
        case V_DELEMPTY: { std::wstring p = expandEnv(a.a); RemoveDirectoryW(p.c_str()); } break;
        case V_FILEOFF: fileOff(c, a.a); break;
        case V_KFRESTORE: restoreKnownFolders(a.a); break;
        case V_LNKCLEAN: cleanLinks(expandEnv(L"%ProgramData%\\Microsoft\\Windows\\Start Menu\\Programs")); cleanLinks(expandEnv(L"%APPDATA%\\Microsoft\\Windows\\Start Menu\\Programs")); break;
        case V_EVTCLEAR: clearEventLogs(); break;
        case V_CLEANMGR: {
            std::wstring base = L"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\VolumeCaches\\";
            size_t p = 0; DWORD two = 2;
            for (;;) { size_t q = a.a.find(L'|', p); std::wstring cat = a.a.substr(p, q == std::wstring::npos ? std::wstring::npos : q - p); RegPath k = regParse(base + cat); if (regKeyExists(k)) regWriteRaw(k, L"StateFlags0064", REG_DWORD, std::string((char*)&two, 4), false); if (q == std::wstring::npos) break; p = q + 1; }
            runHidden(L"cleanmgr.exe /sagerun:64", 1800000, false);
        } break;
        case V_HOSTSCLEAN: {
            std::wstring h = expandEnv(L"%SystemRoot%\\System32\\drivers\\etc\\hosts");
            std::string s = readFileBytes(h); if (s.empty()) break;
            writeFileUtf8(g_eng.dataDir + L"\\hosts.backup", s);
            std::string out, ln; std::string pat = u16to8(a.a); size_t removed = 0;
            for (size_t p = 0; p <= s.size(); p++) {
                if (p == s.size() || s[p] == '\n') { std::string t = ln; size_t f = t.find_first_not_of(" \t"); if (f != std::string::npos && t.compare(f, pat.size(), pat) == 0) removed++; else { out += ln; if (p < s.size()) out += '\n'; } ln.clear(); }
                else ln += s[p];
            }
            if (removed) { if (!writeFileUtf8(h, out)) { TIScope t; writeFileUtf8(h, out); } }
        } break;
        case V_EFIHIDE: hideEfi(); break;

        case V_PW: powerSet(c, a); break;
        case V_PLAN: powerPlan(c, a); break;
        case V_PLANSET: { GUID g; if (parseGuid(a.a, g)) { GUID prev = activeScheme(); if (c.j) c.j->add({ L"PACT", guidStr(prev) }); PowerSetActiveScheme(nullptr, &g); } } break;

        case V_VERIFY: {
            RegPath p = regParse(std::wstring(SVC_ROOT) + L"\\" + a.a);
            if (regKeyExists(p) && regDword(p, L"Start", (DWORD)a.n) != (DWORD)a.n) c.warn.push_back(a.b);
        } break;
        case V_REFRESH: c.refresh = true; break;
        case V_GPUPREF: gpuPref(c, a); break;
        case V_STARTUPOFF: startupOff(c, a.a); break;
        default: break;
        }
    }
}

static void undoJournal(Journal& j) {
    std::wstring featOn, capAdd;
    std::vector<std::vector<std::wstring>> nvRows;
    for (auto it = j.rows.rbegin(); it != j.rows.rend(); ++it) {
        auto& r = *it; const std::wstring& k = r[0];
        if (k == L"NV" && r.size() >= 4) nvRows.push_back(r);
        else if (k == L"V" && r.size() >= 6) {
            RegPath p = regParse(r[1]);
            if (r[3] == L"1") regWriteRaw(p, r[2], (DWORD)_wtoi(r[4].c_str()), fromHex(u16to8(r[5])), false);
            else regDeleteValue(p, r[2], false);
        } else if (k == L"K" && r.size() >= 3) {
            RegPath p = regParse(r[1]);
            if (r[2] == L"0") regDeleteTree(p, false);
            else if (r.size() >= 4 && fileExists(r[3])) { if (!regRestoreKey(p, r[3])) { TIScope t; regRestoreKey(p, r[3]); } }
        } else if (k == L"T" && r.size() >= 3) s_tasks.setEnabled(r[1], r[2] == L"1");
        else if (k == L"TD" && r.size() >= 3) s_tasks.restore(r[1], u8to16(readFileBytes(r[2])));
        else if (k == L"F" && r.size() >= 3) { if (!MoveFileExW(r[1].c_str(), r[2].c_str(), MOVEFILE_REPLACE_EXISTING)) { TIScope t; MoveFileExW(r[1].c_str(), r[2].c_str(), MOVEFILE_REPLACE_EXISTING); } }
        else if (k == L"FEAT" && r.size() >= 2) featOn += (featOn.empty() ? L"" : L",") + psq(r[1]);
        else if (k == L"CAP" && r.size() >= 2) capAdd += (capAdd.empty() ? L"" : L",") + psq(r[1]);
        else if (k == L"MMA") runPowerShell(L"Enable-MMAgent -MemoryCompression");
        else if (k == L"BIND" && r.size() >= 2) runPowerShell(L"Get-NetAdapter -Physical | % { foreach($c in @(" + r[1] + L")){ Enable-NetAdapterBinding -Name $_.Name -ComponentID $c } }");
        else if (k == L"WAKE" && r.size() >= 2) runPowerShell(L"Get-CimInstance -Namespace root\\wmi -ClassName MSPower_DeviceEnable | ? { $_.InstanceName -eq " + psq(r[1]) + L" } | % { Set-CimInstance -InputObject $_ -Property @{Enable=$true} }");
        else if (k == L"DEV" && r.size() >= 2) deviceById(r[1], true);
        else if (k == L"FWG" && r.size() >= 2) runHidden(L"netsh.exe advfirewall firewall set rule group=\"" + r[1] + L"\" new enable=yes");
        else if (k == L"PWV" && r.size() >= 6) {
            GUID s, sub, set; parseGuid(r[1], s); parseGuid(r[2], sub); parseGuid(r[3], set);
            if (r[4] != L"-") PowerWriteACValueIndex(nullptr, &s, &sub, &set, (DWORD)_wtoi(r[4].c_str()));
            if (r[5] != L"-") PowerWriteDCValueIndex(nullptr, &s, &sub, &set, (DWORD)_wtoi(r[5].c_str()));
            GUID act = activeScheme(); PowerSetActiveScheme(nullptr, &act);
        } else if (k == L"PACT" && r.size() >= 2) { GUID g; if (parseGuid(r[1], g)) PowerSetActiveScheme(nullptr, &g); }
        else if (k == L"PNEW" && r.size() >= 2) { GUID g; if (parseGuid(r[1], g)) { GUID act = activeScheme(); if (IsEqualGUID(act, g)) { GUID bal; parseGuid(L"381b4222-f694-41f0-9685-ff5bb260df2e", bal); PowerSetActiveScheme(nullptr, &bal); } PowerDeleteScheme(nullptr, &g); } }
        else if (k == L"OWNTASK" && r.size() >= 2) { runHidden(L"schtasks.exe /Delete /F /TN \"cr1mix\\" + r[1] + L"\""); if (r[1] == L"TimerResolution") killProcesses(L"cr1mix"); }
    }
    nvUndo(nvRows);
    if (!featOn.empty()) runPowerShell(L"foreach($f in @(" + featOn + L")){ Enable-WindowsOptionalFeature -Online -FeatureName $f -NoRestart -All | Out-Null }", 1800000);
    if (!capAdd.empty()) runPowerShell(L"foreach($c in @(" + capAdd + L")){ Add-WindowsCapability -Online -Name $c | Out-Null }", 1800000);
}

static void revertDefaults(Ctx& c, const Tweak& t) {
    if (t.hasOff) { execute(c, t.off, nullptr); return; }
    for (auto it = t.on.rbegin(); it != t.on.rend(); ++it) {
        const Act& a = *it;
        if (a.v == V_REG && a.b.size() >= 0 && !(a.f & (AF_EXISTS | AF_DEEP))) {
            bool policy = icontains(a.a, L"\\Policies\\");
            if (a.d == L"-" || (a.d.empty() && policy)) { std::vector<RegPath> ks; expandKeys(a.a, a.f, ks); for (auto& k : ks) regDeleteValue(k, a.b, (a.f & AF_TI) != 0); }
            else if (a.d.size() > 2 && a.d[1] == L':') { DWORD ty; std::string d; if (encodeValue((u8)a.d[0], a.d.substr(2), ty, d)) { std::vector<RegPath> ks; expandKeys(a.a, a.f, ks); for (auto& k : ks) regWriteRaw(k, a.b, ty, d, (a.f & AF_TI) != 0); } }
        } else if (a.v == V_SVC && a.n2 >= 0) { for (auto& s : serviceNames(a.a)) { DWORD v = (DWORD)a.n2; regWriteRaw(regParse(std::wstring(SVC_ROOT) + L"\\" + s), L"Start", REG_DWORD, std::string((char*)&v, 4), false); } }
        else if (a.v == V_TASK && a.n == 0) { for (auto& p : s_tasks.match(a.a)) s_tasks.setEnabled(p, true); }
        else if (a.v == V_FILEOFF) { for (auto& f : globPaths(a.a + L".cr1mixoff")) { std::wstring to = f.substr(0, f.size() - 10); TIScope ti; MoveFileExW(f.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING); } }
        else if (a.v == V_TIMER) runHidden(L"schtasks.exe /Delete /F /TN \"cr1mix\\TimerResolution\"");
        else if (a.v == V_SCHED) runHidden(L"schtasks.exe /Delete /F /TN \"cr1mix\\ShaderCleanup\"");
    }
}

static int checkAct(const Act& a) {
    switch (a.v) {
    case V_REG: {
        if (a.f & (AF_EXISTS | AF_DEEP | AF_NIC | AF_NV | AF_AMD | AF_WIRED) || a.a.find(L'*') != std::wstring::npos) return -1;
        DWORD t; std::string d; if (!encodeValue(a.type, a.c, t, d)) return -1;
        RegPath p = regParse(a.a); if (!p.root) return -1;
        return sameValue(regRead(p, a.b), t, d) ? 1 : 0;
    }
    case V_REGDEL: { if (a.a.find(L'*') != std::wstring::npos) return -1; return regRead(regParse(a.a), a.b).exists ? 0 : 1; }
    case V_SVC: {
        if (a.a.find_first_of(L"*?") != std::wstring::npos) return -1;
        RegPath p = regParse(std::wstring(SVC_ROOT) + L"\\" + a.a); if (!regKeyExists(p)) return -1;
        return regDword(p, L"Start", 99) == (DWORD)a.n ? 1 : 0;
    }
    case V_TASK: {
        if (a.n == 2 || a.a.back() == L'\\' || a.a.find_first_of(L"*?") != std::wstring::npos) return -1;
        int e = s_tasks.enabled(a.a); if (e < 0) return -1;
        return e == a.n ? 1 : 0;
    }
    case V_PW: {
        GUID s = activeScheme(), sub = powerSub(a.a), set; parseGuid(a.b, set);
        DWORD v; if (a.n != INT32_MIN) { if (PowerReadACValueIndex(nullptr, &s, &sub, &set, &v) != ERROR_SUCCESS) return -1; return v == (DWORD)a.n ? 1 : 0; }
        if (a.n2 != INT32_MIN) { if (PowerReadDCValueIndex(nullptr, &s, &sub, &set, &v) != ERROR_SUCCESS) return -1; return v == (DWORD)a.n2 ? 1 : 0; }
        return -1;
    }
    case V_PLAN: { GUID g; if (!parseGuid(a.a, g)) return -1; return IsEqualGUID(activeScheme(), g) ? 1 : 0; }
    case V_PLANSET: { GUID g; if (!parseGuid(a.a, g)) return -1; return IsEqualGUID(activeScheme(), g) ? 1 : 0; }
    case V_FILEOFF: { auto f = globPaths(a.a + L".cr1mixoff"); return f.empty() ? 0 : 1; }
    case V_NVDRS: return nvCheck(a);
    default: return -1;
    }
}
static float matchRatio(const std::vector<Act>& acts, int* checkable) {
    int n = 0, m = 0; bool plan = false;
    for (auto& a : acts) {
        if (a.v == V_PLAN) plan = true;
        if (plan && a.v == V_PW) continue;
        int r = checkAct(a); if (r < 0) continue; n++; m += r;
    }
    if (checkable) *checkable = n;
    return n ? (float)m / n : 0.f;
}

static std::wstring jfile(const Tweak& t, const std::string& item = "") {
    return g_eng.stateDir + L"\\" + u8to16(t.id) + (item.empty() ? L"" : L"#" + u8to16(item)) + L".jrn";
}
bool Engine::journaled(const Tweak& t, const std::string& item) const { return fileExists(jfile(t, item)); }

void Engine::detect(Tweak& t) {
    if (t.kind == TK_ACTION) { t.state = -1; return; }
    if (t.kind == TK_PICK) { for (size_t i = 0; i < t.opts.size(); i++) t.picked[i] = journaled(t, t.opts[i].key) ? 1 : 0; return; }
    if (t.kind == TK_TOGGLE) {
        int n = 0; float r = matchRatio(t.on, &n);
        t.state = journaled(t) ? 1 : (n > 0 && r >= 0.85f) ? 1 : 0;
        return;
    }
    t.state = -1;
    if (journaled(t)) { Journal j; j.load(jfile(t)); for (auto& r : j.rows) if (r[0] == L"OPT" && r.size() > 1) for (size_t i = 0; i < t.opts.size(); i++) if (u8to16(t.opts[i].key) == r[1]) t.state = (int)i; }
    if (t.state >= 0) return;
    for (size_t i = 0; i < t.opts.size(); i++) {
        if (t.opts[i].hidden) continue;
        int n = 0; float r = matchRatio(t.opts[i].acts, &n);
        if (n > 0 && r >= 0.999f) { t.state = (int)i; return; }
    }
}

bool Engine::hardwareFits(const Tweak& t) const {
    if ((t.fl & TF_NVIDIA) && !mc.nvidia) return false;
    if ((t.fl & TF_AMD) && !mc.amd) return false;
    return true;
}
bool Engine::requirementsMet(const Tweak& t, std::wstring* why) const {
    if ((t.fl & TF_NVIDIA) && !mc.nvidia) { if (why) *why = L"Needs an NVIDIA GPU"; return false; }
    if ((t.fl & TF_AMD) && !mc.amd) { if (why) *why = L"Needs an AMD GPU"; return false; }
    if ((t.fl & TF_DESKTOP) && mc.laptop) { if (why) *why = L"Meant for desktops"; return false; }
    if ((t.fl & TF_LAPTOP) && !mc.laptop) { if (why) *why = L"Meant for laptops"; return false; }
    if ((t.fl & TF_RAM12) && mc.ramGB < 11.5) { if (why) *why = L"Needs 12 GB of RAM"; return false; }
    if ((t.fl & TF_RAM4) && mc.ramGB < 3.5) { if (why) *why = L"Needs 4 GB of RAM"; return false; }
    return true;
}

typedef struct { DWORD dwEventType; DWORD dwRestorePtType; INT64 llSequenceNumber; WCHAR szDescription[256]; } RPINFO;
typedef struct { UINT nStatus; INT64 llSequenceNumber; } RPSTATUS;
typedef BOOL(WINAPI* SRSetRestorePointW_t)(RPINFO*, RPSTATUS*);
static bool createRestorePoint(const std::wstring& desc) {
    DWORD zero = 0;
    regWriteRaw(regParse(L"HKLM\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\SystemRestore"), L"SystemRestorePointCreationFrequency", REG_DWORD, std::string((char*)&zero, 4), false);
    runPowerShell(L"Enable-ComputerRestore -Drive ($env:SystemDrive + '\\')", 60000);
    HMODULE m = LoadLibraryW(L"srclient.dll"); if (!m) return false;
    auto fn = (SRSetRestorePointW_t)GetProcAddress(m, "SRSetRestorePointW");
    bool ok = false;
    if (fn) {
        RPINFO ri{}; RPSTATUS st{}; ri.dwEventType = 100; ri.dwRestorePtType = 12; wcsncpy(ri.szDescription, desc.c_str(), 255);
        if (fn(&ri, &st)) { ri.dwEventType = 101; ri.llSequenceNumber = st.llSequenceNumber; fn(&ri, &st); ok = true; }
    }
    FreeLibrary(m); return ok;
}
bool createRestorePointNow(const std::wstring& desc) { return createRestorePoint(desc); }

bool backupRegistry(std::wstring& outDir, SayFn say) {
    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t stamp[64]; swprintf(stamp, 64, L"%04d-%02d-%02d %02d.%02d.%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    outDir = expandEnv(L"%SystemDrive%\\cr1mix app Backups\\Registry ") + stamp;
    if (!ensureDir(outDir)) { say(L"Could not create " + outDir, 3); return false; }
    enablePrivilege(SE_BACKUP_NAME);
    struct H { const wchar_t* key; const wchar_t* file; const wchar_t* label; };
    const H hives[] = { {L"HKLM\\SYSTEM", L"SYSTEM.hiv", L"System settings"}, {L"HKLM\\SOFTWARE", L"SOFTWARE.hiv", L"Software settings"}, {L"HKCU", L"USER.hiv", L"Your user settings"}, {L"HKU\\.DEFAULT", L"DEFAULT.hiv", L"Default user"} };
    int ok = 0, n = 0;
    for (auto& h : hives) {
        n++; g_eng.progress = (float)(n - 1) / 4.f;
        { std::lock_guard<std::mutex> lk(g_eng.mx); g_eng.current = std::wstring(L"Saving ") + h.label; }
        if (regSaveKey(regParse(h.key), outDir + L"\\" + h.file)) { ok++; say(std::wstring(h.label) + L" saved", 1); }
        else say(std::wstring(h.label) + L" could not be saved", 2);
    }
    std::string readme = "cr1mix app registry backup\r\n\r\nThese are full hive copies. To restore one, boot into Windows Recovery or run as administrator:\r\n"
        "  reg restore HKLM\\SYSTEM SYSTEM.hiv   (and the same for SOFTWARE)\r\nUSER.hiv is your HKEY_CURRENT_USER, DEFAULT.hiv is HKEY_USERS\\.DEFAULT.\r\n";
    writeFileUtf8(outDir + L"\\README.txt", readme);
    return ok > 0;
}

static std::wstring lowerW(std::wstring s) { for (auto& ch : s) ch = towlower(ch); return s; }
bool Engine::keepsService(const std::wstring& name) { std::lock_guard<std::mutex> lk(kmx); return keep.count(lowerW(name)) != 0; }
void Engine::setKeepService(const std::wstring& name, bool k) {
    std::wstring all;
    { std::lock_guard<std::mutex> lk(kmx); if (k) keep.insert(lowerW(name)); else keep.erase(lowerW(name)); for (auto& s : keep) all += s + L"\n"; }
    writeFileUtf8(dataDir + L"\\services_keep.txt", u16to8(all));
}
void Engine::loadKeep() {
    std::wstring all = u8to16(readFileBytes(dataDir + L"\\services_keep.txt"));
    std::lock_guard<std::mutex> lk(kmx); keep.clear();
    size_t p = 0; while (p < all.size()) { size_t e = all.find(L'\n', p); if (e == std::wstring::npos) e = all.size(); std::wstring l = all.substr(p, e - p); while (!l.empty() && (l.back() == L'\r' || l.back() == L' ')) l.pop_back(); if (!l.empty()) keep.insert(lowerW(l)); p = e + 1; }
}

void Engine::start() {
    wchar_t la[MAX_PATH]; GetEnvironmentVariableW(L"LOCALAPPDATA", la, MAX_PATH);
    dataDir = std::wstring(la) + L"\\cr1mix"; stateDir = dataDir + L"\\state";
    ensureDir(stateDir);
    loadKeep();
    mc = detectMachine();
    th = std::thread([this] { loop(); });
    th.detach();
    detectAll();
}
void Engine::submit(const Job& j) { { std::lock_guard<std::mutex> lk(qmx); q.push_back(j); } qcv.notify_one(); }
void Engine::detectAll() { Job j; j.title = L"__detect"; submit(j); }

void Engine::loop() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    for (;;) {
        Job j;
        { std::unique_lock<std::mutex> lk(qmx); qcv.wait(lk, [&] { return !q.empty(); }); j = q.front(); q.pop_front(); }
        if (j.title == L"__detect") {
            NvScope nvs;
            detecting = true;
            for (auto& t : g_cat.tweaks) detect(t);
            detecting = false; generation++;
            continue;
        }
        runJob(j);
    }
}

void Engine::runJob(Job& job) {
    running = true; progress = 0;
    { std::lock_guard<std::mutex> lk(mx); jobTitle = job.title; log.clear(); current.clear(); lastOk = lastFail = 0; }
    auto say = [&](const std::wstring& s, int kind) { std::lock_guard<std::mutex> lk(mx); log.push_back({ s, kind }); };
    lastTask = (bool)job.fn;
    if (job.fn) {
        int fails = 0;
        job.fn([&](const std::wstring& s, int kind) { if (kind >= 2) fails++; say(s, kind); });
        lastOk = fails ? 0 : 1; lastFail = fails;
        progress = 1; generation++;
        { std::lock_guard<std::mutex> lk(mx); current = L"Finished"; }
        running = false;
        return;
    }

    int total = 0, done = 0;
    for (auto& it : job.items) {
        Tweak& t = g_cat.tweaks[it.tweak];
        if ((it.op == J_CHOOSE || it.op == J_RUN) && it.opt >= 0 && it.opt < (int)t.opts.size()) total += (int)t.opts[it.opt].acts.size();
        else if (it.op == J_PICK) for (int k : it.items) total += (int)t.opts[k].acts.size();
        else total += (int)t.on.size();
    }
    total = std::max(total, 1);
    auto tick = [&](const Act& a) { done++; progress = (float)done / total; std::lock_guard<std::mutex> lk(mx); current = describeAct(a); };

    if (restorePoint) {
        { std::lock_guard<std::mutex> lk(mx); current = L"Creating a restore point"; }
        bool rp = createRestorePoint(L"cr1mix app: " + job.title);
        say(rp ? L"Restore point created" : L"Restore point skipped (System Restore is off or busy)", rp ? 1 : 2);
    }

    bool refresh = false;
    for (auto& it : job.items) {
        Tweak& t = g_cat.tweaks[it.tweak];
        if (it.op != J_REVERT && !hardwareFits(t)) { say(t.name + L": skipped, this PC does not have that GPU", 2); t.busy = false; continue; }
        t.busy = true;
        Ctx c; c.t = &t; c.ti = (t.fl & TF_TI) != 0;
        Journal j;
        switch (it.op) {
        case J_APPLY: case J_RUN:
            if (t.kind != TK_ACTION) { j.load(jfile(t)); c.j = &j; }
            if (it.op == J_RUN && it.opt >= 0 && it.opt < (int)t.opts.size()) { execute(c, t.on, tick); execute(c, t.opts[it.opt].acts, tick); }
            else execute(c, t.on, tick);
            if (c.j) j.save();
            if (t.kind == TK_TOGGLE) t.state = 1;
            break;
        case J_REVERT:
            if (journaled(t)) { j.load(jfile(t)); undoJournal(j); DeleteFileW(jfile(t).c_str()); }
            else revertDefaults(c, t);
            t.state = 0;
            break;
        case J_CHOOSE: {
            if (it.opt < 0 || it.opt >= (int)t.opts.size()) break;
            if (journaled(t)) { Journal old; old.load(jfile(t)); undoJournal(old); DeleteFileW(jfile(t).c_str()); }
            j.file = jfile(t); c.j = &j;
            execute(c, t.opts[it.opt].acts, tick);
            j.add({ L"OPT", u8to16(t.opts[it.opt].key) }); j.save();
            t.state = it.opt;
        } break;
        case J_PICK:
            for (int k : it.items) {
                Journal ji; ji.load(jfile(t, t.opts[k].key)); c.j = &ji;
                execute(c, t.opts[k].acts, tick);
                ji.add({ L"PICK", u8to16(t.opts[k].key) }); ji.save();
                t.picked[k] = 1;
            }
            break;
        }
        refresh |= c.refresh;
        for (auto& w : c.warn) say(w, 2);
        if (c.fails) { lastFail++; say(t.name + L": " + std::to_wstring(c.fails) + L" change(s) could not be made", 2); }
        else { lastOk++; say(t.name + (it.op == J_REVERT ? L": restored" : L": done"), 1); }
        if (t.fl & TF_REBOOT) needReboot = true;
        if (t.fl & TF_LOGOFF) needLogoff = true;
        t.busy = false;
        bool nv = false; for (auto& a : t.on) nv |= a.v == V_NVDRS; for (auto& o : t.opts) for (auto& a : o.acts) nv |= a.v == V_NVDRS;
        if (nv) { NvScope nvs; detect(t); } else detect(t);
    }
    if (refresh) { { std::lock_guard<std::mutex> lk(mx); current = L"Restarting Explorer"; } killProcesses(L"explorer"); }
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, (LPARAM)L"Policy", SMTO_ABORTIFHUNG, 1000, nullptr);
    progress = 1; generation++;
    { std::lock_guard<std::mutex> lk(mx); current = L"Finished"; }
    running = false;
}

typedef LONG(NTAPI* NtSetTimerResolution_t)(ULONG, BOOLEAN, PULONG);
int Engine::runTimerMode(int res) {
    HMODULE nt = GetModuleHandleW(L"ntdll.dll");
    auto fn = (NtSetTimerResolution_t)GetProcAddress(nt, "NtSetTimerResolution");
    if (!fn) return 1;
    ULONG cur = 0; fn((ULONG)res, TRUE, &cur);
    SetPriorityClass(GetCurrentProcess(), IDLE_PRIORITY_CLASS);
    for (;;) Sleep(INFINITE);
    return 0;
}
int Engine::runCleanMode(const std::wstring& what) {
    if (what != L"shader") return 0;
    static const wchar_t* subs[] = { L"NVIDIA\\GLCache", L"NVIDIA\\DXCache", L"AMD\\DxCache", L"AMD\\DxcCache", L"AMD\\GLCache", L"Intel\\ShaderCache", L"D3DSCache", L"Microsoft\\DirectX\\ShaderCache" };
    for (auto& u : globPaths(expandEnv(L"%SystemDrive%\\Users\\*")))
        for (auto s : subs) { std::wstring p = u + L"\\AppData\\Local\\" + s; if (dirExists(p)) deleteTree(p, true, nullptr); }
    return 0;
}
