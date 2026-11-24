#include "stats.h"
#include <winsock2.h>
#include <ws2ipdef.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <psapi.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <thread>

Stats g_stats;

double nowSec() {
    static LARGE_INTEGER f{}; if (!f.QuadPart) QueryPerformanceFrequency(&f);
    LARGE_INTEGER c; QueryPerformanceCounter(&c); return (double)c.QuadPart / f.QuadPart;
}

static std::wstring regStr(HKEY root, const wchar_t* path, const wchar_t* name) {
    wchar_t buf[512]; DWORD sz = sizeof(buf);
    if (RegGetValueW(root, path, name, RRF_RT_REG_SZ, nullptr, buf, &sz) == ERROR_SUCCESS) return buf;
    return L"";
}
static DWORD regDw(HKEY root, const wchar_t* path, const wchar_t* name) {
    DWORD v = 0, sz = 4;
    RegGetValueW(root, path, name, RRF_RT_REG_DWORD, nullptr, &v, &sz);
    return v;
}
static std::wstring trim(std::wstring s) {
    while (!s.empty() && iswspace(s.back())) s.pop_back();
    size_t i = 0; while (i < s.size() && iswspace(s[i])) i++;
    s = s.substr(i);
    std::wstring o; bool sp = false;
    for (wchar_t c : s) { if (iswspace(c)) { if (!sp) o += L' '; sp = true; } else { o += c; sp = false; } }
    return o;
}

typedef LONG(WINAPI* NtQSI)(ULONG, PVOID, ULONG, PULONG);
struct SPPI { LARGE_INTEGER Idle, Kernel, User, Dpc, Intr; ULONG IntrCount; };

static void staticInfo(Stats& s) {
    s.cpuName = trim(regStr(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"ProcessorNameString"));
    s.baseMhz = (int)regDw(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"~MHz");
    DWORD len = 0; GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &len);
    std::vector<char> buf(len);
    if (GetLogicalProcessorInformationEx(RelationProcessorCore, (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)buf.data(), &len)) {
        char* p = buf.data(); char* e = p + len;
        while (p < e) {
            auto* x = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)p;
            s.cores++;
            for (int gi = 0; gi < x->Processor.GroupCount; gi++) {
                KAFFINITY m = x->Processor.GroupMask[gi].Mask;
                while (m) { s.threads += (int)(m & 1); m >>= 1; }
            }
            p += x->Size;
        }
    }
    MEMORYSTATUSEX ms{ sizeof(ms) }; GlobalMemoryStatusEx(&ms);
    s.ramTotalGB = ms.ullTotalPhys / 1073741824.0;
    std::wstring pn = regStr(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"ProductName");
    std::wstring dv = regStr(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"DisplayVersion");
    std::wstring cb = regStr(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"CurrentBuildNumber");
    DWORD ubr = regDw(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"UBR");
    if (_wtoi(cb.c_str()) >= 22000) { size_t k = pn.find(L"Windows 10"); if (k != std::wstring::npos) pn.replace(k, 10, L"Windows 11"); }
    s.osName = pn; s.osVer = dv + L" · build " + cb + L"." + std::to_wstring(ubr);
    wchar_t hn[256]; DWORD hl = 256; GetComputerNameW(hn, &hl); s.hostName = hn;
    wchar_t un[256]; DWORD ul = 256; GetUserNameW(un, &ul); s.userName = un;
    IDXGIFactory1* f = nullptr;
    if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&f))) {
        IDXGIAdapter1* a = nullptr; SIZE_T best = 0;
        for (UINT i = 0; f->EnumAdapters1(i, &a) != DXGI_ERROR_NOT_FOUND; i++) {
            DXGI_ADAPTER_DESC1 d; a->GetDesc1(&d);
            if (!(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) && d.DedicatedVideoMemory >= best) {
                best = d.DedicatedVideoMemory; s.gpuName = trim(d.Description); s.vramTotalGB = d.DedicatedVideoMemory / 1073741824.0;
            }
            a->Release();
        }
        f->Release();
    }
    ULARGE_INTEGER fr, tot, tf; wchar_t sys[8] = L"C:\\";
    wchar_t wd[MAX_PATH]; if (GetWindowsDirectoryW(wd, MAX_PATH)) { sys[0] = wd[0]; }
    if (GetDiskFreeSpaceExW(sys, &fr, &tot, &tf)) s.diskTotalGB = tot.QuadPart / 1073741824.0;
}

