#include "pages.h"

static int shapeFor(const std::string& id) { u64 h = HID(id); return (int)(h % 6) + 1; }

static const char* verbIcon(Verb v) {
    switch (v) {
    case V_REG: case V_REGDEL: case V_KEYADD: case V_KEYDEL: case V_RBIT: case V_RSCAN: case V_SPV: case V_EXTDEL: case V_KMATCH: case V_CONSENT: case V_MITOFF: case V_MSI: return "tune";
    case V_SVC: case V_SVCFIX: case V_SVCDEL: case V_DEFSVC: case V_START: case V_STOP: case V_STOPDIS: case V_RESET: case V_SVCCOPY: return "dns";
    case V_TASK: case V_TIMER: case V_SCHED: return "schedule";
    case V_APPX: case V_AREG: case V_ARESET: case V_MSIX: return "apps";
    case V_FEAT: case V_CAP: return "extension";
    case V_PW: case V_PLAN: case V_PLANSET: return "bolt";
    case V_WINGET: return "download";
    case V_DELDIR: case V_DELFILE: case V_DELCONTENT: case V_DELGLOB: case V_DELEMPTY: case V_FILEOFF: case V_LNKCLEAN: case V_CLEANMGR: return "delete_sweep";
    default: return "terminal";
    }
}

static float tileChips(const Tweak& t, float x, float y, float maxX) {
    struct C { const char* ic; std::wstring s; Col c; };
    std::vector<C> cs;
    std::wstring why;
    if (t.risk >= 3) cs.push_back({ "warning", L"Advanced", app.th.danger });
    else if (t.risk == 2) cs.push_back({ "info", L"Caution", app.th.warn });
    if (t.fl & TF_REBOOT) cs.push_back({ "restart_alt", L"Restart", app.th.textDim });
    if (t.fl & TF_LOGOFF) cs.push_back({ "logout", L"Sign out", app.th.textDim });
    if (!g_eng.requirementsMet(t, &why)) cs.push_back({ "info", why, app.th.textFaint });
    if (t.kind == TK_ACTION) cs.push_back({ "play_arrow", L"One-time", app.th.textFaint });
    if (t.kind == TK_PICK) { int n = 0; for (auto p : t.picked) n += p; cs.push_back({ "delete_sweep", std::to_wstring(n) + L"/" + std::to_wstring(t.opts.size()) + L" removed", app.th.textFaint }); }
    for (auto& c : cs) {
        float w = g.measure(c.s, 11.5f, TW_MED).width + 36;
        if (x + w > maxX) break;
        Rect r{ x, y, w, 26 };
        g.fill(r, withA(c.c, 0.12f), 13);
        g.icon(c.ic, r.x + 15, r.y + 13, 14, c.c);
        g.textR(c.s, { r.x + 26, r.y, w - 28, r.h }, 11.5f, c.c, TW_MED, AL_LEFT, true);
        x += w + 6;
    }
    return x;
}

static std::wstring choiceLabel(const Tweak& t) { return t.state >= 0 && t.state < (int)t.opts.size() ? t.opts[t.state].label : L"Choose"; }
static bool switchOn(const Tweak& t) { return t.state == 1; }
static std::wstring statusText(const Tweak& t) {
    switch (t.kind) {
    case TK_TOGGLE: return t.state == 1 ? L"Applied" : L"Not applied";
    case TK_CHOICE: return t.state >= 0 ? t.opts[t.state].label : L"Not set";
    case TK_PICK: return L"Pick what to remove";
    default: return t.opts.empty() ? L"Runs once" : L"Choose one, runs once";
    }
}
static std::wstring ctrlLabel(const Tweak& t) { return t.kind == TK_CHOICE ? choiceLabel(t) : (t.kind == TK_PICK || !t.opts.empty()) ? L"Choose" : L"Run"; }

