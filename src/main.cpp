#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <commoncontrols.h>
#include <stdio.h>
#include <vector>
#include <algorithm>
#include <utility>
#include "resource.h"
#include "registry.h"
#include "nsmodel.h"
#include "add_dialog.h"
#include "hide_dialog.h"
#include "loc.h"
#pragma comment(lib, "comctl32.lib")

// Заголовок окна с версией и меткой времени сборки (меняется каждую сборку — видно, что версия свежая).
#define SNM_WIDE2(s) L##s
#define SNM_WIDE(s)  SNM_WIDE2(s)
#define SNM_TITLE    L"Shell Namespace Manager   v1.0   (сборка " SNM_WIDE(__DATE__) L" " SNM_WIDE(__TIME__) L")"

// Логический узел: объединяет 64- и 32-битную записи одного GUID в одной ветке.
struct NsNode {
    std::wstring guid, marker, target, displayName, description;
    NsLocation   location = NsLocation::MyComputer;
    NsHive       hive     = NsHive::HKLM;
    bool         in64 = false, in32 = false, hasSort = false, hasNavPin = false;
    DWORD        sortOrder = 0;
    DWORD        navPin = 0;
    int          icon = -1;
};

static HWND        g_tree    = nullptr;
static HWND        g_details = nullptr;
static HWND        g_status  = nullptr;
static HWND        g_tip     = nullptr;
static HFONT       g_font    = nullptr;
static IImageList* g_iml     = nullptr;
static std::vector<NsEntry> g_entries;
static std::vector<NsNode>  g_nodes;

static const int kTbY = 8, kTbH = 28, kViewTop = kTbY + kTbH + 8, kMargin = 8, kDetailsH = 74;

// Компактный тулбар — только частые действия (текст/тултип = русский ключ, переводится через T()).
struct BtnDef { int id; const wchar_t* text; int w; const wchar_t* tip; };
static const BtnDef kButtons[] = {
    { IDC_BTN_REFRESH, L"Обновить",       90,  L"Перечитать список узлов из реестра" },
    { IDC_BTN_ADD,     L"Добавить свою…", 130, L"Добавить свою папку в «Этот компьютер» или дерево (со своей иконкой)" },
    { IDC_BTN_DELETE,  L"Удалить",        90,  L"Удалить выбранный узел из namespace (перед удалением — .reg-бэкап)" },
    { IDC_BTN_UP,      L"Вверх",          70,  L"Поднять выбранный узел выше (меняет SortOrderIndex)" },
    { IDC_BTN_DOWN,    L"Вниз",           70,  L"Опустить выбранный узел ниже (меняет SortOrderIndex)" },
};

static void SetStatus(const wchar_t* s) { SendMessageW(g_status, SB_SETTEXTW, 0, (LPARAM)s); }

static void LayoutStatusParts() {
    if (!g_status) return;
    RECT rc{}; GetClientRect(g_status, &rc);
    int w = rc.right - rc.left;
    int copyW = 190;
    int parts[2] = { (w > copyW ? w - copyW : 0), -1 };
    SendMessageW(g_status, SB_SETPARTS, 2, (LPARAM)parts);
    SendMessageW(g_status, SB_SETTEXTW, 1, (LPARAM)L"© Evgenii Shapovalov 2026");
}

