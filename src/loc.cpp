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
    { L"Действия", L"Actions" },
    { L"Инструменты", L"Tools" },
    { L"Сетевые подключения", L"Network Connections" },
    { L"Программы и компоненты", L"Programs and Features" },
    { L"Диспетчер устройств", L"Device Manager" },
    { L"Управление компьютером", L"Computer Management" },
    { L"Добавить свою команду…", L"Add your command…" },
    { L"Добавить свою команду", L"Add your command" },
    { L"Имя пункта:", L"Item name:" },
    { L"Команда:", L"Command:" },
    { L"Можно просто: ncpa.cpl или diskmgmt.msc. Либо полная команда или путь к .exe.", L"Just type e.g. ncpa.cpl or diskmgmt.msc. Or a full command / path to an .exe." },
    { L"Пункт появится в «Этот компьютер»; двойной клик выполнит команду. Запись в HKCU (для текущего пользователя), убрать можно кнопкой «Удалить».", L"The item appears in This PC; a double-click runs the command. Written to HKCU (current user); remove it with the “Delete” button." },
    { L"Укажите имя пункта.", L"Enter the item name." },
    { L"Укажите команду запуска.", L"Enter the command to run." },
    { L"Запускать от имени администратора (через запрос UAC)", L"Run as administrator (via UAC prompt)" },
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
    { L"Показывать «Этот компьютер» на рабочем столе", L"Show “This PC” on the desktop" },
    { L"Показывать «Корзину» на рабочем столе", L"Show “Recycle Bin” on the desktop" },
    { L"Показывать «Этот компьютер» в дереве слева", L"Show “This PC” in the left tree" },
    { L"Показывать «Корзину» в дереве слева", L"Show “Recycle Bin” in the left tree" },
    { L"Показывать «Управление дисками» в «Этот компьютер»", L"Show “Disk Management” in “This PC”" },
    { L"Управление дисками", L"Disk Management" },
    { L"Корзина", L"Recycle Bin" },
    { L"Перезапустить Проводник", L"Restart Explorer" },
    { L"Применить", L"Apply" },
    { L"Применить (перезапуск Проводника)", L"Apply (restart Explorer)" },
    { L"Применить изменения: перезапустить Проводник, чтобы они вступили в силу (панель задач на миг исчезнет и вернётся)", L"Apply changes: restart Explorer so they take effect (the taskbar briefly disappears and returns)" },
    { L"Применить изменения? Проводник перезапустится: панель задач на мгновение исчезнет и вернётся.", L"Apply changes? Explorer will restart: the taskbar will briefly disappear and return." },
    { L"Применение изменений", L"Applying changes" },
    { L"Изменения применены", L"Changes applied" },
    { L". Нажмите «Применить», чтобы увидеть.", L". Click “Apply” to see it." },
    { L"Порядок изменён. Нажмите «Применить», чтобы увидеть результат.", L"Order changed. Click “Apply” to see the result." },
    { L"  — нажмите «Применить»", L"  — click “Apply”" },
    { L"Полный вид папок — нажмите «Применить»", L"All folders view — click “Apply”" },
    { L"Упрощённый вид папок — нажмите «Применить»", L"Simplified view — click “Apply”" },
    { L"«Главная» скрыта — нажмите «Применить»", L"“Home” hidden — click “Apply”" },
    { L"«Главная» восстановлена — нажмите «Применить»", L"“Home” restored — click “Apply”" },
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
    { L"Панель навигации (слева)", L"Navigation pane (left)" },
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
    { L"«Этот компьютер»", L"“This PC”" },
    { L"«Корзина»", L"“Recycle Bin”" },
    { L" — значок скрыт с рабочего стола", L" — icon hidden from the desktop" },
    { L" — значок показан на рабочем столе", L" — icon shown on the desktop" },
    { L" — добавлено в дерево", L" — added to the navigation tree" },
    { L" — убрано из дерева", L" — removed from the navigation tree" },
    { L" — добавлено в «Этот компьютер»", L" — added to “This PC”" },
    { L" — убрано из «Этот компьютер»", L" — removed from “This PC”" },
    { L"[закреплён]", L"[pinned]" },
    { L"[закреплён в дереве навигации]", L"[pinned to navigation tree]" },
    { L"Системный узел, закреплён через System.IsPinnedToNameSpaceTree. «Удалить» — открепит его.", L"A system node pinned via System.IsPinnedToNameSpaceTree. “Delete” will unpin it." },
    { L"Закреплённый узел нельзя перемещать", L"A pinned node cannot be reordered" },
    { L"Открепить «", L"Unpin “" },
    { L"» из дерева навигации?\n\nЭто системный узел (не namespace-запись). В Проводнике он исчезнет из левой панели.", L"” from the navigation tree?\n\nThis is a system node (not a namespace entry). It will disappear from the left pane in Explorer." },
    { L"Открепить узел", L"Unpin node" },
    { L"Узел откреплён. Нажмите «Применить», чтобы увидеть.", L"Node unpinned. Click “Apply” to see it." },
    { L". Нажмите «Перезапустить Проводник», чтобы увидеть.", L". Click “Restart Explorer” to see it." },
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
    { L"Панель навигации (слева)", L"Navigation pane (left)" },
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
    // управление порядком: ручной ввод индекса, зоны, перетаскивание, полоса
    { L"Задать порядок (индекс)", L"Set order (index)" },
    { L"Задать порядок (индекс)…", L"Set order (index)…" },
    { L"Индекс:", L"Index:" },
    { L"Индекс", L"Index" },
    { L"Введите целое число от 0 до 9999.", L"Enter a whole number from 0 to 9999." },
    { L"Индекс изменён. Нажмите «Применить», чтобы увидеть результат.", L"Index changed. Click “Apply” to see the result." },
    { L"Не удалось задать индекс", L"Failed to set index" },
    { L"Фиксированный узел", L"Fixed node" },
    { L"«%s» — системный узел, Проводник держит его на позиции %u. Порядок изменить нельзя.", L"“%s” is a system node; Explorer keeps it at position %u. Its order cannot be changed." },
    { L"↑ над «Этот компьютер»: меньше %u", L"↑ above This PC: less than %u" },
    { L"↑ между ПК и Корзиной: %u-%u      ↓ под Корзиной: больше %u", L"↑ between PC and Recycle Bin: %u-%u      ↓ below Recycle Bin: over %u" },
    { L"≈ полоса Проводника (~%u) — поймай сортировкой", L"≈ Explorer divider (~%u) — catch it by sorting" },
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
