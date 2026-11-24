#include "pages.h"
#include "discord.h"
#include "update.h"
#include <tuple>

struct SetupState {
    bool init = false;
    int purpose = 2;
    int defender = 0;
    int updates = 1;
    int mitig = 0, spectre = 0;
    int ai = 0;
    int ux = 1;
    int cpu = 0;
    int svc = 1;
    int tasks = 1;
    int search = 0;
    int msi = 1;
    int telemetry = 0;
    int privacy = 0;
    int runtimes = 0;
    int browser = 1;
    bool ngen = false;
    bool rmEdge = true, rmOneDrive = true, rmTeams = true, rmWsl = true, rmCalc = false, rmNotepad = false, rmPaint = true, rmTerminal = false;
    bool rmBloat = true, rmGameBar = true, rmA11y = false, rmStore = false, rmPhotos = false, rmCamera = true, rmSnip = false, rmMaps = true, rmClock = true, rmXbox = true;
    bool rmPrinting = true, rmBluetooth = true, rmWifi = false;
    bool ctxMenu = true, darkMode = true, tbLeft = false, tbSmall = false, noTransp = false, openShell = true;
    bool lookWidgets = true, lookTaskbar = true, lookStart = true, lookFiles = true;
};
static SetupState ss;

static const char* kCovered[] = {
    "win.defender", "win.wumanual", "win.wudrivers", "win.wuoff", "cpu.vbs", "cpu.procmit", "cpu.downfall", "cpu.spectre",
    "apps.debloat", "apps.edgeremove", "apps.onedrive", "apps.teamsoffice", "apps.chatinstall", "apps.gamebar", "apps.a11ytools", "apps.store", "apps.xbox",
    "apps.copilot", "apps.recall", "apps.edgeai", "win.printing", "net.bluetooth", "net.wifisvc",
    "look.classicmenu", "look.dark", "look.taskbarleft", "look.taskbarsmall", "look.transparency", "apps.openshell",
    "look.xamlanim", "look.shellanim", "look.visualfx", "win.hibernate", "game.dvr",
    "cpu.plan", "cpu.park", "cpu.boost", "cpu.idle", "win.svcpreset", "win.taskmaint", "win.maintenance", "win.defragtask", "win.servicingtasks",
    "win.nosearch", "win.searchsvc", "gpu.msi", "win.diagtrack", "apps.runtimes", "apps.browser", "win.netngen",
};

