#include "update.h"
#include "sys.h"
#include "json.h"
#include <thread>
#include <wincrypt.h>

Updater g_upd;

static const wchar_t* kHosts[] = { L"https://cr1mix.pages.dev" };
static std::wstring s_url, s_sha;

static std::wstring updatesDir() { return expandEnv(L"%LOCALAPPDATA%\\cr1mix\\updates"); }

static std::wstring sha256File(const std::wstring& f) {
    std::string data = readFileBytes(f); if (data.empty()) return L"";
    HCRYPTPROV prov = 0; HCRYPTHASH h = 0; std::wstring out;
    if (CryptAcquireContextW(&prov, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
        if (CryptCreateHash(prov, CALG_SHA_256, 0, 0, &h)) {
            if (CryptHashData(h, (const BYTE*)data.data(), (DWORD)data.size(), 0)) {
                BYTE dig[32]; DWORD n = 32;
                if (CryptGetHashParam(h, HP_HASHVAL, dig, &n, 0)) { wchar_t b[3]; for (DWORD i = 0; i < n; i++) { swprintf(b, 3, L"%02x", dig[i]); out += b; } }
            }
            CryptDestroyHash(h);
        }
        CryptReleaseContext(prov, 0);
    }
    return out;
}

static std::wstring productVersion(const std::wstring& file) {
    DWORD hnd = 0, n = GetFileVersionInfoSizeW(file.c_str(), &hnd); if (!n) return L"";
    std::vector<BYTE> buf(n); if (!GetFileVersionInfoW(file.c_str(), 0, n, buf.data())) return L"";
    struct LC { WORD lang, cp; }* lc = nullptr; UINT len = 0;
    if (!VerQueryValueW(buf.data(), L"\\VarFileInfo\\Translation", (void**)&lc, &len) || len < sizeof(LC)) return L"";
    wchar_t q[96]; swprintf(q, 96, L"\\StringFileInfo\\%04x%04x\\ProductVersion", lc->lang, lc->cp);
    wchar_t* v = nullptr; if (!VerQueryValueW(buf.data(), q, (void**)&v, &len) || !v) return L"";
    return v;
}

void Updater::cleanup() {
    for (auto dir : { L"%LOCALAPPDATA%\\cr1mix\\updates", L"%LOCALAPPDATA%\\cr1mix\\downloads" }) {
        std::wstring d = expandEnv(dir);
        if (!dirExists(d)) continue;
        for (auto& f : globPaths(d + L"\\*")) DeleteFileW(f.c_str());
        RemoveDirectoryW(d.c_str());
    }
    std::wstring old = selfPath() + L".old";
    if (fileExists(old)) DeleteFileW(old.c_str());
}

void Updater::check() {
    if (state == Checking || state == Downloading || state == Handover) return;
    state = Checking;
    std::thread([this] {
        std::vector<std::wstring> hosts;
        wchar_t devHost[512];
        if (GetEnvironmentVariableW(L"CR1MIX_UPDATE_HOST", devHost, 512)) {
            std::wstring h = devHost;
            if (h.rfind(L"http://127.0.0.1:", 0) == 0 || h.rfind(L"http://localhost:", 0) == 0) hosts.push_back(h);
        }
        for (auto h : kHosts) hosts.push_back(h);
        for (auto& host : hosts) {
            std::string body;
            if (!httpGetText(host + L"/version.json", body)) continue;
            JV v = jparse(body);
            int build = (int)v["build"].num(0);
            std::wstring url = u8to16(v["url"].str()), sha = u8to16(v["sha256"].str());
            for (auto& ch : sha) ch = towlower(ch);
            if (build <= 0 || url.empty()) continue;
            bool own = false;
            for (auto& h : hosts) if (url.rfind(h + L"/", 0) == 0) own = true;
            if (!own || url.find_first_of(L" \"<>\\") != std::wstring::npos) continue;
            if (sha.size() != 64 || sha.find_first_not_of(L"0123456789abcdef") != std::wstring::npos) continue;
            {
                std::lock_guard<std::mutex> lk(mx);
                latest = u8to16(v["version"].str()); notes = u8to16(v["notes"].str());
                s_url = url; s_sha = sha;
            }
            state = build > CR1MIX_BUILD ? Available : UpToDate;
            return;
        }
        state = Offline;
    }).detach();
}

void Updater::install() {
    if (state != Available && state != Failed) return;
    state = Downloading;
    std::thread([this] {
        auto fail = [this](const std::wstring& why) { std::lock_guard<std::mutex> lk(mx); err = why; state = Failed; };
        std::wstring url, sha, ver; { std::lock_guard<std::mutex> lk(mx); url = s_url; sha = s_sha; ver = latest; }
        if (url.empty() || sha.size() != 64) return fail(L"No update information, check again later");

        std::wstring dir = updatesDir(); ensureDir(dir);
        std::wstring file = dir + L"\\cr1mix app setup " + ver + L".exe";
        DeleteFileW(file.c_str());
        if (!httpDownloadFile(url, file, 64u << 20)) { DeleteFileW(file.c_str()); return fail(L"The download failed. Check your connection and try again"); }
        if (sha256File(file) != sha) { DeleteFileW(file.c_str()); return fail(L"The download did not match the website, nothing was installed"); }
        if (productVersion(file) != ver) { DeleteFileW(file.c_str()); return fail(L"The download is not version " + ver + L", nothing was installed"); }

        std::wstring args = L"--update \"" + selfPath() + L"\"";
        SHELLEXECUTEINFOW sei{ sizeof(sei) };
        sei.fMask = SEE_MASK_NOASYNC; sei.lpVerb = L"open"; sei.lpFile = file.c_str(); sei.lpParameters = args.c_str();
        sei.lpDirectory = dir.c_str(); sei.nShow = SW_SHOWNORMAL;
        if (!ShellExecuteExW(&sei)) { DeleteFileW(file.c_str()); return fail(L"The installer could not be opened, nothing was changed"); }
        state = Handover;
        if (HWND w = FindWindowW(L"cr1mixWnd", nullptr)) PostMessageW(w, WM_CLOSE, 0, 0);
    }).detach();
}
