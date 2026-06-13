#include "registry.h"
#include <utility>
#include <stdio.h>
#include <objbase.h>
#include <objidl.h>
#include <aclapi.h>

static const wchar_t* kExplorer    = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer";
static const wchar_t* kExplorerWow = L"SOFTWARE\\Wow6432Node\\Microsoft\\Windows\\CurrentVersion\\Explorer";

static HKEY HiveRoot(NsHive h) { return h == NsHive::HKLM ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER; }

// Прочитать строковое значение (value=nullptr -> (default)) из root\sub.
static std::wstring RegReadString(HKEY root, const std::wstring& sub, const wchar_t* value) {
    HKEY k;
    if (RegOpenKeyExW(root, sub.c_str(), 0, KEY_READ, &k) != ERROR_SUCCESS) return L"";
    std::wstring result;
    DWORD type = 0, cb = 0;
    if (RegQueryValueExW(k, value, nullptr, &type, nullptr, &cb) == ERROR_SUCCESS
        && cb > 0 && (type == REG_SZ || type == REG_EXPAND_SZ)) {
        std::wstring buf(cb / sizeof(wchar_t) + 1, L'\0');
        DWORD cb2 = cb;
        if (RegQueryValueExW(k, value, nullptr, &type, (LPBYTE)buf.data(), &cb2) == ERROR_SUCCESS) {
            buf.resize(wcslen(buf.c_str()));
            result = buf;
        }
    }
    RegCloseKey(k);
    return result;
}

static bool RegReadDword(HKEY root, const std::wstring& sub, const wchar_t* value, DWORD& out) {
    HKEY k;
    if (RegOpenKeyExW(root, sub.c_str(), 0, KEY_READ, &k) != ERROR_SUCCESS) return false;
    DWORD type = 0, data = 0, cb = sizeof(data);
    bool ok = false;
    if (RegQueryValueExW(k, value, nullptr, &type, (LPBYTE)&data, &cb) == ERROR_SUCCESS && type == REG_DWORD) {
        out = data; ok = true;
    }
    RegCloseKey(k);
    return ok;
}