static void tweakTile(const Rect& r, int ti) {
    Tweak& t = g_cat.tweaks[ti];
    const Theme& T = app.th;
    u64 id = HID(t.id);
    bool on = tweakOn(t);
    bool busy = t.busy;
    Rect ctrl{ 0, 0, 0, 0 };
    bool ctrlHot = false;
    if (t.kind == TK_TOGGLE) {
        ctrl = { r.r() - 74, r.y + 20, 56, 34 };
        if (ui.toggle(id ^ 0xA1, ctrl.x + 2, ctrl.y + 1, switchOn(t), true, busy)) requestToggle(ti, t.state != 1);
        ctrlHot = ui.hot == (id ^ 0xA1);
    } else {
        std::wstring lbl = ctrlLabel(t);
        bool actOpts = t.kind == TK_ACTION && !t.opts.empty();
        const char* ic = t.kind == TK_CHOICE || actOpts ? "expand_more" : t.kind == TK_PICK ? "checklist" : "play_arrow";
        float w = std::min(170.f, g.measure(lbl, 13.5f, TW_MED).width + 58);
        ctrl = { r.r() - w - 18, r.y + 18, w, 38 };
        u64 cid = id ^ 0xA2;
        bool c;
        if (t.kind == TK_ACTION) c = ui.btnTonal(cid, ctrl, lbl, ic, !busy);
        else {
            c = ui.behave(cid, ctrl);
            g.fill(ctrl, t.state >= 0 && t.kind == TK_CHOICE ? T.accentDim : T.surfHi, 19);
            ui.stateLayer(cid, ctrl, 19, T.text); ui.drawRipples(cid, ctrl, 19, T.text);
            g.text(lbl, ctrl.x + 16, ctrl.y + 9.5f, 13.5f, T.text, TW_MED, ctrl.w - 46);
            g.icon(ic, ctrl.r() - 20, ctrl.y + 19, 18, T.textDim);
        }
        if (busy) ui.spinner(ctrl.x - 16, ctrl.y + 19, 8, 2.5f, T.accent);
        ctrlHot = ui.hot == cid;
        if (c) { if (t.kind == TK_ACTION) requestRun(ti); else app.detail = ti; }
    }
    (void)on;
    bool click = !ctrlHot && ui.behave(id, r);
    if (click) app.detail = ti;
}
static float controlWidth(const Tweak& t) {
    if (t.kind == TK_TOGGLE) return 74;
    std::wstring lbl = ctrlLabel(t);
    return std::min(170.f, g.measure(lbl, 13.5f, TW_MED).width + 58) + 22;
}

static void tileBackground(const Rect& r, int ti) {
    Tweak& t = g_cat.tweaks[ti]; const Theme& T = app.th; u64 id = HID(t.id);
    float h = ui.anim(id ^ 0xB0, 0, 14, 0); (void)h;
    float onA = ui.anim(id ^ 0xB1, tweakOn(t) ? 1.f : 0.f, 10);
    bool hov = ui.mouseIn(r);
    float ha = ui.anim(id ^ 0xB2, hov ? 1.f : 0.f, 14);
    Col bg = mix(T.card, T.cardHi, ha * 0.8f);
    surfaceCard(r, 22, bg);
    if (onA > 0.01f) g.stroke(r, withA(T.accent, 0.55f * onA), 22, 1.5f);
    ui.drawRipples(id, r, 22, T.text);
}
static void tileContent(const Rect& r, int ti) {
    Tweak& t = g_cat.tweaks[ti]; const Theme& T = app.th; u64 id = HID(t.id);
    float onA = ui.anim(id ^ 0xB1, tweakOn(t) ? 1.f : 0.f, 10);
    float ha = ui.anim(id ^ 0xB2, ui.mouseIn(r) ? 1.f : 0.f, 14);
    iconBlob(r.x + 38, r.y + 38, 48, t.icon.c_str(), mix(T.accentDim, T.accent, onA), mix(T.accent, T.onAccent, onA), shapeFor(t.id), ha * 0.35f);
    float tw = r.w - 74 - controlWidth(t) - 8;
    g.text(t.name, r.x + 74, r.y + 20, 15, T.text, TW_BOLD, tw);
    g.text(statusText(t), r.x + 74, r.y + 41, 12, tweakOn(t) ? T.accent : T.textFaint, TW_MED, tw);
    Rect dr{ r.x + 20, r.y + 74, r.w - 40, r.h - 74 - 40 };
    float lh = g.measure(L"Ag", 12.5f, TW_REG).height;
    dr.h = floorf(dr.h / lh) * lh + 1;
    g.pushClip(dr);
    g.text(t.desc, dr.x, dr.y, 12.5f, T.textDim, TW_REG, dr.w, AL_LEFT, true);
    g.popClip();
    tileChips(t, r.x + 18, r.b() - 38, r.r() - 18);
}

