#include "add_dialog.h"
#include "registry.h"
#include "resource.h"
#include "loc.h"
#include <shlobj.h>
#include <stdlib.h>
#include <string>

static std::wstring s_guid, s_backup;
static HICON        s_previewIcon = nullptr;

static std::wstring DlgText(HWND dlg, int id) {
    wchar_t b[1024] = L"";
    GetDlgItemTextW(dlg, id, b, 1024);
    return std::wstring(b);
}

// Разобрать "путь,индекс" и показать иконку в превью-контроле.
static void UpdateIconPreview(HWND dlg) {
    std::wstring cur = DlgText(dlg, IDC_ADD_ICON);
    int idx = 0;
    std::wstring pathPart = cur;
    size_t comma = cur.find_last_of(L',');
    if (comma != std::wstring::npos) { pathPart = cur.substr(0, comma); idx = _wtoi(cur.c_str() + comma + 1); }
    wchar_t expanded[MAX_PATH] = L"";
    if (!ExpandEnvironmentStringsW(pathPart.c_str(), expanded, MAX_PATH)) lstrcpynW(expanded, pathPart.c_str(), MAX_PATH);
    HICON hIco = nullptr;
    SHDefExtractIconW(expanded, idx, 0, &hIco, nullptr, 32);
    SendMessageW(GetDlgItem(dlg, IDC_ADD_ICON_PREVIEW), STM_SETICON, (WPARAM)hIco, 0);
    if (s_previewIcon) DestroyIcon(s_previewIcon);
    s_previewIcon = hIco;
}

static INT_PTR CALLBACK AddDlgProc(HWND dlg, UINT msg, WPARAM w, LPARAM l) {
    (void)l;
    switch (msg) {
    case WM_INITDIALOG:
        SetDlgItemTextW(dlg, IDC_ADD_ICON, L"%SystemRoot%\\system32\\shell32.dll,-44");
        CheckRadioButton(dlg, IDC_ADD_HIVE_HKCU, IDC_ADD_HIVE_HKLM, IDC_ADD_HIVE_HKCU);
        CheckDlgButton(dlg, IDC_ADD_LOC_TREE, BST_CHECKED);
        UpdateIconPreview(dlg);
        LocDialog(dlg);
        return TRUE;

    case WM_COMMAND:
        switch (LOWORD(w)) {
        case IDC_ADD_ICON:
            if (HIWORD(w) == EN_CHANGE) UpdateIconPreview(dlg);
            return TRUE;
        case IDC_ADD_TARGET_BROWSE: {
            BROWSEINFOW bi{};
            bi.hwndOwner = dlg;
            bi.lpszTitle = T(L"Выберите папку-цель");
            bi.ulFlags   = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
            PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
            if (pidl) {
                wchar_t path[MAX_PATH] = L"";
                if (SHGetPathFromIDListW(pidl, path)) SetDlgItemTextW(dlg, IDC_ADD_TARGET, path);
                CoTaskMemFree(pidl);
            }
            return TRUE;
        }
        case IDC_ADD_ICON_BROWSE: {
            std::wstring cur = DlgText(dlg, IDC_ADD_ICON);
            int idx = 0;
            std::wstring pathPart = cur;
            size_t comma = cur.find_last_of(L',');
            if (comma != std::wstring::npos) { pathPart = cur.substr(0, comma); idx = _wtoi(cur.c_str() + comma + 1); }
            wchar_t pbuf[MAX_PATH] = L"";
            lstrcpynW(pbuf, pathPart.empty() ? L"%SystemRoot%\\system32\\shell32.dll" : pathPart.c_str(), MAX_PATH);
            if (PickIconDlg(dlg, pbuf, MAX_PATH, &idx)) {
                std::wstring res = std::wstring(pbuf) + L"," + std::to_wstring(idx);
                SetDlgItemTextW(dlg, IDC_ADD_ICON, res.c_str()); // EN_CHANGE обновит превью
            }
            return TRUE;
        }
        case IDOK: {
            AddRequest req;
            req.name         = DlgText(dlg, IDC_ADD_NAME);
            req.target       = DlgText(dlg, IDC_ADD_TARGET);
            req.iconPath     = DlgText(dlg, IDC_ADD_ICON);
            req.toMyComputer = IsDlgButtonChecked(dlg, IDC_ADD_LOC_PC)   == BST_CHECKED;
            req.toDesktop    = IsDlgButtonChecked(dlg, IDC_ADD_LOC_TREE) == BST_CHECKED;
            req.hive         = (IsDlgButtonChecked(dlg, IDC_ADD_HIVE_HKLM) == BST_CHECKED) ? NsHive::HKLM : NsHive::HKCU;
            std::wstring guid, backup, err;
            if (AddFolderShortcut(req, guid, backup, err)) {
                s_guid = guid; s_backup = backup;
                EndDialog(dlg, IDOK);
            } else {
                MessageBoxW(dlg, err.c_str(), T(L"Не удалось добавить"), MB_OK | MB_ICONWARNING);
            }
            return TRUE;
        }
        case IDCANCEL:
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        }
        break;

    case WM_DESTROY:
        if (s_previewIcon) { DestroyIcon(s_previewIcon); s_previewIcon = nullptr; }
        return FALSE;
    }
    return FALSE;
}