static std::vector<JobItem> buildPlan() {
    std::vector<JobItem> out;
    std::set<std::string> covered(std::begin(kCovered), std::end(kCovered));
    auto idx = [](const char* id) { return g_cat.find(id); };
    auto apply = [&](const char* id) { int i = idx(id); if (i < 0) return; Tweak& t = g_cat.tweaks[i]; if (!g_eng.requirementsMet(t)) return; if (t.kind == TK_TOGGLE && t.state == 1) return; out.push_back({ i, t.kind == TK_ACTION ? J_RUN : J_APPLY, -1, {} }); };
    auto choose = [&](const char* id, int o) { int i = idx(id); if (i < 0) return; Tweak& t = g_cat.tweaks[i]; if (o < 0 || o >= (int)t.opts.size() || t.state == o) return; out.push_back({ i, J_CHOOSE, o, {} }); };
    auto run = [&](const char* id, int o = -1) { int i = idx(id); if (i >= 0) out.push_back({ i, J_RUN, o, {} }); };

    for (auto& t : g_cat.tweaks) {
        if (covered.count(t.id) || t.kind == TK_ACTION || !g_eng.requirementsMet(t)) continue;
        if (t.tab == "look") continue;
        bool rec = (t.fl & TF_REC) != 0;
        bool prv = t.tab == "prv";
        if (prv && ss.telemetry == 1) continue;
        if (!rec && !(prv && ss.privacy == 0 && t.risk == 1)) continue;
        if (t.risk >= 3) continue;
        if (t.risk == 2 && !rec) continue;
        if (ss.purpose == 1 && t.tab == "game") continue;
        if (t.kind == TK_TOGGLE) { if (t.state != 1) out.push_back({ t.idx, J_APPLY, -1, {} }); }
        else if (t.kind == TK_CHOICE) {
            int o = recOption(t);
            if (o >= 0 && t.state != o) out.push_back({ t.idx, J_CHOOSE, o, {} });
        }
    }
    if (ss.defender > 0) choose("win.defender", ss.defender);
    if (ss.updates >= 1) { apply("win.wumanual"); apply("win.wudrivers"); }
    if (ss.updates == 2) apply("win.wuoff");
    if (ss.mitig == 1) { apply("cpu.vbs"); apply("cpu.procmit"); apply("cpu.downfall"); }
    if (ss.spectre == 1) apply("cpu.spectre");
    if (ss.telemetry == 0) apply("win.diagtrack");
    {
        int i = idx("apps.debloat");
        if (i >= 0) {
            Tweak& t = g_cat.tweaks[i]; std::vector<int> items;
            auto item = [&](const char* key, bool on) { if (!on) return; for (size_t k = 0; k < t.opts.size(); k++) if (t.opts[k].key == key && !t.picked[k]) items.push_back((int)k); };
            item("msextras", ss.rmBloat); item("office", ss.rmBloat); item("phone", ss.rmBloat); item("widgets", ss.rmBloat); item("thirdparty", ss.rmBloat); item("oem", ss.rmBloat);
            item("copilot", ss.ai == 0); item("teams", ss.rmTeams); item("paint", ss.rmPaint); item("photos", ss.rmPhotos); item("camera", ss.rmCamera);
            item("alarms", ss.rmClock); item("maps", ss.rmMaps); item("calc", ss.rmCalc); item("notepad", ss.rmNotepad); item("snip", ss.rmSnip);
            item("terminal", ss.rmTerminal); item("wsl", ss.rmWsl);
            if (!items.empty()) out.push_back({ i, J_PICK, -1, items });
        }
    }
    if (ss.rmTeams) { apply("apps.teamsoffice"); apply("apps.chatinstall"); }
    if (ss.rmGameBar) apply("apps.gamebar");
    if (ss.rmA11y) apply("apps.a11ytools");
    if (ss.rmStore) apply("apps.store");
    if (ss.rmXbox) apply("apps.xbox");
    if (ss.ai == 0) { apply("apps.copilot"); apply("apps.recall"); apply("apps.edgeai"); }
    if (ss.rmPrinting) apply("win.printing");
    if (ss.rmBluetooth) apply("net.bluetooth");
    if (ss.rmWifi) apply("net.wifisvc");
    if (ss.ctxMenu) apply("look.classicmenu");
    if (ss.darkMode) apply("look.dark");
    if (ss.tbLeft) apply("look.taskbarleft");
    if (ss.tbSmall) apply("look.taskbarsmall");
    if (ss.noTransp) apply("look.transparency");
    if (ss.lookWidgets) apply("look.widgets");
    if (ss.lookTaskbar) { apply("look.taskview"); apply("look.chaticon"); apply("look.auxpins"); apply("look.endtask"); }
    if (ss.lookStart) { apply("look.startpins"); apply("look.startrec"); apply("look.startlayout"); }
    if (ss.lookFiles) { apply("look.extensions"); apply("look.shortcut"); apply("look.ctxclean"); }
    if (ss.ux == 0) apply("win.hibernate");
    apply("game.dvr");
    choose("cpu.plan", ss.cpu == 0 ? (g_eng.mc.laptop ? 1 : 0) : 2);
    if (ss.cpu == 0) { apply("cpu.park"); apply("cpu.boost"); }
    choose("win.svcpreset", ss.svc + 1);
    if (ss.tasks == 0) { apply("win.taskmaint"); apply("win.maintenance"); }
    if (ss.search == 1) apply("win.nosearch");
    if (ss.msi == 0) apply("gpu.msi");
    if (ss.rmOneDrive) run("apps.onedrive");
    if (ss.runtimes == 0) run("apps.runtimes");
    if (ss.browser > 0) run("apps.browser", ss.browser == 1 ? 1 : ss.browser == 2 ? 0 : 2);
    if (ss.rmEdge) run("apps.edgeremove");
    if (ss.openShell) run("apps.openshell");
    if (ss.ngen) run("win.netngen");
    return out;
}

static float qRow(float x, float y, float w, const char* icon, const wchar_t* title, const wchar_t* sub, float ctrlW, std::function<void(const Rect&)> ctrl, bool draw = true) {
    const Theme& T = app.th;
    bool narrow = w < 760;
    float tw = narrow ? w - 80 : w - ctrlW - 110;
    float sh = g.measure(sub, 12.5f, TW_REG, tw, true).height;
    float h = std::max(76.f, 24 + 22 + sh + 18);
    if (narrow) h += 56;
    if (!draw) return h;
    iconBlob(x + 38, y + 38, 40, icon, T.accentDim, T.accent, 1);
    g.text(title, x + 72, y + 16, 15, T.text, TW_BOLD, tw);
    g.text(sub, x + 72, y + 40, 12.5f, T.textDim, TW_REG, tw, AL_LEFT, true);
    Rect cr = narrow ? Rect{ x + 72, y + h - 58, std::min(ctrlW, w - 92), 42 } : Rect{ x + w - ctrlW - 20, y + (h - 42) / 2, ctrlW, 42 };
    ctrl(cr);
    return h;
}
static float chipGroup(float x, float y, float w, u64 base, std::vector<std::tuple<bool*, const char*, const wchar_t*>> items, bool draw = true) {
    float cx = x, cy = y;
    for (size_t i = 0; i < items.size(); i++) {
        auto& [v, ic, s] = items[i];
        float cw2 = ui.chipW(s, ic) + 10;
        if (cx + cw2 > x + w) { cx = x; cy += 44; }
        if (draw && ui.chip(HID("qchip", base * 64 + i), cx, cy, s, *v, ic)) *v = !*v;
        cx += cw2;
    }
    return cy + 44 - y;
}

