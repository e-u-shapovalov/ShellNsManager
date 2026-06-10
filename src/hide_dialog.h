#pragma once
#include <windows.h>
#include <string>

// Диалог «Скрыть встроенный элемент». Возвращает true, если что-то скрыто; hiddenName — имя.
bool ShowHideDialog(HWND parent, std::wstring& hiddenName);