// Best-effort: SortOrderIndex и Target лежат в регистрации CLSID, а не в NameSpace.
static void ReadClsidExtras(NsEntry& e) {
    const std::wstring& g = e.guid;
    std::vector<std::pair<HKEY, std::wstring>> cands;
    if (e.hive == NsHive::HKCU) {
        cands.emplace_back(HKEY_CURRENT_USER,  L"SOFTWARE\\Classes\\CLSID\\" + g);
        cands.emplace_back(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Classes\\CLSID\\" + g);
    } else if (e.wow64) {
        cands.emplace_back(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Classes\\Wow6432Node\\CLSID\\" + g);
        cands.emplace_back(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Wow6432Node\\Classes\\CLSID\\" + g);
        cands.emplace_back(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Classes\\CLSID\\" + g);
    } else {
        cands.emplace_back(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Classes\\CLSID\\" + g);
    }
    for (const auto& c : cands) {
        if (!e.hasSort) {
            DWORD s = 0;
            if (RegReadDword(c.first, c.second, L"SortOrderIndex", s)) { e.sortOrder = s; e.hasSort = true; }
        }
        if (e.target.empty()) {
            std::wstring t = RegReadString(c.first, c.second + L"\\Instance\\InitPropertyBag", L"Target");
            if (!t.empty()) e.target = t;
        }
        if (!e.hasNavPin) {
            DWORD p = 0;
            if (RegReadDword(c.first, c.second, L"System.IsPinnedToNameSpaceTree", p)) { e.navPin = p; e.hasNavPin = true; }
        }
        if (e.hasSort && !e.target.empty() && e.hasNavPin) break;
    }
}

static void EnumOneRoot(NsHive hive, NsLocation loc, bool wow64,
                        const wchar_t* explorerBase, std::vector<NsEntry>& out) {
    HKEY root = HiveRoot(hive);
    std::wstring nsSub = explorerBase;
    nsSub += (loc == NsLocation::MyComputer) ? L"\\MyComputer\\NameSpace" : L"\\Desktop\\NameSpace";
    HKEY ns;
    if (RegOpenKeyExW(root, nsSub.c_str(), 0, KEY_READ, &ns) != ERROR_SUCCESS) return;
    wchar_t name[256];
    for (DWORD i = 0;; ++i) {
        DWORD cch = (DWORD)_countof(name);
        LONG r = RegEnumKeyExW(ns, i, name, &cch, nullptr, nullptr, nullptr, nullptr);
        if (r != ERROR_SUCCESS) break;          // ERROR_NO_MORE_ITEMS или иная ошибка
        if (name[0] != L'{') continue;          // пропустить DelegateFolders и не-GUID
        CLSID guidCheck;
        if (FAILED(CLSIDFromString(name, &guidCheck))) continue; // только корректные {GUID}: имя попадёт в командную строку reg.exe и в пути для ACL
        NsEntry e;
        e.guid = name;
        e.location = loc; e.hive = hive; e.wow64 = wow64;
        e.marker = RegReadString(root, nsSub + L"\\" + name, nullptr);
        ReadClsidExtras(e);
        out.push_back(std::move(e));
    }
    RegCloseKey(ns);
}

std::vector<NsEntry> EnumNamespace() {
    std::vector<NsEntry> out;
    EnumOneRoot(NsHive::HKLM, NsLocation::MyComputer, false, kExplorer,    out);
    EnumOneRoot(NsHive::HKLM, NsLocation::Desktop,    false, kExplorer,    out);
    EnumOneRoot(NsHive::HKLM, NsLocation::MyComputer, true,  kExplorerWow, out);
    EnumOneRoot(NsHive::HKLM, NsLocation::Desktop,    true,  kExplorerWow, out);
    EnumOneRoot(NsHive::HKCU, NsLocation::MyComputer, false, kExplorer,    out);
    EnumOneRoot(NsHive::HKCU, NsLocation::Desktop,    false, kExplorer,    out);
    return out;
}

// Полный путь к системной утилите в System32. Процесс запущен с requireAdministrator, поэтому
// запускать reg.exe/taskkill по короткому имени нельзя: при lpApplicationName=nullptr CreateProcess
// ищет файл в т.ч. в каталоге программы и текущем каталоге — туда можно подложить чужой .exe.
static std::wstring SystemExe(const wchar_t* exe) {
    wchar_t dir[MAX_PATH] = L"";
    UINT n = GetSystemDirectoryW(dir, MAX_PATH);
    std::wstring p = (n > 0 && n < MAX_PATH) ? std::wstring(dir) : std::wstring(L"C:\\Windows\\System32");
    if (!p.empty() && p.back() != L'\\') p += L'\\';
    return p + exe;
}

bool ExportKey(const std::wstring& regKeyPath, const std::wstring& regFile, std::wstring& err) {
    std::wstring exe = SystemExe(L"reg.exe");
    std::wstring cmd = L"\"" + exe + L"\" export \"" + regKeyPath + L"\" \"" + regFile + L"\" /y";
    std::vector<wchar_t> buf(cmd.begin(), cmd.end()); buf.push_back(L'\0');
    // Рабочий каталог дочернего reg.exe — System32, а не (возможно, доступный на запись обычному
    // пользователю) каталог программы: иначе reg.exe мог бы подхватить подложенную в CWD DLL.
    wchar_t sysdir[MAX_PATH] = L"";
    UINT sdn = GetSystemDirectoryW(sysdir, MAX_PATH);
    LPCWSTR cwd = (sdn > 0 && sdn < MAX_PATH) ? sysdir : nullptr;
    STARTUPINFOW si{}; si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exe.c_str(), buf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, cwd, &si, &pi)) {
        err = L"Не удалось запустить reg.exe"; return false;
    }
    DWORD waited = WaitForSingleObject(pi.hProcess, 30000); // не висеть вечно, если reg.exe залип
    if (waited == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
        err = L"reg.exe export не ответил за 30 секунд"; return false;
    }
    DWORD code = 1; GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    if (code != 0) { err = L"reg.exe export вернул код " + std::to_wstring(code); return false; }
    return true;
}

// ---- Добавление folder-shortcut (запись в реестр, рецепт §3.4) ----

static const wchar_t* kFolderShortcutClsid = L"{0AFACED1-E828-11D1-9187-B532F1E9575D}";

static bool DeleteKeyForced(HKEY root, const std::wstring& sub); // определён ниже (для отката/очистки)

static std::wstring RegRootName(HKEY root) {
    return (root == HKEY_LOCAL_MACHINE) ? L"HKEY_LOCAL_MACHINE" : L"HKEY_CURRENT_USER";
}

static bool KeyExists(HKEY root, const std::wstring& sub) {
    HKEY k;
    if (RegOpenKeyExW(root, sub.c_str(), 0, KEY_READ, &k) != ERROR_SUCCESS) return false;
    RegCloseKey(k);
    return true;
}

static bool WriteVal(HKEY root, const std::wstring& sub, const wchar_t* name, DWORD type, const void* data, DWORD cb) {
    HKEY k;
    if (RegCreateKeyExW(root, sub.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr) != ERROR_SUCCESS) return false;
    LONG r = RegSetValueExW(k, name, 0, type, (const BYTE*)data, cb);
    RegCloseKey(k);
    return r == ERROR_SUCCESS;
}
static bool WriteSz(HKEY root, const std::wstring& sub, const wchar_t* name, const std::wstring& val) {
    return WriteVal(root, sub, name, REG_SZ, val.c_str(), (DWORD)((val.size()+1)*sizeof(wchar_t)));
}
static bool WriteExpand(HKEY root, const std::wstring& sub, const wchar_t* name, const std::wstring& val) {
    return WriteVal(root, sub, name, REG_EXPAND_SZ, val.c_str(), (DWORD)((val.size()+1)*sizeof(wchar_t)));
}
static bool WriteDw(HKEY root, const std::wstring& sub, const wchar_t* name, DWORD val) {
    return WriteVal(root, sub, name, REG_DWORD, &val, sizeof(val));
}

static std::wstring MakeBackupDir() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring dir = exePath;
    size_t p = dir.find_last_of(L"\\/");
    if (p != std::wstring::npos) dir.resize(p + 1);
    SYSTEMTIME st; GetLocalTime(&st);
    // Миллисекунды в имени каталога: иначе две операции в одну и ту же секунду попали бы в один
    // каталог и второй .reg-бэкап молча затёр бы первый (потеря точки восстановления).
    wchar_t ts[40];
    swprintf_s(ts, L"%04d%02d%02d-%02d%02d%02d-%03d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    std::wstring base = dir + L"backups";
    CreateDirectoryW(base.c_str(), nullptr);
    std::wstring full = base + L"\\" + ts;
    CreateDirectoryW(full.c_str(), nullptr);
    return full + L"\\";
}

static bool WriteShortcut(HKEY root, const std::wstring& classesBase, const wchar_t* explorerBase,
                          const AddRequest& req, const std::wstring& g) {
    std::wstring clsid = classesBase + L"\\CLSID\\" + g;
    bool ok = true;
    auto W = [&](bool r){ if (!r) ok = false; };
    W(WriteSz    (root, clsid, nullptr, req.name));
    W(WriteDw    (root, clsid, L"System.IsPinnedToNameSpaceTree", 1));
    W(WriteDw    (root, clsid, L"SortOrderIndex", 0x42));
    W(WriteExpand(root, clsid + L"\\DefaultIcon", nullptr, req.iconPath));
    W(WriteExpand(root, clsid + L"\\InProcServer32", nullptr, L"%SystemRoot%\\system32\\shell32.dll"));
    W(WriteSz    (root, clsid + L"\\InProcServer32", L"ThreadingModel", L"Both"));
    W(WriteSz    (root, clsid + L"\\Instance", L"CLSID", kFolderShortcutClsid));
    W(WriteDw    (root, clsid + L"\\Instance\\InitPropertyBag", L"Attributes", 0x11));
    W(WriteSz    (root, clsid + L"\\Instance\\InitPropertyBag", L"Target", req.target));
    W(WriteDw    (root, clsid + L"\\ShellFolder", L"Attributes", 0xF080004D));
    W(WriteDw    (root, clsid + L"\\ShellFolder", L"FolderValueFlags", 0x28));
    std::wstring exp = explorerBase;
    if (req.toMyComputer) W(WriteSz(root, exp + L"\\MyComputer\\NameSpace\\" + g, nullptr, req.name));
    if (req.toDesktop)    W(WriteSz(root, exp + L"\\Desktop\\NameSpace\\" + g, nullptr, req.name));
    return ok;
}

// Бэкап затрагиваемых NameSpace-веток перед добавлением. Бэкапятся только реально
// существующие ветки (для нового пользователя ветки может ещё не быть — бэкапить нечего).
// Если экспорт СУЩЕСТВУЮЩЕЙ ветки не удался — возвращаем false (добавление будет отменено).
static bool BackupBeforeAdd(HKEY root, const AddRequest& req, const std::wstring& dir, std::wstring& err) {
    std::wstring rr = RegRootName(root);
    auto exp = [&](const wchar_t* base, const wchar_t* loc, const wchar_t* file) -> bool {
        std::wstring sub = std::wstring(base) + L"\\" + loc + L"\\NameSpace";
        HKEY t;
        if (RegOpenKeyExW(root, sub.c_str(), 0, KEY_READ, &t) != ERROR_SUCCESS) return true; // ветки ещё нет — бэкапить нечего
        RegCloseKey(t);
        std::wstring e;
        if (!ExportKey(rr + L"\\" + sub, dir + file, e)) { err = e; return false; }
        return true;
    };
    if (req.hive == NsHive::HKLM) {
        if (req.toMyComputer) {
            if (!exp(kExplorer,    L"MyComputer", L"MyComputer_64.reg")) return false;
            if (!exp(kExplorerWow, L"MyComputer", L"MyComputer_32.reg")) return false;
        }
        if (req.toDesktop) {
            if (!exp(kExplorer,    L"Desktop",    L"Desktop_64.reg"))    return false;
            if (!exp(kExplorerWow, L"Desktop",    L"Desktop_32.reg"))    return false;
        }
    } else {
        if (req.toMyComputer && !exp(kExplorer, L"MyComputer", L"MyComputer_HKCU.reg")) return false;
        if (req.toDesktop    && !exp(kExplorer, L"Desktop",    L"Desktop_HKCU.reg"))    return false;
    }
    return true;
}

bool AddFolderShortcut(const AddRequest& req, std::wstring& newGuid, std::wstring& backupPath, std::wstring& err) {
    if (req.name.empty())   { err = L"Укажите имя узла."; return false; }
    if (req.target.empty()) { err = L"Укажите папку-цель."; return false; }
    if (!req.toMyComputer && !req.toDesktop) { err = L"Выберите хотя бы одно место (Этот компьютер / Дерево)."; return false; }

    // Цель должна быть реально существующей папкой — иначе получится «битый» узел.
    wchar_t tgt[MAX_PATH] = L"";
    if (!ExpandEnvironmentStringsW(req.target.c_str(), tgt, MAX_PATH)) lstrcpynW(tgt, req.target.c_str(), MAX_PATH);
    DWORD ta = GetFileAttributesW(tgt);
    if (ta == INVALID_FILE_ATTRIBUTES || !(ta & FILE_ATTRIBUTE_DIRECTORY)) {
        err = L"Папка-цель не найдена или это не папка:\n" + std::wstring(tgt); return false;
    }

    GUID g;
    if (FAILED(CoCreateGuid(&g))) { err = L"Не удалось сгенерировать GUID."; return false; }
    wchar_t gs[64] = L"";
    StringFromGUID2(g, gs, 64);
    newGuid = gs;

    HKEY root = (req.hive == NsHive::HKLM) ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;

    backupPath = MakeBackupDir();
    if (!BackupBeforeAdd(root, req, backupPath, err)) {
        err = L"Бэкап перед добавлением не создан — операция отменена: " + err;
        return false;
    }

    bool ok = WriteShortcut(root, L"SOFTWARE\\Classes", kExplorer, req, newGuid);
    if (req.hive == NsHive::HKLM)
        ok = WriteShortcut(root, L"SOFTWARE\\Wow6432Node\\Classes", kExplorerWow, req, newGuid) && ok;

    if (!ok) {
        // откат частично созданных ключей, чтобы не осталось «полуузла»
        DeleteKeyForced(root, L"SOFTWARE\\Classes\\CLSID\\" + newGuid);
        DeleteKeyForced(root, std::wstring(kExplorer) + L"\\MyComputer\\NameSpace\\" + newGuid);
        DeleteKeyForced(root, std::wstring(kExplorer) + L"\\Desktop\\NameSpace\\" + newGuid);
        if (req.hive == NsHive::HKLM) {
            DeleteKeyForced(root, L"SOFTWARE\\Wow6432Node\\Classes\\CLSID\\" + newGuid);
            DeleteKeyForced(root, std::wstring(kExplorerWow) + L"\\MyComputer\\NameSpace\\" + newGuid);
            DeleteKeyForced(root, std::wstring(kExplorerWow) + L"\\Desktop\\NameSpace\\" + newGuid);
        }
        err = L"Запись в реестр не удалась — изменения отменены (для HKLM нужны права администратора)."; return false;
    }
    return true;
}

static bool EnablePriv(LPCWSTR name);  // определён ниже

// Удалить ключ; при ACCESS_DENIED — сменить владельца и выдать доступ (для системных узлов).
// В обоих исходах (успех/неудача) пытаемся вернуть исходные DACL и владельца — иначе системный
// ключ останется с owner=Admins, что меняет его защиту.
static bool DeleteKeyForced(HKEY root, const std::wstring& sub) {
    if (RegDeleteTreeW(root, sub.c_str()) == ERROR_SUCCESS) return true;  // обычный путь
    std::wstring name = (root == HKEY_LOCAL_MACHINE ? std::wstring(L"MACHINE\\") : std::wstring(L"CURRENT_USER\\")) + sub;
    SID_IDENTIFIER_AUTHORITY nt = SECURITY_NT_AUTHORITY;
    PSID admins = nullptr;
    if (!AllocateAndInitializeSid(&nt, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0,0,0,0,0,0, &admins)) return false;
    PSECURITY_DESCRIPTOR sd = nullptr; PSID owner = nullptr; PACL dacl = nullptr;
    bool ok = false;
    if (GetNamedSecurityInfoW(&name[0], SE_REGISTRY_KEY, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
                              &owner, nullptr, &dacl, nullptr, &sd) == ERROR_SUCCESS) {
        if (SetNamedSecurityInfoW(&name[0], SE_REGISTRY_KEY, OWNER_SECURITY_INFORMATION, admins, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
            EXPLICIT_ACCESSW ea{};
            ea.grfAccessPermissions = KEY_ALL_ACCESS | DELETE;
            ea.grfAccessMode        = SET_ACCESS;
            ea.grfInheritance       = CONTAINER_INHERIT_ACE;
            ea.Trustee.TrusteeForm  = TRUSTEE_IS_SID;
            ea.Trustee.TrusteeType  = TRUSTEE_IS_GROUP;
            ea.Trustee.ptstrName    = (LPWSTR)admins;
            PACL newDacl = nullptr;
            if (SetEntriesInAclW(1, &ea, dacl, &newDacl) == ERROR_SUCCESS) {
                if (SetNamedSecurityInfoW(&name[0], SE_REGISTRY_KEY, DACL_SECURITY_INFORMATION, nullptr, nullptr, newDacl, nullptr) == ERROR_SUCCESS)
                    ok = (RegDeleteTreeW(root, sub.c_str()) == ERROR_SUCCESS);
                if (newDacl) LocalFree(newDacl);
            }
            // вернуть исходный DACL (пока владеем ключом) — и при успехе, и при неудаче удаления
            SetNamedSecurityInfoW(&name[0], SE_REGISTRY_KEY, DACL_SECURITY_INFORMATION,
                                  nullptr, nullptr, dacl, nullptr);
            // вернуть исходного владельца (TrustedInstaller) — нужна SeRestore; одна повторная попытка
            if (SetNamedSecurityInfoW(&name[0], SE_REGISTRY_KEY, OWNER_SECURITY_INFORMATION,
                                      owner, nullptr, nullptr, nullptr) != ERROR_SUCCESS) {
                EnablePriv(SE_RESTORE_NAME);
                SetNamedSecurityInfoW(&name[0], SE_REGISTRY_KEY, OWNER_SECURITY_INFORMATION,
                                      owner, nullptr, nullptr, nullptr);
            }
        }
        if (sd) LocalFree(sd);
    }
    if (admins) FreeSid(admins);
    return ok;
}

bool DeleteEntry(NsHive hive, NsLocation loc, const std::wstring& guid, std::wstring& backupPath, std::wstring& err) {
    HKEY root = (hive == NsHive::HKLM) ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;
    const wchar_t* locName = (loc == NsLocation::MyComputer) ? L"MyComputer" : L"Desktop";
    std::wstring rr = RegRootName(root);
    backupPath = MakeBackupDir();
    EnablePriv(SE_TAKE_OWNERSHIP_NAME);
    EnablePriv(SE_RESTORE_NAME);

    // Собрать реально существующие виды (64/32 или HKCU) этой записи.
    struct View { std::wstring nsLoc, keyFull, tag; };
    std::vector<View> views;
    auto consider = [&](const wchar_t* base, const wchar_t* tag){
        std::wstring nsLoc   = std::wstring(base) + L"\\" + locName + L"\\NameSpace";
        std::wstring keyFull = nsLoc + L"\\" + guid;
        HKEY t;
        if (RegOpenKeyExW(root, keyFull.c_str(), 0, KEY_READ, &t) != ERROR_SUCCESS) return; // нет в этом виде
        RegCloseKey(t);
        views.push_back({ nsLoc, keyFull, tag });
    };
    consider(kExplorer, (hive == NsHive::HKLM) ? L"_64" : L"_HKCU");
    if (hive == NsHive::HKLM) consider(kExplorerWow, L"_32");

    if (views.empty()) { err = L"Узел не найден в реестре."; return false; }

    // Сначала бэкап ВСЕХ затронутых веток. Без успешного бэкапа ничего не удаляем (UI обещает обратимость).
    for (const auto& v : views) {
        std::wstring e;
        if (!ExportKey(rr + L"\\" + v.nsLoc, backupPath + locName + v.tag + L".reg", e)) {
            err = L"Бэкап не создан — удаление отменено: " + e; return false;
        }
    }

    // Наш ли это folder-shortcut (Instance\CLSID == CLSID_FolderShortcut)? Только их CLSID подчищаем.
    std::wstring clsidSub = L"SOFTWARE\\Classes\\CLSID\\" + guid;
    bool ownShortcut = (lstrcmpiW(RegReadString(root, clsidSub + L"\\Instance", L"CLSID").c_str(),
                                  kFolderShortcutClsid) == 0);

    // Основную ветку (_64/_HKCU) удаляем строго; Wow6432Node-зеркало (_32) — best-effort:
    // 64-бит Проводник его не читает, а системное зеркалирование Classes может мешать удалению.
    // Так не остаётся «полусостояния» между двумя строгими ветками (строгая всегда одна).
    // НО: послабление для _32 действует ТОЛЬКО когда есть и строгая ветка. Если узел существует
    // лишь в 32-бит виде, его удаление строгое — иначе отрапортуем «успех» без фактического удаления.
    bool hasStrict = false;
    for (const auto& v : views) if (v.tag != std::wstring(L"_32")) { hasStrict = true; break; }
    bool ok = true;
    for (const auto& v : views) {
        if (DeleteKeyForced(root, v.keyFull)) continue;
        if (v.tag == std::wstring(L"_32") && hasStrict) continue; // зеркало при наличии строгой ветки — не критично
        ok = false;
    }
    if (!ok) { err = L"Не удалось удалить узел из основной ветки (нужны права администратора?)."; return false; }

    // Тот же GUID мог быть добавлен и в «Этот компьютер», и в дерево (общий CLSID). Если после
    // удаления он где-то ещё используется — CLSID не трогаем, иначе сломаем оставшийся узел.
    auto guidStillUsed = [&](){
        const wchar_t* bases[2] = { kExplorer, kExplorerWow };
        int nb = (hive == NsHive::HKLM) ? 2 : 1;
        const wchar_t* locs[2] = { L"MyComputer", L"Desktop" };
        for (int b = 0; b < nb; ++b) for (int l = 0; l < 2; ++l) {
            std::wstring p = std::wstring(bases[b]) + L"\\" + locs[l] + L"\\NameSpace\\" + guid;
            HKEY t;
            if (RegOpenKeyExW(root, p.c_str(), 0, KEY_READ, &t) == ERROR_SUCCESS) { RegCloseKey(t); return true; }
        }
        return false;
    };

    // Орфан-CLSID: убираем только для созданных приложением shortcut-узлов (системные не трогаем).
    // Удаляем строго после успешного бэкапа — правило «без .reg-бэкапа не удаляем». Орфан-CLSID без
    // NameSpace-записи в Проводнике невидим, поэтому при провале бэкапа безопаснее его оставить.
    if (ownShortcut && !guidStillUsed()) {
        std::wstring e;
        if (ExportKey(rr + L"\\" + clsidSub, backupPath + L"CLSID.reg", e))
            DeleteKeyForced(root, clsidSub);
        if (hive == NsHive::HKLM) {
            std::wstring wow = L"SOFTWARE\\Wow6432Node\\Classes\\CLSID\\" + guid;
            std::wstring e2;
            if (ExportKey(rr + L"\\" + wow, backupPath + L"CLSID_32.reg", e2))
                DeleteKeyForced(root, wow);
        }
    }
    return true;
}

static bool SaveTextFileUtf16(const std::wstring& path, const std::wstring& text) {
    HANDLE hf = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hf == INVALID_HANDLE_VALUE) return false;
    const unsigned char bom[2] = { 0xFF, 0xFE };
    DWORD wr = 0;
    DWORD cb = (DWORD)(text.size() * sizeof(wchar_t));
    // Это .reg-бэкап — нельзя считать его созданным, если запись прошла лишь частично (диск полон,
    // обрыв). Проверяем каждый WriteFile и сбрасываем на диск; при сбое битый файл удаляем, чтобы он
    // не выглядел валидным бэкапом, а вызывающий код отменил операцию.
    bool ok = (WriteFile(hf, bom, 2, &wr, nullptr) && wr == 2);
    if (ok) ok = (WriteFile(hf, text.c_str(), cb, &wr, nullptr) && wr == cb);
    if (ok) ok = (FlushFileBuffers(hf) != FALSE);
    CloseHandle(hf);
    if (!ok) DeleteFileW(path.c_str());
    return ok;
}

static bool BackupKeyState(HKEY root, const std::wstring& sub, const std::wstring& file,
                           const std::wstring& dir, std::wstring& err) {
    if (KeyExists(root, sub)) {
        std::wstring e;
        if (!ExportKey(RegRootName(root) + L"\\" + sub, dir + file, e)) {
            err = L"Бэкап не создан — операция отменена: " + e;
            return false;
        }
        return true;
    }

    std::wstring reg = L"Windows Registry Editor Version 5.00\r\n\r\n[-";
    reg += RegRootName(root);
    reg += L"\\";
    reg += sub;
    reg += L"]\r\n";
    if (!SaveTextFileUtf16(dir + file, reg)) {
        err = L"Не удалось сохранить бэкап " + file + L" — операция отменена.";
        return false;
    }
    return true;
}

static bool EnablePriv(LPCWSTR name) {
    HANDLE tok = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &tok)) return false;
    LUID luid; bool ok = false;
    if (LookupPrivilegeValueW(nullptr, name, &luid)) {
        TOKEN_PRIVILEGES tp{};
        tp.PrivilegeCount = 1;
        tp.Privileges[0].Luid = luid;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        AdjustTokenPrivileges(tok, FALSE, &tp, sizeof(tp), nullptr, nullptr);
        ok = (GetLastError() == ERROR_SUCCESS);
    }
    CloseHandle(tok);
    return ok;
}

// Результат записи в защищённый HKLM-ключ:
//   Ok               — значение записано, исходные DACL и владелец возвращены;
//   WroteNotRestored — значение записано, но вернуть DACL/владельца НЕ удалось (ключ остался
//                      с владельцем Administrators — нужно предупредить пользователя, не повторять);
//   NotWrote         — значение не записано (можно безопасно повторить).
enum class ProtWrite { Ok, WroteNotRestored, NotWrote };

// Запись DWORD в защищённый HKLM-ключ: взять владение у TrustedInstaller, выдать запись
// Administrators, записать, затем ВЕРНУТЬ исходные DACL и владельца. Возврат различает три
// исхода (см. ProtWrite) — нельзя путать «не записано» с «записано, но права не восстановлены».
static ProtWrite WriteProtectedDword(const std::wstring& sub, const wchar_t* valueName, DWORD value) {
    std::wstring name = L"MACHINE\\" + sub;
    SID_IDENTIFIER_AUTHORITY nt = SECURITY_NT_AUTHORITY;
    PSID admins = nullptr;
    if (!AllocateAndInitializeSid(&nt, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0,0,0,0,0,0, &admins))
        return ProtWrite::NotWrote; // SID администраторов не выделен — ничего не трогали, можно повторить

    PSECURITY_DESCRIPTOR sd = nullptr;
    PSID  origOwner = nullptr;
    PACL  origDacl  = nullptr;
    bool wrote     = false;  // значение записано
    bool restoreOk = true;   // исходные DACL и владелец возвращены
    if (GetNamedSecurityInfoW(&name[0], SE_REGISTRY_KEY,
            OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
            &origOwner, nullptr, &origDacl, nullptr, &sd) == ERROR_SUCCESS) {
        if (SetNamedSecurityInfoW(&name[0], SE_REGISTRY_KEY, OWNER_SECURITY_INFORMATION,
                                  admins, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
            EXPLICIT_ACCESSW ea{};
            ea.grfAccessPermissions = KEY_SET_VALUE | KEY_QUERY_VALUE;
            ea.grfAccessMode        = SET_ACCESS;
            ea.grfInheritance       = NO_INHERITANCE;
            ea.Trustee.TrusteeForm  = TRUSTEE_IS_SID;
            ea.Trustee.TrusteeType  = TRUSTEE_IS_GROUP;
            ea.Trustee.ptstrName    = (LPWSTR)admins;
            PACL newDacl = nullptr;
            if (SetEntriesInAclW(1, &ea, origDacl, &newDacl) == ERROR_SUCCESS) {
                if (SetNamedSecurityInfoW(&name[0], SE_REGISTRY_KEY, DACL_SECURITY_INFORMATION,
                                          nullptr, nullptr, newDacl, nullptr) == ERROR_SUCCESS) {
                    HKEY k;
                    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, sub.c_str(), 0, KEY_SET_VALUE, &k) == ERROR_SUCCESS) {
                        wrote = (RegSetValueExW(k, valueName, 0, REG_DWORD,
                                                (const BYTE*)&value, sizeof(value)) == ERROR_SUCCESS);
                        RegCloseKey(k);
                    }
                }
                // вернуть исходный DACL (пока владеем ключом) — результат проверяем
                if (SetNamedSecurityInfoW(&name[0], SE_REGISTRY_KEY, DACL_SECURITY_INFORMATION,
                                          nullptr, nullptr, origDacl, nullptr) != ERROR_SUCCESS)
                    restoreOk = false;
                if (newDacl) LocalFree(newDacl);
            } else {
                restoreOk = false; // не удалось собрать временный DACL
            }
            // вернуть исходного владельца (нужен SeRestore); одна повторная попытка
            if (SetNamedSecurityInfoW(&name[0], SE_REGISTRY_KEY, OWNER_SECURITY_INFORMATION,
                                      origOwner, nullptr, nullptr, nullptr) != ERROR_SUCCESS) {
                EnablePriv(SE_RESTORE_NAME);
                if (SetNamedSecurityInfoW(&name[0], SE_REGISTRY_KEY, OWNER_SECURITY_INFORMATION,
                                          origOwner, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
                    restoreOk = false;
            }
        }
        if (sd) LocalFree(sd);
    }
    if (admins) FreeSid(admins);
    if (!wrote) return ProtWrite::NotWrote;
    return restoreOk ? ProtWrite::Ok : ProtWrite::WroteNotRestored;
}

bool SetSortOrder(const std::vector<SortItem>& items, std::wstring& backupPath, std::wstring& err) {
    if (items.empty()) { err = L"Нет узлов для сортировки."; return false; }
    backupPath = MakeBackupDir();

    // документирующий бэкап текущих значений
    std::wstring reg = L"Windows Registry Editor Version 5.00\r\n\r\n";
    auto emitB = [&](const wchar_t* rn, HKEY root, const std::wstring& sub){
        DWORD cur = 0; bool had = RegReadDword(root, sub, L"SortOrderIndex", cur);
        reg += L"["; reg += rn; reg += L"\\"; reg += sub; reg += L"]\r\n";
        if (had) { wchar_t hx[16]; swprintf_s(hx, L"%08lx", cur); reg += L"\"SortOrderIndex\"=dword:"; reg += hx; reg += L"\r\n"; }
        else     { reg += L"\"SortOrderIndex\"=-\r\n"; }
        reg += L"\r\n";
    };
    for (const auto& it : items) {
        if (it.hive == NsHive::HKCU) emitB(L"HKEY_CURRENT_USER", HKEY_CURRENT_USER, L"SOFTWARE\\Classes\\CLSID\\" + it.guid);
        else { emitB(L"HKEY_LOCAL_MACHINE", HKEY_LOCAL_MACHINE, L"SOFTWARE\\Classes\\CLSID\\" + it.guid);
               emitB(L"HKEY_LOCAL_MACHINE", HKEY_LOCAL_MACHINE, L"SOFTWARE\\Wow6432Node\\Classes\\CLSID\\" + it.guid); }
    }
    if (!SaveTextFileUtf16(backupPath + L"sort_before.reg", reg)) {
        err = L"Не удалось сохранить бэкап sort_before.reg — сортировка отменена."; return false;
    }

    EnablePriv(SE_TAKE_OWNERSHIP_NAME);
    EnablePriv(SE_RESTORE_NAME);

    bool ok = true;
    std::wstring aclWarn;  // ключи, где значение записано, но исходные права/владельца вернуть не удалось
    for (size_t i = 0; i < items.size(); ++i) {
        const auto& it = items[i];
        DWORD val = (DWORD)(i + 1) * 10;
        std::wstring primary = L"SOFTWARE\\Classes\\CLSID\\" + it.guid;
        if (it.hive == NsHive::HKCU) {
            DWORD cur = 0; bool had = RegReadDword(HKEY_CURRENT_USER, primary, L"SortOrderIndex", cur);
            if (had && cur == val) continue;
            if (!WriteDw(HKEY_CURRENT_USER, primary, L"SortOrderIndex", val)) ok = false;
        } else {
            // основной 64-бит ключ — строго
            DWORD cur = 0; bool had = RegReadDword(HKEY_LOCAL_MACHINE, primary, L"SortOrderIndex", cur);
            if (!(had && cur == val)) {
                ProtWrite r = WriteProtectedDword(primary, L"SortOrderIndex", val);
                if (r == ProtWrite::NotWrote) ok = false;
                else if (r == ProtWrite::WroteNotRestored) aclWarn += L"  HKLM\\" + primary + L"\n";
            }
            // Wow6432Node-зеркало пишется всегда (не пропускается из-за continue основного),
            // но best-effort: 64-бит Проводник его не читает, провал записи не валит сортировку.
            std::wstring wow = L"SOFTWARE\\Wow6432Node\\Classes\\CLSID\\" + it.guid;
            DWORD curW = 0; bool hadW = RegReadDword(HKEY_LOCAL_MACHINE, wow, L"SortOrderIndex", curW);
            if (!(hadW && curW == val)) {
                ProtWrite rw = WriteProtectedDword(wow, L"SortOrderIndex", val);
                if (rw == ProtWrite::WroteNotRestored) aclWarn += L"  HKLM\\" + wow + L"\n";
            }
        }
    }
    if (!ok) {
        err = L"Не все узлы удалось записать (смена владельца не прошла?).";
        if (!aclWarn.empty()) // не терять предупреждение про уже записанные ключи с невосстановленными правами
            err += L"\n\nКроме того, у части записанных узлов не восстановлены исходные права/владельца:\n" + aclWarn;
        return false;
    }
    if (!aclWarn.empty())
        err = L"Порядок применён, но у части ключей не удалось восстановить исходные права/владельца. "
              L"Не повторяйте операцию; проверьте ACL вручную:\n" + aclWarn;
    return true;
}

bool GetNavAllFolders() {
    DWORD v = 0;
    RegReadDword(HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced",
                 L"NavPaneShowAllFolders", v);
    return v != 0;
}
// Документирующий бэкап одного значения (или его отсутствия) перед правкой Advanced-флага.
static std::wstring RegDwordBackupLine(const wchar_t* rootName, const std::wstring& sub,
                                       const wchar_t* valueName, HKEY root) {
    DWORD cur = 0; bool had = RegReadDword(root, sub, valueName, cur);
    std::wstring s = L"["; s += rootName; s += L"\\"; s += sub; s += L"]\r\n\"";
    s += valueName; s += L"\"=";
    if (had) { wchar_t hx[16]; swprintf_s(hx, L"%08lx", cur); s += L"dword:"; s += hx; }
    else     { s += L"-"; }
    s += L"\r\n\r\n";
    return s;
}

bool SetNavAllFolders(bool on) {
    std::wstring sub = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced";
    std::wstring dir = MakeBackupDir();
    std::wstring reg = L"Windows Registry Editor Version 5.00\r\n\r\n"
                     + RegDwordBackupLine(L"HKEY_CURRENT_USER", sub, L"NavPaneShowAllFolders", HKEY_CURRENT_USER);
    if (!SaveTextFileUtf16(dir + L"navallfolders_before.reg", reg)) return false; // нет бэкапа — не пишем
    return WriteDw(HKEY_CURRENT_USER, sub, L"NavPaneShowAllFolders", on ? 1u : 0u);
}

bool SetHubMode(bool hide) {
    std::wstring subU = std::wstring(kExplorer) + L"\\Advanced";
    std::wstring dir = MakeBackupDir();
    std::wstring reg = L"Windows Registry Editor Version 5.00\r\n\r\n"
                     + RegDwordBackupLine(L"HKEY_CURRENT_USER",  subU,      L"HubMode", HKEY_CURRENT_USER)
                     + RegDwordBackupLine(L"HKEY_LOCAL_MACHINE", kExplorer, L"HubMode", HKEY_LOCAL_MACHINE);
    if (!SaveTextFileUtf16(dir + L"hubmode_before.reg", reg)) return false; // нет бэкапа — не пишем
    DWORD v = hide ? 1u : 0u;
    bool ok = WriteDw(HKEY_CURRENT_USER, subU, L"HubMode", v);
    WriteDw(HKEY_LOCAL_MACHINE, kExplorer, L"HubMode", v); // машинно (если есть права)
    return ok;
}

bool SetNavTreeHidden(const std::wstring& guid, bool hidden, std::wstring& backupPath, std::wstring& err) {
    std::wstring sub    = L"SOFTWARE\\Classes\\CLSID\\" + guid;
    std::wstring wowSub = L"SOFTWARE\\Wow6432Node\\Classes\\CLSID\\" + guid;
    DWORD curL = 0, curU = 0, curW = 0;
    bool hadL = RegReadDword(HKEY_LOCAL_MACHINE, sub, L"System.IsPinnedToNameSpaceTree", curL);
    bool hadU = RegReadDword(HKEY_CURRENT_USER,  sub, L"System.IsPinnedToNameSpaceTree", curU);
    // 32-бит зеркало (Wow6432Node) трогаем только если ключ реально существует — обычно его нет,
    // и плодить пустой ключ (а с ним и проблему отката) незачем.
    HKEY wk; bool wowExists = (RegOpenKeyExW(HKEY_LOCAL_MACHINE, wowSub.c_str(), 0, KEY_READ, &wk) == ERROR_SUCCESS);
    if (wowExists) RegCloseKey(wk);
    bool hadW = wowExists && RegReadDword(HKEY_LOCAL_MACHINE, wowSub, L"System.IsPinnedToNameSpaceTree", curW);

    backupPath = MakeBackupDir();
    std::wstring reg = L"Windows Registry Editor Version 5.00\r\n\r\n";
    reg += L"[HKEY_LOCAL_MACHINE\\" + sub + L"]\r\n";
    if (hadL) { wchar_t hx[16]; swprintf_s(hx, L"%08lx", curL); reg += L"\"System.IsPinnedToNameSpaceTree\"=dword:"; reg += hx; reg += L"\r\n"; }
    else      { reg += L"\"System.IsPinnedToNameSpaceTree\"=-\r\n"; }
    reg += L"\r\n[HKEY_CURRENT_USER\\" + sub + L"]\r\n";
    if (hadU) { wchar_t hx[16]; swprintf_s(hx, L"%08lx", curU); reg += L"\"System.IsPinnedToNameSpaceTree\"=dword:"; reg += hx; reg += L"\r\n"; }
    else      { reg += L"\"System.IsPinnedToNameSpaceTree\"=-\r\n"; }
    if (wowExists) {
        reg += L"\r\n[HKEY_LOCAL_MACHINE\\" + wowSub + L"]\r\n";
        if (hadW) { wchar_t hx[16]; swprintf_s(hx, L"%08lx", curW); reg += L"\"System.IsPinnedToNameSpaceTree\"=dword:"; reg += hx; reg += L"\r\n"; }
        else      { reg += L"\"System.IsPinnedToNameSpaceTree\"=-\r\n"; }
    }
    if (!SaveTextFileUtf16(backupPath + L"navtree_before.reg", reg)) {
        err = L"Не удалось сохранить бэкап navtree_before.reg — операция отменена."; return false;
    }

    EnablePriv(SE_TAKE_OWNERSHIP_NAME);
    EnablePriv(SE_RESTORE_NAME);
    DWORD val = hidden ? 0u : 1u;
    ProtWrite r = WriteProtectedDword(sub, L"System.IsPinnedToNameSpaceTree", val); // HKLM (смена владельца) — его читает Проводник
    // Если строгая HKLM-запись не прошла — выходим до HKCU-зеркала: его Проводник видит через
    // HKCR-merge, и записывать его при отказе значило бы оставить тихое частичное состояние.
    if (r == ProtWrite::NotWrote) { err = L"Не удалось записать (смена владельца не прошла?)."; return false; }
    WriteDw(HKEY_CURRENT_USER, sub, L"System.IsPinnedToNameSpaceTree", val);        // HKCU заодно

    // 32-бит зеркало — best-effort, только для уже существующего ключа (он зафиксирован в бэкапе выше).
    ProtWrite rw = wowExists ? WriteProtectedDword(wowSub, L"System.IsPinnedToNameSpaceTree", val)
                             : ProtWrite::NotWrote;

    if (r == ProtWrite::WroteNotRestored || rw == ProtWrite::WroteNotRestored) {
        err = L"Изменение применено, но восстановить исходные права/владельца ключа не удалось. "
              L"Не повторяйте операцию; проверьте ACL вручную:\n  HKLM\\" + sub;
        if (rw == ProtWrite::WroteNotRestored) err += L"\n  HKLM\\" + wowSub;
    }
    return true;
}

// Создать подключ под защищённым родителем (при отказе — сменить владельца родителя, выдать доступ).
static bool CreateKeyForced(HKEY root, const std::wstring& parentSub, const std::wstring& child, const std::wstring& defVal) {
    std::wstring full = parentSub + L"\\" + child;
    HKEY k; DWORD disp;
    if (RegCreateKeyExW(root, full.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &k, &disp) == ERROR_SUCCESS) {
        if (!defVal.empty()) RegSetValueExW(k, nullptr, 0, REG_SZ, (const BYTE*)defVal.c_str(), (DWORD)((defVal.size()+1)*sizeof(wchar_t)));
        RegCloseKey(k);
        return true;
    }
    std::wstring pname = (root == HKEY_LOCAL_MACHINE ? std::wstring(L"MACHINE\\") : std::wstring(L"CURRENT_USER\\")) + parentSub;
    SID_IDENTIFIER_AUTHORITY nt = SECURITY_NT_AUTHORITY;
    PSID admins = nullptr;
    if (!AllocateAndInitializeSid(&nt, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0,0,0,0,0,0, &admins)) return false;
    PSECURITY_DESCRIPTOR sd = nullptr; PSID owner = nullptr; PACL dacl = nullptr;
    bool ok = false;
    if (GetNamedSecurityInfoW(&pname[0], SE_REGISTRY_KEY, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
                              &owner, nullptr, &dacl, nullptr, &sd) == ERROR_SUCCESS) {
        if (SetNamedSecurityInfoW(&pname[0], SE_REGISTRY_KEY, OWNER_SECURITY_INFORMATION, admins, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
            EXPLICIT_ACCESSW ea{};
            ea.grfAccessPermissions = KEY_WRITE | KEY_CREATE_SUB_KEY;
            ea.grfAccessMode        = SET_ACCESS;
            // NO_INHERITANCE: доступ нужен только самому родителю (создать подключ), наследовать
            // временный admins-ACE в создаваемый дочерний ключ нельзя — он бы там и остался,
            // ослабив защиту системной ветки после восстановления DACL родителя.
            ea.grfInheritance       = NO_INHERITANCE;
            ea.Trustee.TrusteeForm  = TRUSTEE_IS_SID;
            ea.Trustee.TrusteeType  = TRUSTEE_IS_GROUP;
            ea.Trustee.ptstrName    = (LPWSTR)admins;
            PACL newDacl = nullptr;
            if (SetEntriesInAclW(1, &ea, dacl, &newDacl) == ERROR_SUCCESS) {
                if (SetNamedSecurityInfoW(&pname[0], SE_REGISTRY_KEY, DACL_SECURITY_INFORMATION, nullptr, nullptr, newDacl, nullptr) == ERROR_SUCCESS) {
                    if (RegCreateKeyExW(root, full.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &k, &disp) == ERROR_SUCCESS) {
                        if (!defVal.empty()) RegSetValueExW(k, nullptr, 0, REG_SZ, (const BYTE*)defVal.c_str(), (DWORD)((defVal.size()+1)*sizeof(wchar_t)));
                        RegCloseKey(k);
                        ok = true;
                    }
                }
                // вернуть исходный DACL родителя — пока мы ещё владельцы ключа; результат проверяем
                if (SetNamedSecurityInfoW(&pname[0], SE_REGISTRY_KEY, DACL_SECURITY_INFORMATION,
                                          nullptr, nullptr, dacl, nullptr) != ERROR_SUCCESS) {
                    // DACL не восстановлен — оставляем ключ под нашим DACL (он пускает админа), на работу не влияет
                }
                if (newDacl) LocalFree(newDacl);
            }
            // вернуть исходного владельца (TrustedInstaller) — нужна SeRestore; одна повторная попытка
            if (SetNamedSecurityInfoW(&pname[0], SE_REGISTRY_KEY, OWNER_SECURITY_INFORMATION,
                                      owner, nullptr, nullptr, nullptr) != ERROR_SUCCESS) {
                EnablePriv(SE_RESTORE_NAME);
                SetNamedSecurityInfoW(&pname[0], SE_REGISTRY_KEY, OWNER_SECURITY_INFORMATION,
                                      owner, nullptr, nullptr, nullptr);
            }
        }
        if (sd) LocalFree(sd);
    }
    if (admins) FreeSid(admins);
    return ok;
}

static const wchar_t* kHomeGuid = L"{f874310e-b6b7-47dc-bc84-b9e6b38f5903}"; // «Главная» (Home)

bool GetHomeHidden() {
    std::wstring sub = std::wstring(kExplorer) + L"\\Desktop\\NameSpace\\" + kHomeGuid;
    HKEY k;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, sub.c_str(), 0, KEY_READ, &k) == ERROR_SUCCESS) { RegCloseKey(k); return false; }
    return true; // записи нет -> скрыт
}

bool SetHomeHidden(bool hidden, std::wstring& backupPath, std::wstring& err) {
    backupPath = MakeBackupDir();
    EnablePriv(SE_TAKE_OWNERSHIP_NAME);
    EnablePriv(SE_RESTORE_NAME);

    std::wstring e2;
    if (!ExportKey(L"HKEY_LOCAL_MACHINE\\" + std::wstring(kExplorer) + L"\\Desktop\\NameSpace", backupPath + L"Desktop_64.reg", e2)) {
        err = L"Бэкап не создан — операция отменена: " + e2; return false;
    }
    bool back32 = ExportKey(L"HKEY_LOCAL_MACHINE\\" + std::wstring(kExplorerWow) + L"\\Desktop\\NameSpace", backupPath + L"Desktop_32.reg", e2); // 32-бит — best-effort

    std::wstring ns64 = std::wstring(kExplorer)    + L"\\Desktop\\NameSpace";
    std::wstring ns32 = std::wstring(kExplorerWow) + L"\\Desktop\\NameSpace";
    if (hidden) {
        DeleteKeyForced(HKEY_LOCAL_MACHINE, ns64 + L"\\" + kHomeGuid);
        if (back32) DeleteKeyForced(HKEY_LOCAL_MACHINE, ns32 + L"\\" + kHomeGuid); // 32-бит — best-effort, только при успешном бэкапе
        // Перепроверяем факт: 64-бит запись (её читает Проводник) не должна больше существовать.
        HKEY t;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, (ns64 + L"\\" + kHomeGuid).c_str(), 0, KEY_READ, &t) == ERROR_SUCCESS) {
            RegCloseKey(t);
            err = L"Не удалось скрыть «Главную» (запись не удалена — нужны права администратора?)."; return false;
        }
    } else {
        if (!CreateKeyForced(HKEY_LOCAL_MACHINE, ns64, kHomeGuid, L"CLSID_MSGraphHomeFolder")) {
            err = L"Не удалось восстановить запись «Главная»."; return false;
        }
        CreateKeyForced(HKEY_LOCAL_MACHINE, ns32, kHomeGuid, L"CLSID_MSGraphHomeFolder"); // 32-бит — best-effort
    }
    return true;
}