void drawSetup(const Rect& area) {
    const float kMeasure = -1e9f;
    if (!ss.init) { ss.init = true; if (g_eng.mc.laptop) { ss.cpu = 1; ss.rmBluetooth = false; } }
    Rect col = contentColumn(area, 1040);
    app.scSetup.begin(area);
    float x = col.x, y = area.y + 26 - app.scSetup.off, w = col.w;
    const Theme& T = app.th;
    iconBlob(x + 30, y + 30, 60, "magic_button", T.primaryC, T.onPrimaryC, 3, ui.time * 0.2f);
    g.text(L"Quick setup", x + 76, y + 2, 30, T.text, TW_BOLD);
    g.text(L"Answer what you want, then check the list before anything changes.", x + 78, y + 42, 13.5f, T.textDim, TW_MED);
    y += 96;

    auto seg = [&](u64 id, int& v, std::vector<std::wstring> opts) { return [&v, id, opts](const Rect& r) { v = ui.segmented(id, r, opts, v); }; };

    auto section = [&](const wchar_t* title, std::function<float(float)> body) {
        g.text(title, x + 4, y, 15, T.textDim, TW_BOLD); y += 30;
        float h = body(kMeasure);
        surfaceCard({ x, y, w, h + 12 }, 28, T.card);
        body(y + 6);
        y += h + 12 + 26;
    };
    auto rows = [&](float yy, std::vector<std::function<float(float, bool)>> rs) {
        float total = 0;
        for (size_t i = 0; i < rs.size(); i++) {
            float h = rs[i](yy <= kMeasure ? kMeasure : yy + total, yy > kMeasure);
            total += h;
            if (yy > kMeasure && i + 1 < rs.size()) g.line(x + 24, yy + total, x + w - 24, yy + total, withA(T.text, 0.06f), 1);
        }
        return total;
    };
    auto Q = [&](const char* ic, const wchar_t* t1, const wchar_t* t2, float cw, std::function<void(const Rect&)> c) {
        return [=](float yy, bool draw) -> float {
            return qRow(x, yy, w, ic, t1, t2, cw, c, draw);
        };
    };

    section(L"This PC", [&](float yy) { return rows(yy, {
        Q("devices", L"What is this PC for?", L"Gaming adds the latency, priority and input tweaks. Everyday skips the Gaming category.", 380, seg(HID("q_purpose"), ss.purpose, { L"Gaming", L"Everyday", L"Both" })),
    }); });

    section(L"Security & updates", [&](float yy) { return rows(yy, {
        Q("shield", L"Microsoft Defender", L"Disable can be switched back later. Remove is permanent. Turn off Tamper Protection in Windows Security first.", 380, seg(HID("q_def"), ss.defender, { L"Keep", L"Disable", L"Remove" })),
        Q("system_update_alt", L"Windows Update", L"Manual asks before downloading and keeps your own drivers. Off stops updates completely.", 380, seg(HID("q_wu"), ss.updates, { L"Automatic", L"Manual", L"Off" })),
        Q("security", L"Security mitigations & virtualization", L"VBS, memory integrity and exploit protection. Off gives a few FPS, but some anti-cheats will not start.", 260, seg(HID("q_mit"), ss.mitig, { L"Keep", L"Off" })),
        Q("gpp_bad", L"Spectre & Meltdown fixes", L"Off helps CPU-bound games a little, mostly on older Intel CPUs, at the cost of security.", 260, seg(HID("q_spec"), ss.spectre, { L"Keep", L"Off" })),
    }); });

    section(L"Apps to remove", [&](float yy) {
        float hh = 0;
        if (yy > kMeasure) g.text(L"Selected apps are removed. Untick anything you use.", x + 24, yy + 14, 12.5f, T.textDim, TW_REG, w - 48);
        hh += 40;
        float ch = 0;
        if (yy > kMeasure) ch = chipGroup(x + 24, yy + hh, w - 48, 1, {
            {&ss.rmBloat, "delete_sweep", L"Preinstalled bloat"}, {&ss.rmEdge, "public", L"Microsoft Edge"}, {&ss.rmOneDrive, "cloud", L"OneDrive"}, {&ss.rmTeams, "groups", L"Teams"},
            {&ss.rmXbox, "sports_esports", L"Xbox apps"}, {&ss.rmGameBar, "gamepad", L"Game Bar"}, {&ss.rmStore, "storefront", L"Microsoft Store"}, {&ss.rmWsl, "terminal", L"WSL"},
            {&ss.rmPaint, "brush", L"Paint"}, {&ss.rmPhotos, "photo_library", L"Photos"}, {&ss.rmCamera, "videocam_off", L"Camera"}, {&ss.rmSnip, "screen_record", L"Snipping Tool"},
            {&ss.rmMaps, "map", L"Maps"}, {&ss.rmClock, "schedule", L"Clock"}, {&ss.rmCalc, "apps", L"Calculator"}, {&ss.rmNotepad, "description", L"Notepad"},
            {&ss.rmTerminal, "terminal", L"Terminal"}, {&ss.rmA11y, "accessibility_new", L"Accessibility tools"} });
        else {
            float cx = 0; int lines = 1; const wchar_t* ls[] = { L"Preinstalled bloat", L"Microsoft Edge", L"OneDrive", L"Teams", L"Xbox apps", L"Game Bar", L"Microsoft Store", L"WSL", L"Paint", L"Photos", L"Camera", L"Snipping Tool", L"Maps", L"Clock", L"Calculator", L"Notepad", L"Terminal", L"Accessibility tools" };
            for (auto s : ls) { float cw2 = ui.chipW(s, "apps") + 10; if (cx + cw2 > w - 48) { cx = 0; lines++; } cx += cw2; }
            ch = lines * 44.f;
        }
        hh += ch + 10;
        if (yy > kMeasure) g.line(x + 24, yy + hh, x + w - 24, yy + hh, withA(T.text, 0.06f), 1);
        hh += rows(yy <= kMeasure ? kMeasure : yy + hh, {
            Q("smart_toy", L"Copilot & Windows AI", L"Copilot, Recall, Click to Do, AI in Paint, Photos, Notepad and Edge.", 260, seg(HID("q_ai"), ss.ai, { L"Remove", L"Keep" })),
        });
        return hh;
    });

    section(L"Hardware you do not use", [&](float yy) {
        float hh = 0;
        if (yy > kMeasure) g.text(L"Selected features are turned off. Wi-Fi, Bluetooth and printing are never touched unless you pick them here.", x + 24, yy + 14, 12.5f, T.textDim, TW_REG, w - 48);
        hh += 40;
        if (yy > kMeasure) chipGroup(x + 24, yy + hh, w - 48, 2, { {&ss.rmPrinting, "print_disabled", L"Printing"}, {&ss.rmBluetooth, "bluetooth_disabled", L"Bluetooth"}, {&ss.rmWifi, "wifi_off", L"Wi-Fi"} });
        hh += 44 + 10;
        return hh;
    });

    section(L"Look", [&](float yy) {
        float hh = 40;
        bool dr = yy > kMeasure;
        if (dr) g.text(L"Only what you select here is changed. Nothing else in Look is touched.", x + 24, yy + 14, 12.5f, T.textDim, TW_REG, w - 48);
        hh += chipGroup(x + 24, dr ? yy + hh : 0, w - 48, 3, { {&ss.ctxMenu, "menu", L"Classic right-click menu"}, {&ss.darkMode, "dark_mode", L"Dark mode"},
            {&ss.lookWidgets, "widgets", L"No widgets & news"}, {&ss.lookTaskbar, "view_week", L"Clean taskbar"}, {&ss.lookStart, "push_pin", L"Clean Start menu"},
            {&ss.lookFiles, "description", L"File extensions & tidy menus"}, {&ss.tbLeft, "format_align_left", L"Taskbar on the left"},
            {&ss.tbSmall, "density_small", L"Small taskbar"}, {&ss.noTransp, "blur_off", L"No transparency"}, {&ss.openShell, "start", L"Open-Shell Start menu"} }, dr) + 10;
        if (dr) g.line(x + 24, yy + hh, x + w - 24, yy + hh, withA(T.text, 0.06f), 1);
        hh += rows(yy <= kMeasure ? kMeasure : yy + hh, {
            Q("animation", L"Hibernation & background recording", L"Off turns off hibernation (frees disk space the size of your RAM) and Game DVR background recording.", 260, seg(HID("q_ux"), ss.ux, { L"Off", L"Keep" })),
        });
        return hh;
    });

    section(L"Performance", [&](float yy) { return rows(yy, {
        Q("rocket_launch", L"CPU & power tuning", L"Maximum uses the cr1mix app power plan with every core unparked. Lighter keeps Windows Balanced, best for laptops on battery.", 300, seg(HID("q_cpu"), ss.cpu, { L"Maximum", L"Lighter" })),
        Q("dns", L"Service preset", L"Light only stops telemetry. Balanced is the safe daily set. Extreme also stops sharing, sync and remote services you rarely need.", 380, seg(HID("q_svc"), ss.svc, { L"Light", L"Balanced", L"Extreme" })),
        Q("event_busy", L"Background maintenance tasks", L"Disable stops the nightly maintenance window and the diagnostic tasks.", 300, seg(HID("q_tasks"), ss.tasks, { L"Disable", L"Keep" })),
        Q("search_off", L"Windows Search", L"Disable removes the search engine, so Start search stops working. Use it with Open-Shell.", 300, seg(HID("q_search"), ss.search, { L"Keep", L"Disable" })),
        Q("settings_input_component", L"MSI interrupt mode", L"Message-signaled interrupts for GPU, network and NVMe. Lower latency on most hardware.", 260, seg(HID("q_msi"), ss.msi, { L"On", L"Off" })),
    }); });

    section(L"Privacy", [&](float yy) { return rows(yy, {
        Q("query_stats", L"Telemetry & data collection", L"Block turns off diagnostic data, error reports, ads and tracking and blocks the telemetry service.", 300, seg(HID("q_tel"), ss.telemetry, { L"Block", L"Keep" })),
        Q("privacy_tip", L"Privacy hardening", L"Maximum also turns off every remaining privacy switch that has no side effects.", 300, seg(HID("q_prv"), ss.privacy, { L"Maximum", L"Standard" })),
    }); });

    section(L"Install", [&](float yy) { return rows(yy, {
        Q("download", L"Visual C++ & DirectX runtimes", L"Every redistributable older games and tools need, straight from Microsoft.", 300, seg(HID("q_rt"), ss.runtimes, { L"Install", L"Skip" })),
        Q("public", L"Web browser", L"Downloaded from the official site with privacy settings and uBlock Origin. Pick one before removing Edge.", 420, seg(HID("q_br"), ss.browser, { L"None", L"Brave", L"Firefox", L"Chrome" })),
        Q("code", L"Precompile .NET now", L"Runs NGEN once, which can take up to 30 minutes. Windows otherwise does it on its own in the background.", 260, [](const Rect& r) { int v = ss.ngen ? 0 : 1; v = ui.segmented(HID("q_ngen"), r, { L"Yes", L"No" }, v); ss.ngen = v == 0; }),
    }); });

    auto plan = buildPlan();
    std::map<std::string, int> per;
    for (auto& it : plan) per[g_cat.tweaks[it.tweak].tab]++;
    float rh = 120 + (((float)g_cat.tabs.size() + 2) / 3) * 34;
    Rect rv{ x, y, w, rh };
    surfaceCard(rv, 28, T.primaryC);
    g.text(L"Review", rv.x + 26, rv.y + 22, 20, T.onPrimaryC, TW_BOLD);
    g.text(std::to_wstring(plan.size()) + L" changes. Open the list to see every one before it is applied.", rv.x + 26, rv.y + 52, 13.5f, withA(T.onPrimaryC, 0.85f), TW_REG, rv.w - 280);
    Grid gg{ rv.x + 26, rv.w - 52, 12, 3 };
    for (size_t i = 0; i < g_cat.tabs.size(); i++) {
        Rect c = gg.cell((int)i, rv.y + 90 + (i / 3) * 34, 28);
        g.icon(g_cat.tabs[i].icon.c_str(), c.x + 12, c.y + 14, 18, T.onPrimaryC);
        g.textR(g_cat.tabs[i].title, { c.x + 30, c.y, c.w - 70, 28 }, 13.5f, T.onPrimaryC, TW_MED);
        g.textR(std::to_wstring(per[g_cat.tabs[i].id]), { c.x, c.y, c.w - 8, 28 }, 13.5f, withA(T.onPrimaryC, 0.8f), TW_BOLD, AL_RIGHT);
    }
    Col sav = ui.th.accent, savOn = ui.th.onAccent;
    ui.th.accent = T.onPrimaryC; ui.th.onAccent = T.primaryC;
    if (ui.btnFilled(HID("setupgo"), { rv.r() - 230, rv.y + 26, 204, 50 }, L"See " + std::to_wstring(plan.size()) + L" changes", "view_list", !plan.empty() && !g_eng.running))
        app.openPlan(L"Quick setup", plan);
    ui.th.accent = sav; ui.th.onAccent = savOn;
    y += rh + 60;
    app.scSetup.end(area, y + app.scSetup.off - area.y);
}