static bool matches(const Tweak& t, const std::wstring& q) {
    if (q.empty()) return true;
    return icontains(t.name, q) || icontains(t.desc, q);
}

void drawTab(const Rect& area) {
    if (app.tab < 0 || app.tab >= (int)g_cat.tabs.size()) return;
    Tab& tb = g_cat.tabs[app.tab];
    Scroller& sc = app.scTab[app.tab % 16];
    Rect col = contentColumn(area);
    const Theme& T = app.th;
    sc.begin(area);
    float y = area.y + 24 - sc.off;

    int total = 0, on = appliedCount(tb, &total);
    iconBlob(col.x + 30, y + 30, 60, tb.icon.c_str(), T.primaryC, T.onPrimaryC, 1, ui.time * 0.12f);
    g.text(tb.title, col.x + 76, y + 2, 30, T.text, TW_BOLD);
    int shownCount = 0; for (auto& s : tb.subs) for (int i : s.tweaks) shownCount += tweakVisible(g_cat.tweaks[i]);
    g.text(std::to_wstring(shownCount) + L" tweaks · " + std::to_wstring(on) + L" applied", col.x + 78, y + 42, 13.5f, T.textDim, TW_MED);
    float sw = std::min(320.f, col.w * 0.34f);
    Rect sr{ col.r() - sw, y + 8, sw, 46 };
    ui.textField(HID("tabsearch", app.tab), sr, app.search, L"Search this category", "search");
    y += 84;

    float hh = 230;
    tabHero(app.tab, { col.x, y, col.w, hh });
    int cols = columnsFor(col.w);
    y += hh + 16 + (cols == 2 ? hh + 16 : cols == 1 ? (hh + 16) * 2 : 0);

    if (app.search.empty() && tb.id != "look") { automationCard(app.tab, { col.x, y, col.w, 104 }); y += 104 + 28; }

    int tc = col.w >= 1100 ? 3 : col.w >= 680 ? 2 : 1;
    Grid gr{ col.x, col.w, 14, tc };
    float th2 = 168;
    int shown = 0;
    for (auto& sub : tb.subs) {
        std::vector<int> list; for (int i : sub.tweaks) if (tweakVisible(g_cat.tweaks[i]) && matches(g_cat.tweaks[i], app.search)) list.push_back(i);
        if (list.empty()) continue;
        int sOn = 0, sAll = 0; for (int i : list) { if (g_cat.tweaks[i].kind != TK_ACTION) { sAll++; sOn += tweakOn(g_cat.tweaks[i]); } }
        sectionTitle(col.x, y, sub.title, sAll ? std::to_wstring(sOn) + L" of " + std::to_wstring(sAll) + L" on" : L"");
        y += 40;
        for (size_t k = 0; k < list.size(); k++) {
            Rect r = gr.cell((int)k, y + (k / tc) * (th2 + 14), th2);
            if (r.b() < area.y - 20 || r.y > area.b() + 20) continue;
            tileBackground(r, list[k]);
            tileContent(r, list[k]);
            tweakTile(r, list[k]);
            shown++;
        }
        y += ((list.size() + tc - 1) / tc) * (th2 + 14) + 18;
    }
    if (!app.search.empty() && !shown) { g.textR(L"Nothing here matches \"" + app.search + L"\"", { col.x, y, col.w, 60 }, 15, T.textDim, TW_MED, AL_CENTER); y += 60; }
    y += 80;
    sc.end(area, y + sc.off - area.y);
}

static int s_actFor = -1, s_actOpt = 0;