static const wchar_t* kQuickAccessDelegateGuid = L"{3936E9E4-D92C-4EEE-A85A-BC16D5EA0819}";
static const wchar_t* kQAShellFolder = L"SOFTWARE\\Classes\\CLSID\\{679f85cb-0220-4080-b29b-5540cc05aab6}\\ShellFolder";

static std::wstring QuickAccessNavDelegateSub(const wchar_t* explorerBase) {
    return std::wstring(explorerBase) + L"\\Desktop\\NameSpace\\DelegateFolders\\" + kQuickAccessDelegateGuid;
}

static std::wstring QuickAccessHomeDelegateSub() {
    return std::wstring(kExplorer) + L"\\HomeFolderMSGraph\\NameSpace\\DelegateFolders\\" + kQuickAccessDelegateGuid;
}

static bool HasQuickAccessNavDelegateLayout() {
    return KeyExists(HKEY_LOCAL_MACHINE, std::wstring(kExplorer) + L"\\Desktop\\NameSpace\\DelegateFolders")
        || KeyExists(HKEY_LOCAL_MACHINE, QuickAccessNavDelegateSub(kExplorer))
        || KeyExists(HKEY_LOCAL_MACHINE, QuickAccessNavDelegateSub(kExplorerWow));
}

static bool HasQuickAccessHomeDelegateLayout() {
    return KeyExists(HKEY_LOCAL_MACHINE, std::wstring(kExplorer) + L"\\HomeFolderMSGraph\\NameSpace\\DelegateFolders")
        || KeyExists(HKEY_LOCAL_MACHINE, QuickAccessHomeDelegateSub());
}

