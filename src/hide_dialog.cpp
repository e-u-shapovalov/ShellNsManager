#include "hide_dialog.h"
#include "registry.h"
#include "nsmodel.h"
#include "resource.h"
#include "loc.h"
#include <vector>

static std::vector<ShellChild> s_items;
static std::wstring            s_hidden;

static INT_PTR CALLBACK HideDlgProc(HWND dlg, UINT msg, WPARAM w, LPARAM l) {
    (void)l;
    switch (msg) {
    case WM_INITDIALOG: {
        s_items = EnumDesktopBuiltins();
        HWND lb = GetDlgItem(dlg, IDC_HIDE_LIST);
        for (const auto& it : s_items) {
            std::wstring line = it.name + L"      " + it.guid;
            SendMessageW(lb, LB_ADDSTRING, 0, (LPARAM)line.c_str());
        }
        LocDialog(dlg);
        return TRUE;
    }
    case WM_COMMAND:
        switch (LOWORD(w)) {
        case IDOK: {
            int sel = (int)SendMessageW(GetDlgItem(dlg, IDC_HIDE_LIST), LB_GETCURSEL, 0, 0);
            if (sel < 0 || sel >= (int)s_items.size()) {
                MessageBoxW(dlg, T(L"Выберите элемент в списке."), T(L"Скрыть"), MB_OK|MB_ICONINFORMATION);
                return TRUE;
            }
            const ShellChild& it = s_items[sel];
            std::wstring backup, err;
            if (SetNavTreeHidden(it.guid, true, backup, err)) {
                if (!err.empty()) MessageBoxW(dlg, err.c_str(), T(L"Внимание: права ключа не восстановлены"), MB_OK|MB_ICONWARNING);
                s_hidden = it.name;
                EndDialog(dlg, IDOK);
            } else {
                MessageBoxW(dlg, err.c_str(), T(L"Не удалось"), MB_OK|MB_ICONWARNING);
            }
            return TRUE;
        }
        case IDCANCEL:
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

bool ShowHideDialog(HWND parent, std::wstring& hiddenName) {
    s_items.clear(); s_hidden.clear();
    INT_PTR r = DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_HIDE), parent, HideDlgProc, 0);
    if (r == IDOK) { hiddenName = s_hidden; return true; }
    return false;
}