bool ShowAddDialog(HWND parent, std::wstring& guid, std::wstring& backupPath) {
    s_guid.clear(); s_backup.clear();
    INT_PTR r = DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_ADD), parent, AddDlgProc, 0);
    if (r == IDOK) { guid = s_guid; backupPath = s_backup; return true; }
    return false;
}

// ---- Диалог «Добавить свою команду» ----

static std::wstring s_cmdName, s_cmdCommand, s_cmdIcon;
static bool         s_cmdAdmin = false;
static HICON        s_cmdPreviewIcon = nullptr;

static void UpdateCmdIconPreview(HWND dlg) {
    std::wstring cur = DlgText(dlg, IDC_CMD_ICON);
    int idx = 0;
    std::wstring pathPart = cur;
    size_t comma = cur.find_last_of(L',');
    if (comma != std::wstring::npos) { pathPart = cur.substr(0, comma); idx = _wtoi(cur.c_str() + comma + 1); }
    wchar_t expanded[MAX_PATH] = L"";
    if (!ExpandEnvironmentStringsW(pathPart.c_str(), expanded, MAX_PATH)) lstrcpynW(expanded, pathPart.c_str(), MAX_PATH);
    HICON hIco = nullptr;
    SHDefExtractIconW(expanded, idx, 0, &hIco, nullptr, 32);
    SendMessageW(GetDlgItem(dlg, IDC_CMD_ICON_PREVIEW), STM_SETICON, (WPARAM)hIco, 0);
    if (s_cmdPreviewIcon) DestroyIcon(s_cmdPreviewIcon);
    s_cmdPreviewIcon = hIco;
}

static INT_PTR CALLBACK CmdDlgProc(HWND dlg, UINT msg, WPARAM w, LPARAM l) {
    (void)l;
    switch (msg) {
    case WM_INITDIALOG:
        SetDlgItemTextW(dlg, IDC_CMD_ICON, L"%SystemRoot%\\system32\\imageres.dll,-27");
        UpdateCmdIconPreview(dlg);
        LocDialog(dlg);
        return TRUE;

    case WM_COMMAND:
        switch (LOWORD(w)) {
        case IDC_CMD_ICON:
            if (HIWORD(w) == EN_CHANGE) UpdateCmdIconPreview(dlg);
            return TRUE;
        case IDC_CMD_ICON_BROWSE: {
            std::wstring cur = DlgText(dlg, IDC_CMD_ICON);
            int idx = 0;
            std::wstring pathPart = cur;
            size_t comma = cur.find_last_of(L',');
            if (comma != std::wstring::npos) { pathPart = cur.substr(0, comma); idx = _wtoi(cur.c_str() + comma + 1); }
            wchar_t pbuf[MAX_PATH] = L"";
            lstrcpynW(pbuf, pathPart.empty() ? L"%SystemRoot%\\system32\\shell32.dll" : pathPart.c_str(), MAX_PATH);
            if (PickIconDlg(dlg, pbuf, MAX_PATH, &idx)) {
                std::wstring res = std::wstring(pbuf) + L"," + std::to_wstring(idx);
                SetDlgItemTextW(dlg, IDC_CMD_ICON, res.c_str()); // EN_CHANGE обновит превью
            }
            return TRUE;
        }
        case IDOK: {
            std::wstring name    = DlgText(dlg, IDC_CMD_NAME);
            std::wstring command = DlgText(dlg, IDC_CMD_COMMAND);
            std::wstring icon    = DlgText(dlg, IDC_CMD_ICON);
            if (name.empty())    { MessageBoxW(dlg, T(L"Укажите имя пункта."),  T(L"Добавить свою команду"), MB_OK|MB_ICONINFORMATION); return TRUE; }
            if (command.empty()) { MessageBoxW(dlg, T(L"Укажите команду запуска."), T(L"Добавить свою команду"), MB_OK|MB_ICONINFORMATION); return TRUE; }
            s_cmdName = name; s_cmdCommand = command; s_cmdIcon = icon;
            s_cmdAdmin = (IsDlgButtonChecked(dlg, IDC_CMD_ADMIN) == BST_CHECKED);
            EndDialog(dlg, IDOK);
            return TRUE;
        }
        case IDCANCEL:
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        }
        break;

    case WM_DESTROY:
        if (s_cmdPreviewIcon) { DestroyIcon(s_cmdPreviewIcon); s_cmdPreviewIcon = nullptr; }
        return FALSE;
    }
    return FALSE;
}

bool ShowAddCommandDialog(HWND parent, std::wstring& name, std::wstring& command, std::wstring& iconPath, bool& runAsAdmin) {
    s_cmdName.clear(); s_cmdCommand.clear(); s_cmdIcon.clear(); s_cmdAdmin = false;
    INT_PTR r = DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_ADDCMD), parent, CmdDlgProc, 0);
    if (r == IDOK) { name = s_cmdName; command = s_cmdCommand; iconPath = s_cmdIcon; runAsAdmin = s_cmdAdmin; return true; }
    return false;
}