static bool SetDelegateKeyVisible(const std::wstring& sub, const wchar_t* defVal, bool visible,
                                  bool strict, std::wstring& err) {
    if (visible) {
        size_t p = sub.find_last_of(L'\\');
        if (p == std::wstring::npos) return false;
        std::wstring parent = sub.substr(0, p);
        std::wstring child = sub.substr(p + 1);
        if (!CreateKeyForced(HKEY_LOCAL_MACHINE, parent, child, defVal) && strict) {
            err = L"Не удалось восстановить ключ:\n  HKLM\\" + sub;
            return false;
        }
        return true;
    }

    if (!KeyExists(HKEY_LOCAL_MACHINE, sub)) return true;
    if (!DeleteKeyForced(HKEY_LOCAL_MACHINE, sub) && strict) {
        err = L"Не удалось удалить ключ:\n  HKLM\\" + sub;
        return false;
    }
    return true;
}

static bool BackupQuickAccessNavDelegates(const std::wstring& backupPath, std::wstring& err) {
    if (!HasQuickAccessNavDelegateLayout()) return true;
    if (!BackupKeyState(HKEY_LOCAL_MACHINE, QuickAccessNavDelegateSub(kExplorer),    L"qa_nav_delegate_64.reg", backupPath, err)) return false;
    if (!BackupKeyState(HKEY_LOCAL_MACHINE, QuickAccessNavDelegateSub(kExplorerWow), L"qa_nav_delegate_32.reg", backupPath, err)) return false;
    return true;
}

