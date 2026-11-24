#pragma once
#include "common.h"
#include <mutex>

struct Hist {
    static const int N = 90;
    float v[N] = {};
    int n = 0;
    void push(float x) { memmove(v, v + 1, sizeof(float) * (N - 1)); v[N - 1] = x; if (n < N) n++; }
    float last() const { return v[N - 1]; }
    float maxv() const { float m = 0; for (int i = N - n; i < N; i++) m = std::max(m, v[i]); return m; }
};

struct Stats {
    std::mutex mx;
    std::wstring cpuName, gpuName, osName, osVer, hostName, userName;
    int cores = 0, threads = 0, baseMhz = 0;
    double ramTotalGB = 0, vramTotalGB = 0, diskTotalGB = 0;
    float cpu = 0, gpu = 0, ram = 0, clockGHz = 0, vramUsedGB = 0, ramUsedGB = 0, diskPct = 0, diskFreeGB = 0, diskAct = 0;
    float gpu3d = 0, gpuCopy = 0, gpuVideo = 0, gpuCompute = 0;
    double netDown = 0, netUp = 0;
    std::vector<float> perCore;
    u64 uptimeSec = 0; int procs = 0, thrds = 0, handles = 0;
    Hist hCpu, hGpu, hRam, hDown, hUp, hDisk;
    double sampleTime = 0;
    u64 sampleCount = 0;
    void start();
    void snapshot(Stats& out);
};
extern Stats g_stats;
double nowSec();
