#pragma once
#include <string>
#include <vector>

// "::{GUID}" -> человеческое имя (SHParseDisplayName -> SIGDN_NORMALDISPLAY).
// На провал — LocalizedString/(default) из CLSID, иначе сам guid.
std::wstring ResolveDisplayName(const std::wstring& guid);

// Индекс иконки в системном image list (SHIL_SMALL); -1 на провал.
int ResolveSysIconIndex(const std::wstring& guid);

// Описание (InfoTip) узла из регистрации CLSID; "" если нет.
std::wstring ResolveInfoTip(const std::wstring& guid);

// Дочерний CLSID-элемент рабочего стола (для скрытия встроенных).
struct ShellChild { std::wstring guid, name; };
std::vector<ShellChild> EnumDesktopBuiltins();