static HMENU BuildMenu() {
    HMENU bar = CreateMenu();

    HMENU node = CreatePopupMenu();
    AppendMenuW(node, MF_STRING, IDC_BTN_ADD,     T(L"Добавить свою папку…"));
    AppendMenuW(node, MF_STRING, IDC_BTN_DELETE,  T(L"Удалить узел"));
    AppendMenuW(node, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(node, MF_STRING, IDC_BTN_UP,      T(L"Переместить вверх"));
    AppendMenuW(node, MF_STRING, IDC_BTN_DOWN,    T(L"Переместить вниз"));
    AppendMenuW(node, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(node, MF_STRING, IDC_BTN_HIDE,    T(L"Скрыть встроенный элемент…"));
    AppendMenuW(node, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(node, MF_STRING, IDC_BTN_REFRESH, T(L"Обновить список"));
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)node, T(L"Узел"));

    HMENU view = CreatePopupMenu();
    AppendMenuW(view, MF_STRING, IDC_BTN_ALLFOLDERS,  T(L"Полный вид папок"));
    AppendMenuW(view, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(view, MF_STRING, IDC_BTN_QUICKACCESS, T(L"Показывать «Главная»"));
    AppendMenuW(view, MF_STRING, IDC_BTN_CLEARQA,     T(L"Показывать «Быстрый доступ»"));
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)view, T(L"Вид"));

    HMENU expl = CreatePopupMenu();
    AppendMenuW(expl, MF_STRING, IDC_BTN_RESTART, T(L"Перезапустить Проводник"));
    AppendMenuW(expl, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(expl, MF_STRING, IDM_RESETQA,   T(L"Очистить «Быстрый доступ»"));
    AppendMenuW(expl, MF_STRING, IDM_QA_DEFAULTS, T(L"Вернуть «Быстрый доступ» по умолчанию"));
    AppendMenuW(expl, MF_STRING, IDM_DISABLEQA, T(L"Выключить «Быстрый доступ» полностью"));
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)expl, T(L"Проводник"));

    HMENU lang = CreatePopupMenu();
    AppendMenuW(lang, MF_STRING, IDM_LANG_RU, T(L"Русский"));
    AppendMenuW(lang, MF_STRING, IDM_LANG_EN, T(L"English"));
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)lang, T(L"Язык"));

    HMENU help = CreatePopupMenu();
    AppendMenuW(help, MF_STRING, IDM_ABOUT, T(L"О программе"));
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)help, T(L"Справка"));

    return bar;
}

static void CreateTree(HWND hwnd) {
    g_tree = CreateWindowExW(WS_EX_CLIENTEDGE, WC_TREEVIEWW, L"",
        WS_CHILD|WS_VISIBLE|TVS_HASBUTTONS|TVS_HASLINES|TVS_LINESATROOT|TVS_SHOWSELALWAYS|TVS_INFOTIP,
        0,0,0,0, hwnd, (HMENU)(UINT_PTR)IDC_TREE, GetModuleHandleW(nullptr), nullptr);
    if (g_font) SendMessageW(g_tree, WM_SETFONT, (WPARAM)g_font, TRUE);
    if (SUCCEEDED(SHGetImageList(SHIL_SMALL, IID_PPV_ARGS(&g_iml))))
        TreeView_SetImageList(g_tree, (HIMAGELIST)g_iml, TVSIL_NORMAL);
}