static void worker() {
    Stats& S = g_stats;
    staticInfo(S);
    NtQSI ntq = (NtQSI)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQuerySystemInformation");
    PDH_HQUERY q = nullptr; PdhOpenQueryW(nullptr, 0, &q);
    PDH_HCOUNTER cGpu = nullptr, cVram = nullptr, cPerf = nullptr, cDisk = nullptr;
    PdhAddEnglishCounterW(q, L"\\GPU Engine(*)\\Utilization Percentage", 0, &cGpu);
    PdhAddEnglishCounterW(q, L"\\GPU Adapter Memory(*)\\Dedicated Usage", 0, &cVram);
    PdhAddEnglishCounterW(q, L"\\Processor Information(_Total)\\% Processor Performance", 0, &cPerf);
    PdhAddEnglishCounterW(q, L"\\PhysicalDisk(_Total)\\% Disk Time", 0, &cDisk);
    PdhCollectQueryData(q);
    FILETIME pi, pk, pu; GetSystemTimes(&pi, &pk, &pu);
    std::vector<SPPI> prevCore(S.threads > 0 ? S.threads : 64), curCore(prevCore.size());
    ULONG rl = 0;
    if (ntq) ntq(8, prevCore.data(), (ULONG)(prevCore.size() * sizeof(SPPI)), &rl);
    int nCore = rl / sizeof(SPPI);
    u64 pIn = 0, pOut = 0; double pT = nowSec();
    auto netTotals = [&](u64& in, u64& out) {
        in = out = 0; PMIB_IF_TABLE2 t = nullptr;
        if (GetIfTable2(&t) == NO_ERROR) {
            for (ULONG i = 0; i < t->NumEntries; i++) {
                auto& r = t->Table[i];
                if (!r.InterfaceAndOperStatusFlags.HardwareInterface || r.OperStatus != IfOperStatusUp) continue;
                if (r.Type == IF_TYPE_SOFTWARE_LOOPBACK) continue;
                in += r.InOctets; out += r.OutOctets;
            }
            FreeMibTable(t);
        }
    };
    netTotals(pIn, pOut);
    std::vector<char> gbuf;
    for (;;) {
        Sleep(1000);
        FILETIME ci, ck, cu; GetSystemTimes(&ci, &ck, &cu);
        auto u = [](FILETIME a) { return ((u64)a.dwHighDateTime << 32) | a.dwLowDateTime; };
        u64 idle = u(ci) - u(pi), kern = u(ck) - u(pk), user = u(cu) - u(pu);
        pi = ci; pk = ck; pu = cu;
        float cpu = (kern + user) ? 100.f * (float)(kern + user - idle) / (float)(kern + user) : 0.f;
        std::vector<float> pc;
        if (ntq && nCore > 0) {
            ntq(8, curCore.data(), (ULONG)(curCore.size() * sizeof(SPPI)), &rl);
            for (int i = 0; i < nCore; i++) {
                u64 id = curCore[i].Idle.QuadPart - prevCore[i].Idle.QuadPart;
                u64 tt = (curCore[i].Kernel.QuadPart - prevCore[i].Kernel.QuadPart) + (curCore[i].User.QuadPart - prevCore[i].User.QuadPart);
                pc.push_back(tt ? clampf(100.f * (float)(tt - id) / (float)tt, 0, 100) : 0);
            }
            prevCore = curCore;
        }
        PdhCollectQueryData(q);
        float e3d = 0, ecopy = 0, evid = 0, ecomp = 0, eother = 0;
        std::map<std::wstring, float> sums;
        DWORD bs = 0, cnt = 0;
        if (cGpu && PdhGetFormattedCounterArrayW(cGpu, PDH_FMT_DOUBLE, &bs, &cnt, nullptr) == PDH_MORE_DATA) {
            gbuf.resize(bs);
            auto* it = (PDH_FMT_COUNTERVALUE_ITEM_W*)gbuf.data();
            if (PdhGetFormattedCounterArrayW(cGpu, PDH_FMT_DOUBLE, &bs, &cnt, it) == ERROR_SUCCESS) {
                for (DWORD i = 0; i < cnt; i++) {
                    std::wstring n = it[i].szName; size_t k = n.find(L"engtype_");
                    if (k == std::wstring::npos) continue;
                    sums[n.substr(k + 8)] += (float)it[i].FmtValue.doubleValue;
                }
            }
        }
        for (auto& p : sums) {
            float v = std::min(100.f, p.second);
            if (p.first == L"3D") e3d = v; else if (p.first == L"Copy") ecopy = v;
            else if (p.first.find(L"Video") != std::wstring::npos) evid = std::max(evid, v);
            else if (p.first.find(L"Compute") != std::wstring::npos) ecomp = std::max(ecomp, v);
            else eother = std::max(eother, v);
        }
        float gpu = std::max(std::max(e3d, ecopy), std::max(std::max(evid, ecomp), eother));
        double vram = 0;
        if (cVram) {
            bs = 0; cnt = 0;
            if (PdhGetFormattedCounterArrayW(cVram, PDH_FMT_LARGE, &bs, &cnt, nullptr) == PDH_MORE_DATA) {
                gbuf.resize(bs); auto* it = (PDH_FMT_COUNTERVALUE_ITEM_W*)gbuf.data();
                if (PdhGetFormattedCounterArrayW(cVram, PDH_FMT_LARGE, &bs, &cnt, it) == ERROR_SUCCESS)
                    for (DWORD i = 0; i < cnt; i++) vram += (double)it[i].FmtValue.largeValue;
            }
        }
        PDH_FMT_COUNTERVALUE fv{};
        float perf = 100;
        if (cPerf && PdhGetFormattedCounterValue(cPerf, PDH_FMT_DOUBLE, nullptr, &fv) == ERROR_SUCCESS) perf = (float)fv.doubleValue;
        float dact = 0;
        if (cDisk && PdhGetFormattedCounterValue(cDisk, PDH_FMT_DOUBLE, nullptr, &fv) == ERROR_SUCCESS) dact = std::min(100.f, (float)fv.doubleValue);
        MEMORYSTATUSEX ms{ sizeof(ms) }; GlobalMemoryStatusEx(&ms);
        u64 nIn, nOut; netTotals(nIn, nOut); double t = nowSec(); double dtn = std::max(0.2, t - pT);
        double down = nIn >= pIn ? (nIn - pIn) / dtn : 0, up = nOut >= pOut ? (nOut - pOut) / dtn : 0;
        pIn = nIn; pOut = nOut; pT = t;
        PERFORMANCE_INFORMATION pinf{ sizeof(pinf) }; GetPerformanceInfo(&pinf, sizeof(pinf));
        ULARGE_INTEGER fr, tot, tf; wchar_t sys[8] = L"C:\\"; wchar_t wd[MAX_PATH]; if (GetWindowsDirectoryW(wd, MAX_PATH)) sys[0] = wd[0];
        float dpct = 0, dfree = 0;
        if (GetDiskFreeSpaceExW(sys, &fr, &tot, &tf) && tot.QuadPart) { dpct = 100.f * (1.f - (float)fr.QuadPart / (float)tot.QuadPart); dfree = (float)(fr.QuadPart / 1073741824.0); }
        {
            std::lock_guard<std::mutex> lk(S.mx);
            S.cpu = cpu; S.gpu = gpu; S.gpu3d = e3d; S.gpuCopy = ecopy; S.gpuVideo = evid; S.gpuCompute = ecomp;
            S.ram = (float)ms.dwMemoryLoad; S.ramUsedGB = (float)((ms.ullTotalPhys - ms.ullAvailPhys) / 1073741824.0);
            S.clockGHz = S.baseMhz * perf / 100.f / 1000.f;
            S.vramUsedGB = (float)(vram / 1073741824.0);
            S.perCore = pc; S.netDown = down; S.netUp = up; S.diskPct = dpct; S.diskFreeGB = dfree; S.diskAct = dact;
            S.uptimeSec = GetTickCount64() / 1000; S.procs = pinf.ProcessCount; S.thrds = pinf.ThreadCount; S.handles = pinf.HandleCount;
            S.hCpu.push(cpu); S.hGpu.push(gpu); S.hRam.push(S.ram);
            S.hDown.push((float)down); S.hUp.push((float)up); S.hDisk.push(dact);
            S.sampleTime = nowSec(); S.sampleCount++;
        }
    }
}

