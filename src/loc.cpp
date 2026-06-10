#include "loc.h"
#include <windows.h>
#include <map>
#include <vector>
#include <string>

static int g_lang = 0;
static std::map<std::wstring, std::wstring> g_map;

// Встроенная таблица перевода (русский -> English). Используется и для генерации lang\en.lang.
// Чтобы добавить язык: скопировать en.lang, перевести правую часть, выбрать файл — но сейчас меню даёт ru/en.
static const wchar_t* kEN[][2] = {
    // меню
    { L"Узел", L"Node" },
    { L"Вид", L"View" },
    { L"Проводник", L"Explorer" },
    { L"Справка", L"Help" },
    { L"Язык", L"Language" },
    { L"Русский", L"Russian" },
    { L"English", L"English" },
    { L"Добавить свою папку…", L"Add your folder…" },
    { L"Удалить узел", L"Delete node" },
    { L"Переместить вверх", L"Move up" },
    { L"Переместить вниз", L"Move down" },
    { L"Скрыть встроенный элемент…", L"Hide built-in item…" },
    { L"Обновить список", L"Refresh list" },
    { L"Полный вид папок", L"Show all folders" },
    { L"Показывать «Главная»", L"Show “Home”" },
    { L"Показывать «Быстрый доступ»", L"Show “Quick access”" },
    { L"Перезапустить Проводник", L"Restart Explorer" },
    { L"О программе", L"About" },
    // тулбар
    { L"Обновить", L"Refresh" },
    { L"Добавить свою…", L"Add folder…" },
    { L"Удалить", L"Delete" },
    { L"Вверх", L"Up" },
    { L"Вниз", L"Down" },
    // тултипы
    { L"Перечитать список узлов из реестра", L"Re-read the node list from the registry" },
    { L"Добавить свою папку в «Этот компьютер» или дерево (со своей иконкой)", L"Add your folder to This PC or the tree (with your icon)" },
    { L"Удалить выбранный узел из namespace (перед удалением — .reg-бэкап)", L"Delete the selected node from the namespace (.reg backup is made first)" },
    { L"Поднять выбранный узел выше (меняет SortOrderIndex)", L"Move the selected node up (changes SortOrderIndex)" },
    { L"Опустить выбранный узел ниже (меняет SortOrderIndex)", L"Move the selected node down (changes SortOrderIndex)" },
    // корни дерева
    { L"Этот компьютер", L"This PC" },
    { L"Дерево (рабочий стол)", L"Tree (Desktop)" },
    // статус-бар / сообщения
    { L"Выберите узел", L"Select a node" },
    { L"Выберите конкретный узел (не заголовок)", L"Select a specific node (not a header)" },
    { L"Узел уже с краю", L"The node is already at the edge" },
    { L"Порядок изменён. Нажмите «Перезапустить Проводник», чтобы увидеть результат в дереве.", L"Order changed. Click “Restart Explorer” to see the result in the tree." },
    { L"Не удалось изменить порядок", L"Failed to change order" },
    { L"Внимание: права ключа не восстановлены", L"Warning: key permissions not restored" },
    { L"Проводник перезапущен", L"Explorer restarted" },
    { L"Полный вид папок — перезапустите Проводник", L"All folders view — restart Explorer" },
    { L"Упрощённый вид папок — перезапустите Проводник", L"Simplified view — restart Explorer" },
    { L"«Главная» скрыта — перезапустите Проводник", L"“Home” hidden — restart Explorer" },
    { L"«Главная» восстановлена — перезапустите Проводник", L"“Home” restored — restart Explorer" },
    { L"Быстрый доступ скрыт — Проводник перезапущен", L"Quick access hidden — Explorer restarted" },
    { L"Быстрый доступ показан — Проводник перезапущен", L"Quick access shown — Explorer restarted" },
    { L"Не удалось", L"Failed" },
    { L"Не удалось: бэкап не создан", L"Failed: backup not created" },
    { L"Не удалось добавить", L"Failed to add" },
    { L"Не удалось удалить", L"Failed to delete" },
    { L"Удаление узла", L"Delete node" },
    { L"Подтверждение", L"Confirm" },
    { L"Перезапустить Проводник? Панель задач на мгновение исчезнет и вернётся.", L"Restart Explorer? The taskbar will briefly disappear and return." },
    { L"Маркер: ", L"Marker: " },
    { L"Описание: ", L"Description: " },
    // диалог «Добавить свою папку»
    { L"Добавить свою папку", L"Add your folder" },
    { L"Имя в Проводнике:", L"Name in Explorer:" },
    { L"Папка-цель:", L"Target folder:" },
    { L"Иконка:", L"Icon:" },
    { L"Обзор…", L"Browse…" },
    { L"Выбрать…", L"Choose…" },
    { L"Нажмите «Выбрать…»: встроенная иконка Windows или свой файл .ico / .exe / .dll. Хранится ссылка на файл — не удаляйте и не перемещайте его.", L"Click “Choose…”: a built-in Windows icon or your own .ico / .exe / .dll file. A reference to the file is stored — do not delete or move it." },
    { L"Где показать узел", L"Where to show the node" },
    { L"Дерево (рабочий стол)", L"Tree (Desktop)" },
    { L"Куда писать в реестр", L"Where to write in the registry" },
    { L"HKCU — только для текущего пользователя", L"HKCU — current user only" },
    { L"HKLM — для всех пользователей (системно)", L"HKLM — all users (system-wide)" },
    { L"Создать", L"Create" },
    { L"Отмена", L"Cancel" },
    { L"Выберите папку-цель", L"Select the target folder" },
    // диалог «Скрыть встроенный»
    { L"Скрыть встроенный элемент из дерева", L"Hide a built-in item from the tree" },
    { L"Выберите встроенный элемент Проводника (например, «Панель управления»). Он будет откреплён от дерева навигации (через смену владельца ключа). Доступ по пути сохранится; снять скрытие — реимпортом navtree_before.reg из папки backups.", L"Select a built-in Explorer item (e.g. “Control Panel”). It will be unpinned from the navigation tree (via taking key ownership). Access by path stays; to undo — re-import navtree_before.reg from the backups folder." },
    { L"Скрыть", L"Hide" },
    { L"Выберите элемент в списке.", L"Select an item in the list." },
    // форматы / фрагменты статусов и деталей
    { L"Узлов: %d  (записей в реестре: %d)", L"Nodes: %d  (registry entries: %d)" },
    { L"Добавлено: ", L"Added: " },
    { L"   ·   бэкап: ", L"   ·   backup: " },
    { L"Скрыто: ", L"Hidden: " },
    { L"  — нажмите «Перезапустить Проводник»", L"  — click “Restart Explorer”" },
    { L"Удалено. Бэкап: ", L"Deleted. Backup: " },
    { L"(64+32-бит)", L"(64+32-bit)" },
    { L"(64-бит)", L"(64-bit)" },
    { L"(32-бит)", L"(32-bit)" },
    { L"Удалить «", L"Delete “" },
    { L"» из namespace?\n\nGUID: ", L"” from the namespace?\n\nGUID: " },
    { L"\nКуст: ", L"\nHive: " },
    { L"\n\nПеред удалением будет сохранён .reg-бэкап — операция обратима.", L"\n\nA .reg backup will be made first — the operation is reversible." },
    { L"Очистить «Быстрый доступ»", L"Clear “Quick access”" },
    { L"Очистить Быстрый доступ", L"Clear Quick access" },
    { L"Открепить все папки из «Быстрого доступа»?\n\nПроводник будет перезапущен: панель задач на мгновение исчезнет и вернётся. Закреплённые папки можно закрепить заново. Частые папки появляются заново по мере работы; чтобы выключить совсем — «Выключить «Быстрый доступ» полностью».", L"Unpin all folders from “Quick access”?\n\nExplorer will restart: the taskbar will briefly disappear and return. You can pin folders again later. Frequent folders reappear as you work; to turn them off entirely use “Turn off “Quick access” completely”." },
    { L"Быстрый доступ очищен. Бэкап: ", L"Quick access cleared. Backup: " },
    { L"Вернуть «Быстрый доступ» по умолчанию", L"Restore “Quick access” defaults" },
    { L"Вернуть стандартные папки «Быстрого доступа»?\n\nПроводник будет перезапущен, а файл Quick Access будет удалён. Windows заново создаст стандартный список: Desktop, Downloads, Документы, Изображения, Музыка, Видео.", L"Restore the default “Quick access” folders?\n\nExplorer will restart, and the Quick Access file will be deleted. Windows will recreate the default list: Desktop, Downloads, Documents, Pictures, Music, Videos." },
    { L"Быстрый доступ по умолчанию", L"Quick access defaults" },
    { L"Быстрый доступ возвращён по умолчанию. Бэкап: ", L"Quick access defaults restored. Backup: " },
    // выключение «Быстрого доступа» полностью
    { L"Выключить «Быстрый доступ» полностью", L"Turn off “Quick access” completely" },
    { L"Выключение Быстрого доступа", L"Turn off Quick access" },
    { L"Выключить «Быстрый доступ» полностью?\n\nПроводник будет открывать «Этот компьютер», недавние и частые отключатся, а узел «Быстрый доступ» скроется из дерева. Обратимо: сохраняется бэкап .reg.", L"Turn off “Quick access” completely?\n\nFile Explorer will open “This PC”, recent and frequent items will be disabled, and the “Quick access” node will be hidden from the tree. Reversible: a .reg backup is saved." },
    { L"Включить «Быстрый доступ» обратно?\n\nПроводник снова будет показывать «Быстрый доступ» и собирать историю.", L"Turn “Quick access” back on?\n\nFile Explorer will show “Quick access” again and collect history." },
    { L"Быстрый доступ выключен — Проводник перезапущен. Бэкап: ", L"Quick access turned off — Explorer restarted. Backup: " },
    { L"Быстрый доступ включён — Проводник перезапущен. Бэкап: ", L"Quick access turned on — Explorer restarted. Backup: " },
};

