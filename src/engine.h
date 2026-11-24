#pragma once
#include "catalog.h"
#include "sys.h"
#include <atomic>
#include <mutex>
#include <thread>
#include <deque>
#include <condition_variable>

enum JobOp : u8 { J_APPLY, J_REVERT, J_CHOOSE, J_PICK, J_RUN };
struct JobItem { int tweak = -1; JobOp op = J_APPLY; int opt = -1; std::vector<int> items; };
struct LogLine { std::wstring s; int kind = 0; };
using SayFn = std::function<void(const std::wstring&, int)>;
struct Job { std::wstring title; std::vector<JobItem> items; std::function<void(SayFn)> fn; };

bool createRestorePointNow(const std::wstring& desc);
bool backupRegistry(std::wstring& outDir, SayFn say);

struct Engine {
    std::wstring dataDir, stateDir;
    Machine mc;

    std::atomic<bool> running{ false };
    std::atomic<float> progress{ 0 };
    std::atomic<bool> detecting{ false };
    std::atomic<int> generation{ 0 };
    std::mutex mx;
    std::wstring jobTitle, current;
    std::vector<LogLine> log;
    bool needReboot = false, needLogoff = false;
    int lastOk = 0, lastFail = 0;
    bool lastTask = false;

    std::atomic<bool> restorePoint{ false };

    bool keepsService(const std::wstring& name);
    void setKeepService(const std::wstring& name, bool keep);
    void loadKeep();

    void start();
    void submit(const Job& j);
    void detectAll();
    bool journaled(const Tweak& t, const std::string& item = "") const;
    bool requirementsMet(const Tweak& t, std::wstring* why = nullptr) const;
    bool hardwareFits(const Tweak& t) const;

    static int runTimerMode(int res100ns);
    static int runCleanMode(const std::wstring& what);

private:
    std::mutex kmx; std::set<std::wstring> keep;
    std::thread th;
    std::mutex qmx; std::condition_variable qcv; std::deque<Job> q;
    void loop();
    void runJob(Job& j);
    void detect(Tweak& t);
};
extern Engine g_eng;
