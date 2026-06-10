#pragma once
#include <windows.h>
#include <string>

// Показать модальный диалог «Добавить свою папку».
// Возвращает true, если узел создан; guid/backupPath — результат.
bool ShowAddDialog(HWND parent, std::wstring& guid, std::wstring& backupPath);