static void CreateButtons(HWND hwnd) {
    int x = kMargin;
    for (const auto& b : kButtons) {
        HWND hb = CreateWindowExW(0, L"BUTTON", T(b.text),
            WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_PUSHBUTTON,
            x, kTbY, b.w, kTbH, hwnd, (HMENU)(UINT_PTR)b.id, GetModuleHandleW(nullptr), nullptr);
        if (g_font) SendMessageW(hb, WM_SETFONT, (WPARAM)g_font, TRUE);
        if (g_tip && b.tip) {
            TTTOOLINFOW ti{}; ti.cbSize = sizeof(ti);
            ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
            ti.hwnd = hwnd;
            ti.uId  = (UINT_PTR)hb;
            ti.lpszText = (LPWSTR)T(b.tip);
            SendMessageW(g_tip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
        }
        x += b.w + 6;
    }
}

// Объединить записи реестра в логические узлы (схлопнуть 64/32-бит дубли) + резолв.
static void BuildNodes() {
    g_entries = EnumNamespace();
    g_nodes.clear();
    for (const auto& e : g_entries) {
        NsNode* found = nullptr;
        for (auto& n : g_nodes)
            if (n.location==e.location && n.hive==e.hive && lstrcmpiW(n.guid.c_str(), e.guid.c_str())==0) { found=&n; break; }
        if (!found) {
            NsNode n; n.guid=e.guid; n.location=e.location; n.hive=e.hive; n.marker=e.marker;
            g_nodes.push_back(std::move(n));
            found = &g_nodes.back();
        }
        if (e.wow64) found->in32 = true; else found->in64 = true;
        if (e.hasSort && !found->hasSort) { found->hasSort=true; found->sortOrder=e.sortOrder; }
        if (e.hasNavPin && !found->hasNavPin) { found->hasNavPin=true; found->navPin=e.navPin; }
        if (found->target.empty() && !e.target.empty()) found->target=e.target;
        if (found->marker.empty() && !e.marker.empty()) found->marker=e.marker;
    }
    for (auto& n : g_nodes) {
        n.displayName = ResolveDisplayName(n.guid);
        if (n.displayName==n.guid && !n.marker.empty()) n.displayName=n.marker;
        n.description = ResolveInfoTip(n.guid);
        n.icon = ResolveSysIconIndex(n.guid);
    }
}

static HTREEITEM InsertTreeItem(HTREEITEM parent, const wchar_t* text, int image, LPARAM lp, bool bold) {
    TVINSERTSTRUCTW tis{};
    tis.hParent = parent;
    tis.hInsertAfter = TVI_LAST;
    tis.item.mask = TVIF_TEXT|TVIF_PARAM | (image>=0 ? (TVIF_IMAGE|TVIF_SELECTEDIMAGE) : 0u);
    tis.item.pszText = (LPWSTR)text;
    tis.item.lParam = lp;
    if (image>=0) { tis.item.iImage=image; tis.item.iSelectedImage=image; }
    if (bold) { tis.item.mask|=TVIF_STATE; tis.item.state=TVIS_BOLD; tis.item.stateMask=TVIS_BOLD; }
    return TreeView_InsertItem(g_tree, &tis);
}

static std::vector<int> SectionOrder(NsLocation loc) {
    std::vector<int> idxs;
    for (int i=0;i<(int)g_nodes.size();++i) if (g_nodes[i].location==loc) idxs.push_back(i);
    std::sort(idxs.begin(), idxs.end(), [](int a, int b){
        DWORD sa = g_nodes[a].hasSort ? g_nodes[a].sortOrder : 0x7FFFFFFFu;
        DWORD sb = g_nodes[b].hasSort ? g_nodes[b].sortOrder : 0x7FFFFFFFu;
        if (sa != sb) return sa < sb;
        return lstrcmpiW(g_nodes[a].displayName.c_str(), g_nodes[b].displayName.c_str()) < 0;
    });
    return idxs;
}

static bool IsVisibleTreeNode(int i) {
    if (i < 0 || i >= (int)g_nodes.size()) return false;
    if (g_nodes[i].location == NsLocation::Desktop && g_nodes[i].hive == NsHive::HKLM
        && (!g_nodes[i].hasNavPin || g_nodes[i].navPin == 0)) {
        return false;
    }
    return true;
}

static void AddChildren(HTREEITEM root, NsLocation loc) {
    std::vector<int> order = SectionOrder(loc);
    for (int i : order) {
        if (!IsVisibleTreeNode(i)) continue;
        std::wstring t = g_nodes[i].displayName;
        bool collide = false;
        for (int j : order)
            if (j != i && lstrcmpiW(g_nodes[j].displayName.c_str(), g_nodes[i].displayName.c_str()) == 0) { collide = true; break; }
        if (collide && !g_nodes[i].marker.empty() && g_nodes[i].marker != g_nodes[i].displayName)
            t += L"  · " + g_nodes[i].marker;
        if (g_nodes[i].hive == NsHive::HKCU) t += L"   (HKCU)";
        InsertTreeItem(root, t.c_str(), g_nodes[i].icon, (LPARAM)i, false);
    }
}

static void Populate() {
    BuildNodes();
    TreeView_DeleteAllItems(g_tree);
    HTREEITEM rPC   = InsertTreeItem(TVI_ROOT, T(L"Этот компьютер"),       -1, (LPARAM)(-1), true);
    HTREEITEM rTree = InsertTreeItem(TVI_ROOT, T(L"Дерево (рабочий стол)"), -1, (LPARAM)(-1), true);
    AddChildren(rPC,   NsLocation::MyComputer);
    AddChildren(rTree, NsLocation::Desktop);
    SetWindowTextW(g_details, L"");
    int visible = 0;
    for (int i = 0; i < (int)g_nodes.size(); ++i) if (IsVisibleTreeNode(i)) ++visible;
    wchar_t st[160]; swprintf_s(st, T(L"Узлов: %d  (записей в реестре: %d)"), visible, (int)g_entries.size());
    SetStatus(st);
}

static void ShowDetails(int idx) {
    if (idx < 0 || idx >= (int)g_nodes.size()) { SetWindowTextW(g_details, L""); return; }
    const NsNode& n = g_nodes[idx];
    std::wstring views;
    if (n.hive==NsHive::HKCU) views = L"HKCU";
    else { views = L"HKLM "; views += (n.in64&&n.in32) ? T(L"(64+32-бит)") : (n.in64 ? T(L"(64-бит)") : T(L"(32-бит)")); }
    std::wstring t = n.displayName + L"   ·   " + views + L"\r\n";
    t += n.guid;
    if (n.hasSort) { wchar_t b[32]; swprintf_s(b, L"%lu", n.sortOrder); t += L"    Sort: "; t += b; }
    else            t += L"    Sort: —";
    t += L"    Target: " + (n.target.empty() ? std::wstring(L"—") : n.target);
    if (!n.marker.empty() && n.marker != n.displayName) t += std::wstring(L"\r\n") + T(L"Маркер: ") + n.marker;
    if (!n.description.empty())                          t += std::wstring(L"\r\n") + T(L"Описание: ") + n.description;
    SetWindowTextW(g_details, t.c_str());
}

static void RunTaskkill() {
    wchar_t dir[MAX_PATH] = L"";
    UINT n = GetSystemDirectoryW(dir, MAX_PATH);
    std::wstring p = (n > 0 && n < MAX_PATH) ? std::wstring(dir) : std::wstring(L"C:\\Windows\\System32");
    if (!p.empty() && p.back() != L'\\') p += L'\\';
    std::wstring cmd = L"\"" + p + L"taskkill.exe\" /f /im explorer.exe";

    std::vector<wchar_t> b(cmd.begin(), cmd.end()); b.push_back(L'\0');
    STARTUPINFOW si{}; si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    // Рабочий каталог дочернего процесса — System32, а не (возможно, доступный на запись обычному
    // пользователю) каталог программы: иначе CWD дочернего taskkill.exe был бы вектором DLL planting.
    if (CreateProcessW(nullptr, b.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, p.c_str(), &si, &pi)) {
        if (WaitForSingleObject(pi.hProcess, 5000) == WAIT_TIMEOUT) {
            // завис — принудительно завершаем, иначе handle утечёт и процесс-зомби останется
            TerminateProcess(pi.hProcess, 1);
            WaitForSingleObject(pi.hProcess, 1000);
        }
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    }
}

static void LaunchExplorer(bool withMyComputer) {
    wchar_t dir[MAX_PATH] = L"";
    UINT n = GetWindowsDirectoryW(dir, MAX_PATH);
    std::wstring p = (n > 0 && n < MAX_PATH) ? std::wstring(dir) : std::wstring(L"C:\\Windows");
    if (!p.empty() && p.back() != L'\\') p += L'\\';
    std::wstring expl = p + L"explorer.exe";

    if (!withMyComputer) {
        ShellExecuteW(nullptr, nullptr, expl.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    } else {
        ShellExecuteW(nullptr, L"open", expl.c_str(), L"shell:MyComputerFolder", nullptr, SW_SHOWNORMAL);
    }
}

static void RestartExplorer() {
    RunTaskkill();
    LaunchExplorer(false);
    Sleep(1200);
    LaunchExplorer(true);
}

static void SelectNodeByGuid(NsLocation loc, const std::wstring& guid) {
    int want = -1;
    for (int i=0;i<(int)g_nodes.size();++i)
        if (g_nodes[i].location==loc && lstrcmpiW(g_nodes[i].guid.c_str(), guid.c_str())==0) { want=i; break; }
    if (want < 0) return;
    for (HTREEITEM root = TreeView_GetRoot(g_tree); root; root = TreeView_GetNextSibling(g_tree, root))
        for (HTREEITEM ch = TreeView_GetChild(g_tree, root); ch; ch = TreeView_GetNextSibling(g_tree, ch)) {
            TVITEMW ti{}; ti.mask=TVIF_PARAM; ti.hItem=ch; TreeView_GetItem(g_tree, &ti);
            if ((int)ti.lParam == want) { TreeView_SelectItem(g_tree, ch); TreeView_EnsureVisible(g_tree, ch); return; }
        }
}

static void MoveSelected(int dir) {
    HTREEITEM sel = TreeView_GetSelection(g_tree);
    if (!sel) { SetStatus(T(L"Выберите узел")); return; }
    TVITEMW ti{}; ti.mask=TVIF_PARAM; ti.hItem=sel; TreeView_GetItem(g_tree, &ti);
    int idx = (int)ti.lParam;
    if (idx < 0 || idx >= (int)g_nodes.size()) { SetStatus(T(L"Выберите конкретный узел (не заголовок)")); return; }
    NsLocation loc = g_nodes[idx].location;
    std::wstring guid = g_nodes[idx].guid;
    std::vector<int> order = SectionOrder(loc);
    int pos = -1;
    for (int k=0;k<(int)order.size();++k) if (order[k]==idx) { pos=k; break; }
    int np = pos + dir;
    if (pos < 0 || np < 0 || np >= (int)order.size()) { SetStatus(T(L"Узел уже с краю")); return; }
    std::swap(order[pos], order[np]);
    std::vector<SortItem> items;
    for (int ni : order) items.push_back({ g_nodes[ni].hive, g_nodes[ni].guid });
    std::wstring backup, err;
    if (SetSortOrder(items, backup, err)) {
        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
        Populate();
        SelectNodeByGuid(loc, guid);
        if (!err.empty()) MessageBoxW(g_tree, err.c_str(), T(L"Внимание: права ключа не восстановлены"), MB_OK|MB_ICONWARNING);
        SetStatus(T(L"Порядок изменён. Нажмите «Перезапустить Проводник», чтобы увидеть результат в дереве."));
    } else {
        MessageBoxW(g_tree, err.c_str(), T(L"Не удалось изменить порядок"), MB_OK|MB_ICONWARNING);
    }
}

static HRESULT CALLBACK AboutCallback(HWND, UINT msg, WPARAM, LPARAM l, LONG_PTR) {
    if (msg == TDN_HYPERLINK_CLICKED) {
        wchar_t dir[MAX_PATH] = L"";
        UINT n = GetWindowsDirectoryW(dir, MAX_PATH);
        std::wstring p = (n > 0 && n < MAX_PATH) ? std::wstring(dir) : std::wstring(L"C:\\Windows");
        if (!p.empty() && p.back() != L'\\') p += L'\\';
        std::wstring expl = p + L"explorer.exe";
        ShellExecuteW(nullptr, L"open", expl.c_str(), (LPCWSTR)l, nullptr, SW_SHOWNORMAL);
    }
    return S_OK;
}

static void RelocalizeUI(HWND h) {
    HMENU old = GetMenu(h);
    SetMenu(h, BuildMenu());
    if (old) DestroyMenu(old);
    DrawMenuBar(h);
    for (const auto& b : kButtons) {
        HWND hb = GetDlgItem(h, b.id);
        if (hb) SetWindowTextW(hb, T(b.text));
        if (g_tip && hb && b.tip) {
            TTTOOLINFOW ti{}; ti.cbSize = sizeof(ti);
            ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
            ti.hwnd = h;
            ti.uId  = (UINT_PTR)hb;
            ti.lpszText = (LPWSTR)T(b.tip);
            SendMessageW(g_tip, TTM_UPDATETIPTEXTW, 0, (LPARAM)&ti);
        }
    }
    Populate();
}

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE: {
        NONCLIENTMETRICSW ncm{ sizeof(ncm) };
        if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0))
            g_font = CreateFontIndirectW(&ncm.lfMessageFont);
        SetMenu(h, BuildMenu());
        CreateTree(h);
        g_details = CreateWindowExW(0, L"EDIT", L"",
            WS_CHILD|WS_VISIBLE|WS_BORDER|ES_MULTILINE|ES_READONLY, 0,0,0,0, h,
            (HMENU)(UINT_PTR)IDC_DETAILS, GetModuleHandleW(nullptr), nullptr);
        if (g_font) SendMessageW(g_details, WM_SETFONT, (WPARAM)g_font, TRUE);
        g_tip = CreateWindowExW(0, TOOLTIPS_CLASSW, nullptr,
            WS_POPUP|TTS_ALWAYSTIP|TTS_NOPREFIX, 0,0,0,0, h, nullptr, GetModuleHandleW(nullptr), nullptr);
        SendMessageW(g_tip, TTM_SETMAXTIPWIDTH, 0, 380);
        CreateButtons(h);
        g_status = CreateWindowExW(0, STATUSCLASSNAMEW, nullptr,
            WS_CHILD|WS_VISIBLE|SBARS_SIZEGRIP, 0,0,0,0, h,
            (HMENU)(UINT_PTR)IDC_STATUS, GetModuleHandleW(nullptr), nullptr);
        if (g_font) SendMessageW(g_status, WM_SETFONT, (WPARAM)g_font, TRUE);
        LayoutStatusParts();
        Populate();
        return 0;
    }
    case WM_SIZE:
        if (g_tree && g_status && g_details) {
            SendMessageW(g_status, WM_SIZE, 0, 0);
            LayoutStatusParts();
            RECT rs{}; GetWindowRect(g_status, &rs);
            int sh = (int)(rs.bottom - rs.top);
            int W = LOWORD(l), H = HIWORD(l);
            int cw = W - 2*kMargin; if (cw < 0) cw = 0;
            int detY  = H - sh - kDetailsH;
            int treeH = detY - kViewTop - 4; if (treeH < 0) treeH = 0;
            MoveWindow(g_tree,    kMargin, kViewTop, cw, treeH, TRUE);
            MoveWindow(g_details, kMargin, detY,     cw, kDetailsH, TRUE);
        }
        return 0;
    case WM_INITMENUPOPUP: {
        HMENU mp = (HMENU)w;
        CheckMenuItem(mp, IDC_BTN_ALLFOLDERS,  MF_BYCOMMAND | (GetNavAllFolders()     ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuItem(mp, IDC_BTN_QUICKACCESS, MF_BYCOMMAND | (GetHomeHidden()        ? MF_UNCHECKED : MF_CHECKED));
        CheckMenuItem(mp, IDC_BTN_CLEARQA,     MF_BYCOMMAND | (GetQuickAccessHidden() ? MF_UNCHECKED : MF_CHECKED));
        CheckMenuItem(mp, IDM_DISABLEQA,       MF_BYCOMMAND | (GetQuickAccessDisabled() ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuRadioItem(mp, IDM_LANG_RU, IDM_LANG_EN, (LocGetLang()==0 ? IDM_LANG_RU : IDM_LANG_EN), MF_BYCOMMAND);
        return 0;
    }
    case WM_NOTIFY: {
        LPNMHDR nh = (LPNMHDR)l;
        if (nh->idFrom == (UINT_PTR)IDC_TREE) {
            if (nh->code == TVN_SELCHANGEDW) {
                LPNMTREEVIEWW tv = (LPNMTREEVIEWW)l;
                ShowDetails((int)tv->itemNew.lParam);
            } else if (nh->code == TVN_GETINFOTIPW) {
                NMTVGETINFOTIPW* it = (NMTVGETINFOTIPW*)l;
                int i = (int)it->lParam;
                if (i >= 0 && i < (int)g_nodes.size() && it->pszText && it->cchTextMax > 0) {
                    const NsNode& n = g_nodes[i];
                    std::wstring tip = !n.description.empty() ? n.description
                                     : (n.marker != n.displayName ? n.marker : std::wstring());
                    if (!tip.empty()) lstrcpynW(it->pszText, tip.c_str(), it->cchTextMax);
                }
            }
        }
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(w)) {
        case IDC_BTN_REFRESH:
            Populate();
            break;
        case IDC_BTN_ADD: {
            std::wstring guid, backup;
            if (ShowAddDialog(h, guid, backup)) {
                SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
                Populate();
                std::wstring s = T(L"Добавлено: ") + guid + T(L"   ·   бэкап: ") + backup;
                SetStatus(s.c_str());
            }
            break;
        }
        case IDC_BTN_HIDE: {
            std::wstring name;
            if (ShowHideDialog(h, name)) {
                SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
                Populate();
                std::wstring s = T(L"Скрыто: ") + name + T(L"  — нажмите «Перезапустить Проводник»");
                SetStatus(s.c_str());
            }
            break;
        }
        case IDC_BTN_DELETE: {
            HTREEITEM sel = TreeView_GetSelection(g_tree);
            if (!sel) { SetStatus(T(L"Выберите узел")); break; }
            TVITEMW ti{}; ti.mask = TVIF_PARAM; ti.hItem = sel; TreeView_GetItem(g_tree, &ti);
            int idx = (int)ti.lParam;
            if (idx < 0 || idx >= (int)g_nodes.size()) { SetStatus(T(L"Выберите конкретный узел (не заголовок)")); break; }
            const NsNode& n = g_nodes[idx];
            std::wstring msg = T(L"Удалить «") + n.displayName + T(L"» из namespace?\n\nGUID: ") + n.guid +
                               T(L"\nКуст: ") + (n.hive==NsHive::HKCU ? std::wstring(L"HKCU") : std::wstring(L"HKLM")) +
                               T(L"\n\nПеред удалением будет сохранён .reg-бэкап — операция обратима.");
            if (MessageBoxW(h, msg.c_str(), T(L"Удаление узла"), MB_YESNO|MB_ICONWARNING) == IDYES) {
                std::wstring backup, err;
                if (DeleteEntry(n.hive, n.location, n.guid, backup, err)) {
                    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
                    std::wstring s = T(L"Удалено. Бэкап: ") + backup;
                    Populate();
                    SetStatus(s.c_str());
                } else {
                    MessageBoxW(h, err.c_str(), T(L"Не удалось удалить"), MB_OK|MB_ICONWARNING);
                }
            }
            break;
        }
        case IDC_BTN_ALLFOLDERS: {
            bool now = !GetNavAllFolders();
            if (SetNavAllFolders(now)) {
                SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
                SetStatus(now ? T(L"Полный вид папок — перезапустите Проводник")
                              : T(L"Упрощённый вид папок — перезапустите Проводник"));
            } else {
                SetStatus(T(L"Не удалось: бэкап не создан"));
            }
            break;
        }
        case IDC_BTN_QUICKACCESS: {
            bool hide = !GetHomeHidden();
            std::wstring b, e;
            if (SetHomeHidden(hide, b, e)) {
                std::wstring b2, e2;
                SetNavTreeHidden(L"{f874310e-b6b7-47dc-bc84-b9e6b38f5903}", hide, b2, e2);
                SetHubMode(hide);
                SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
                if (!e2.empty()) MessageBoxW(h, e2.c_str(), T(L"Внимание: права ключа не восстановлены"), MB_OK|MB_ICONWARNING);
                SetStatus(hide ? T(L"«Главная» скрыта — перезапустите Проводник")
                               : T(L"«Главная» восстановлена — перезапустите Проводник"));
            } else {
                MessageBoxW(h, e.c_str(), T(L"Не удалось"), MB_OK|MB_ICONWARNING);
            }
            break;
        }
        case IDC_BTN_CLEARQA: {
            bool hide = !GetQuickAccessHidden();
            std::wstring b, e;
            if (SetQuickAccessHidden(hide, b, e)) {
                RestartExplorer();
                if (!e.empty()) MessageBoxW(h, e.c_str(), T(L"Внимание: права ключа не восстановлены"), MB_OK|MB_ICONWARNING);
                SetStatus(hide ? T(L"Быстрый доступ скрыт — Проводник перезапущен")
                               : T(L"Быстрый доступ показан — Проводник перезапущен"));
            } else {
                MessageBoxW(h, e.c_str(), T(L"Не удалось"), MB_OK|MB_ICONWARNING);
            }
            break;
        }
        case IDC_BTN_RESTART:
            if (MessageBoxW(h, T(L"Перезапустить Проводник? Панель задач на мгновение исчезнет и вернётся."),
                            T(L"Подтверждение"), MB_YESNO|MB_ICONQUESTION) == IDYES) {
                RestartExplorer();
                SetStatus(T(L"Проводник перезапущен"));
            }
            break;
        case IDM_RESETQA:
            if (MessageBoxW(h, T(L"Открепить все папки из «Быстрого доступа»?\n\nПроводник будет перезапущен: панель задач на мгновение исчезнет и вернётся. Закреплённые папки можно закрепить заново. Частые папки появляются заново по мере работы; чтобы выключить совсем — «Выключить «Быстрый доступ» полностью»."),
                            T(L"Очистить Быстрый доступ"), MB_YESNO|MB_ICONQUESTION) == IDYES) {
                std::wstring backup, err;
                RunTaskkill();
                if (ClearQuickAccess(backup, err)) {
                    LaunchExplorer(false);
                    Sleep(1200);
                    LaunchExplorer(true);
                    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
                    std::wstring s = T(L"Быстрый доступ очищен. Бэкап: ") + backup;
                    SetStatus(s.c_str());
                } else {
                    LaunchExplorer(false);
                    Sleep(1200);
                    LaunchExplorer(true);
                    MessageBoxW(h, err.c_str(), T(L"Не удалось"), MB_OK|MB_ICONWARNING);
                }
            }
            break;
        case IDM_QA_DEFAULTS:
            if (MessageBoxW(h, T(L"Вернуть стандартные папки «Быстрого доступа»?\n\nПроводник будет перезапущен, а файл Quick Access будет удалён. Windows заново создаст стандартный список: Desktop, Downloads, Документы, Изображения, Музыка, Видео."),
                            T(L"Быстрый доступ по умолчанию"), MB_YESNO|MB_ICONQUESTION) == IDYES) {
                std::wstring backup, err;
                RunTaskkill();
                if (RestoreQuickAccessDefaults(backup, err)) {
                    LaunchExplorer(false);
                    Sleep(1200);
                    LaunchExplorer(true);
                    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
                    std::wstring s = T(L"Быстрый доступ возвращён по умолчанию. Бэкап: ") + backup;
                    SetStatus(s.c_str());
                } else {
                    LaunchExplorer(false);
                    Sleep(1200);
                    LaunchExplorer(true);
                    MessageBoxW(h, err.c_str(), T(L"Не удалось"), MB_OK|MB_ICONWARNING);
                }
            }
            break;
        case IDM_DISABLEQA: {
            bool disable = !GetQuickAccessDisabled();
            const wchar_t* confirm = disable
                ? T(L"Выключить «Быстрый доступ» полностью?\n\nПроводник будет открывать «Этот компьютер», недавние и частые отключатся, а узел «Быстрый доступ» скроется из дерева. Обратимо: сохраняется бэкап .reg.")
                : T(L"Включить «Быстрый доступ» обратно?\n\nПроводник снова будет показывать «Быстрый доступ» и собирать историю.");
            if (MessageBoxW(h, confirm, T(L"Выключение Быстрого доступа"), MB_YESNO|MB_ICONQUESTION) == IDYES) {
                std::wstring backup, err;
                if (SetQuickAccessDisabled(disable, backup, err)) {
                    RestartExplorer();
                    if (!err.empty()) MessageBoxW(h, err.c_str(), T(L"Внимание: права ключа не восстановлены"), MB_OK|MB_ICONWARNING);
                    std::wstring s = (disable ? T(L"Быстрый доступ выключен — Проводник перезапущен. Бэкап: ")
                                              : T(L"Быстрый доступ включён — Проводник перезапущен. Бэкап: ")) + backup;
                    SetStatus(s.c_str());
                } else {
                    MessageBoxW(h, err.c_str(), T(L"Не удалось"), MB_OK|MB_ICONWARNING);
                }
            }
            break;
        }
        case IDC_BTN_UP:
            MoveSelected(-1);
            break;
        case IDC_BTN_DOWN:
            MoveSelected(+1);
            break;
        case IDM_LANG_RU:
            if (LocGetLang() != 0) { LocSetLang(0); RelocalizeUI(h); }
            break;
        case IDM_LANG_EN:
            if (LocGetLang() != 1) { LocSetLang(1); RelocalizeUI(h); }
            break;
        case IDM_ABOUT: {
            TASKDIALOGCONFIG c{};
            c.cbSize = sizeof(c);
            c.hwndParent = h;
            c.dwFlags = TDF_ENABLE_HYPERLINKS;
            c.dwCommonButtons = TDCBF_OK_BUTTON;
            c.pszWindowTitle = T(L"О программе");
            c.pszMainIcon = TD_INFORMATION_ICON;
            c.pszMainInstruction = L"Shell Namespace Manager   v1.0";
            c.pszContent = L"Evgenii Shapovalov\n<a href=\"https://github.com/e-u-shapovalov\">github.com/e-u-shapovalov</a>";
            c.pfCallback = AboutCallback;
            TaskDialogIndirect(&c, nullptr, nullptr, nullptr);
            break;
        }
        }
        return 0;
    case WM_DESTROY:
        if (g_iml)  { g_iml->Release(); g_iml = nullptr; }
        if (g_font) { DeleteObject(g_font); g_font = nullptr; }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h,m,w,l);
}

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE, PWSTR, int show) {
    // Процесс запускается с requireAdministrator. Чтобы рядом лежащую DLL (если .exe запущен из
    // каталога, доступного на запись обычному пользователю — Downloads и т.п.) нельзя было
    // подгрузить в обход System32 (DLL planting), убираем из путей поиска каталог приложения и
    // текущий каталог: системные библиотеки и шелл-хелперы грузятся только из System32.
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32);
    SetDllDirectoryW(L"");
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    LocSetLang(LocGetLang()); // применить сохранённый язык до построения интерфейса
    INITCOMMONCONTROLSEX icc{ sizeof icc, ICC_TREEVIEW_CLASSES|ICC_BAR_CLASSES|ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);
    WNDCLASSEXW wc{ sizeof wc }; wc.lpfnWndProc=WndProc; wc.hInstance=hi;
    wc.lpszClassName=L"ShellNsManagerWnd"; wc.hCursor=LoadCursor(nullptr,IDC_ARROW);
    wc.hIcon   = (HICON)LoadImageW(hi, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE);
    wc.hIconSm = (HICON)LoadImageW(hi, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON, 16, 16, 0);
    wc.hbrBackground=(HBRUSH)(UINT_PTR)(COLOR_WINDOW+1); RegisterClassExW(&wc);
    HWND h=CreateWindowExW(0, wc.lpszClassName, SNM_TITLE,
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,CW_USEDEFAULT, 960,640,
        nullptr,nullptr,hi,nullptr);
    ShowWindow(h, show); UpdateWindow(h);
    MSG msg; while (GetMessageW(&msg,nullptr,0,0)){ TranslateMessage(&msg); DispatchMessageW(&msg);}
    CoUninitialize(); return 0;
}
