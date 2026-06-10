#pragma once
#include <windows.h>
#include <string>
#include <vector>

// Где узел виден в Проводнике.
enum class NsLocation { MyComputer, Desktop };
// Куст реестра.
enum class NsHive { HKLM, HKCU };

// Одна namespace-запись (узел) в одной конкретной ветке/виде реестра.
struct NsEntry {
    std::wstring guid;                       // "{....}" как в реестре
    std::wstring marker;                     // (default) самого NameSpace-ключа (тех. маркер)
    NsLocation   location = NsLocation::MyComputer;
    NsHive       hive     = NsHive::HKLM;
    bool         wow64    = false;           // true = 32-битный вид (Wow6432Node)
    bool         hasSort  = false;
    DWORD        sortOrder = 0;              // SortOrderIndex из CLSID\{guid}, если есть
    bool         hasNavPin = false;
    DWORD        navPin    = 0;              // System.IsPinnedToNameSpaceTree, если есть
    std::wstring target;                     // CLSID\{guid}\Instance\InitPropertyBag\Target, если есть
};

// Перечислить все namespace-узлы во всех ветках/видах (HKLM 64/32, HKCU).
std::vector<NsEntry> EnumNamespace();

// Экспорт ветки реестра в .reg (бэкап) через reg.exe export. err — текст ошибки.
bool ExportKey(const std::wstring& regKeyPath, const std::wstring& regFile, std::wstring& err);

// Запрос на добавление folder-shortcut namespace-расширения.
struct AddRequest {
    std::wstring name;       // отображаемое имя в Проводнике
    std::wstring target;     // реальная папка-цель на диске
    std::wstring iconPath;   // "путь,индекс" или "%SystemRoot%\\system32\\shell32.dll,-44"
    bool         toMyComputer = false;
    bool         toDesktop    = true;
    NsHive       hive         = NsHive::HKCU;
};

// Создать folder-shortcut (CLSID_FolderShortcut, рецепт §3.4) и прописать в namespace.
// Перед записью делает .reg-бэкап. newGuid — сгенерированный GUID, backupPath — папка бэкапа.
bool AddFolderShortcut(const AddRequest& req, std::wstring& newGuid, std::wstring& backupPath, std::wstring& err);

// Удалить namespace-узел (запись {guid} в MyComputer/Desktop NameSpace) во всех видах ветки.
// Бэкап ВСЕХ затронутых веток делается до удаления; если бэкап не удался — удаление отменяется.
// Для созданных приложением folder-shortcut'ов (Instance\CLSID == CLSID_FolderShortcut) дополнительно
// удаляется и регистрация CLSID (бэкап CLSID.reg). Системные расширения CLSID не трогаются.
bool DeleteEntry(NsHive hive, NsLocation loc, const std::wstring& guid, std::wstring& backupPath, std::wstring& err);

// Узел для сортировки: куст + GUID.
struct SortItem { NsHive hive; std::wstring guid; };

// Записать порядок: SortOrderIndex = 10,20,30… по позиции в itemsInOrder. HKCU — напрямую;
// HKLM (системные) — со сменой владельца CLSID-ключа (TrustedInstaller→Administrators→назад),
// нужен запуск от админа. Пишутся только реально изменившиеся ключи. Бэкап sort_before.reg.
// При успехе с непустым err — порядок применён, но права/владельца части ключей восстановить
// не удалось (предупреждение для UI, повторять не нужно).
bool SetSortOrder(const std::vector<SortItem>& itemsInOrder, std::wstring& backupPath, std::wstring& err);

// Режим «показать все папки» в дереве навигации (HKCU\...\Advanced\NavPaneShowAllFolders).
bool GetNavAllFolders();
bool SetNavAllFolders(bool on);

// Режим HubMode Проводника (true = скрыт). Пишется в HKCU\...\Advanced и HKLM (если есть права).
bool SetHubMode(bool hide);

// Открепить/закрепить элемент в дереве навигации через System.IsPinnedToNameSpaceTree=0/1
// в HKLM\Software\Classes\CLSID\{guid} (его читает Проводник; со сменой владельца) и HKCU.
// Бэкап navtree_before.reg. Доступ по пути сохраняется.
// ПРИМЕЧАНИЕ: при успехе с непустым err — значение применено, но права/владельца ключа
// восстановить не удалось (предупреждение для UI, повторять операцию не нужно).
bool SetNavTreeHidden(const std::wstring& guid, bool hidden, std::wstring& backupPath, std::wstring& err);

// Скрыть/показать «Главная» (Home): удаление/восстановление записи {f874310e} в Desktop\NameSpace
// (со сменой владельца). Рабочий способ на Win11 24H2/25H2. hidden=true → удалить запись.
bool GetHomeHidden();
bool SetHomeHidden(bool hidden, std::wstring& backupPath, std::wstring& err);

// Очистить закреплённые/частые папки «Быстрого доступа»: бэкап + валидный пустой
// AutomaticDestinations-файл. Проводник должен быть остановлен вызывающим кодом.
bool ClearQuickAccess(std::wstring& backupPath, std::wstring& err);

// Вернуть стандартное наполнение «Быстрого доступа»: бэкап + удаление AutomaticDestinations,
// чтобы Проводник заново создал дефолтные Desktop/Downloads/Документы/и т.п.
bool RestoreQuickAccessDefaults(std::wstring& backupPath, std::wstring& err);

// Скрыть/показать «Быстрый доступ» в дереве: современный DelegateFolders + старый
// {679f85cb}\ShellFolder\Attributes (бит 0x00600000 → a0600000 = скрыт).
// HKLM, смена владельца. Бэкап. Пины не трогаются.
// При успехе с непустым err — атрибут применён, но права/владельца ключа восстановить
// не удалось (предупреждение для UI, повторять не нужно).
bool GetQuickAccessHidden();
bool SetQuickAccessHidden(bool hidden, std::wstring& backupPath, std::wstring& err);

// Полностью выключить/включить «Быстрый доступ» (Home): скрыть узел/секцию Quick Access +
// LaunchTo=1, ShowRecent=0, ShowFrequent=0 (HKCU), чтобы Проводник открывал «Этот компьютер»
// и не наполнял историю заново.
// Обратимо: .reg-бэкап. При непустом err — настройки применены, но ACL узла вернуть не удалось.
bool GetQuickAccessDisabled();
bool SetQuickAccessDisabled(bool disable, std::wstring& backupPath, std::wstring& err);
