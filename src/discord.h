#pragma once
#include "common.h"
#include <atomic>
#include <mutex>

struct Discord {
    std::string clientId = "1562141148087062558";
    int port = 51894;
    std::string redirect() { return "http://localhost:" + std::to_string(port) + "/callback"; }

    enum State { Idle, Waiting, Fetching, Ok, Error } ;
    std::atomic<int> state{ Idle };
    std::mutex mx;
    std::string id, username, display, avatarHash, token, err;
    std::string avatarPng;
    std::atomic<bool> avatarReady{ false };
    std::atomic<bool> dirty{ false };

    std::wstring dataDir;
    void init(const std::wstring& dataDir);
    void login();
    void loginBrowser();
    void cancel();
    std::atomic<int> via{ 0 };
    std::atomic<bool> cancelled{ false };
    std::atomic<uintptr_t> listenSock{ 0 };
    void logout();
    bool loggedIn() { return state == Ok; }
    void loadSaved();
    void save();
};
extern Discord g_dc;