const wchar_t* T(const wchar_t* ru) {
    if (g_lang == 0) return ru;
    auto it = g_map.find(ru);
    return (it != g_map.end() && !it->second.empty()) ? it->second.c_str() : ru;
}

static BOOL CALLBACK locChildProc(HWND c, LPARAM) {
    wchar_t buf[1024] = L"";
    GetWindowTextW(c, buf, 1024);
    if (buf[0]) { const wchar_t* tr = T(buf); if (tr != buf) SetWindowTextW(c, tr); }
    return TRUE;
}

void LocDialog(HWND dlg) {
    if (g_lang == 0) return;
    locChildProc(dlg, 0);               // заголовок самого диалога
    EnumChildWindows(dlg, locChildProc, 0); // все контролы
}

static std::wstring LangDir() {
    wchar_t exe[MAX_PATH] = L"";
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring d = exe;
    size_t p = d.find_last_of(L"\\/");
    if (p != std::wstring::npos) d.resize(p + 1);
    return d + L"lang\\";
}

static void SaveUtf16(const std::wstring& path, const std::wstring& text) {
    HANDLE hf = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hf == INVALID_HANDLE_VALUE) return;
    const unsigned char bom[2] = { 0xFF, 0xFE };
    DWORD wr = 0;
    WriteFile(hf, bom, 2, &wr, nullptr);
    WriteFile(hf, text.c_str(), (DWORD)(text.size() * sizeof(wchar_t)), &wr, nullptr);
    CloseHandle(hf);
}