static std::wstring svcDisplay(const std::wstring& name, bool* installed) {
    static std::map<std::wstring, std::pair<std::wstring, bool>> cache;
    auto it = cache.find(name);
    if (it == cache.end()) {
        std::wstring out; bool inst = false;
        if (name.find_first_of(L"*?") != std::wstring::npos) { out = L"Per-user copies of " + name.substr(0, name.find(L'_')); inst = true; }
        else {
            HKEY k;
            if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, (L"SYSTEM\\CurrentControlSet\\Services\\" + name).c_str(), 0, KEY_READ, &k) == ERROR_SUCCESS) {
                inst = true;
                wchar_t buf[512]; DWORD sz = 0;
                if (RegLoadMUIStringW(k, L"DisplayName", buf, sizeof(buf), &sz, 0, nullptr) == ERROR_SUCCESS) out = buf;
                else { DWORD ty = 0, cb = sizeof(buf); if (RegQueryValueExW(k, L"DisplayName", nullptr, &ty, (BYTE*)buf, &cb) == ERROR_SUCCESS && ty == REG_SZ && buf[0] != L'@') out = buf; }
                RegCloseKey(k);
            }
            if (out.empty()) out = name;
        }
        it = cache.emplace(name, std::make_pair(out, inst)).first;
    }
    if (installed) *installed = it->second.second;
    return it->second.first;
}
static float presetEditor(Tweak& t, int ti, float x, float y, float cw, const Rect& sheet) {
    const Theme& T = app.th;
    static int view = -1;
    if (view < 1 || view >= (int)t.opts.size()) view = t.state >= 1 ? t.state : 2;
    g.line(x, y, x + cw, y, withA(T.text, 0.08f), 1); y += 18;
    g.text(L"Customize the presets", x, y, 16, T.text, TW_BOLD); y += 28;
    g.text(L"Every service a preset changes is listed here. Untick one to keep it exactly as it is. Your choices count for every preset and for Quick setup too.", x, y, 12.5f, T.textDim, TW_REG, cw, AL_LEFT, true);
    y += g.measure(L"Every service a preset changes is listed here. Untick one to keep it exactly as it is. Your choices count for every preset and for Quick setup too.", 12.5f, TW_REG, cw, true).height + 14;
    std::vector<std::wstring> names; for (size_t i = 1; i < t.opts.size(); i++) names.push_back(t.opts[i].label);
    int v = ui.segmented(HID("psView"), { x, y, cw, 42 }, names, view - 1) + 1; if (v != view) view = v;
    y += 56;
    std::vector<const Act*> svcs; std::set<std::wstring> seen;
    for (auto& a : t.opts[view].acts) if (a.v == V_SVC && a.n >= 3) { std::wstring k = a.a; for (auto& ch : k) ch = towlower(ch); if (seen.insert(k).second) svcs.push_back(&a); }
    int kept = 0; for (auto* a : svcs) kept += g_eng.keepsService(a->a);
    g.text(std::to_wstring(svcs.size() - kept) + L" services change · " + std::to_wstring(kept) + L" kept", x, y, 13, T.textFaint, TW_BOLD);
    float bw = 150;
    if (kept && ui.btnText(HID("psAll"), { x + cw - bw, y - 12, bw, 36 }, L"Change all", "select_all"))
        for (auto* a : svcs) g_eng.setKeepService(a->a, false);
    y += 30;
    for (size_t i = 0; i < svcs.size(); i++) {
        const Act& a = *svcs[i];
        Rect row{ x - 8, y, cw + 16, 48 };
        if (row.b() > sheet.y && row.y < sheet.b()) {
            bool inst = false; std::wstring dn = svcDisplay(a.a, &inst);
            bool keepIt = g_eng.keepsService(a.a);
            u64 rid = HID("psRow", (u64)i * 7 + view);
            bool c = ui.behave(rid, row);
            g.fill(row, keepIt ? T.surf2 : withA(T.accent, 0.08f), 14);
            ui.stateLayer(rid, row, 14, T.text);
            if (ui.checkbox(rid ^ 5, row.x + 12, row.y + 14, !keepIt)) c = true;
            float tw = cw - 160;
            g.textR(dn, { row.x + 50, row.y + 5, tw, 22 }, 13.5f, inst ? T.text : T.textFaint, TW_MED);
            g.textR(a.a + (inst ? L"" : L" · not on this PC"), { row.x + 50, row.y + 25, tw, 18 }, 11.5f, T.textFaint, TW_REG);
            g.textR(keepIt ? L"Kept" : a.n == 4 ? L"Disabled" : L"Manual", { row.r() - 110, row.y, 96, row.h }, 12.5f, keepIt ? T.textFaint : (a.n == 4 ? T.warn : T.accent), TW_MED, AL_RIGHT);
            if (c) g_eng.setKeepService(a.a, !keepIt);
        }
        y += 54;
    }
    y += 8;
    bool cur = t.state == view;
    if (ui.btnFilled(HID("psApply", ti), { x, y, 260, 46 }, (cur ? L"Apply " : L"Use ") + t.opts[view].label + (cur ? L" again" : L""), "bolt", !t.busy && !g_eng.running))
        requestChoice(ti, view);
    y += 56;
    if (cur) { g.text(L"Applying again first restores the previous start types, then applies the preset with your changes.", x, y, 12, T.textFaint, TW_REG, cw, AL_LEFT, true); y += 36; }
    return y;
}
void drawDetailSheet() {
    int ti = app.detail; if (ti < 0) return;
    Tweak& t = g_cat.tweaks[ti]; const Theme& T = app.th;
    float a = ui.anim(HID("sheetIn", ti), 1.f, 16, 0.f);
    g.fill({ 0, 0, g.W, g.H }, withA(rgb(0x000000), 0.32f * a), 0);
    float w = std::min(560.f, std::max(420.f, g.W * 0.40f));
    Rect r{ g.W - w * a - 12 * a, 44, w, g.H - 56 };
    if (ui.in.pressed && !r.hit(ui.in.mx, ui.in.my) && ui.in.my > 40 && !app.confirmOpen) { app.detail = -1; return; }
    g.shadow(r, 28, 1.2f);
    g.fill(r, T.surfHi, 28);
    addNoDrag(r);
    if (ui.iconBtn(HID("detailClose"), r.r() - 34, r.y + 34, "close", 40)) { app.detail = -1; return; }

    Rect body{ r.x, r.y + 8, r.w, r.h - 16 };
    app.scDetail.begin(body);
    float x = r.x + 28, y = r.y + 28 - app.scDetail.off, cw = r.w - 56;
    bool on = tweakOn(t);
    iconBlob(x + 34, y + 34, 68, t.icon.c_str(), on ? T.accent : T.primaryC, on ? T.onAccent : T.onPrimaryC, shapeFor(t.id), ui.time * 0.2f);
    y += 84;
    g.text(t.name, x, y, 24, T.text, TW_BOLD, cw - 40, AL_LEFT, true); y += g.measure(t.name, 24, TW_BOLD, cw - 40, true).height + 6;
    std::wstring tabName = g_cat.tab(t.tab) ? g_cat.tab(t.tab)->title : L"";
    g.text(tabName, x, y, 13, T.textFaint, TW_MED); y += 28;
    tileChips(t, x - 2, y, x + cw); y += 40;
    float dh = g.measure(t.desc, 14, TW_REG, cw, true).height;
    g.text(t.desc, x, y, 14, T.textDim, TW_REG, cw, AL_LEFT, true); y += dh + 22;

    if (t.kind == TK_TOGGLE) {
        Rect row{ x - 8, y, cw + 16, 64 };
        surfaceCard(row, 20, T.surfHi);
        std::wstring head = t.state == 1 ? L"Applied" : L"Not applied";
        std::wstring hint = g_eng.journaled(t) ? L"Your previous values are saved, switch it off to restore them" : L"Switch it on to apply";
        g.text(head, row.x + 20, row.y + 13, 15, T.text, TW_BOLD);
        g.text(hint, row.x + 20, row.y + 35, 12, T.textFaint, TW_MED);
        if (ui.toggle(HID("dtoggle", ti), row.r() - 72, row.y + 16, switchOn(t), true, t.busy)) requestToggle(ti, t.state != 1);
        y += 80;
    } else if (t.kind == TK_CHOICE) {
        g.text(L"Choose", x, y, 13, T.textFaint, TW_BOLD); y += 26;
        for (size_t i = 0; i < t.opts.size(); i++) {
            if (t.opts[i].hidden) continue;
            Rect row{ x - 8, y, cw + 16, 52 };
            u64 rid = HID("dopt", ti * 64 + i);
            bool sel = t.state == (int)i;
            bool c = ui.behave(rid, row);
            g.fill(row, sel ? T.accentDim : T.surfHi, 18);
            ui.stateLayer(rid, row, 18, T.text); ui.drawRipples(rid, row, 18, T.text);
            g.ring(row.x + 30, row.y + 26, 9, sel ? T.accent : T.textDim, 2);
            if (sel) g.circle(row.x + 30, row.y + 26, 5, T.accent);
            g.text(t.opts[i].label, row.x + 52, row.y + 15, 14.5f, T.text, TW_MED, row.w - 120);
            if (t.opts[i].rec) { float bw; ui.badge(row.r() - 110, row.y + 15, L"Recommended", withA(T.accent, 0.15f), T.accent, &bw); }
            if (c && !sel && !t.busy) requestChoice(ti, (int)i);
            y += 58;
        }
        if (g_eng.journaled(t)) { if (ui.btnOutlined(HID("dundo", ti), { x, y + 4, 200, 42 }, L"Restore previous", "undo", !t.busy)) requestToggle(ti, false); y += 58; }
        y += 8;
        if (t.id == "win.svcpreset") { y = presetEditor(t, ti, x, y, cw, r); app.scDetail.end(body, y + 30 + app.scDetail.off - body.y); return; }
    } else if (t.kind == TK_PICK) {
        static std::set<int> sel; static int selFor = -1;
        if (selFor != ti) { sel.clear(); for (size_t i = 0; i < t.opts.size(); i++) if (t.opts[i].rec && !t.picked[i]) sel.insert((int)i); selFor = ti; }
        g.text(L"Pick what to remove", x, y, 13, T.textFaint, TW_BOLD); y += 26;
        for (size_t i = 0; i < t.opts.size(); i++) {
            Rect row{ x - 8, y, cw + 16, 50 };
            u64 rid = HID("dpick", ti * 64 + i);
            bool done = t.picked[i] != 0, s = sel.count((int)i) != 0;
            bool c = !done && ui.behave(rid, row);
            g.fill(row, s ? T.accentDim : T.surfHi, 16);
            if (!done) { ui.stateLayer(rid, row, 16, T.text); ui.drawRipples(rid, row, 16, T.text); }
            if (done) g.icon("check_circle", row.x + 30, row.y + 25, 20, T.good);
            else { if (ui.checkbox(rid ^ 9, row.x + 21, row.y + 16, s)) c = true; }
            g.text(t.opts[i].label, row.x + 52, row.y + 15, 14, done ? T.textFaint : T.text, TW_MED, row.w - 150);
            if (done) g.textR(L"Removed", { row.r() - 120, row.y, 104, row.h }, 12.5f, T.good, TW_MED, AL_RIGHT);
            else if (t.opts[i].rec) { float bw; ui.badge(row.r() - 110, row.y + 14, L"Recommended", withA(T.accent, 0.15f), T.accent, &bw); }
            if (c) { if (s) sel.erase((int)i); else sel.insert((int)i); }
            y += 56;
        }
        if (ui.btnFilled(HID("dpickgo", ti), { x, y + 6, 220, 46 }, sel.empty() ? L"Select apps" : L"Remove " + std::to_wstring(sel.size()) + L" group" + (sel.size() == 1 ? L"" : L"s"), "delete_sweep", !sel.empty() && !t.busy))
            requestPick(ti, std::vector<int>(sel.begin(), sel.end()));
        y += 72;
    } else if (!t.opts.empty()) {
        if (s_actFor != ti) { s_actFor = ti; s_actOpt = 0; for (size_t i = 0; i < t.opts.size(); i++) if (t.opts[i].rec) s_actOpt = (int)i; }
        int& pickOpt = s_actOpt;
        g.text(L"Choose", x, y, 13, T.textFaint, TW_BOLD); y += 26;
        for (size_t i = 0; i < t.opts.size(); i++) {
            if (t.opts[i].hidden) continue;
            Rect row{ x - 8, y, cw + 16, 52 };
            u64 rid = HID("dact", ti * 64 + i);
            bool sel = pickOpt == (int)i;
            if (ui.behave(rid, row)) pickOpt = (int)i;
            g.fill(row, sel ? T.accentDim : T.surfHi, 18);
            ui.stateLayer(rid, row, 18, T.text); ui.drawRipples(rid, row, 18, T.text);
            g.ring(row.x + 30, row.y + 26, 9, sel ? T.accent : T.textDim, 2);
            if (sel) g.circle(row.x + 30, row.y + 26, 5, T.accent);
            g.textR(t.opts[i].label, { row.x + 52, row.y, row.w - 120, row.h }, 14.5f, T.text, TW_MED);
            y += 58;
        }
        if (ui.btnFilled(HID("drun", ti), { x, y + 4, 240, 46 }, (t.id == "apps.browser" ? L"Install " : L"Run: ") + t.opts[pickOpt].label, "download", !t.busy && !g_eng.running)) requestRun(ti, pickOpt);
        y += 70;
    } else {
        if (ui.btnFilled(HID("drun", ti), { x, y, 180, 46 }, L"Run now", "play_arrow", !t.busy)) requestRun(ti);
        y += 66;
    }

    g.text(L"What this changes", x, y, 16, T.text, TW_BOLD); y += 32;
    std::vector<const Act*> acts;
    if (t.kind == TK_CHOICE) { int o = t.state >= 0 ? t.state : 0; for (size_t i = 0; i < t.opts.size(); i++) if (!t.opts[i].hidden && (int)i == o) for (auto& a : t.opts[i].acts) acts.push_back(&a); }
    else if (t.kind == TK_PICK) { for (auto& o : t.opts) for (auto& a : o.acts) acts.push_back(&a); }
    else { for (auto& a : t.on) acts.push_back(&a); if (t.kind == TK_ACTION && s_actFor == ti && s_actOpt < (int)t.opts.size()) for (auto& a : t.opts[s_actOpt].acts) acts.push_back(&a); }
    if (t.kind == TK_CHOICE) { g.text(L"Showing: " + choiceLabel(t), x, y - 4, 12, T.textFaint, TW_MED); y += 18; }
    int shownN = 0;
    for (auto* a : acts) {
        if (shownN >= 220) { g.text(L"… and " + std::to_wstring(acts.size() - shownN) + L" more", x, y, 12.5f, T.textFaint, TW_MED); y += 24; break; }
        std::wstring d = describeAct(*a); if (d.empty()) continue;
        float hh = g.measure(d, 12.5f, TW_REG, cw - 34, true).height;
        if (y + hh > r.y - 40 && y < r.b() + 40) {
            g.icon(verbIcon(a->v), x + 9, y + 9, 15, T.accent);
            g.text(d, x + 28, y, 12.5f, T.textDim, TW_REG, cw - 34, AL_LEFT, true);
        }
        y += hh + 8; shownN++;
    }
    if (t.hasOff && t.kind != TK_ACTION) {
        y += 10; g.text(L"Switching it off runs", x, y, 13, T.textFaint, TW_BOLD); y += 24;
        for (auto& a : t.off) { std::wstring d = describeAct(a); float hh = g.measure(d, 12.5f, TW_REG, cw - 34, true).height; g.icon("undo", x + 9, y + 9, 15, T.textFaint); g.text(d, x + 28, y, 12.5f, T.textDim, TW_REG, cw - 34, AL_LEFT, true); y += hh + 8; }
    }
    y += 30;
    app.scDetail.end(body, y + app.scDetail.off - body.y);
}

