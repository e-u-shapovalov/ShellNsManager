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
#define SNM_VERSION  L"1.3.0"   // релиз; нерелизные сборки между релизами нумеруются 1.3.0.001, .002…
#define SNM_TITLE    L"Shell Namespace Manager   v" SNM_VERSION L"   (сборка " SNM_WIDE(__DATE__) L" " SNM_WIDE(__TIME__) L")"

// Логический узел: объединяет 64- и 32-битную записи одного GUID в одной ветке.
struct NsNode {
    std::wstring guid, marker, target, displayName, description;
    NsLocation   location = NsLocation::MyComputer;
    NsHive       hive     = NsHive::HKLM;
    bool         in64 = false, in32 = false, hasSort = false, hasNavPin = false;
    bool         isPin = false;   // закреплён через System.IsPinnedToNameSpaceTree (не namespace-запись)
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

// CLSID значков рабочего стола (для флажков показа/скрытия через HideDesktopIcons) и узлов дерева.
static const wchar_t* kGuidThisPC     = L"{20D04FE0-3AEA-1069-A2D8-08002B30309D}"; // Этот компьютер
static const wchar_t* kGuidRecycleBin = L"{645FF040-5081-101B-9F08-00AA002F954E}"; // Корзина

// Эффективные индексы системных узлов на шкале SortOrderIndex навигации (замерено на Win11 25H2
// build 26200): их собственный реестровый индекс Проводник игнорирует и ставит их по этим числам.
// Пользовательский пин с индексом < kEffThisPC — над «Этот компьютер»; между kEffThisPC и
// kEffRecycle — между ними; > kEffRecycle — под Корзиной. На других версиях Windows могут отличаться.
static const DWORD kEffThisPC  = 80;
static const DWORD kEffRecycle = 120;
static const DWORD kBarHint    = 70;   // ориентир «плавающей» полосы Проводника (импровизированный, ~70)
// Готовые системные инструменты — узлы-команды в «Этот компьютер» (свой фиксированный CLSID на каждый,
// чтобы флажок надёжно находил узел). Команда — REG_EXPAND_SZ через %SystemRoot%; запуск двойным кликом.
static const wchar_t* kGuidDiskMgmt   = L"{874C19C0-3F96-4669-B0DF-CCE51559C7BD}";
static const wchar_t* kGuidNetwork    = L"{03371F15-400F-4C4F-A7AB-7858B5F263B5}";
static const wchar_t* kGuidAppwiz     = L"{67194069-BBC9-46BE-BA4D-9C792881A321}";
static const wchar_t* kGuidDevmgmt    = L"{0431FE3F-FE62-43C8-9F0D-AFD5CF3A2D4D}";
static const wchar_t* kGuidCompmgmt   = L"{E74ACC6C-0086-4FE4-85ED-E4CEC62EA53F}";

// Таблица готовых инструментов: пункт меню, CLSID, имя (ключ T()), апплет в скобках, иконка, команда.
struct CmdDef { int id; const wchar_t* guid; const wchar_t* name; const wchar_t* hint; const wchar_t* icon; const wchar_t* command; };
static const CmdDef kCommands[] = {
    { IDM_TREE_DISK,    kGuidDiskMgmt,  L"Управление дисками",      L"diskmgmt.msc", L"%SystemRoot%\\system32\\imageres.dll,-27",
      L"%SystemRoot%\\system32\\mmc.exe \"%SystemRoot%\\system32\\diskmgmt.msc\"" },
    { IDM_CMD_NETWORK,  kGuidNetwork,   L"Сетевые подключения",     L"ncpa.cpl",     L"%SystemRoot%\\system32\\netshell.dll,0",
      L"%SystemRoot%\\system32\\control.exe ncpa.cpl" },
    { IDM_CMD_APPWIZ,   kGuidAppwiz,    L"Программы и компоненты",   L"appwiz.cpl",   L"%SystemRoot%\\system32\\appwiz.cpl,0",
      L"%SystemRoot%\\system32\\control.exe appwiz.cpl" },
    { IDM_CMD_DEVMGMT,  kGuidDevmgmt,   L"Диспетчер устройств",     L"devmgmt.msc",  L"%SystemRoot%\\system32\\devmgr.dll,0",
      L"%SystemRoot%\\system32\\mmc.exe \"%SystemRoot%\\system32\\devmgmt.msc\"" },
    { IDM_CMD_COMPMGMT, kGuidCompmgmt,  L"Управление компьютером",   L"compmgmt.msc", L"%SystemRoot%\\system32\\mycomput.dll,0",
      L"%SystemRoot%\\system32\\mmc.exe \"%SystemRoot%\\system32\\compmgmt.msc\"" },
};

// Компактный тулбар — только частые действия (текст/тултип = русский ключ, переводится через T()).
struct BtnDef { int id; const wchar_t* text; int w; const wchar_t* tip; };
static const BtnDef kButtons[] = {
    { IDC_BTN_REFRESH, L"Обновить",       90,  L"Перечитать список узлов из реестра" },
    { IDC_BTN_ADD,     L"Добавить свою…", 130, L"Добавить свою папку в «Этот компьютер» или дерево (со своей иконкой)" },
    { IDC_BTN_DELETE,  L"Удалить",        90,  L"Удалить выбранный узел из namespace (перед удалением — .reg-бэкап)" },
    { IDC_BTN_UP,      L"Вверх",          70,  L"Поднять выбранный узел выше (меняет SortOrderIndex)" },
    { IDC_BTN_DOWN,    L"Вниз",           70,  L"Опустить выбранный узел ниже (меняет SortOrderIndex)" },
    { IDC_BTN_RESTART, L"Применить",      100, L"Применить изменения: перезапустить Проводник, чтобы они вступили в силу (панель задач на миг исчезнет и вернётся)" },
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
    AppendMenuW(view, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(view, MF_STRING, IDM_DESKTOP_PC,  T(L"Показывать «Этот компьютер» на рабочем столе"));
    AppendMenuW(view, MF_STRING, IDM_DESKTOP_BIN, T(L"Показывать «Корзину» на рабочем столе"));
    AppendMenuW(view, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(view, MF_STRING, IDM_TREE_PC,   T(L"Показывать «Этот компьютер» в дереве слева"));
    AppendMenuW(view, MF_STRING, IDM_TREE_BIN,  T(L"Показывать «Корзину» в дереве слева"));
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)view, T(L"Вид"));

    // Меню «Инструменты»: готовые системные команды в «Этот компьютер» (флажки) + своя команда.
    HMENU tools = CreatePopupMenu();
    for (const auto& c : kCommands) {
        std::wstring label = std::wstring(T(c.name)) + L"   (" + c.hint + L")";
        AppendMenuW(tools, MF_STRING, c.id, label.c_str());
    }
    AppendMenuW(tools, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(tools, MF_STRING, IDM_CMD_CUSTOM, T(L"Добавить свою команду…"));
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)tools, T(L"Инструменты"));

