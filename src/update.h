#pragma once
#include "common.h"
#include <atomic>
#include <mutex>

struct Updater {
    enum State { Idle, Checking, UpToDate, Available, Downloading, Failed, Offline, Handover };
    std::atomic<int> state{ Idle };
    std::mutex mx;
    std::wstring latest, notes, err;
    void check();
    void install();
    void cleanup();
};
extern Updater g_upd;
#define kUpdateHow L"cr1mix app downloads the new version from cr1mix.pages.dev, checks it, installs it and opens again. Your tweaks and settings stay."