void drawSettings(const Rect& area) {
    Rect col = contentColumn(area, 980);
    app.scSettings.begin(area);
    float x = col.x, y = area.y + 26 - app.scSettings.off, w = col.w;
    const Theme& T = app.th;
    iconBlob(x + 30, y + 30, 60, "settings", T.primaryC, T.onPrimaryC, 4, ui.time * 0.15f);
    g.text(L"Settings", x + 76, y + 2, 30, T.text, TW_BOLD);
    g.text(L"Make cr1mix app look and behave the way you like", x + 78, y + 42, 13.5f, T.textDim, TW_MED);
    y += 100;

    auto group = [&](const wchar_t* title, float h) { Rect r{ x, y + 32, w, h }; g.text(title, x + 4, y, 15, T.textDim, TW_BOLD); surfaceCard(r, 28, T.card); y += h + 32 + 26; return r; };
    auto row = [&](const Rect& card, float ry, const char* ic, const wchar_t* t1, const wchar_t* t2) {
        iconBlob(card.x + 42, ry + 30, 40, ic, T.accentDim, T.accent, 1);
        g.text(t1, card.x + 76, ry + 12, 15, T.text, TW_MED, card.w - 360);
        g.text(t2, card.x + 76, ry + 34, 12.5f, T.textFaint, TW_REG, card.w - 360);
    };

    Rect ap = group(L"Appearance", 4 * 80 + 20);
    float ry = ap.y + 10;
    row(ap, ry, "contrast", L"Theme", L"Dark, light or follow Windows");
    int th = ui.segmented(HID("themeSeg"), { ap.r() - 330, ry + 12, 306, 40 }, { L"Dark", L"Light", L"System" }, app.pf.theme);
    if (th != app.pf.theme) { app.pf.theme = th; app.applyTheme(); app.pf.save(); }
    ry += 80;
    row(ap, ry, "palette", L"Accent", L"Every surface is tinted by this colour");
    static const int seeds[] = { 0x9EC9BD, 0x8FB3FF, 0xB69CFF, 0xF2A7C3, 0xFFB38A, 0xE7C66B, 0x9BD08C, 0x8ED1E8 };
    for (int i = 0; i < 8; i++) {
        float cx = ap.r() - 330 + 18 + i * 38, cy = ry + 32;
        u64 id = HID("seed", i); bool hov = false;
        bool c = ui.behave(id, { cx - 16, cy - 16, 32, 32 }, &hov);
        Col sc = makeTheme(app.dark, seeds[i], 0).accent;
        bool on = app.pf.seed == seeds[i];
        float s = ui.anim(id, on ? 1.f : hov ? 0.5f : 0.f, 16);
        if (on) g.ring(cx, cy, 17, sc, 2);
        ui.blob(cx, cy, 12 + s * 2, on ? 1 : 0, sc, ui.time * 0.3f);
        if (c) { app.pf.seed = seeds[i]; app.applyTheme(); app.pf.save(); }
    }
    ry += 80;
    row(ap, ry, "blur_on", L"Transparency", L"How much of the blurred desktop shows through");
    bool ch = false;
    float tv = ui.slider(HID("transp"), { ap.r() - 330, ry + 25, 250, 14 }, app.pf.transparency / 0.35f, &ch);
    wchar_t pb[16]; swprintf(pb, 16, L"%d%%", (int)(app.pf.transparency * 100 + 0.5f));
    g.textR(pb, { ap.r() - 70, ry + 18, 46, 24 }, 13.5f, T.textDim, TW_MED, AL_RIGHT);
    if (ch) { app.pf.transparency = tv * 0.35f; app.applyTheme(); }
    if (ui.in.released && ui.active == HID("transp")) app.pf.save();
    ry += 80;
    row(ap, ry, "animation", L"Animations", L"Springy transitions and moving shapes");
    if (ui.toggle(HID("motion"), ap.r() - 80, ry + 14, app.pf.motion)) { app.pf.motion = !app.pf.motion; app.pf.save(); }

    Rect sf = group(L"Safety", 2 * 80 + 20);
    ry = sf.y + 10;
    row(sf, ry, "backup", L"Restore point before changes", L"Windows restore point before each batch of changes");
    if (ui.toggle(HID("rp2"), sf.r() - 80, ry + 14, app.pf.restorePoint)) { app.pf.restorePoint = !app.pf.restorePoint; g_eng.restorePoint = app.pf.restorePoint; app.pf.save(); }
    ry += 80;
    row(sf, ry, "undo", L"Undo everything", L"Switch back every tweak cr1mix app applied, using the saved values");
    if (ui.btnOutlined(HID("undoall"), { sf.r() - 190, ry + 12, 166, 42 }, L"Undo all", "undo", !g_eng.running)) {
        std::vector<JobItem> items;
        for (auto& t : g_cat.tweaks) if (t.kind != TK_PICK && t.kind != TK_ACTION && g_eng.journaled(t)) items.push_back({ t.idx, J_REVERT, -1, {} });
        if (items.empty()) app.showToast(L"Nothing to undo");
        else app.confirm(L"Undo everything?", std::to_wstring(items.size()) + L" tweaks go back to the values they had before cr1mix app changed them. Removed apps are not reinstalled.", L"Undo all", 2,
            [items] { Job j; j.title = L"Undo everything"; j.items = items; for (auto& it : items) g_cat.tweaks[it.tweak].busy = true; g_eng.submit(j); });
    }

    Rect ac = group(L"Account", 100);
    ID2D1Bitmap* av = avatarBitmap();
    if (av) g.imageCircle(av, ac.x + 50, ac.y + 50, 28); else iconBlob(ac.x + 50, ac.y + 50, 56, "person", T.primaryC, T.onPrimaryC, 1);
    std::wstring nm; { std::lock_guard<std::mutex> lk(g_dc.mx); nm = u8to16(!g_dc.display.empty() ? g_dc.display : g_dc.username); }
    int dst = g_dc.state;
    std::wstring line2 = g_dc.loggedIn() ? L"Signed in with Discord"
        : dst == Discord::Waiting ? (g_dc.via == 2 ? L"Finish signing in in the browser window that opened" : L"Looking for the Discord app…")
        : dst == Discord::Fetching ? L"Loading your profile…"
        : dst == Discord::Error ? u8to16(g_dc.err) : L"Sign in to show your Discord profile on Home";
    g.textR(g_dc.loggedIn() ? nm : L"Not signed in", { ac.x + 92, ac.y + 22, ac.w - 320, 26 }, 16, T.text, TW_BOLD);
    g.textR(line2, { ac.x + 92, ac.y + 48, ac.w - 320, 22 }, 12.5f, dst == Discord::Error ? T.danger : T.textFaint, TW_REG);
    if (g_dc.loggedIn()) { if (ui.btnOutlined(HID("logout"), { ac.r() - 170, ac.y + 29, 146, 42 }, L"Sign out", "logout")) g_dc.logout(); }
    else if (dst == Discord::Waiting || dst == Discord::Fetching) {
        ui.spinner(ac.r() - 210, ac.y + 50, 9, 2.5f, T.accent);
        if (dst == Discord::Waiting && ui.btnOutlined(HID("login2c"), { ac.r() - 170, ac.y + 29, 146, 42 }, L"Cancel", "close")) g_dc.cancel();
    }
    else if (ui.btnFilled(HID("login2"), { ac.r() - 190, ac.y + 29, 166, 42 }, dst == Discord::Error ? L"Try again" : L"Sign in", "login")) g_dc.login();

    {
        Rect up = group(L"Updates", 80);
        int us = g_upd.state;
        std::wstring v; { std::lock_guard<std::mutex> lk(g_upd.mx); v = us == Updater::Failed ? g_upd.err : g_upd.latest; }
        std::wstring line = us == Updater::Checking ? L"Checking the website…" : us == Updater::UpToDate ? L"You have the latest version (" CR1MIX_VERSION L")"
            : us == Updater::Available ? L"Version " + v + L" is available"
            : us == Updater::Downloading ? L"Downloading version " + v + L"…"
            : us == Updater::Handover ? L"Installing version " + v + L"…"
            : us == Updater::Failed ? v : us == Updater::Offline ? L"Could not check right now. Try again later" : L"Updates come from cr1mix.pages.dev";
        row(up, up.y + 10, "system_update_alt", L"App updates", line.c_str());
        Rect b{ up.r() - 190, up.y + 19, 166, 42 };
        if (us == Updater::Available) { if (ui.btnFilled(HID("updNow"), b, L"Update", "download")) app.confirm(L"Update to " + v + L"?", kUpdateHow, L"Update", 1, [] { g_upd.install(); }); }
        else if (us == Updater::Failed && !g_upd.latest.empty()) { if (ui.btnFilled(HID("updRetry"), b, L"Try again", "refresh")) g_upd.install(); }
        else if (ui.btnOutlined(HID("updCheck"), b, L"Check now", "refresh", us != Updater::Checking && us != Updater::Downloading && us != Updater::Handover)) g_upd.check();
    }

    Rect ab = group(L"About", 176);
    if (g.logo) g.imageTint(g.logo, { ab.x + 26, ab.y + 22, 48, 48 }, app.th.text);
    g.textR(L"cr1mix app", { ab.x + 92, ab.y + 20, ab.w - 120, 28 }, 17, T.text, TW_BOLD);
    g.textR(L"Version " CR1MIX_VERSION L" · Windows, tuned your way", { ab.x + 92, ab.y + 46, ab.w - 120, 22 }, 12.5f, T.textFaint, TW_REG);
    g.line(ab.x + 24, ab.y + 92, ab.r() - 24, ab.y + 92, withA(T.text, 0.07f), 1);
    float mx = ab.x + 50, my = ab.y + 134;
    if (g.author) { g.circle(mx, my, 27, withA(T.accent, 0.35f)); g.imageCircle(g.author, mx, my, 24); }
    else iconBlob(mx, my, 48, "person", T.primaryC, T.onPrimaryC, 1);
    g.textR(L"Made by", { ab.x + 92, ab.y + 108, ab.w - 120, 22 }, 12.5f, T.textFaint, TW_MED);
    g.textR(L"cr1mix", { ab.x + 92, ab.y + 128, ab.w - 120, 28 }, 16, T.text, TW_BOLD);
    {
        float bw = 132, bx = ab.r() - 24 - bw;
        auto open = [](const wchar_t* url) { ShellExecuteW(nullptr, L"open", url, nullptr, nullptr, SW_SHOWNORMAL); };
        if (ui.btnTonal(HID("aboutDiscord"), { bx, ab.y + 113, bw, 42 }, L"Discord", "forum")) open(L"https://discord.com/invite/NjkgT7vXBb");
        if (ui.btnTonal(HID("aboutTiktok"), { bx - bw - 10, ab.y + 113, bw, 42 }, L"TikTok", "music_note")) open(L"https://www.tiktok.com/@cr1mix");
    }
    y += 20;
    app.scSettings.end(area, y + app.scSettings.off - area.y);
}

