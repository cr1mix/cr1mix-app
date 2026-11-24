#include "sys.h"
#include <tlhelp32.h>
#include <shlwapi.h>
#include <winhttp.h>
#include <intrin.h>

RegPath regParse(const std::wstring& full) {
    RegPath p; size_t s = full.find(L'\\');
    std::wstring r = s == std::wstring::npos ? full : full.substr(0, s);
    p.sub = s == std::wstring::npos ? L"" : full.substr(s + 1);
    p.rootName = r;
    if (r == L"HKLM" || r == L"HKEY_LOCAL_MACHINE") p.root = HKEY_LOCAL_MACHINE;
    else if (r == L"HKCU" || r == L"HKEY_CURRENT_USER") p.root = HKEY_CURRENT_USER;
    else if (r == L"HKU" || r == L"HKEY_USERS") p.root = HKEY_USERS;
    else if (r == L"HKCR" || r == L"HKEY_CLASSES_ROOT") { p.root = HKEY_LOCAL_MACHINE; p.sub = L"SOFTWARE\\Classes\\" + p.sub; p.rootName = L"HKLM"; }
    while (!p.sub.empty() && p.sub.back() == L'\\') p.sub.pop_back();
    return p;
}
std::wstring regJoin(const RegPath& p) { return p.rootName + L"\\" + p.sub; }

static const REGSAM V64 = KEY_WOW64_64KEY;