void Stats::start() { std::thread(worker).detach(); }

void Stats::snapshot(Stats& o) {
    std::lock_guard<std::mutex> lk(mx);
    o.cpuName = cpuName; o.gpuName = gpuName; o.osName = osName; o.osVer = osVer; o.hostName = hostName; o.userName = userName;
    o.cores = cores; o.threads = threads; o.baseMhz = baseMhz; o.ramTotalGB = ramTotalGB; o.vramTotalGB = vramTotalGB; o.diskTotalGB = diskTotalGB;
    o.cpu = cpu; o.gpu = gpu; o.ram = ram; o.clockGHz = clockGHz; o.vramUsedGB = vramUsedGB; o.ramUsedGB = ramUsedGB;
    o.diskPct = diskPct; o.diskFreeGB = diskFreeGB; o.diskAct = diskAct; o.gpu3d = gpu3d; o.gpuCopy = gpuCopy; o.gpuVideo = gpuVideo; o.gpuCompute = gpuCompute;
    o.netDown = netDown; o.netUp = netUp; o.perCore = perCore; o.uptimeSec = uptimeSec; o.procs = procs; o.thrds = thrds; o.handles = handles;
    o.hCpu = hCpu; o.hGpu = hGpu; o.hRam = hRam; o.hDown = hDown; o.hUp = hUp; o.hDisk = hDisk; o.sampleTime = sampleTime; o.sampleCount = sampleCount;
}