    HMENU expl = CreatePopupMenu();
    AppendMenuW(expl, MF_STRING, IDC_BTN_RESTART, T(L"Применить (перезапуск Проводника)"));
    AppendMenuW(expl, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(expl, MF_STRING, IDM_RESETQA,   T(L"Очистить «Быстрый доступ»"));
    AppendMenuW(expl, MF_STRING, IDM_QA_DEFAULTS, T(L"Вернуть «Быстрый доступ» по умолчанию"));
    AppendMenuW(expl, MF_STRING, IDM_DISABLEQA, T(L"Выключить «Быстрый доступ» полностью"));
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)expl, T(L"Действия"));

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
    // Системные узлы, закреплённые в дереве навигации (pin) без namespace-записи: иначе они видны
    // в Проводнике, но не в приложении. Показываем их в разделе «Панель навигации (слева)».
    auto addPinNode = [&](const wchar_t* guid) {
        if (!GetNavTreePinned(guid)) return;
        // Реестровый SortOrderIndex системного узла Проводник ИГНОРИРУЕТ — он ставит его на свой
        // внутренний «эффективный» индекс (kEffThisPC/kEffRecycle). Берём именно его, чтобы дерево
        // приложения совпадало с реальным деревом Проводника, а пользовательские пины корректно
        // ложились в три зоны (над This PC / между / под Корзиной).
        DWORD eff = (lstrcmpiW(guid, kGuidRecycleBin)==0) ? kEffRecycle : kEffThisPC;
        for (auto& n : g_nodes)
            if (n.location==NsLocation::Desktop && lstrcmpiW(n.guid.c_str(), guid)==0) {
                // Узел уже есть как namespace-запись (Корзина — в Desktop\NameSpace HKLM). Не дублируем:
                // помечаем закреплённым системным и ставим эффективный индекс.
                n.isPin = true;
                if (!n.hasNavPin) { n.hasNavPin = true; n.navPin = 1; }
                n.hasSort = true; n.sortOrder = eff;
                return;
            }
        NsNode n; n.guid=guid; n.location=NsLocation::Desktop; n.hive=NsHive::HKCU; n.isPin=true;
        n.hasSort = true; n.sortOrder = eff;
        g_nodes.push_back(std::move(n));
    };
    addPinNode(kGuidThisPC);
    addPinNode(kGuidRecycleBin);
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
    // Единая шкала: пользовательские пины — по своему SortOrderIndex, системные узлы — по своему
    // эффективному индексу (kEffThisPC/kEffRecycle, проставлен в addPinNode). Так пользовательский
    // узел может стоять над «Этот компьютер», между ним и Корзиной или под Корзиной — ровно как в
    // реальном дереве Проводника.
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
    bool barDone = false;
    for (int i : order) {
        if (!IsVisibleTreeNode(i)) continue;
        // в навигации — пустая строка-место для полосы ~70 на границе индекса kBarHint (перед первым узлом ≥70)
        if (loc == NsLocation::Desktop && !barDone && g_nodes[i].hasSort && g_nodes[i].sortOrder >= kBarHint) {
            InsertTreeItem(root, L"", -1, (LPARAM)(-2), false);
            barDone = true;
        }
        std::wstring t = T(g_nodes[i].displayName.c_str());   // системные имена («Корзина»→Recycle Bin) переводятся; свои папки остаются как есть
        bool collide = false;
        for (int j : order)
            if (j != i && lstrcmpiW(g_nodes[j].displayName.c_str(), g_nodes[i].displayName.c_str()) == 0) { collide = true; break; }
        if (collide && !g_nodes[i].marker.empty() && g_nodes[i].marker != g_nodes[i].displayName)
            t += L"  · " + g_nodes[i].marker;
        if (g_nodes[i].hasSort) { wchar_t b[24]; swprintf_s(b, L"   · #%lu", g_nodes[i].sortOrder); t += b; }
        if (g_nodes[i].hive == NsHive::HKCU) t += L"   (HKCU)";
        if (g_nodes[i].isPin) t += std::wstring(L"   ") + T(L"[закреплён]");
        InsertTreeItem(root, t.c_str(), g_nodes[i].icon, (LPARAM)i, false);
    }
}

