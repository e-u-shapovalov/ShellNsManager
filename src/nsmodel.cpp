#include "nsmodel.h"
#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>

static std::wstring RegStr(HKEY root, const std::wstring& sub, const wchar_t* val) {
    HKEY k;
    if (RegOpenKeyExW(root, sub.c_str(), 0, KEY_READ, &k) != ERROR_SUCCESS) return L"";
    std::wstring out;
    DWORD type = 0, cb = 0;
    if (RegQueryValueExW(k, val, nullptr, &type, nullptr, &cb) == ERROR_SUCCESS
        && cb > 0 && (type == REG_SZ || type == REG_EXPAND_SZ)) {
        std::wstring buf(cb / sizeof(wchar_t) + 1, L'\0');
        DWORD cb2 = cb;
        if (RegQueryValueExW(k, val, nullptr, &type, (LPBYTE)buf.data(), &cb2) == ERROR_SUCCESS) {
            buf.resize(wcslen(buf.c_str()));
            out = buf;
        }
    }
    RegCloseKey(k);
    return out;
}

// Значение из регистрации CLSID\{guid}[\sub], пробуя HKLM, HKCU и Wow6432Node.
static std::wstring ClsidStr(const std::wstring& guid, const wchar_t* sub, const wchar_t* val) {
    std::wstring tail = L"\\CLSID\\" + guid;
    if (sub && *sub) { tail += L"\\"; tail += sub; }
    std::wstring s = RegStr(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Classes" + tail, val);
    if (s.empty()) s = RegStr(HKEY_CURRENT_USER,  L"SOFTWARE\\Classes" + tail, val);
    if (s.empty()) s = RegStr(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Wow6432Node\\Classes" + tail, val);
    return s;
}

// "@dll,-resID" -> локализованная строка; обычная строка возвращается как есть.
static std::wstring LoadIndirect(const std::wstring& raw) {
    if (raw.empty()) return L"";
    if (raw[0] == L'@') {
        wchar_t buf[512] = L"";
        if (SUCCEEDED(SHLoadIndirectString(raw.c_str(), buf, 512, nullptr)) && buf[0]) return buf;
        return L"";
    }
    return raw;
}

std::wstring ResolveDisplayName(const std::wstring& guid) {
    std::wstring path = L"::" + guid;            // "::{GUID}"
    PIDLIST_ABSOLUTE pidl = nullptr;
    std::wstring res;
    if (SUCCEEDED(SHParseDisplayName(path.c_str(), nullptr, &pidl, 0, nullptr)) && pidl) {
        PWSTR name = nullptr;
        if (SUCCEEDED(SHGetNameFromIDList(pidl, SIGDN_NORMALDISPLAY, &name)) && name) {
            res = name;
            CoTaskMemFree(name);
        }
        CoTaskMemFree(pidl);
    }
    if (!res.empty()) return res;
    // запасной вариант: локализованное имя из регистрации CLSID
    std::wstring ls = LoadIndirect(ClsidStr(guid, nullptr, L"LocalizedString"));
    if (!ls.empty()) return ls;
    std::wstring def = LoadIndirect(ClsidStr(guid, nullptr, nullptr));
    if (!def.empty()) return def;
    return guid;
}

int ResolveSysIconIndex(const std::wstring& guid) {
    std::wstring path = L"::" + guid;
    PIDLIST_ABSOLUTE pidl = nullptr;
    int idx = -1;
    if (SUCCEEDED(SHParseDisplayName(path.c_str(), nullptr, &pidl, 0, nullptr)) && pidl) {
        SHFILEINFOW sfi{};
        if (SHGetFileInfoW((LPCWSTR)pidl, 0, &sfi, sizeof(sfi),
                           SHGFI_PIDL | SHGFI_SYSICONINDEX | SHGFI_SMALLICON)) {
            idx = sfi.iIcon;
        }
        CoTaskMemFree(pidl);
    }
    return idx;
}

std::wstring ResolveInfoTip(const std::wstring& guid) {
    return LoadIndirect(ClsidStr(guid, nullptr, L"InfoTip"));
}

std::vector<ShellChild> EnumDesktopBuiltins() {
    std::vector<ShellChild> out;
    IShellFolder* desktop = nullptr;
    if (FAILED(SHGetDesktopFolder(&desktop)) || !desktop) return out;
    IEnumIDList* en = nullptr;
    if (SUCCEEDED(desktop->EnumObjects(nullptr,
            (SHCONTF)(SHCONTF_FOLDERS | SHCONTF_NONFOLDERS | SHCONTF_INCLUDEHIDDEN), &en)) && en) {
        LPITEMIDLIST child = nullptr;
        while (en->Next(1, &child, nullptr) == S_OK) {
            STRRET sr{};
            if (SUCCEEDED(desktop->GetDisplayNameOf((PCUITEMID_CHILD)child, SHGDN_FORPARSING, &sr))) {
                PWSTR parsing = nullptr;
                if (SUCCEEDED(StrRetToStrW(&sr, (PCUITEMID_CHILD)child, &parsing)) && parsing) {
                    std::wstring p = parsing; CoTaskMemFree(parsing);
                    if (p.size() > 3 && p.compare(0, 3, L"::{") == 0) {   // CLSID-папка рабочего стола
                        ShellChild sc; sc.guid = p.substr(2);
                        STRRET sr2{};
                        if (SUCCEEDED(desktop->GetDisplayNameOf((PCUITEMID_CHILD)child, SHGDN_NORMAL, &sr2))) {
                            PWSTR nm = nullptr;
                            if (SUCCEEDED(StrRetToStrW(&sr2, (PCUITEMID_CHILD)child, &nm)) && nm) { sc.name = nm; CoTaskMemFree(nm); }
                        }
                        if (sc.name.empty()) sc.name = sc.guid;
                        out.push_back(sc);
                    }
                }
            }
            CoTaskMemFree(child);
        }
        en->Release();
    }
    desktop->Release();
    return out;
}
