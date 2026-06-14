#pragma once
#include <windows.h>
#include <string>

// Показать модальный диалог «Добавить свою папку».
// Возвращает true, если узел создан; guid/backupPath — результат.
bool ShowAddDialog(HWND parent, std::wstring& guid, std::wstring& backupPath);

// Показать модальный диалог «Добавить свою команду» (имя + команда + иконка + «от админа»).
// Возвращает true, если пользователь нажал OK; name/command/iconPath/runAsAdmin — введённые значения.
bool ShowAddCommandDialog(HWND parent, std::wstring& name, std::wstring& command, std::wstring& iconPath, bool& runAsAdmin);