static void Populate() {
    BuildNodes();
    TreeView_DeleteAllItems(g_tree);
    HTREEITEM rPC   = InsertTreeItem(TVI_ROOT, T(L"Этот компьютер"),       -1, (LPARAM)(-1), true);
    HTREEITEM rTree = InsertTreeItem(TVI_ROOT, T(L"Панель навигации (слева)"), -1, (LPARAM)(-1), true);
    AddChildren(rPC,   NsLocation::MyComputer);
    AddChildren(rTree, NsLocation::Desktop);
    TreeView_Expand(g_tree, rPC,   TVE_EXPAND); // держим корни раскрытыми — состояние не теряется после операций
    TreeView_Expand(g_tree, rTree, TVE_EXPAND);
    SetWindowTextW(g_details, L"");
    int visible = 0;
    for (int i = 0; i < (int)g_nodes.size(); ++i) if (IsVisibleTreeNode(i)) ++visible;
    wchar_t st[160]; swprintf_s(st, T(L"Узлов: %d  (записей в реестре: %d)"), visible, (int)g_entries.size());
    SetStatus(st);
}

static void ShowDetails(int idx) {
    if (idx < 0 || idx >= (int)g_nodes.size()) { SetWindowTextW(g_details, L""); return; }
    const NsNode& n = g_nodes[idx];
    if (n.isPin) {
        std::wstring t = n.displayName + L"   ·   HKCU   " + T(L"[закреплён в дереве навигации]") + L"\r\n" + n.guid
                       + L"\r\n" + T(L"Системный узел, закреплён через System.IsPinnedToNameSpaceTree. «Удалить» — открепит его.");
        SetWindowTextW(g_details, t.c_str());
        return;
    }
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

// Закрепить/открепить системный узел (Этот компьютер, Корзина) в дереве навигации Проводника
// через System.IsPinnedToNameSpaceTree. В дереве самого приложения это не отражается (это свойство
// CLSID, а не namespace-запись), поэтому Populate() здесь не нужен.
static void ToggleNavPin(HWND h, const wchar_t* name, const wchar_t* guid) {
    bool pinned = GetNavTreePinned(guid);
    std::wstring backup, err;
    if (SetNavTreePinned(guid, !pinned, backup, err)) {
        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
        std::wstring s = L"«" + std::wstring(name) + L"»"
                       + (pinned ? T(L" — убрано из дерева") : T(L" — добавлено в дерево"))
                       + T(L". Нажмите «Применить», чтобы увидеть.");
        SetStatus(s.c_str());
    } else {
        MessageBoxW(h, err.c_str(), T(L"Не удалось"), MB_OK|MB_ICONWARNING);
    }
}

// Добавить/убрать готовый узел-команду внутри «Этот компьютер» (двойной клик запускает инструмент).
static void ToggleMyComputerCommand(HWND h, const CmdDef& cmd) {
    const wchar_t* name = T(cmd.name);
    bool present = GetMyComputerNode(cmd.guid);
    std::wstring backup, err;
    bool ok = present ? RemoveMyComputerNode(cmd.guid, backup, err)
                      : AddMyComputerCommand(cmd.guid, name, cmd.icon, cmd.command, backup, err);
    if (ok) {
        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
        Populate(); // узел появился/исчез в «Этот компьютер» — перечитать дерево приложения
        std::wstring s = L"«" + std::wstring(name) + L"»"
                       + (present ? T(L" — убрано из «Этот компьютер»") : T(L" — добавлено в «Этот компьютер»"))
                       + T(L". Нажмите «Применить», чтобы увидеть.");
        SetStatus(s.c_str());
    } else {
        MessageBoxW(h, err.c_str(), T(L"Не удалось"), MB_OK|MB_ICONWARNING);
    }
}

// Превратить «короткое» имя апплета в полную команду: ncpa.cpl → control.exe ncpa.cpl;
// diskmgmt.msc → mmc.exe "…\diskmgmt.msc". Полную команду (с пробелом/путём) или .exe не трогаем.
static std::wstring NormalizeCommand(std::wstring cmd) {
    size_t a = cmd.find_first_not_of(L" \t");
    if (a == std::wstring::npos) return cmd;
    size_t b = cmd.find_last_not_of(L" \t");
    cmd = cmd.substr(a, b - a + 1);
    bool bare = cmd.find_first_of(L" \t\\/") == std::wstring::npos; // голое имя файла, без аргументов и пути
    auto endsWith = [&](const wchar_t* ext) {
        int n = (int)wcslen(ext);
        return (int)cmd.size() > n &&
               CompareStringOrdinal(cmd.c_str() + cmd.size() - n, n, ext, n, TRUE) == CSTR_EQUAL;
    };
    if (bare && endsWith(L".cpl"))
        return L"%SystemRoot%\\system32\\control.exe " + cmd;
    if (bare && endsWith(L".msc"))
        return L"%SystemRoot%\\system32\\mmc.exe %SystemRoot%\\system32\\" + cmd; // путь System32 без пробелов — кавычки не нужны
    return cmd;
}

// Обернуть команду в запуск от администратора (через UAC). exe и аргументы разделяются, чтобы
// Start-Process получил их корректно; %SystemRoot% раскроет Проводник (команда — REG_EXPAND_SZ).
static std::wstring ElevateCommand(const std::wstring& cmd) {
    std::wstring exe, args;
    size_t i = 0;
    if (!cmd.empty() && cmd[0] == L'"') {
        size_t end = cmd.find(L'"', 1);
        exe = cmd.substr(1, (end == std::wstring::npos) ? std::wstring::npos : end - 1);
        i = (end == std::wstring::npos) ? cmd.size() : end + 1;
    } else {
        size_t sp = cmd.find(L' ');
        exe = cmd.substr(0, sp);
        i = (sp == std::wstring::npos) ? cmd.size() : sp;
    }
    while (i < cmd.size() && cmd[i] == L' ') ++i;
    args = cmd.substr(i);
    auto esc = [](const std::wstring& s) {  // экранировать ' для single-quoted powershell-строки
        std::wstring r;
        for (wchar_t c : s) { if (c == L'\'') r += L"''"; else r += c; }
        return r;
    };
    std::wstring ps = L"%SystemRoot%\\system32\\WindowsPowerShell\\v1.0\\powershell.exe -WindowStyle Hidden "
                      L"-Command \"Start-Process '" + esc(exe) + L"'";
    if (!args.empty()) ps += L" -ArgumentList '" + esc(args) + L"'";
    ps += L" -Verb RunAs\"";
    return ps;
}

// Диалог «Добавить свою команду» — пользователь вводит имя/команду/иконку, узел создаётся с новым GUID.
static void AddCustomCommandDialog(HWND h) {
    std::wstring name, command, icon, guid, backup, err;
    bool admin = false;
    if (!ShowAddCommandDialog(h, name, command, icon, admin)) return;
    command = NormalizeCommand(command);
    if (admin) command = ElevateCommand(command);
    if (AddCustomCommand(name, icon, command, guid, backup, err)) {
        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
        Populate();
        std::wstring s = L"«" + name + L"»" + T(L" — добавлено в «Этот компьютер»")
                       + T(L". Нажмите «Применить», чтобы увидеть.");
        SetStatus(s.c_str());
    } else {
        MessageBoxW(h, err.c_str(), T(L"Не удалось добавить"), MB_OK|MB_ICONWARNING);
    }
}

// Переключить видимость значка рабочего стола (Этот компьютер / Корзина) и показать результат.
static void ToggleDesktopIcon(HWND h, const wchar_t* guid, const wchar_t* name) {
    bool hide = !GetDesktopIconHidden(guid);
    std::wstring backup, err;
    if (SetDesktopIconHidden(guid, hide, backup, err)) {
        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
        std::wstring s = std::wstring(name) + (hide ? T(L" — значок скрыт с рабочего стола")
                                                    : T(L" — значок показан на рабочем столе"));
        SetStatus(s.c_str());
    } else {
        MessageBoxW(h, err.c_str(), T(L"Не удалось"), MB_OK|MB_ICONWARNING);
    }
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
    if (g_nodes[idx].isPin) { SetStatus(T(L"Закреплённый узел нельзя перемещать")); return; }
    NsLocation loc = g_nodes[idx].location;
    std::wstring guid = g_nodes[idx].guid;
    std::vector<int> order = SectionOrder(loc);
    int pos = -1;
    for (int k=0;k<(int)order.size();++k) if (order[k]==idx) { pos=k; break; }
    if (pos < 0) { SetStatus(T(L"Узел уже с краю")); return; }
    // order содержит и невидимые узлы (скрытые HKLM-узлы Desktop без nav-pin).
    // Меняемся местами с ближайшим ВИДИМЫМ соседом, иначе своп пришёлся бы
    // на скрытый узел и в дереве ничего бы не изменилось.
    int np = pos + dir;
    while (np >= 0 && np < (int)order.size() && !IsVisibleTreeNode(order[np])) np += dir;
    if (np < 0 || np >= (int)order.size()) { SetStatus(T(L"Узел уже с краю")); return; }
    std::swap(order[pos], order[np]);
    std::vector<SortItem> items;
    for (int ni : order) items.push_back({ g_nodes[ni].hive, g_nodes[ni].guid });
    std::wstring backup, err;
    if (SetSortOrder(items, backup, err)) {
        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
        Populate();
        SelectNodeByGuid(loc, guid);
        if (!err.empty()) MessageBoxW(g_tree, err.c_str(), T(L"Внимание: права ключа не восстановлены"), MB_OK|MB_ICONWARNING);
        SetStatus(T(L"Порядок изменён. Нажмите «Применить», чтобы увидеть результат."));
    } else {
        MessageBoxW(g_tree, err.c_str(), T(L"Не удалось изменить порядок"), MB_OK|MB_ICONWARNING);
    }
}

// ===== Управление порядком узлов: ручной ввод индекса, черта-разделитель, перетаскивание =====

static DWORD        s_sortIdxResult  = 0;
static DWORD        s_sortIdxInitial = 0;
static std::wstring s_sortIdxName;

static INT_PTR CALLBACK SortIdxDlgProc(HWND dlg, UINT m, WPARAM w, LPARAM) {
    switch (m) {
    case WM_INITDIALOG: {
        SetDlgItemInt(dlg, IDC_SORTIDX_EDIT, s_sortIdxInitial, FALSE);
        wchar_t info[320];
        swprintf_s(info, T(L"«%s»\r\n\r\nМеньше %u — над «Этот компьютер»;   %u…%u — между ним и Корзиной;   больше %u — под Корзиной."),
                   s_sortIdxName.c_str(), (unsigned)kEffThisPC, (unsigned)kEffThisPC, (unsigned)kEffRecycle, (unsigned)kEffRecycle);
        SetDlgItemTextW(dlg, IDC_SORTIDX_INFO, info);
        SendDlgItemMessageW(dlg, IDC_SORTIDX_EDIT, EM_SETSEL, 0, -1);
        SetFocus(GetDlgItem(dlg, IDC_SORTIDX_EDIT));
        LocDialog(dlg);
        return FALSE;
    }
    case WM_COMMAND:
        if (LOWORD(w) == IDOK) {
            BOOL ok = FALSE; UINT v = GetDlgItemInt(dlg, IDC_SORTIDX_EDIT, &ok, FALSE);
            if (!ok || v > 9999) { MessageBoxW(dlg, T(L"Введите целое число от 0 до 9999."), T(L"Индекс"), MB_OK|MB_ICONINFORMATION); return TRUE; }
            s_sortIdxResult = v; EndDialog(dlg, IDOK); return TRUE;
        }
        if (LOWORD(w) == IDCANCEL) { EndDialog(dlg, IDCANCEL); return TRUE; }
        break;
    }
    return FALSE;
}

static bool ShowSortIdxDialog(HWND parent, const std::wstring& name, DWORD initial, DWORD& out) {
    s_sortIdxName = name; s_sortIdxInitial = initial;
    if (DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_SORTIDX), parent, SortIdxDlgProc, 0) == IDOK) {
        out = s_sortIdxResult; return true;
    }
    return false;
}