static bool SetQuickAccessNavDelegatesVisible(bool visible, std::wstring& err) {
    if (!HasQuickAccessNavDelegateLayout()) return true;
    bool ok = SetDelegateKeyVisible(QuickAccessNavDelegateSub(kExplorer), L"CLSID_FrequentPlacesFolder", visible, true, err);
    std::wstring ignored;
    SetDelegateKeyVisible(QuickAccessNavDelegateSub(kExplorerWow), L"CLSID_FrequentPlacesFolder", visible, false, ignored);
    return ok;
}

static bool BackupQuickAccessHomeDelegate(const std::wstring& backupPath, std::wstring& err) {
    if (!HasQuickAccessHomeDelegateLayout()) return true;
    return BackupKeyState(HKEY_LOCAL_MACHINE, QuickAccessHomeDelegateSub(), L"qa_home_delegate.reg", backupPath, err);
}

static bool SetQuickAccessHomeDelegateVisible(bool visible, std::wstring& err) {
    if (!HasQuickAccessHomeDelegateLayout()) return true;
    return SetDelegateKeyVisible(QuickAccessHomeDelegateSub(), L"Frequent Places Folder", visible, true, err);
}

static bool BackupQuickAccessFile(const std::wstring& src, const std::wstring& dst, std::wstring& err) {
    DWORD attr = GetFileAttributesW(src.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) return true;
    if (!CopyFileW(src.c_str(), dst.c_str(), FALSE)) {
        err = L"Не удалось сохранить бэкап файла:\n" + src;
        return false;
    }
    return true;
}