static std::wstring ReadUtf16(const std::wstring& path) {
    HANDLE hf = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hf == INVALID_HANDLE_VALUE) return L"";
    DWORD size = GetFileSize(hf, nullptr);
    std::vector<unsigned char> buf(size ? size : 1);
    DWORD rd = 0;
    ReadFile(hf, buf.data(), size, &rd, nullptr);
    CloseHandle(hf);
    size_t off = (rd >= 2 && buf[0] == 0xFF && buf[1] == 0xFE) ? 2 : 0;
    std::wstring s;
    if (rd > off) s.assign((const wchar_t*)(buf.data() + off), (rd - off) / sizeof(wchar_t));
    return s;
}

static void GenerateEn(const std::wstring& path) {
    std::wstring s = L"# ShellNsManager — файл языка / language file.\r\n";
    s += L"# Формат: русский<TAB>перевод. Скопируйте файл и переведите правую часть под свой язык.\r\n";
    s += L"# Format: russian<TAB>translation. Copy this file and translate the right side.\r\n\r\n";
    for (const auto& p : kEN) { s += p[0]; s += L"\t"; s += p[1]; s += L"\r\n"; }
    SaveUtf16(path, s);
}

static void LoadMap(const std::wstring& path) {
    g_map.clear();
    std::wstring all = ReadUtf16(path);
    size_t i = 0;
    while (i < all.size()) {
        size_t e = all.find(L'\n', i);
        std::wstring line = all.substr(i, (e == std::wstring::npos ? all.size() : e) - i);
        i = (e == std::wstring::npos) ? all.size() : e + 1;
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        if (line.empty() || line[0] == L'#') continue;
        size_t tab = line.find(L'\t');
        if (tab == std::wstring::npos) continue;
        std::wstring k = line.substr(0, tab), v = line.substr(tab + 1);
        if (!k.empty()) g_map[k] = v;
    }
}

void LocSetLang(int lang) {
    g_lang = lang;
    g_map.clear();
    if (lang != 0) {
        std::wstring dir = LangDir();
        CreateDirectoryW(dir.c_str(), nullptr);
        std::wstring path = dir + L"en.lang";
        if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) GenerateEn(path);
        LoadMap(path);
    }
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\ShellNsManager", 0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr) == ERROR_SUCCESS) {
        DWORD v = (DWORD)lang;
        RegSetValueExW(k, L"Lang", 0, REG_DWORD, (const BYTE*)&v, sizeof(v));
        RegCloseKey(k);
    }
}

int LocGetLang() {
    DWORD v = 0, cb = sizeof(v), type = 0;
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\ShellNsManager", 0, KEY_READ, &k) == ERROR_SUCCESS) {
        RegQueryValueExW(k, L"Lang", nullptr, &type, (LPBYTE)&v, &cb);
        RegCloseKey(k);
    }
    return (int)v;
}