// Записать пользовательскому узлу конкретный SortOrderIndex и перечитать дерево.
static void ApplyUserIndex(int idx, DWORD index) {
    if (idx < 0 || idx >= (int)g_nodes.size() || g_nodes[idx].isPin) return;
    NsHive hive = g_nodes[idx].hive;
    NsLocation loc = g_nodes[idx].location;
    std::wstring guid = g_nodes[idx].guid;
    std::wstring backup, err;
    if (SetUserSortIndex(hive, guid, index, backup, err)) {
        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
        Populate();
        SelectNodeByGuid(loc, guid);
        if (!err.empty()) MessageBoxW(g_tree, err.c_str(), T(L"Внимание: права ключа не восстановлены"), MB_OK|MB_ICONWARNING);
        SetStatus(T(L"Индекс изменён. Нажмите «Применить», чтобы увидеть результат."));
    } else {
        MessageBoxW(g_tree, err.c_str(), T(L"Не удалось задать индекс"), MB_OK|MB_ICONWARNING);
    }
}

// Двойной клик: пользовательскому узлу — ввод индекса; системному — сообщение, что он фиксирован.
static bool OnTreeDblClk(HWND h) {
    HTREEITEM sel = TreeView_GetSelection(g_tree);
    if (!sel) return false;
    TVITEMW ti{}; ti.mask = TVIF_PARAM; ti.hItem = sel; TreeView_GetItem(g_tree, &ti);
    int idx = (int)ti.lParam;
    if (idx < 0 || idx >= (int)g_nodes.size()) return false;
    if (g_nodes[idx].isPin) {
        wchar_t m[256];
        swprintf_s(m, T(L"«%s» — системный узел, Проводник держит его на позиции %u. Порядок изменить нельзя."),
                   T(g_nodes[idx].displayName.c_str()), (unsigned)g_nodes[idx].sortOrder);
        MessageBoxW(h, m, T(L"Фиксированный узел"), MB_OK|MB_ICONINFORMATION);
        return true;
    }
    if (g_nodes[idx].location != NsLocation::Desktop) return false;  // порядок задаём только в навигации
    DWORD init = g_nodes[idx].hasSort ? g_nodes[idx].sortOrder : 50;
    DWORD val = 0;
    if (ShowSortIdxDialog(h, g_nodes[idx].displayName, init, val)) ApplyUserIndex(idx, val);
    return true;
}