static std::wstring planLine(const JobItem& it) {
    const Tweak& t = g_cat.tweaks[it.tweak];
    switch (it.op) {
    case J_CHOOSE: return L"Set to " + (it.opt >= 0 && it.opt < (int)t.opts.size() ? t.opts[it.opt].label : L"?");
    case J_PICK: { std::wstring s = L"Remove: "; for (size_t k = 0; k < it.items.size(); k++) { if (k) s += L", "; s += t.opts[it.items[k]].label; } return s; }
    case J_RUN: return L"Run once";
    default: return L"Apply";
    }
}
void drawPlanSheet() {
    const Theme& T = app.th;
    float a = ui.anim(HID("planIn"), 1.f, 16, 0.f);
    g.fill({ 0, 0, g.W, g.H }, withA(rgb(0x000000), 0.32f * a), 0);
    float w = std::min(600.f, std::max(440.f, g.W * 0.42f));
    Rect r{ g.W - w * a - 12 * a, 44, w, g.H - 56 };
    if (ui.in.pressed && !r.hit(ui.in.mx, ui.in.my) && ui.in.my > 40 && !app.confirmOpen) { app.planOpen = false; return; }
    g.shadow(r, 28, 1.2f);
    g.fill(r, T.surfHi, 28);
    addNoDrag(r);
    if (ui.iconBtn(HID("planClose"), r.r() - 34, r.y + 34, "close", 40)) { app.planOpen = false; return; }
    float x = r.x + 26, cw = r.w - 52;
    int nSel = 0; for (auto s : app.planSel) nSel += s;
    iconBlob(x + 26, r.y + 42, 52, "auto_awesome", T.primaryC, T.onPrimaryC, 3, ui.time * 0.2f);
    g.textR(app.planTitle, { x + 66, r.y + 20, cw - 120, 28 }, 19, T.text, TW_BOLD);
    g.textR(std::to_wstring(nSel) + L" of " + std::to_wstring(app.planItems.size()) + L" selected. Untick anything you want to skip.", { x + 66, r.y + 48, cw - 110, 20 }, 12.5f, T.textFaint, TW_MED);

    Rect body{ r.x, r.y + 86, r.w, r.h - 86 - 86 };
    app.scSheet.begin(body);
    float y = body.y + 4 - app.scSheet.off;
    std::string lastTab;
    for (size_t i = 0; i < app.planItems.size(); i++) {
        const JobItem& it = app.planItems[i];
        const Tweak& t = g_cat.tweaks[it.tweak];
        if (t.tab != lastTab) {
            lastTab = t.tab;
            const Tab* tb = g_cat.tab(t.tab);
            y += 8;
            if (tb && y > body.y - 40 && y < body.b()) { g.icon(tb->icon.c_str(), x + 10, y + 12, 18, T.accent); g.textR(tb->title, { x + 28, y, cw - 30, 24 }, 13.5f, T.textDim, TW_BOLD); }
            y += 30;
        }
        Rect row{ x - 6, y, cw + 12, 58 };
        if (row.b() > body.y - 10 && row.y < body.b() + 10) {
            u64 rid = HID("planRow", i);
            bool on = app.planSel[i] != 0;
            bool c = ui.behave(rid, row);
            g.fill(row, on ? withA(T.accent, 0.10f) : T.surf2, 16);
            ui.stateLayer(rid, row, 16, T.text); ui.drawRipples(rid, row, 16, T.text);
            if (ui.checkbox(rid ^ 7, row.x + 14, row.y + 19, on)) c = true;
            g.icon(t.icon.c_str(), row.x + 60, row.y + 29, 20, on ? T.accent : T.textFaint);
            float tw = row.w - 96 - (t.risk >= 2 ? 90 : 0);
            g.textR(t.name, { row.x + 80, row.y + 8, tw, 22 }, 14, on ? T.text : T.textDim, TW_MED);
            g.textR(planLine(it), { row.x + 80, row.y + 30, tw, 20 }, 12, T.textFaint, TW_REG);
            if (t.risk >= 2) { float bw; ui.badge(row.r() - 92, row.y + 18, t.risk >= 3 ? L"Advanced" : L"Caution", withA(t.risk >= 3 ? T.danger : T.warn, 0.14f), t.risk >= 3 ? T.danger : T.warn, &bw); }
            if (c) app.planSel[i] = on ? 0 : 1;
        }
        y += 64;
    }
    y += 10;
    app.scSheet.end(body, y + app.scSheet.off - body.y);

    Rect ft{ r.x, r.b() - 86, r.w, 86 };
    g.line(ft.x + 20, ft.y, ft.r() - 20, ft.y, withA(T.text, 0.08f), 1);
    bool all = nSel == (int)app.planItems.size();
    if (ui.btnText(HID("planAll"), { x - 6, ft.y + 22, 150, 42 }, all ? L"Select none" : L"Select all", all ? "deselect" : "select_all"))
        for (auto& s : app.planSel) s = all ? 0 : 1;
    if (ui.btnFilled(HID("planGo"), { ft.r() - 26 - 200, ft.y + 20, 200, 46 }, nSel ? L"Apply " + std::to_wstring(nSel) : L"Nothing selected", "bolt", nSel > 0 && !g_eng.running)) {
        std::vector<JobItem> sel;
        for (size_t i = 0; i < app.planItems.size(); i++) if (app.planSel[i]) sel.push_back(app.planItems[i]);
        std::wstring title = app.planTitle;
        app.planOpen = false;
        applyMany(title, sel);
    }
}