void drawProgress() {
    static double finishedAt = -100; static bool wasRunning = false, dismissed = true;
    bool run = g_eng.running;
    if (run) { wasRunning = true; dismissed = false; }
    if (!run && wasRunning) { wasRunning = false; finishedAt = nowSec(); }
    bool show = run || (!dismissed && (nowSec() - finishedAt < 6 || g_eng.needReboot));
    float a = ui.anim(HID("progIn"), show ? 1.f : 0.f, 12);
    if (a < 0.01f) return;
    const Theme& T = app.th;
    std::wstring title, cur; std::vector<LogLine> log; int ok = 0, fail = 0;
    { std::lock_guard<std::mutex> lk(g_eng.mx); title = g_eng.jobTitle; cur = g_eng.current; log = g_eng.log; ok = g_eng.lastOk; fail = g_eng.lastFail; }
    float ex = ui.anim(HID("progEx"), app.sheet ? 1.f : 0.f, 14);
    float w = std::min(620.f, g.W - 40), hBase = 84, hLog = std::min(300.f, 30.f + log.size() * 24.f);
    float h = hBase + ex * hLog;
    Rect r{ g.W / 2 - w / 2, g.H - h - 18 + (1 - a) * 40, w, h };
    g.opacity = a;
    g.shadow(r, 26, 1.0f);
    g.fill(r, T.surfHighest, 26);
    addNoDrag(r);
    if (run) ui.wavyRing(r.x + 42, r.y + 42, 18, 4, std::max(0.04f, (float)g_eng.progress), T.accent, T.track, ui.time * 5, 1.6f, 7);
    else iconBlob(r.x + 42, r.y + 42, 40, fail ? "warning" : "check", fail ? T.warn : T.accent, fail ? rgb(0x2A1B00) : T.onAccent, 1, 0);
    g.text(run ? title : (fail ? L"Finished with warnings" : L"All done"), r.x + 76, r.y + 20, 15, T.text, TW_BOLD, r.w - 300);
    std::wstring sub = run ? cur : (g_eng.lastTask && !log.empty()) ? log.back().s : std::to_wstring(ok) + L" applied" + (fail ? L" · " + std::to_wstring(fail) + L" with warnings" : L"") + (g_eng.needReboot ? L" · restart to finish" : L"");
    g.text(sub, r.x + 76, r.y + 44, 12.5f, T.textDim, TW_REG, r.w - (g_eng.needReboot && !run ? 330 : 140));
    if (!run && g_eng.needReboot) { if (ui.btnFilled(HID("reboot"), { r.r() - 236, r.y + 20, 140, 44 }, L"Restart", "restart_alt")) app.confirm(L"Restart now?", L"Save your work first. cr1mix app finishes the changes that need a restart.", L"Restart", 1, [] { runReboot(); }); }
    if (ui.iconBtn(HID("progExp"), r.r() - 82, r.y + 42, app.sheet ? "expand_more" : "expand_less", 40)) app.sheet = !app.sheet;
    if (!run && ui.iconBtn(HID("progClose"), r.r() - 38, r.y + 42, "close", 40)) { dismissed = true; app.sheet = false; }
    if (ex > 0.02f) {
        Rect lr{ r.x + 24, r.y + hBase, r.w - 48, h - hBase - 12 };
        g.pushClip(lr);
        float ly = lr.y;
        int start = std::max(0, (int)log.size() - (int)(lr.h / 24));
        for (int i = start; i < (int)log.size(); i++) {
            const char* ic = log[i].kind == 1 ? "check_circle" : log[i].kind == 2 ? "warning" : log[i].kind == 3 ? "error" : "info";
            Col c = log[i].kind == 1 ? T.good : log[i].kind == 2 ? T.warn : log[i].kind == 3 ? T.danger : T.textDim;
            g.icon(ic, lr.x + 9, ly + 10, 15, c);
            g.text(log[i].s, lr.x + 28, ly + 1, 12.5f, T.textDim, TW_REG, lr.w - 30);
            ly += 24;
        }
        g.popClip();
    }
    g.opacity = 1;
    if (run) ui.anyAnim = true;
}