// Правый клик по узлу дерева: контекстное меню «Задать порядок (индекс)…» / «Удалить узел».
static void OnTreeRClick(HWND h) {
    POINT pt; GetCursorPos(&pt);
    POINT cpt = pt; ScreenToClient(g_tree, &cpt);
    TVHITTESTINFO ht{}; ht.pt = cpt;
    HTREEITEM hit = TreeView_HitTest(g_tree, &ht);
    if (!hit) return;
    TreeView_SelectItem(g_tree, hit);
    TVITEMW ti{}; ti.mask = TVIF_PARAM; ti.hItem = hit; TreeView_GetItem(g_tree, &ti);
    int idx = (int)ti.lParam;
    if (idx < 0 || idx >= (int)g_nodes.size()) return;   // заголовок/разделитель — меню не показываем

    HMENU menu = CreatePopupMenu();
    if (!g_nodes[idx].isPin && g_nodes[idx].location == NsLocation::Desktop)
        AppendMenuW(menu, MF_STRING, IDM_CTX_SORT, T(L"Задать порядок (индекс)…"));
    AppendMenuW(menu, MF_STRING, IDC_BTN_DELETE, T(L"Удалить узел"));
    int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, h, nullptr);
    DestroyMenu(menu);
    if (cmd == IDM_CTX_SORT) {
        DWORD init = g_nodes[idx].hasSort ? g_nodes[idx].sortOrder : 50;
        DWORD val = 0;
        if (ShowSortIdxDialog(h, g_nodes[idx].displayName, init, val)) ApplyUserIndex(idx, val);
    } else if (cmd == IDC_BTN_DELETE) {
        SendMessageW(h, WM_COMMAND, MAKEWPARAM(IDC_BTN_DELETE, 0), 0);
    }
}

