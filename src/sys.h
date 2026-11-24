#pragma once
#include "common.h"

struct RegPath { HKEY root = nullptr; std::wstring sub; std::wstring rootName; };
RegPath regParse(const std::wstring& full);
std::wstring regJoin(const RegPath& p);
struct RegVal { bool exists = false; DWORD type = 0; std::string data; };
RegVal regRead(const RegPath& p, const std::wstring& name);
bool regWriteRaw(const RegPath& p, const std::wstring& name, DWORD type, const std::string& data, bool ti);
bool regDeleteValue(const RegPath& p, const std::wstring& name, bool ti);
bool regKeyExists(const RegPath& p);
bool regCreateKey(const RegPath& p, bool ti);
bool regDeleteTree(const RegPath& p, bool ti);
bool regSaveKey(const RegPath& p, const std::wstring& file);
bool regRestoreKey(const RegPath& p, const std::wstring& file);
std::vector<std::wstring> regSubkeys(const RegPath& p);
std::vector<std::wstring> regValueNames(const RegPath& p);
std::wstring regString(const RegPath& p, const std::wstring& name);
DWORD regDword(const RegPath& p, const std::wstring& name, DWORD def);
bool encodeValue(u8 t, const std::wstring& text, DWORD& type, std::string& data);
bool sameValue(const RegVal& v, DWORD type, const std::string& data);
std::string toHex(const std::string& b);
std::string fromHex(const std::string& h);

std::wstring expandEnv(const std::wstring& s);
bool wildMatch(const std::wstring& pattern, const std::wstring& s);
bool icontains(const std::wstring& hay, const std::wstring& needle);
std::wstring lower(std::wstring s);
std::vector<std::wstring> globPaths(const std::wstring& pattern);
bool fileExists(const std::wstring& p);
bool dirExists(const std::wstring& p);
bool deleteTree(const std::wstring& p, bool contentOnly, int* removed);
bool deleteFileHard(const std::wstring& p);
bool writeFileUtf8(const std::wstring& p, const std::string& s);
std::string readFileBytes(const std::wstring& p);
bool ensureDir(const std::wstring& p);

struct ProcResult { DWORD code = (DWORD)-1; std::string out; bool ok = false; };
ProcResult runHidden(const std::wstring& cmdline, DWORD timeoutMs = 600000, bool capture = true);
ProcResult runPowerShell(const std::wstring& script, DWORD timeoutMs = 900000);
int killProcesses(const std::wstring& pattern);
std::wstring selfPath();

bool enablePrivilege(const wchar_t* name);
bool tiAvailable();
struct TIScope { bool on = false; explicit TIScope(bool want = true); ~TIScope(); };

struct Machine { bool laptop = false, vm = false, hypervisor = false, x64 = true, nvidia = false, amd = false; double ramGB = 0; DWORD build = 0; };
Machine detectMachine();

bool httpGetText(const std::wstring& url, std::string& out, size_t maxBytes = 65536);
bool httpDownloadFile(const std::wstring& url, const std::wstring& file, size_t maxBytes);