static bool DeleteQuickAccessFile(const std::wstring& src, std::wstring& err) {
    if (GetFileAttributesW(src.c_str()) == INVALID_FILE_ATTRIBUTES) return true;
    if (!DeleteFileW(src.c_str()) && GetFileAttributesW(src.c_str()) != INVALID_FILE_ATTRIBUTES) {
        err = L"Не удалось удалить файл истории Quick Access:\n" + src;
        return false;
    }
    return true;
}

static bool WriteZeroStream(IStorage* stg, const wchar_t* name, ULONG size) {
    IStream* stm = nullptr;
    HRESULT hr = stg->CreateStream(name, STGM_CREATE | STGM_READWRITE | STGM_SHARE_EXCLUSIVE, 0, 0, &stm);
    if (FAILED(hr) || !stm) return false;

    std::vector<BYTE> zero(size, 0);
    ULONG written = 0;
    hr = stm->Write(zero.data(), size, &written);
    if (SUCCEEDED(hr)) hr = stm->Commit(STGC_DEFAULT);
    stm->Release();
    return SUCCEEDED(hr) && written == size;
}

static bool WriteEmptyQuickAccessFile(const std::wstring& path, std::wstring& err) {
    std::wstring tmp = path + L".tmp";
    DeleteFileW(tmp.c_str());

    IStorage* stg = nullptr;
    HRESULT hr = StgCreateDocfile(tmp.c_str(),
        STGM_CREATE | STGM_READWRITE | STGM_SHARE_EXCLUSIVE, 0, &stg);
    if (FAILED(hr) || !stg) {
        err = L"Не удалось создать пустой файл Quick Access.";
        DeleteFileW(tmp.c_str());
        return false;
    }

    bool ok = WriteZeroStream(stg, L"1", 427)
           && WriteZeroStream(stg, L"2", 429)
           && WriteZeroStream(stg, L"3", 429)
           && WriteZeroStream(stg, L"DestListPropertyStore", 4)
           && SUCCEEDED(stg->Commit(STGC_DEFAULT));
    stg->Release();

    if (!ok) {
        err = L"Не удалось записать пустой файл Quick Access.";
        DeleteFileW(tmp.c_str());
        return false;
    }
    if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        err = L"Не удалось заменить файл Quick Access:\n" + path;
        DeleteFileW(tmp.c_str());
        return false;
    }
    return true;
}