// Черты зон: импровизированная полоса Проводника (~70, пунктир) над первым узлом с индексом ≥70,
// и якорные сплошные над «Этот компьютер»/«Корзиной» с подписью зоны справа (прозрачный фон).
static LRESULT TreeCustomDraw(LPNMTVCUSTOMDRAW cd) {
    switch (cd->nmcd.dwDrawStage) {
    case CDDS_PREPAINT:     return CDRF_NOTIFYITEMDRAW;
    case CDDS_ITEMPREPAINT: return CDRF_NOTIFYPOSTPAINT;
    case CDDS_ITEMPOSTPAINT: {
        HTREEITEM hItem = (HTREEITEM)cd->nmcd.dwItemSpec;
        TVITEMW ti{}; ti.mask = TVIF_PARAM; ti.hItem = hItem;
        if (!TreeView_GetItem(g_tree, &ti)) return CDRF_DODEFAULT;
        int idx = (int)ti.lParam;
        RECT rc; if (!TreeView_GetItemRect(g_tree, hItem, &rc, FALSE)) return CDRF_DODEFAULT;
        RECT cr; GetClientRect(g_tree, &cr);
        HDC hdc = cd->nmcd.hdc;
        HGDIOBJ oldF = g_font ? SelectObject(hdc, g_font) : nullptr;
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, GetSysColor(COLOR_GRAYTEXT));

        // пустая строка-место (lParam == -2): импровизированная полоса Проводника ~70 в собственной
        // строке (вставлена на границе индекса kBarHint) — пунктир по центру + текст справа.
        if (idx == -2) {
            int my = (rc.top + rc.bottom) / 2;
            RECT rcTx; TreeView_GetItemRect(g_tree, hItem, &rcTx, TRUE);    // текстовая часть — начинается после иконки
            wchar_t m[160]; swprintf_s(m, T(L"≈ полоса Проводника (~%u) — поймай сортировкой"), (unsigned)kBarHint);
            SIZE sz{}; GetTextExtentPoint32W(hdc, m, lstrlenW(m), &sz);
            int tx = cr.right - 12 - sz.cx;                 // текст справа
            HPEN pen = CreatePen(PS_DOT, 1, GetSysColor(COLOR_GRAYTEXT));
            HGDIOBJ op = SelectObject(hdc, pen);
            MoveToEx(hdc, rcTx.left, my, nullptr); LineTo(hdc, tx - 8, my);  // от после иконки до перед текстом — не задевает ни иконку, ни текст
            SelectObject(hdc, op); DeleteObject(pen);
            TextOutW(hdc, tx, my - sz.cy / 2, m, lstrlenW(m));
            if (oldF) SelectObject(hdc, oldF);
            return CDRF_DODEFAULT;
        }
        if (idx < 0 || idx >= (int)g_nodes.size()) { if (oldF) SelectObject(hdc, oldF); return CDRF_DODEFAULT; }

        // якорные сплошные черты зон + подпись справа (для «Этот компьютер» и «Корзины»)
        if (g_nodes[idx].isPin) {
            bool isBin = (lstrcmpiW(g_nodes[idx].guid.c_str(), kGuidRecycleBin) == 0);
            wchar_t m[200];
            if (isBin) swprintf_s(m, T(L"↑ между ПК и Корзиной: %u-%u      ↓ под Корзиной: больше %u"),
                                  (unsigned)kEffThisPC, (unsigned)kEffRecycle, (unsigned)kEffRecycle);
            else       swprintf_s(m, T(L"↑ над «Этот компьютер»: меньше %u"), (unsigned)kEffThisPC);
            SIZE sz{}; GetTextExtentPoint32W(hdc, m, lstrlenW(m), &sz);
            int tx = cr.right - 12 - sz.cx;
            int my = (rc.top + rc.bottom) / 2;
            RECT rcTx; TreeView_GetItemRect(g_tree, hItem, &rcTx, TRUE);     // конец текста узла («Этот компьютер · #80 …»)
            HPEN pen = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_GRAYTEXT));
            HGDIOBJ op = SelectObject(hdc, pen);
            MoveToEx(hdc, rcTx.right + 10, my, nullptr); LineTo(hdc, tx - 8, my);  // по центру строки, между именем узла и подписью зоны
            SelectObject(hdc, op); DeleteObject(pen);
            TextOutW(hdc, tx, my - sz.cy / 2, m, lstrlenW(m));
        }

        if (oldF) SelectObject(hdc, oldF);
        return CDRF_DODEFAULT;
    }
    }
    return CDRF_DODEFAULT;
}

// ---- Перетаскивание пользовательских узлов между зонами ----
static bool       g_dragging = false;
static int        g_dragIdx  = -1;
static HIMAGELIST g_dragImg  = nullptr;

static void OnBeginDrag(HWND h, LPNMTREEVIEWW tv) {
    int idx = (int)tv->itemNew.lParam;
    if (idx < 0 || idx >= (int)g_nodes.size()) return;
    if (g_nodes[idx].isPin || g_nodes[idx].location != NsLocation::Desktop) return;  // системные/заголовки не таскаем
    g_dragImg = TreeView_CreateDragImage(g_tree, tv->itemNew.hItem);
    if (!g_dragImg) return;
    g_dragIdx  = idx;
    g_dragging = true;
    ImageList_BeginDrag(g_dragImg, 0, 0, 0);
    POINT pt = tv->ptDrag; ClientToScreen(g_tree, &pt);
    ImageList_DragEnter(nullptr, pt.x, pt.y);
    SetCapture(h);
}

static void OnDragMove(HWND h, int cx, int cy) {
    if (!g_dragging) return;
    POINT screen = { cx, cy }; ClientToScreen(h, &screen);
    ImageList_DragMove(screen.x, screen.y);
    POINT inTree = screen; ScreenToClient(g_tree, &inTree);
    TVHITTESTINFO ht{}; ht.pt = inTree;
    HTREEITEM tgt = TreeView_HitTest(g_tree, &ht);
    ImageList_DragShowNolock(FALSE);
    TreeView_SelectDropTarget(g_tree, tgt);
    ImageList_DragShowNolock(TRUE);
}