void drawConfirm() {
    const Theme& T = app.th;
    float a = ui.anim(HID("confIn"), 1.f, 16, 0.f);
    g.fill({ 0, 0, g.W, g.H }, withA(rgb(0x000000), 0.40f * a), 0);
    float w = std::min(520.f, g.W - 60);
    float bh = g.measure(app.confirmBody, 14, TW_REG, w - 56, true).height;
    float h = 108 + bh + 32 + 44 + 24;
    Rect r{ g.W / 2 - w / 2, g.H / 2 - h / 2 + (1 - a) * 20, w, h };
    g.opacity = a;
    g.shadow(r, 30, 1.2f);
    g.fill(r, T.surfHi, 30);
    addNoDrag(r);
    bool danger = app.confirmRisk >= 3;
    iconBlob(r.x + w / 2, r.y + 40, 44, danger ? "warning" : app.confirmRisk == 2 ? "info" : "help", danger ? T.dangerC : T.primaryC, danger ? T.danger : T.onPrimaryC, 1, 0);
    g.textR(app.confirmTitle, { r.x + 20, r.y + 72, w - 40, 30 }, 20, T.text, TW_BOLD, AL_CENTER);
    g.text(app.confirmBody, r.x + 28, r.y + 108, 14, T.textDim, TW_REG, w - 56, AL_LEFT, true);
    float okW = std::max(150.f, g.measure(app.confirmOk, 14, TW_MED).width + 64);
    Rect ok{ r.r() - 24 - okW, r.b() - 68, okW, 44 }, cancel{ ok.x - 120, ok.y, 110, 44 };
    if (ui.btnText(HID("confNo"), cancel, L"Cancel")) app.confirmOpen = false;
    bool go = danger ? ui.btnDanger(HID("confYes"), ok, app.confirmOk) : ui.btnFilled(HID("confYes"), ok, app.confirmOk);
    g.opacity = 1;
    if (go) { app.confirmOpen = false; auto fn = app.confirmFn; if (fn) fn(); }
}