bool ClearQuickAccess(std::wstring& backupPath, std::wstring& err) {
    backupPath = MakeBackupDir();

    wchar_t appdata[MAX_PATH] = L"";
    if (!GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH)) {
        err = L"Не удалось определить APPDATA для текущего пользователя.";
        return false;
    }

    std::wstring dir = std::wstring(appdata) + L"\\Microsoft\\Windows\\Recent\\AutomaticDestinations\\";
    std::wstring quick = dir + L"f01b4d95cf55d32a.automaticDestinations-ms";
    std::wstring frequent = dir + L"5f7b5f1e01b83767.automaticDestinations-ms";

    if (!BackupQuickAccessFile(quick, backupPath + L"f01b4d95cf55d32a.automaticDestinations-ms", err)) return false;
    if (!BackupQuickAccessFile(frequent, backupPath + L"5f7b5f1e01b83767.automaticDestinations-ms", err)) return false;
    if (!WriteEmptyQuickAccessFile(quick, err)) return false;
    if (!DeleteQuickAccessFile(frequent, err)) return false;
    return true; // даже если откреплять было нечего — это не ошибка
}

bool RestoreQuickAccessDefaults(std::wstring& backupPath, std::wstring& err) {
    backupPath = MakeBackupDir();

    wchar_t appdata[MAX_PATH] = L"";
    if (!GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH)) {
        err = L"Не удалось определить APPDATA для текущего пользователя.";
        return false;
    }

    std::wstring dir = std::wstring(appdata) + L"\\Microsoft\\Windows\\Recent\\AutomaticDestinations\\";
    std::wstring quick = dir + L"f01b4d95cf55d32a.automaticDestinations-ms";
    std::wstring frequent = dir + L"5f7b5f1e01b83767.automaticDestinations-ms";

    if (!BackupQuickAccessFile(quick, backupPath + L"f01b4d95cf55d32a.automaticDestinations-ms", err)) return false;
    if (!BackupQuickAccessFile(frequent, backupPath + L"5f7b5f1e01b83767.automaticDestinations-ms", err)) return false;
    if (!DeleteQuickAccessFile(quick, err)) return false;
    if (!DeleteQuickAccessFile(frequent, err)) return false;
    return true;
}

bool GetQuickAccessHidden() {
    DWORD a = 0;
    if (RegReadDword(HKEY_LOCAL_MACHINE, kQAShellFolder, L"Attributes", a) && (a & 0x00400000u) != 0) return true;
    if (HasQuickAccessNavDelegateLayout() && !KeyExists(HKEY_LOCAL_MACHINE, QuickAccessNavDelegateSub(kExplorer))) return true;
    return false;
}

static bool SetQuickAccessHiddenCore(bool hidden, std::wstring& backupPath, std::wstring& err) {
    DWORD cur = 0;
    bool had = RegReadDword(HKEY_LOCAL_MACHINE, kQAShellFolder, L"Attributes", cur);
    if (!had) cur = 0xa0000000u;
    DWORD val = hidden ? (cur | 0x00600000u) : (cur & ~0x00600000u);

    std::wstring reg = L"Windows Registry Editor Version 5.00\r\n\r\n";
    wchar_t hx[16]; swprintf_s(hx, L"%08lx", cur);
    reg += L"[HKEY_LOCAL_MACHINE\\"; reg += kQAShellFolder; reg += L"]\r\n\"Attributes\"=dword:"; reg += hx; reg += L"\r\n\r\n";
    // HKCU-зеркало этого же атрибута тоже перезаписывается ниже — фиксируем и его исходное значение.
    reg += RegDwordBackupLine(L"HKEY_CURRENT_USER", kQAShellFolder, L"Attributes", HKEY_CURRENT_USER);
    if (!SaveTextFileUtf16(backupPath + L"qa_attr_before.reg", reg)) {
        err = L"Не удалось сохранить бэкап qa_attr_before.reg — операция отменена."; return false;
    }
    if (!BackupQuickAccessNavDelegates(backupPath, err)) return false;

    EnablePriv(SE_TAKE_OWNERSHIP_NAME);
    EnablePriv(SE_RESTORE_NAME);
    ProtWrite r = KeyExists(HKEY_LOCAL_MACHINE, kQAShellFolder)
        ? WriteProtectedDword(kQAShellFolder, L"Attributes", val)          // HKLM (смена владельца)
        : ProtWrite::Ok;
    WriteDw(HKEY_CURRENT_USER, kQAShellFolder, L"Attributes", val);
    if (!SetQuickAccessNavDelegatesVisible(!hidden, err)) return false;
    if (r == ProtWrite::NotWrote) { err = L"Не удалось записать атрибуты Quick Access (смена владельца не прошла?)."; return false; }
    if (r == ProtWrite::WroteNotRestored)
        err = std::wstring(L"Атрибут применён, но восстановить исходные права/владельца ключа не удалось. "
              L"Не повторяйте операцию; проверьте ACL вручную:\n  HKLM\\") + kQAShellFolder;
    return true;
}

bool SetQuickAccessHidden(bool hidden, std::wstring& backupPath, std::wstring& err) {
    backupPath = MakeBackupDir();
    return SetQuickAccessHiddenCore(hidden, backupPath, err);
}

// ---- Полное выключение «Быстрого доступа» ----
// Скрывает узел (как SetQuickAccessHidden) и выключает «движок» истории, чтобы Home не наполнялся
// заново: LaunchTo=1 (открывать «Этот компьютер»), ShowRecent=0, ShowFrequent=0 (HKCU, личные).
// Все ключи проверены (НЕ выдуманные политики). Обратимо: .reg-бэкап + повторное нажатие.
bool GetQuickAccessDisabled() {
    std::wstring adv = std::wstring(kExplorer) + L"\\Advanced";
    DWORD lt = 0, sr = 1, sf = 1;
    RegReadDword(HKEY_CURRENT_USER, adv,       L"LaunchTo",     lt);
    RegReadDword(HKEY_CURRENT_USER, kExplorer, L"ShowRecent",   sr);
    RegReadDword(HKEY_CURRENT_USER, kExplorer, L"ShowFrequent", sf);
    bool homeSectionHidden = !HasQuickAccessHomeDelegateLayout()
                          || !KeyExists(HKEY_LOCAL_MACHINE, QuickAccessHomeDelegateSub());
    return (lt == 1 && sr == 0 && sf == 0 && GetQuickAccessHidden() && homeSectionHidden);
}