RegVal regRead(const RegPath& p, const std::wstring& name) {
    RegVal v; HKEY k;
    if (RegOpenKeyExW(p.root, p.sub.c_str(), 0, KEY_QUERY_VALUE | V64, &k) != ERROR_SUCCESS) return v;
    DWORD type = 0, sz = 0;
    if (RegQueryValueExW(k, name.c_str(), nullptr, &type, nullptr, &sz) == ERROR_SUCCESS) {
        v.data.resize(sz);
        if (RegQueryValueExW(k, name.c_str(), nullptr, &type, sz ? (BYTE*)&v.data[0] : nullptr, &sz) == ERROR_SUCCESS) { v.exists = true; v.type = type; v.data.resize(sz); }
    }
    RegCloseKey(k);
    return v;
}
static LSTATUS writeOnce(const RegPath& p, const std::wstring& name, DWORD type, const std::string& data) {
    HKEY k; LSTATUS st = RegCreateKeyExW(p.root, p.sub.c_str(), 0, nullptr, 0, KEY_SET_VALUE | V64, nullptr, &k, nullptr);
    if (st != ERROR_SUCCESS) return st;
    st = RegSetValueExW(k, name.c_str(), 0, type, (const BYTE*)data.data(), (DWORD)data.size());
    RegCloseKey(k); return st;
}
bool regWriteRaw(const RegPath& p, const std::wstring& name, DWORD type, const std::string& data, bool ti) {
    if (!p.root) return false;
    LSTATUS st = ti ? ERROR_ACCESS_DENIED : writeOnce(p, name, type, data);
    if (st == ERROR_ACCESS_DENIED) { TIScope t; if (t.on) st = writeOnce(p, name, type, data); }
    return st == ERROR_SUCCESS;
}
static LSTATUS delOnce(const RegPath& p, const std::wstring& name) {
    HKEY k; LSTATUS st = RegOpenKeyExW(p.root, p.sub.c_str(), 0, KEY_SET_VALUE | V64, &k);
    if (st == ERROR_FILE_NOT_FOUND) return ERROR_SUCCESS;
    if (st != ERROR_SUCCESS) return st;
    st = RegDeleteValueW(k, name.c_str()); RegCloseKey(k);
    return st == ERROR_FILE_NOT_FOUND ? ERROR_SUCCESS : st;
}
bool regDeleteValue(const RegPath& p, const std::wstring& name, bool ti) {
    if (!p.root) return false;
    LSTATUS st = ti ? ERROR_ACCESS_DENIED : delOnce(p, name);
    if (st == ERROR_ACCESS_DENIED) { TIScope t; if (t.on) st = delOnce(p, name); }
    return st == ERROR_SUCCESS;
}
bool regKeyExists(const RegPath& p) {
    HKEY k; if (!p.root || RegOpenKeyExW(p.root, p.sub.c_str(), 0, KEY_QUERY_VALUE | V64, &k) != ERROR_SUCCESS) return false;
    RegCloseKey(k); return true;
}
bool regCreateKey(const RegPath& p, bool ti) {
    auto once = [&]() { HKEY k; LSTATUS st = RegCreateKeyExW(p.root, p.sub.c_str(), 0, nullptr, 0, KEY_READ | V64, nullptr, &k, nullptr); if (st == ERROR_SUCCESS) RegCloseKey(k); return st; };
    LSTATUS st = ti ? ERROR_ACCESS_DENIED : once();
    if (st == ERROR_ACCESS_DENIED) { TIScope t; if (t.on) st = once(); }
    return st == ERROR_SUCCESS;
}
bool regDeleteTree(const RegPath& p, bool ti) {
    if (!p.root || p.sub.empty()) return false;
    auto once = [&]() -> LSTATUS {
        size_t s = p.sub.find_last_of(L'\\');
        std::wstring parent = s == std::wstring::npos ? L"" : p.sub.substr(0, s), leaf = s == std::wstring::npos ? p.sub : p.sub.substr(s + 1);
        HKEY k; LSTATUS st = RegOpenKeyExW(p.root, parent.c_str(), 0, KEY_ALL_ACCESS | V64, &k);
        if (st == ERROR_FILE_NOT_FOUND) return ERROR_SUCCESS;
        if (st != ERROR_SUCCESS) return st;
        st = RegDeleteTreeW(k, leaf.c_str());
        if (st == ERROR_SUCCESS || st == ERROR_FILE_NOT_FOUND) { RegDeleteKeyExW(k, leaf.c_str(), V64, 0); st = ERROR_SUCCESS; }
        RegCloseKey(k); return st;
    };
    LSTATUS st = ti ? ERROR_ACCESS_DENIED : once();
    if (st == ERROR_ACCESS_DENIED) { TIScope t; if (t.on) st = once(); }
    return st == ERROR_SUCCESS;
}
bool regSaveKey(const RegPath& p, const std::wstring& file) {
    enablePrivilege(SE_BACKUP_NAME);
    HKEY k; if (RegOpenKeyExW(p.root, p.sub.c_str(), REG_OPTION_BACKUP_RESTORE, KEY_READ | V64, &k) != ERROR_SUCCESS) return false;
    DeleteFileW(file.c_str());
    LSTATUS st = RegSaveKeyExW(k, file.c_str(), nullptr, REG_LATEST_FORMAT);
    RegCloseKey(k); return st == ERROR_SUCCESS;
}
bool regRestoreKey(const RegPath& p, const std::wstring& file) {
    enablePrivilege(SE_RESTORE_NAME); enablePrivilege(SE_BACKUP_NAME);
    HKEY k; if (RegCreateKeyExW(p.root, p.sub.c_str(), 0, nullptr, REG_OPTION_BACKUP_RESTORE, KEY_ALL_ACCESS | V64, nullptr, &k, nullptr) != ERROR_SUCCESS) return false;
    LSTATUS st = RegRestoreKeyW(k, file.c_str(), REG_FORCE_RESTORE);
    RegCloseKey(k); return st == ERROR_SUCCESS;
}
std::vector<std::wstring> regSubkeys(const RegPath& p) {
    std::vector<std::wstring> out; HKEY k;
    if (!p.root || RegOpenKeyExW(p.root, p.sub.c_str(), 0, KEY_ENUMERATE_SUB_KEYS | V64, &k) != ERROR_SUCCESS) return out;
    wchar_t name[512];
    for (DWORD i = 0;; i++) { DWORD n = 512; if (RegEnumKeyExW(k, i, name, &n, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break; out.push_back(name); }
    RegCloseKey(k); return out;
}
std::vector<std::wstring> regValueNames(const RegPath& p) {
    std::vector<std::wstring> out; HKEY k;
    if (!p.root || RegOpenKeyExW(p.root, p.sub.c_str(), 0, KEY_QUERY_VALUE | V64, &k) != ERROR_SUCCESS) return out;
    wchar_t name[16384];
    for (DWORD i = 0;; i++) { DWORD n = 16384; if (RegEnumValueW(k, i, name, &n, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break; out.push_back(name); }
    RegCloseKey(k); return out;
}
std::wstring regString(const RegPath& p, const std::wstring& name) {
    RegVal v = regRead(p, name);
    if (!v.exists || (v.type != REG_SZ && v.type != REG_EXPAND_SZ && v.type != REG_MULTI_SZ)) return L"";
    std::wstring s((const wchar_t*)v.data.data(), v.data.size() / 2);
    size_t z = s.find(L'\0'); if (z != std::wstring::npos) s.resize(z);
    return s;
}
DWORD regDword(const RegPath& p, const std::wstring& name, DWORD def) {
    RegVal v = regRead(p, name);
    if (!v.exists || v.type != REG_DWORD || v.data.size() < 4) return def;
    DWORD d; memcpy(&d, v.data.data(), 4); return d;
}
std::string toHex(const std::string& b) {
    static const char* H = "0123456789abcdef"; std::string s; s.reserve(b.size() * 2);
    for (unsigned char c : b) { s += H[c >> 4]; s += H[c & 15]; }
    return s;
}
std::string fromHex(const std::string& h) {
    std::string b; auto nib = [](char c) { return c >= '0' && c <= '9' ? c - '0' : (c | 32) - 'a' + 10; };
    for (size_t i = 0; i + 1 < h.size(); i += 2) b += (char)((nib(h[i]) << 4) | nib(h[i + 1]));
    return b;
}
bool encodeValue(u8 t, const std::wstring& text, DWORD& type, std::string& data) {
    switch (t) {
    case 'd': { type = REG_DWORD; DWORD d = (DWORD)wcstoull(text.c_str(), nullptr, 0); data.assign((char*)&d, 4); return true; }
    case 'q': { type = REG_QWORD; unsigned long long q = wcstoull(text.c_str(), nullptr, 0); data.assign((char*)&q, 8); return true; }
    case 's': case 'x': { type = t == 's' ? REG_SZ : REG_EXPAND_SZ; std::wstring w = text; data.assign((const char*)w.c_str(), (w.size() + 1) * 2); return true; }
    case 'm': {
        type = REG_MULTI_SZ; std::wstring w;
        size_t p = 0; for (;;) { size_t q = text.find(L"\\0", p); w += text.substr(p, q == std::wstring::npos ? std::wstring::npos : q - p); w += L'\0'; if (q == std::wstring::npos) break; p = q + 2; }
        if (text.empty()) w = L""; w += L'\0';
        data.assign((const char*)w.c_str(), w.size() * 2); return true;
    }
    case 'b': { type = REG_BINARY; data = fromHex(u16to8(text)); return true; }
    }
    return false;
}
bool sameValue(const RegVal& v, DWORD type, const std::string& data) {
    if (!v.exists) return false;
    if (type == REG_DWORD || type == REG_QWORD) {
        if (v.type != REG_DWORD && v.type != REG_QWORD) return false;
        unsigned long long a = 0, b = 0; memcpy(&a, v.data.data(), std::min<size_t>(8, v.data.size())); memcpy(&b, data.data(), std::min<size_t>(8, data.size()));
        if (v.data.size() == 4) a &= 0xFFFFFFFFull; if (data.size() == 4) b &= 0xFFFFFFFFull;
        return a == b;
    }
    if (type == REG_SZ || type == REG_EXPAND_SZ) {
        if (v.type != REG_SZ && v.type != REG_EXPAND_SZ) return false;
        std::wstring a((const wchar_t*)v.data.data(), v.data.size() / 2), b((const wchar_t*)data.data(), data.size() / 2);
        auto cut = [](std::wstring& s) { size_t z = s.find(L'\0'); if (z != std::wstring::npos) s.resize(z); };
        cut(a); cut(b); return _wcsicmp(a.c_str(), b.c_str()) == 0;
    }
    return v.data == data;
}

std::wstring expandEnv(const std::wstring& s) {
    if (s.find(L'%') == std::wstring::npos) return s;
    wchar_t buf[4096]; DWORD n = ExpandEnvironmentStringsW(s.c_str(), buf, 4096);
    return n && n < 4096 ? std::wstring(buf) : s;
}
std::wstring lower(std::wstring s) { for (auto& c : s) c = towlower(c); return s; }
bool icontains(const std::wstring& hay, const std::wstring& needle) { return lower(hay).find(lower(needle)) != std::wstring::npos; }
bool wildMatch(const std::wstring& pat, const std::wstring& s) {
    const wchar_t* p = pat.c_str(); const wchar_t* t = s.c_str(); const wchar_t *sp = nullptr, *st = nullptr;
    while (*t) {
        if (*p == L'*') { sp = ++p; st = t; continue; }
        if (*p == L'?' || towlower(*p) == towlower(*t)) { p++; t++; continue; }
        if (sp) { p = sp; t = ++st; continue; }
        return false;
    }
    while (*p == L'*') p++;
    return !*p;
}
bool fileExists(const std::wstring& p) { DWORD a = GetFileAttributesW(p.c_str()); return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY); }
bool dirExists(const std::wstring& p) { DWORD a = GetFileAttributesW(p.c_str()); return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY); }
bool ensureDir(const std::wstring& p) {
    if (p.empty() || dirExists(p)) return true;
    size_t s = p.find_last_of(L"\\/"); if (s != std::wstring::npos && s > 2) ensureDir(p.substr(0, s));
    return CreateDirectoryW(p.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
}
std::vector<std::wstring> globPaths(const std::wstring& pattern) {
    std::wstring pat = expandEnv(pattern);
    std::vector<std::wstring> cur;
    size_t first = pat.find(L'*');
    if (first == std::wstring::npos) { if (GetFileAttributesW(pat.c_str()) != INVALID_FILE_ATTRIBUTES) cur.push_back(pat); return cur; }
    size_t segStart = pat.rfind(L'\\', first);
    cur.push_back(pat.substr(0, segStart));
    std::wstring rest = pat.substr(segStart + 1);
    while (!rest.empty()) {
        size_t e = rest.find(L'\\'); std::wstring seg = rest.substr(0, e); rest = e == std::wstring::npos ? L"" : rest.substr(e + 1);
        std::vector<std::wstring> next;
        for (auto& base : cur) {
            if (seg.find_first_of(L"*?") == std::wstring::npos) { std::wstring p = base + L"\\" + seg; if (GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES) next.push_back(p); continue; }
            WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileExW((base + L"\\" + seg).c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, 0);
            if (h == INVALID_HANDLE_VALUE) continue;
            do { if (wcscmp(fd.cFileName, L".") && wcscmp(fd.cFileName, L"..")) next.push_back(base + L"\\" + fd.cFileName); } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
        cur.swap(next);
    }
    return cur;
}
bool deleteFileHard(const std::wstring& p) {
    SetFileAttributesW(p.c_str(), FILE_ATTRIBUTE_NORMAL);
    if (DeleteFileW(p.c_str()) || GetLastError() == ERROR_FILE_NOT_FOUND) return true;
    { TIScope t; if (t.on) { SetFileAttributesW(p.c_str(), FILE_ATTRIBUTE_NORMAL); if (DeleteFileW(p.c_str())) return true; } }
    return MoveFileExW(p.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT) != 0;
}
bool deleteTree(const std::wstring& p, bool contentOnly, int* removed) {
    DWORD a = GetFileAttributesW(p.c_str());
    if (a == INVALID_FILE_ATTRIBUTES) return true;
    if (!(a & FILE_ATTRIBUTE_DIRECTORY)) { bool ok = deleteFileHard(p); if (ok && removed) (*removed)++; return ok; }
    bool all = true;
    if (!(a & FILE_ATTRIBUTE_REPARSE_POINT)) {
        WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileExW((p + L"\\*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, 0);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
                if (!deleteTree(p + L"\\" + fd.cFileName, false, removed)) all = false;
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
    }
    if (contentOnly) return all;
    SetFileAttributesW(p.c_str(), FILE_ATTRIBUTE_NORMAL);
    if (RemoveDirectoryW(p.c_str())) return all;
    { TIScope t; if (t.on && RemoveDirectoryW(p.c_str())) return all; }
    MoveFileExW(p.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
    return false;
}
bool writeFileUtf8(const std::wstring& p, const std::string& s) {
    size_t sl = p.find_last_of(L'\\'); if (sl != std::wstring::npos) ensureDir(p.substr(0, sl));
    HANDLE h = CreateFileW(p.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD w = 0; BOOL ok = WriteFile(h, s.data(), (DWORD)s.size(), &w, nullptr); CloseHandle(h);
    return ok && w == s.size();
}
std::string readFileBytes(const std::wstring& p) {
    HANDLE h = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return {};
    LARGE_INTEGER sz; GetFileSizeEx(h, &sz); std::string s((size_t)sz.QuadPart, 0);
    DWORD r = 0; if (sz.QuadPart) ReadFile(h, &s[0], (DWORD)sz.QuadPart, &r, nullptr); CloseHandle(h);
    s.resize(r); return s;
}

ProcResult runHidden(const std::wstring& cmdline, DWORD timeoutMs, bool capture) {
    ProcResult r;
    SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
    HANDLE rd = nullptr, wr = nullptr;
    if (capture && !CreatePipe(&rd, &wr, &sa, 0)) capture = false;
    if (capture) SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW si{ sizeof(si) }; PROCESS_INFORMATION pi{};
    si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
    HANDLE nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
    if (capture) { si.dwFlags |= STARTF_USESTDHANDLES; si.hStdOutput = wr; si.hStdError = wr; si.hStdInput = nul; }
    std::wstring cl = cmdline;
    BOOL ok = CreateProcessW(nullptr, &cl[0], nullptr, nullptr, capture ? TRUE : FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    if (wr) CloseHandle(wr);
    if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
    if (!ok) { if (rd) CloseHandle(rd); return r; }
    DWORD start = GetTickCount();
    char buf[4096];
    for (;;) {
        if (capture) { DWORD avail = 0; while (PeekNamedPipe(rd, nullptr, 0, nullptr, &avail, nullptr) && avail) { DWORD n = 0; if (!ReadFile(rd, buf, std::min<DWORD>(avail, sizeof(buf)), &n, nullptr) || !n) break; if (r.out.size() < (1 << 20)) r.out.append(buf, n); } }
        DWORD w = WaitForSingleObject(pi.hProcess, 50);
        if (w == WAIT_OBJECT_0) break;
        if (GetTickCount() - start > timeoutMs) { TerminateProcess(pi.hProcess, 1); break; }
    }
    if (capture) { DWORD n = 0; while (ReadFile(rd, buf, sizeof(buf), &n, nullptr) && n) if (r.out.size() < (1 << 20)) r.out.append(buf, n); CloseHandle(rd); }
    GetExitCodeProcess(pi.hProcess, &r.code); r.ok = true;
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    return r;
}
static std::wstring quoteArg(const std::wstring& s) {
    std::wstring o = L"\"";
    size_t bs = 0;
    for (wchar_t ch : s) {
        if (ch == L'\\') { bs++; continue; }
        if (ch == L'"') { o.append(bs * 2 + 1, L'\\'); o += L'"'; }
        else { o.append(bs, L'\\'); o += ch; }
        bs = 0;
    }
    o.append(bs * 2, L'\\'); o += L'"';
    return o;
}
ProcResult runPowerShell(const std::wstring& script, DWORD timeoutMs) {
    std::wstring s = L"$ErrorActionPreference='SilentlyContinue';$ProgressPreference='SilentlyContinue';" + script;
    for (auto& ch : s) if (ch == L'\r' || ch == L'\n') ch = L' ';
    wchar_t sys[MAX_PATH]; GetSystemDirectoryW(sys, MAX_PATH);
    return runHidden(L"\"" + std::wstring(sys) + L"\\WindowsPowerShell\\v1.0\\powershell.exe\" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command " + quoteArg(s), timeoutMs);
}
int killProcesses(const std::wstring& pattern) {
    int n = 0; HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W pe{ sizeof(pe) };
    std::wstring pat = pattern; if (!icontains(pat, L".exe")) pat += L".exe";
    if (Process32FirstW(snap, &pe)) do {
        if (pe.th32ProcessID == GetCurrentProcessId()) continue;
        if (wildMatch(pat, pe.szExeFile)) {
            HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
            if (h) { if (TerminateProcess(h, 0)) n++; CloseHandle(h); }
        }
    } while (Process32NextW(snap, &pe));
    CloseHandle(snap); return n;
}
std::wstring selfPath() { wchar_t p[MAX_PATH]; GetModuleFileNameW(nullptr, p, MAX_PATH); return p; }

bool enablePrivilege(const wchar_t* name) {
    HANDLE tok;
    if (!OpenThreadToken(GetCurrentThread(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, TRUE, &tok) &&
        !OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &tok)) return false;
    TOKEN_PRIVILEGES tp{}; tp.PrivilegeCount = 1; tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    BOOL ok = LookupPrivilegeValueW(nullptr, name, &tp.Privileges[0].Luid) && AdjustTokenPrivileges(tok, FALSE, &tp, 0, nullptr, nullptr) && GetLastError() == ERROR_SUCCESS;
    CloseHandle(tok); return ok;
}
static DWORD findPid(const wchar_t* exe) {
    DWORD pid = 0; HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32W pe{ sizeof(pe) };
    DWORD mySession = 0; ProcessIdToSessionId(GetCurrentProcessId(), &mySession);
    if (Process32FirstW(snap, &pe)) do {
        if (!_wcsicmp(pe.szExeFile, exe)) { DWORD s = 0; ProcessIdToSessionId(pe.th32ProcessID, &s); pid = pe.th32ProcessID; if (s == mySession) break; }
    } while (Process32NextW(snap, &pe));
    CloseHandle(snap); return pid;
}
static HANDLE dupFrom(DWORD pid, TOKEN_TYPE type) {
    HANDLE out = nullptr; HANDLE ph = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!ph) return nullptr;
    HANDLE t; if (OpenProcessToken(ph, TOKEN_DUPLICATE | TOKEN_QUERY, &t)) { DuplicateTokenEx(t, MAXIMUM_ALLOWED, nullptr, SecurityImpersonation, type, &out); CloseHandle(t); }
    CloseHandle(ph); return out;
}
static HANDLE s_ti = nullptr;
static CRITICAL_SECTION* tiLock() { static CRITICAL_SECTION cs; static bool init = false; if (!init) { InitializeCriticalSection(&cs); init = true; } return &cs; }
static DWORD startTI() {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) return 0;
    SC_HANDLE s = OpenServiceW(scm, L"TrustedInstaller", SERVICE_START | SERVICE_QUERY_STATUS);
    DWORD pid = 0;
    if (s) {
        StartServiceW(s, 0, nullptr);
        for (int i = 0; i < 40; i++) {
            SERVICE_STATUS_PROCESS ss{}; DWORD need;
            if (QueryServiceStatusEx(s, SC_STATUS_PROCESS_INFO, (BYTE*)&ss, sizeof(ss), &need) && ss.dwCurrentState == SERVICE_RUNNING) { pid = ss.dwProcessId; break; }
            Sleep(100);
        }
        CloseServiceHandle(s);
    }
    CloseServiceHandle(scm); return pid;
}
static bool ensureTI() {
    EnterCriticalSection(tiLock());
    bool ok = false;
    if (s_ti) { CloseHandle(s_ti); s_ti = nullptr; }
    enablePrivilege(SE_DEBUG_NAME); enablePrivilege(SE_IMPERSONATE_NAME);
    HANDLE sys = dupFrom(findPid(L"winlogon.exe"), TokenImpersonation);
    if (sys && SetThreadToken(nullptr, sys)) {
        DWORD pid = startTI();
        if (pid) s_ti = dupFrom(pid, TokenImpersonation);
        RevertToSelf();
        ok = s_ti != nullptr;
    }
    if (sys) CloseHandle(sys);
    LeaveCriticalSection(tiLock());
    return ok;
}
bool tiAvailable() { return ensureTI(); }
TIScope::TIScope(bool want) { if (want && ensureTI()) on = SetThreadToken(nullptr, s_ti) != 0; }
TIScope::~TIScope() { if (on) RevertToSelf(); }

Machine detectMachine() {
    Machine m;
    SYSTEM_POWER_STATUS ps; if (GetSystemPowerStatus(&ps)) m.laptop = !(ps.BatteryFlag & 128) && ps.BatteryFlag != 255;
    MEMORYSTATUSEX ms{ sizeof(ms) }; GlobalMemoryStatusEx(&ms); m.ramGB = ms.ullTotalPhys / 1073741824.0;
    int r[4]; __cpuid(r, 1); m.hypervisor = (r[2] >> 31) & 1;
    if (m.hypervisor) {
        __cpuid(r, 0x40000000); char v[13] = {}; memcpy(v, &r[1], 4); memcpy(v + 4, &r[2], 4); memcpy(v + 8, &r[3], 4);
        std::string vs = v;
        m.vm = vs.find("VMware") != std::string::npos || vs.find("VBox") != std::string::npos || vs.find("KVM") != std::string::npos || vs.find("TCG") != std::string::npos || vs.find("prl") != std::string::npos || vs.find("XenVMM") != std::string::npos;
        std::wstring model = regString(regParse(L"HKLM\\HARDWARE\\DESCRIPTION\\System\\BIOS"), L"SystemProductName");
        if (icontains(model, L"Virtual Machine") || icontains(model, L"VirtualBox") || icontains(model, L"VMware")) m.vm = true;
    }
    SYSTEM_INFO si; GetNativeSystemInfo(&si); m.x64 = si.wProcessorArchitecture != PROCESSOR_ARCHITECTURE_INTEL;
    m.build = (DWORD)_wtoi(regString(regParse(L"HKLM\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion"), L"CurrentBuildNumber").c_str());
    RegPath disp = regParse(L"HKLM\\SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e968-e325-11ce-bfc1-08002be10318}");
    for (auto& s : regSubkeys(disp)) {
        RegPath k = disp; k.sub += L"\\" + s;
        std::wstring prov = regString(k, L"ProviderName") + L" " + regString(k, L"DriverDesc");
        if (icontains(prov, L"NVIDIA")) m.nvidia = true;
        if (icontains(prov, L"Advanced Micro Devices") || icontains(prov, L"AMD") || icontains(prov, L"Radeon")) m.amd = true;
    }
    return m;
}

bool httpGetText(const std::wstring& url, std::string& out, size_t maxBytes) {
    out.clear();
    URL_COMPONENTS uc{ sizeof(uc) }; wchar_t host[256], path[2048];
    uc.lpszHostName = host; uc.dwHostNameLength = 256; uc.lpszUrlPath = path; uc.dwUrlPathLength = 2048;
    wchar_t extra[1024]; uc.lpszExtraInfo = extra; uc.dwExtraInfoLength = 1024;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) return false;
    std::wstring obj = std::wstring(path) + extra;
    HINTERNET s = WinHttpOpen(L"cr1mix-app/" CR1MIX_VERSION L" (Windows)",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, nullptr, nullptr, 0);
    if (!s) return false;
    HINTERNET c = WinHttpConnect(s, host, uc.nPort, 0);
    HINTERNET r = c ? WinHttpOpenRequest(c, L"GET", obj.c_str(), nullptr, nullptr, nullptr, uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0) : nullptr;
    bool ok = false;
    if (r && WinHttpSendRequest(r, nullptr, 0, nullptr, 0, 0, 0) && WinHttpReceiveResponse(r, nullptr)) {
        DWORD status = 0, sl = sizeof(status);
        WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &status, &sl, nullptr);
        if (status == 200) {
            char buf[8192]; DWORD n = 0; ok = true;
            for (;;) {
                if (!WinHttpReadData(r, buf, sizeof(buf), &n)) { ok = false; break; }
                if (!n) break;
                if (out.size() + n > maxBytes) { ok = false; break; }
                out.append(buf, n);
            }
        }
    }
    if (r) WinHttpCloseHandle(r); if (c) WinHttpCloseHandle(c); WinHttpCloseHandle(s);
    return ok;
}

bool httpDownloadFile(const std::wstring& url, const std::wstring& file, size_t maxBytes) {
    std::string data;
    if (!httpGetText(url, data, maxBytes) || data.empty()) return false;
    HANDLE h = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD w = 0; bool ok = WriteFile(h, data.data(), (DWORD)data.size(), &w, nullptr) && w == data.size();
    CloseHandle(h);
    return ok;
}