// Найти элемент дерева по его lParam (индексу узла) — для краевых drop'ов мимо узлов.
static HTREEITEM FindNavItem(int idx) {
    for (HTREEITEM root = TreeView_GetRoot(g_tree); root; root = TreeView_GetNextSibling(g_tree, root))
        for (HTREEITEM c = TreeView_GetChild(g_tree, root); c; c = TreeView_GetNextSibling(g_tree, c)) {
            TVITEMW ti{}; ti.mask = TVIF_PARAM; ti.hItem = c; TreeView_GetItem(g_tree, &ti);
            if ((int)ti.lParam == idx) return c;
        }
    return nullptr;
}

static void OnDragEnd(HWND h, int cx, int cy) {
    if (!g_dragging) return;
    g_dragging = false;
    ImageList_DragLeave(nullptr);
    ImageList_EndDrag();
    ReleaseCapture();
    if (g_dragImg) { ImageList_Destroy(g_dragImg); g_dragImg = nullptr; }
    TreeView_SelectDropTarget(g_tree, nullptr);

    int dragIdx = g_dragIdx; g_dragIdx = -1;
    if (dragIdx < 0) return;
    POINT screen = { cx, cy }; ClientToScreen(h, &screen);
    POINT inTree = screen; ScreenToClient(g_tree, &inTree);

    std::vector<int> vis;
    for (int i : SectionOrder(NsLocation::Desktop)) if (IsVisibleTreeNode(i)) vis.push_back(i);
    if (vis.empty()) return;

    // позиции якорей в дереве: «Этот компьютер» и «Корзина» задают границы трёх зон
    int pcIdx = -1, binIdx = -1;
    for (int v : vis) {
        if      (lstrcmpiW(g_nodes[v].guid.c_str(), kGuidThisPC)     == 0) pcIdx  = v;
        else if (lstrcmpiW(g_nodes[v].guid.c_str(), kGuidRecycleBin) == 0) binIdx = v;
    }
    HTREEITEM hiFirst = FindNavItem(vis.front());
    HTREEITEM hiPc    = (pcIdx  >= 0) ? FindNavItem(pcIdx)  : nullptr;
    HTREEITEM hiBin   = (binIdx >= 0) ? FindNavItem(binIdx) : nullptr;
    RECT rcFirst{}, rcPc{}, rcBin{};
    if (hiFirst) TreeView_GetItemRect(g_tree, hiFirst, &rcFirst, FALSE);
    if (hiPc)    TreeView_GetItemRect(g_tree, hiPc,  &rcPc,  FALSE);
    if (hiBin)   TreeView_GetItemRect(g_tree, hiBin, &rcBin, FALSE);

    // линейная интерполяция координаты Y → индекс в диапазоне зоны
    auto lerp = [](int y, int y0, int y1, DWORD v0, DWORD v1) -> DWORD {
        if (y1 <= y0) return v0;
        double t = double(y - y0) / double(y1 - y0);
        if (t < 0) t = 0; if (t > 1) t = 1;
        return (DWORD)((double)v0 + t * ((double)v1 - (double)v0) + 0.5);
    };

    int dy = inTree.y;
    DWORD newIndex;
    if (hiPc && dy < rcPc.top) {
        // зона над «Этот компьютер»: 1..78 (выше = меньше; у самого верха ~1, у черты ~78)
        newIndex = lerp(dy, rcFirst.top, rcPc.top, 1, kEffThisPC - 2);
    } else if (hiBin && dy < rcBin.top) {
        // зона между ПК и Корзиной: 81..119
        int y0 = hiPc ? rcPc.bottom : rcFirst.top;
        newIndex = lerp(dy, y0, rcBin.top, kEffThisPC + 1, kEffRecycle - 1);
    } else {
        // зона под Корзиной: 121..200 (ниже = больше)
        int y0 = hiBin ? rcBin.bottom : (hiPc ? rcPc.bottom : rcFirst.top);
        newIndex = lerp(dy, y0, y0 + 200, kEffRecycle + 1, kEffRecycle + 80);
    }
    ApplyUserIndex(dragIdx, newIndex);
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
        CheckMenuItem(mp, IDM_DESKTOP_PC,      MF_BYCOMMAND | (GetDesktopIconHidden(kGuidThisPC)     ? MF_UNCHECKED : MF_CHECKED));
        CheckMenuItem(mp, IDM_DESKTOP_BIN,     MF_BYCOMMAND | (GetDesktopIconHidden(kGuidRecycleBin) ? MF_UNCHECKED : MF_CHECKED));
        CheckMenuItem(mp, IDM_TREE_PC,         MF_BYCOMMAND | (GetNavTreePinned(kGuidThisPC)     ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuItem(mp, IDM_TREE_BIN,        MF_BYCOMMAND | (GetNavTreePinned(kGuidRecycleBin) ? MF_CHECKED : MF_UNCHECKED));
        for (const auto& c : kCommands)
            CheckMenuItem(mp, c.id, MF_BYCOMMAND | (GetMyComputerNode(c.guid) ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuRadioItem(mp, IDM_LANG_RU, IDM_LANG_EN, (LocGetLang()==0 ? IDM_LANG_RU : IDM_LANG_EN), MF_BYCOMMAND);
        return 0;
    }
    case WM_NOTIFY: {
        LPNMHDR nh = (LPNMHDR)l;
        if (nh->idFrom == (UINT_PTR)IDC_TREE) {
            if (nh->code == NM_CUSTOMDRAW)  return TreeCustomDraw((LPNMTVCUSTOMDRAW)l);
            if (nh->code == NM_DBLCLK)      { if (OnTreeDblClk(h)) return 1; return 0; }
            if (nh->code == NM_RCLICK)      { OnTreeRClick(h); return 1; }
            if (nh->code == TVN_BEGINDRAGW) { OnBeginDrag(h, (LPNMTREEVIEWW)l); return 0; }
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
    case WM_MOUSEMOVE:
        if (g_dragging) { OnDragMove(h, (short)LOWORD(l), (short)HIWORD(l)); return 0; }
        break;
    case WM_LBUTTONUP:
        if (g_dragging) { OnDragEnd(h, (short)LOWORD(l), (short)HIWORD(l)); return 0; }
        break;
    case WM_COMMAND:
        // готовые команды-инструменты (таблица kCommands) — toggle узла в «Этот компьютер»
        for (const auto& c : kCommands)
            if (LOWORD(w) == (WORD)c.id) { ToggleMyComputerCommand(h, c); return 0; }
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
                std::wstring s = T(L"Скрыто: ") + name + T(L"  — нажмите «Применить»");
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
            if (n.isPin) {
                std::wstring guid = n.guid, dname = n.displayName;
                std::wstring pmsg = T(L"Открепить «") + dname + T(L"» из дерева навигации?\n\nЭто системный узел (не namespace-запись). В Проводнике он исчезнет из левой панели.");
                if (MessageBoxW(h, pmsg.c_str(), T(L"Открепить узел"), MB_YESNO|MB_ICONQUESTION) == IDYES) {
                    std::wstring backup, err;
                    if (SetNavTreePinned(guid, false, backup, err)) {
                        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
                        Populate();
                        SetStatus(T(L"Узел откреплён. Нажмите «Применить», чтобы увидеть."));
                    } else {
                        MessageBoxW(h, err.c_str(), T(L"Не удалось"), MB_OK|MB_ICONWARNING);
                    }
                }
                break;
            }
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
                SetStatus(now ? T(L"Полный вид папок — нажмите «Применить»")
                              : T(L"Упрощённый вид папок — нажмите «Применить»"));
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
                SetStatus(hide ? T(L"«Главная» скрыта — нажмите «Применить»")
                               : T(L"«Главная» восстановлена — нажмите «Применить»"));
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
        case IDM_DESKTOP_PC:
            ToggleDesktopIcon(h, kGuidThisPC, T(L"«Этот компьютер»"));
            break;
        case IDM_DESKTOP_BIN:
            ToggleDesktopIcon(h, kGuidRecycleBin, T(L"«Корзина»"));
            break;
        case IDM_TREE_PC:
            ToggleNavPin(h, T(L"Этот компьютер"), kGuidThisPC);
            break;
        case IDM_TREE_BIN:
            ToggleNavPin(h, T(L"Корзина"), kGuidRecycleBin);
            break;
        case IDM_CMD_CUSTOM:
            AddCustomCommandDialog(h);
            break;
        case IDC_BTN_RESTART:
            if (MessageBoxW(h, T(L"Применить изменения? Проводник перезапустится: панель задач на мгновение исчезнет и вернётся."),
                            T(L"Применение изменений"), MB_YESNO|MB_ICONQUESTION) == IDYES) {
                RestartExplorer();
                SetStatus(T(L"Изменения применены"));
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
            c.pszMainInstruction = L"Shell Namespace Manager   v" SNM_VERSION;
            c.pszContent = L"Evgenii Shapovalov\n<a href=\"https://github.com/e-u-shapovalov/ShellNsManager\">github.com/e-u-shapovalov/ShellNsManager</a>";
            c.pfCallback = AboutCallback;
            TaskDialogIndirect(&c, nullptr, nullptr, nullptr);
            break;
        }
        }
        return 0;
    case WM_DESTROY:
        // отвязать image list от дерева до Release: дерево держит слабую ссылку (без AddRef)
        if (g_tree) TreeView_SetImageList(g_tree, nullptr, TVSIL_NORMAL);
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
    // Процесс стартует с requireAdministrator (после UAC) и часто не выходит на передний план —
    // окно лишь моргает в панели задач. Принудительно поднимаем его наверх и отдаём фокус: короткий
    // flip TOPMOST + AttachThreadInput к текущему foreground-потоку (обходит блокировку смены фокуса
    // для не-foreground процесса).
    SetWindowPos(h, HWND_TOPMOST,   0,0,0,0, SWP_NOMOVE|SWP_NOSIZE);
    SetWindowPos(h, HWND_NOTOPMOST, 0,0,0,0, SWP_NOMOVE|SWP_NOSIZE);
    {
        HWND  fg    = GetForegroundWindow();
        DWORD fgTid = fg ? GetWindowThreadProcessId(fg, nullptr) : 0;
        DWORD myTid = GetCurrentThreadId();
        if (fgTid && fgTid != myTid) AttachThreadInput(fgTid, myTid, TRUE);
        BringWindowToTop(h);
        SetForegroundWindow(h);
        SetActiveWindow(h);
        if (fgTid && fgTid != myTid) AttachThreadInput(fgTid, myTid, FALSE);
    }
    MSG msg; while (GetMessageW(&msg,nullptr,0,0)){ TranslateMessage(&msg); DispatchMessageW(&msg);}
    CoUninitialize(); return 0;
}