bool SetQuickAccessDisabled(bool disable, std::wstring& backupPath, std::wstring& err) {
    std::wstring adv = std::wstring(kExplorer) + L"\\Advanced";
    backupPath = MakeBackupDir();

    std::wstring reg = L"Windows Registry Editor Version 5.00\r\n\r\n"
        + RegDwordBackupLine(L"HKEY_CURRENT_USER", adv,       L"LaunchTo",     HKEY_CURRENT_USER)
        + RegDwordBackupLine(L"HKEY_CURRENT_USER", kExplorer, L"ShowRecent",   HKEY_CURRENT_USER)
        + RegDwordBackupLine(L"HKEY_CURRENT_USER", kExplorer, L"ShowFrequent", HKEY_CURRENT_USER);
    if (!SaveTextFileUtf16(backupPath + L"disable_qa_before.reg", reg)) {
        err = L"Не удалось сохранить бэкап disable_qa_before.reg — операция отменена."; return false;
    }

    // Бэкап home-delegate — ДО первой записи: SetQuickAccessHiddenCore ниже уже пишет в реестр,
    // и если экспорт home-delegate не удастся после него, мы вернули бы отказ с частично
    // применённым состоянием. Собираем все бэкапы перед любыми изменениями.
    if (!BackupQuickAccessHomeDelegate(backupPath, err)) return false;

    std::wstring e2;
    if (!SetQuickAccessHiddenCore(disable, backupPath, e2)) {
        err = e2.empty() ? L"Не удалось изменить видимость «Быстрого доступа»." : e2;
        return false;
    }
    if (!SetQuickAccessHomeDelegateVisible(!disable, err)) return false;

    // HKCU — личные настройки пользователя, без смены владельца.
    bool ok = true;
    ok = WriteDw(HKEY_CURRENT_USER, adv,       L"LaunchTo",     disable ? 1u : 2u) && ok;
    ok = WriteDw(HKEY_CURRENT_USER, kExplorer, L"ShowRecent",   disable ? 0u : 1u) && ok;
    ok = WriteDw(HKEY_CURRENT_USER, kExplorer, L"ShowFrequent", disable ? 0u : 1u) && ok;
    if (!ok) {
        err = L"Не удалось записать пользовательские параметры Проводника.";
        return false;
    }

    if (!e2.empty()) err = e2; // пробросить предупреждение про ACL, если возникло
    return true;
}

// Значки рабочего стола: HKCU\...\Explorer\HideDesktopIcons\{NewStartPanel,ClassicStartMenu}.
// Значение-DWORD, имя = CLSID значка: 1 = скрыт, 0/нет = показан. NewStartPanel читает современное
// меню «Пуск», ClassicStartMenu — классическое; пишем в оба, чтобы значок вёл себя одинаково.
static const wchar_t* kHideIconsNew =
    L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\HideDesktopIcons\\NewStartPanel";
static const wchar_t* kHideIconsClassic =
    L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\HideDesktopIcons\\ClassicStartMenu";

bool GetDesktopIconHidden(const std::wstring& guid) {
    DWORD v = 0;
    RegReadDword(HKEY_CURRENT_USER, kHideIconsNew, guid.c_str(), v);
    return v != 0;
}

bool SetDesktopIconHidden(const std::wstring& guid, bool hidden, std::wstring& backupPath, std::wstring& err) {
    backupPath = MakeBackupDir();
    std::wstring reg = L"Windows Registry Editor Version 5.00\r\n\r\n"
                     + RegDwordBackupLine(L"HKEY_CURRENT_USER", kHideIconsNew,     guid.c_str(), HKEY_CURRENT_USER)
                     + RegDwordBackupLine(L"HKEY_CURRENT_USER", kHideIconsClassic, guid.c_str(), HKEY_CURRENT_USER);
    if (!SaveTextFileUtf16(backupPath + L"desktopicons_before.reg", reg)) {
        err = L"Не удалось сохранить бэкап — изменение отменено.";
        return false; // нет бэкапа — не пишем
    }
    DWORD v = hidden ? 1u : 0u;
    bool ok = WriteDw(HKEY_CURRENT_USER, kHideIconsNew,     guid.c_str(), v);
    WriteDw(HKEY_CURRENT_USER, kHideIconsClassic, guid.c_str(), v); // второй режим меню — best-effort
    if (!ok) err = L"Не удалось записать значение в реестр.";
    return ok;
}

static std::wstring HkcuClsidKey(const std::wstring& guid) {
    return L"SOFTWARE\\Classes\\CLSID\\" + guid;
}

// === Дерево навигации (левая панель): закрепить системный узел — Этот компьютер, Корзина ===
// За показ в дереве отвечает System.IsPinnedToNameSpaceTree на самом CLSID (per-user override в
// HKCU\Software\Classes\CLSID\{guid}), а НЕ запись в Desktop\NameSpace. Объект уже зарегистрирован
// системой (InProcServer32/ShellFolder в HKLM) — здесь только закрепляем/откалываем его в дереве.
static const wchar_t* kPinValue = L"System.IsPinnedToNameSpaceTree";

bool GetNavTreePinned(const std::wstring& guid) {
    DWORD v = 0;
    RegReadDword(HKEY_CURRENT_USER, HkcuClsidKey(guid), kPinValue, v);
    return v != 0;
}

bool SetNavTreePinned(const std::wstring& guid, bool pinned, std::wstring& backupPath, std::wstring& err) {
    backupPath = MakeBackupDir();
    std::wstring clsid = HkcuClsidKey(guid);
    std::wstring reg = L"Windows Registry Editor Version 5.00\r\n\r\n"
                     + RegDwordBackupLine(L"HKEY_CURRENT_USER", clsid, kPinValue, HKEY_CURRENT_USER);
    if (!SaveTextFileUtf16(backupPath + L"navpin_before.reg", reg)) {
        err = L"Не удалось сохранить бэкап — изменение отменено."; return false; // нет бэкапа — не пишем
    }
    // Пишем явные 1/0 (а не удаляем при откреплении): 0 надёжно прячет даже узел, показанный по
    // умолчанию (Этот компьютер), а откат к исходному состоянию есть в navpin_before.reg.
    if (!WriteDw(HKEY_CURRENT_USER, clsid, kPinValue, pinned ? 1u : 0u)) {
        err = L"Не удалось записать значение в реестр."; return false;
    }
    return true;
}

// === «Этот компьютер» (правая панель): узел-команда (launcher) — Управление дисками ===
// Узел-команду в ЛЕВОЕ дерево чисто реестром добавить нельзя (дерево показывает только папки,
// клик = навигация внутрь, Shell\Open\Command не вызывается — нужна своя namespace-extension DLL).
// Рабочий способ без DLL: положить узел в MyComputer\NameSpace. Он показывается ВНУТРИ окна
// «Этот компьютер», и двойной клик по нему выполняет Shell\Open\Command (объект — не папка).
static std::wstring HkcuMyComputerNsKey(const std::wstring& guid) {
    return std::wstring(kExplorer) + L"\\MyComputer\\NameSpace\\" + guid;
}

bool GetMyComputerNode(const std::wstring& guid) {
    return KeyExists(HKEY_CURRENT_USER, HkcuMyComputerNsKey(guid));
}

bool AddMyComputerCommand(const std::wstring& guid, const std::wstring& name, const std::wstring& iconPath,
                          const std::wstring& command, std::wstring& backupPath, std::wstring& err) {
    backupPath = MakeBackupDir();
    std::wstring clsid = HkcuClsidKey(guid);
    std::wstring nsKey = HkcuMyComputerNsKey(guid);
    // Бэкап обоих ключей (их ещё нет — маркеры [-...] для отката).
    if (!BackupKeyState(HKEY_CURRENT_USER, clsid, L"launcher_clsid_before.reg", backupPath, err)) return false;
    if (!BackupKeyState(HKEY_CURRENT_USER, nsKey, L"launcher_ns_before.reg",    backupPath, err)) return false;

    bool ok = true;
    auto W = [&](bool r){ if (!r) ok = false; };
    W(WriteSz    (HKEY_CURRENT_USER, clsid, nullptr, name));
    W(WriteSz    (HKEY_CURRENT_USER, clsid, L"InfoTip", name));
    W(WriteExpand(HKEY_CURRENT_USER, clsid + L"\\DefaultIcon", nullptr, iconPath));
    W(WriteExpand(HKEY_CURRENT_USER, clsid + L"\\Shell\\Open\\Command", nullptr, command));
    // Attributes=0 → объект не папка: двойной клик вызывает глагол Open (нашу команду), а не вход внутрь.
    W(WriteDw    (HKEY_CURRENT_USER, clsid + L"\\ShellFolder", L"Attributes", 0u));
    W(WriteSz    (HKEY_CURRENT_USER, nsKey, nullptr, name));
    if (!ok) {
        // откат частично созданного — чтобы не осталось «полуузла»
        DeleteKeyForced(HKEY_CURRENT_USER, clsid);
        DeleteKeyForced(HKEY_CURRENT_USER, nsKey);
        err = L"Не удалось создать узел в «Этот компьютер»."; return false;
    }
    return true;
}

bool RemoveMyComputerNode(const std::wstring& guid, std::wstring& backupPath, std::wstring& err) {
    backupPath = MakeBackupDir();
    std::wstring nsKey = HkcuMyComputerNsKey(guid);
    std::wstring clsid = HkcuClsidKey(guid);
    // Бэкап перед удалением: экспорт существующих ключей (откат — реимпортом этих .reg).
    if (!BackupKeyState(HKEY_CURRENT_USER, nsKey, L"launcher_ns_removed.reg",    backupPath, err)) return false;
    if (!BackupKeyState(HKEY_CURRENT_USER, clsid, L"launcher_clsid_removed.reg", backupPath, err)) return false;

    bool ok = DeleteKeyForced(HKEY_CURRENT_USER, nsKey);
    DeleteKeyForced(HKEY_CURRENT_USER, clsid); // наша CLSID-регистрация; best-effort
    if (!ok) { err = L"Не удалось убрать узел из «Этот компьютер»."; return false; }
    return true;
}
