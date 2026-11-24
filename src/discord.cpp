#include "discord.h"
#include "json.h"
#include <winhttp.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <thread>
#pragma comment(lib,"winhttp")
#pragma comment(lib,"ws2_32")

Discord g_dc;

static std::string httpsGet(const wchar_t* host, const std::wstring& path, const std::string& bearer, std::string* outHdr = nullptr) {
    std::string result;
    HINTERNET s = WinHttpOpen(L"cr1mix/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) return result;
    HINTERNET c = WinHttpConnect(s, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (c) {
        HINTERNET r = WinHttpOpenRequest(c, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
        if (r) {
            std::wstring hdr;
            if (!bearer.empty()) hdr = L"Authorization: Bearer " + u8to16(bearer) + L"\r\n";
            if (WinHttpSendRequest(r, hdr.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : hdr.c_str(), (DWORD)hdr.size(), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(r, nullptr)) {
                DWORD n = 0;
                do { n = 0; WinHttpQueryDataAvailable(r, &n); if (!n) break; std::string buf(n, 0); DWORD rd = 0; WinHttpReadData(r, &buf[0], n, &rd); result.append(buf.data(), rd); } while (n);
            }
            WinHttpCloseHandle(r);
        }
        WinHttpCloseHandle(c);
    }
    WinHttpCloseHandle(s);
    return result;
}

static std::string jfield(const std::string& json, const char* key) {
    JV v = jparse(json); return v[key].str();
}

static bool s_rewrite = false;
void Discord::init(const std::wstring& dd) { dataDir = dd; loadSaved(); if (s_rewrite) { s_rewrite = false; save(); } }

void Discord::loadSaved() {
    std::wstring f = dataDir + L"\\discord.json";
    HANDLE h = CreateFileW(f.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD sz = GetFileSize(h, nullptr); std::string s(sz, 0); DWORD r; ReadFile(h, &s[0], sz, &r, nullptr); CloseHandle(h); s.resize(r);
    JV v = jparse(s);
    std::lock_guard<std::mutex> lk(mx);
    id = v["id"].str(); username = v["username"].str(); display = v["display"].str(); avatarHash = v["avatar"].str();
    bool hadToken = !v["token"].str().empty();
    if (!id.empty()) {
        std::wstring af = dataDir + L"\\avatar.png";
        HANDLE ah = CreateFileW(af.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        if (ah != INVALID_HANDLE_VALUE) { DWORD asz = GetFileSize(ah, nullptr); avatarPng.resize(asz); DWORD ar; ReadFile(ah, &avatarPng[0], asz, &ar, nullptr); CloseHandle(ah); avatarPng.resize(ar); avatarReady = true; }
        state = Ok; dirty = true;
    }
    s_rewrite = hadToken && !id.empty();
}
void Discord::save() {
    std::lock_guard<std::mutex> lk(mx);
    std::string s = "{"; auto add = [&](const char* k, const std::string& v, bool last = false) { s += "\""; s += k; s += "\":"; jesc(s, v); if (!last) s += ","; };
    add("id", id); add("username", username); add("display", display); add("avatar", avatarHash, true); s += "}";
    std::wstring f = dataDir + L"\\discord.json";
    HANDLE h = CreateFileW(f.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) { DWORD w; WriteFile(h, s.data(), (DWORD)s.size(), &w, nullptr); CloseHandle(h); }
    if (!avatarPng.empty()) { HANDLE ah = CreateFileW((dataDir + L"\\avatar.png").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr); if (ah != INVALID_HANDLE_VALUE) { DWORD w; WriteFile(ah, avatarPng.data(), (DWORD)avatarPng.size(), &w, nullptr); CloseHandle(ah); } }
}
void Discord::logout() {
    std::lock_guard<std::mutex> lk(mx);
    id.clear(); username.clear(); display.clear(); avatarHash.clear(); token.clear(); avatarPng.clear(); avatarReady = false;
    state = Idle; dirty = true;
    DeleteFileW((dataDir + L"\\discord.json").c_str());
    DeleteFileW((dataDir + L"\\avatar.png").c_str());
}

static std::string urlDecode(const std::string& s) {
    std::string o; for (size_t i = 0; i < s.size(); i++) { if (s[i] == '%' && i + 2 < s.size()) { auto hv = [](char c) { return c <= '9' ? c - '0' : (c | 32) - 'a' + 10; }; o += (char)((hv(s[i + 1]) << 4) | hv(s[i + 2])); i += 2; } else if (s[i] == '+') o += ' '; else o += s[i]; }
    return o;
}

static void fetchProfile(Discord* d, const std::string& token) {
    d->state = Discord::Fetching;
    std::string me = httpsGet(L"discord.com", L"/api/v10/users/@me", token);
    JV v = jparse(me);
    if (v["id"].str().empty()) { d->err = "Could not read profile"; d->state = Discord::Error; d->dirty = true; return; }
    std::string id = v["id"].str(), un = v["username"].str(), disp = v["global_name"].str(), av = v["avatar"].str();
    std::string png;
    if (!av.empty()) png = httpsGet(L"cdn.discordapp.com", u8to16("/avatars/" + id + "/" + av + (av.rfind("a_", 0) == 0 ? ".gif" : ".png") + "?size=160"), "");
    {
        std::lock_guard<std::mutex> lk(d->mx);
        d->id = id; d->username = un; d->display = disp.empty() ? un : disp; d->avatarHash = av;
        d->token.clear();
        d->avatarPng = png; d->avatarReady = !png.empty();
    }
    d->save();
    d->state = Discord::Ok; d->dirty = true;
}

static const char* kCallbackHtml =
"<!doctype html><html><head><meta charset=utf-8><title>cr1mix app</title><style>"
"html,body{height:100%;margin:0;background:#0e0f12;color:#e6e7eb;font-family:Segoe UI,system-ui,sans-serif;"
"display:flex;align-items:center;justify-content:center;flex-direction:column}"
".c{text-align:center}.s{width:46px;height:46px;border:4px solid #2a2c31;border-top-color:#8ab4f8;border-radius:50%;"
"animation:spin 1s linear infinite;margin:0 auto 20px}@keyframes spin{to{transform:rotate(360deg)}}"
"h2{font-weight:500}p{color:#a8acb4}</style></head><body><div class=c><div class=s></div>"
"<h2 id=t>Signing you in to cr1mix app…</h2><p id=m>One moment.</p></div>"
"<script>var h=location.hash.substring(1),t=document.getElementById('t'),m=document.getElementById('m');"
"if(h){fetch('/token?'+h).then(()=>{t.textContent='You are signed in';m.textContent='Taking you to the cr1mix Discord…';"
"setTimeout(()=>{location.href='https://discord.com/invite/NjkgT7vXBb';},1000);});}"
"else{t.textContent='Sign-in was cancelled';m.textContent='You can close this tab and try again in cr1mix app.';}</script></body></html>";

static std::string s_state;
extern "C" BOOLEAN NTAPI SystemFunction036(PVOID buf, ULONG len);
static std::string newState() {
    unsigned char r[16]{}; SystemFunction036(r, sizeof(r));
    std::string s; char b[3];
    for (unsigned char c : r) { snprintf(b, 3, "%02x", c); s += b; }
    return s;
}
static std::string queryValue(const std::string& q, const std::string& key) {
    size_t p = 0;
    while (p < q.size()) {
        size_t e = q.find('&', p); if (e == std::string::npos) e = q.size();
        std::string kv = q.substr(p, e - p); size_t eq = kv.find('=');
        if (eq != std::string::npos && kv.substr(0, eq) == key) return urlDecode(kv.substr(eq + 1));
        p = e + 1;
    }
    return "";
}

static void serverThread(Discord* d) {
    WSADATA w; WSAStartup(MAKEWORD(2, 2), &w);
    SOCKET ls = socket(AF_INET, SOCK_STREAM, 0);
    BOOL yes = TRUE; setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, (char*)&yes, sizeof(yes));
    sockaddr_in a{}; a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); a.sin_port = htons((u_short)d->port);
    if (bind(ls, (sockaddr*)&a, sizeof(a)) != 0 || listen(ls, 4) != 0) {
        d->err = "Port " + std::to_string(d->port) + " busy"; d->state = Discord::Error; d->dirty = true; closesocket(ls); WSACleanup(); return;
    }
    DWORD tmo = 180000; setsockopt(ls, SOL_SOCKET, SO_RCVTIMEO, (char*)&tmo, sizeof(tmo));
    d->listenSock = (uintptr_t)ls; d->cancelled = false;
    std::string token;
    for (int tries = 0; tries < 20 && token.empty(); tries++) {
        SOCKET cs = accept(ls, nullptr, nullptr);
        if (cs == INVALID_SOCKET) break;
        setsockopt(cs, SOL_SOCKET, SO_RCVTIMEO, (char*)&tmo, sizeof(tmo));
        std::string req; char buf[2048]; int n;
        while ((n = recv(cs, buf, sizeof(buf), 0)) > 0) { req.append(buf, n); if (req.find("\r\n\r\n") != std::string::npos) break; }
        std::string path; { size_t s = req.find(' '); size_t e = req.find(' ', s + 1); if (s != std::string::npos && e != std::string::npos) path = req.substr(s + 1, e - s - 1); }
        std::string body, ctype = "text/html";
        if (path.rfind("/token", 0) == 0) {
            size_t q = path.find('?');
            std::string frag = q == std::string::npos ? "" : path.substr(q + 1);
            if (!s_state.empty() && queryValue(frag, "state") == s_state) token = queryValue(frag, "access_token");
            body = token.empty() ? "{\"ok\":false}" : "{\"ok\":true}"; ctype = "application/json";
        } else {
            body = kCallbackHtml;
        }
        std::string resp = "HTTP/1.1 200 OK\r\nContent-Type: " + ctype + "\r\nContent-Length: " + std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
        send(cs, resp.data(), (int)resp.size(), 0);
        closesocket(cs);
        if (!token.empty()) { Sleep(150); break; }
    }
    if (d->listenSock == (uintptr_t)ls) { d->listenSock = 0; closesocket(ls); }
    WSACleanup();
    if (d->cancelled) return;
    if (token.empty()) { if (d->state == Discord::Waiting) { d->err = "Login timed out or was cancelled"; d->state = Discord::Error; d->dirty = true; } }
    else fetchProfile(d, token);
}

static bool pipeWrite(HANDLE h, int op, const std::string& json) {
    std::string f(8, 0); int len = (int)json.size(); memcpy(&f[0], &op, 4); memcpy(&f[4], &len, 4); f += json;
    DWORD w = 0; return WriteFile(h, f.data(), (DWORD)f.size(), &w, nullptr) && w == f.size();
}
static bool pipeRead(HANDLE h, int& op, std::string& json, DWORD timeoutMs) {
    DWORD start = GetTickCount(), avail = 0;
    while (GetTickCount() - start < timeoutMs) { if (!PeekNamedPipe(h, nullptr, 0, nullptr, &avail, nullptr)) return false; if (avail >= 8) break; Sleep(20); }
    if (avail < 8) return false;
    char hdr[8]; DWORD r = 0; if (!ReadFile(h, hdr, 8, &r, nullptr) || r != 8) return false;
    int len = 0; memcpy(&op, hdr, 4); memcpy(&len, hdr + 4, 4);
    if (len < 0 || len > (1 << 20)) return false;
    json.assign(len, 0); DWORD got = 0;
    while (got < (DWORD)len) { DWORD n = 0; if (!ReadFile(h, &json[got], len - got, &n, nullptr) || !n) return false; got += n; }
    return true;
}
static bool ipcLogin(Discord* d) {
    for (int i = 0; i < 10; i++) {
        std::wstring name = L"\\\\.\\pipe\\discord-ipc-" + std::to_wstring(i);
        HANDLE h = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) continue;
        bool ok = false;
        if (pipeWrite(h, 0, "{\"v\":1,\"client_id\":\"" + d->clientId + "\"}")) {
            int op = 0; std::string js;
            if (pipeRead(h, op, js, 4000) && op == 1) {
                JV v = jparse(js);
                JV u = v["data"]["user"];
                std::string id = u["id"].str();
                if (v["evt"].str() == "READY" && !id.empty()) {
                    std::string un = u["username"].str(), disp = u["global_name"].str(), av = u["avatar"].str();
                    d->state = Discord::Fetching; d->dirty = true;
                    std::string png;
                    if (!av.empty()) png = httpsGet(L"cdn.discordapp.com", u8to16("/avatars/" + id + "/" + av + ".png?size=160"), "");
                    {
                        std::lock_guard<std::mutex> lk(d->mx);
                        d->id = id; d->username = un; d->display = disp.empty() ? un : disp; d->avatarHash = av; d->token.clear();
                        d->avatarPng = png; d->avatarReady = !png.empty();
                    }
                    d->save();
                    d->state = Discord::Ok; d->dirty = true;
                    ok = true;
                }
            }
            pipeWrite(h, 2, "{}");
        }
        CloseHandle(h);
        if (ok) return true;
    }
    return false;
}

static void startBrowser(Discord* d) {
    d->via = 2; d->dirty = true;
    if (uintptr_t old = d->listenSock.exchange(0)) closesocket((SOCKET)old);
    Sleep(50);
    s_state = newState();
    std::thread(serverThread, d).detach();
    Sleep(150);
    std::string url = "https://discord.com/oauth2/authorize?client_id=" + d->clientId + "&state=" + s_state +
        "&response_type=token&scope=identify&redirect_uri=" + [&] { std::string r = d->redirect(), o; for (char c : r) { if (isalnum((u8)c) || c == '-' || c == '_' || c == '.' || c == '~') o += c; else { char b[4]; snprintf(b, 4, "%%%02X", (u8)c); o += b; } } return o; }() +
        "&prompt=consent";
    ShellExecuteW(nullptr, L"open", u8to16(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void Discord::login() {
    if (state == Waiting || state == Fetching) return;
    err.clear(); via = 1; state = Waiting; dirty = true;
    std::thread([this] {
        if (ipcLogin(this)) return;
        startBrowser(this);
    }).detach();
}
void Discord::loginBrowser() {
    if (state == Waiting || state == Fetching) return;
    err.clear(); state = Waiting; dirty = true;
    std::thread([this] { startBrowser(this); }).detach();
}
void Discord::cancel() {
    if (state != Waiting) return;
    cancelled = true; state = Idle; dirty = true;
    if (uintptr_t old = listenSock.exchange(0)) closesocket((SOCKET)old);
}
