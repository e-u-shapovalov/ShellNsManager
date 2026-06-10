# Релизы ShellNsManager

## Текущий статус

В рабочей папке может быть локальная сборка `ShellNsManager.exe`, но файл `.gitignore` исключает `.exe` из Git. Это правильно: готовую программу не нужно коммитить в репозиторий как обычный исходный файл.

Для пользователей нужно создать **GitHub Release** и загрузить готовый архив в блок **Assets**. Иначе новичок увидит только исходный код и не поймет, где скачать программу.

## Что загрузить в GitHub Release

Рекомендуемый файл:

```text
ShellNsManager-v1.0-win64.zip
```

Содержимое архива:

```text
ShellNsManager.exe
README.md
README.en.md
DOWNLOAD.md
INSTALL.md
LICENSE
```

Лицензия проекта — **MIT** (файл `LICENSE` в корне репозитория).

Не загружайте вместо релиза только `Source code (zip)`: GitHub создает этот архив автоматически, но обычному пользователю он не подходит.

## Чеклист перед публикацией

- Собрать `ShellNsManager.exe` через `build.bat`.
- Проверить запуск на чистой Windows 10/11.
- Проверить UAC-запрос администратора.
- Проверить добавление папки в «Этот компьютер» и дерево.
- Проверить удаление созданного узла.
- Проверить создание `.reg`-бэкапов в `backups`.
- Проверить скрытие и возврат «Главная» / «Быстрый доступ».
- Добавить скриншоты в `docs/screenshots/` (главное окно — добавлено).
- `LICENSE` — добавлен (MIT).
- Создать git tag, например `v1.0.0`.
- Создать GitHub Release и загрузить `.zip` в **Assets**.

## Готовый текст для релиза v1.0.0

### ShellNsManager v1.0.0

Первый публичный релиз Shell Namespace Manager - portable-утилиты для настройки Проводника Windows без ручного редактирования реестра.

Что можно сделать:

- добавить свою папку в «Этот компьютер» или дерево навигации;
- выбрать имя, папку-цель и иконку;
- работать с `HKCU` и `HKLM`;
- изменить порядок элементов через `SortOrderIndex`;
- удалить namespace-узел с `.reg`-бэкапом;
- скрыть встроенные элементы дерева;
- скрыть или вернуть «Главная» и «Быстрый доступ»;
- очистить закрепленные и частые папки Quick Access;
- перезапустить Проводник из интерфейса.

Важно:

- программа требует Windows и права администратора;
- перед изменениями создаются `.reg`-бэкапы;
- обычным пользователям нужно скачивать архив из **Assets**, а не `Source code`.

English:

ShellNsManager is a portable Windows Explorer Shell Namespace editor. It lets you add custom folders to This PC or the navigation tree, reorder namespace nodes, hide Home and Quick access, and restore changes through automatic `.reg` backups.

Download the ready package from **Assets**. Do not download `Source code` unless you want to build the project yourself.
