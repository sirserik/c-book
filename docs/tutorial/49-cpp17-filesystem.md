# Глава 49. std::filesystem

В наших проектах мы работали с файлами и папками **руками**:
- В `demo-shell` (главы 26-31) — POSIX-вызовы `open`/`read`/`close`/`opendir`/`stat`.
- В `demo-db` — `mkdir`, `pread`/`pwrite`/`fsync`.
- В `demo-chat` — `open`/`read`/`write` для history.

Всё это **POSIX**, на macOS/Linux работает. На **Windows** — другой API: `CreateFile`, `FindFirstFile`, путь с `\`, не `/`. Кросс-платформенный код приходится писать через `#ifdef _WIN32`.

C++17 добавил **`std::filesystem`** — стандартный кросс-платформенный API. В этой главе обзорно: `path`, операции, итерация по каталогам, разбор пути. Финальный демо собирает всё.

## std::filesystem::path

Главный класс — `fs::path`. Представляет путь, **не** проверяет существование.

```cpp
#include <filesystem>
namespace fs = std::filesystem;

fs::path p1 = "/Users/serik/docs/report.pdf";
fs::path p2 = "data";
fs::path p3 = p2 / "config" / "app.toml";   // оператор / для конкатенации
// p3 = "data/config/app.toml"
```

Оператор `/` — кросс-платформенный, использует правильный разделитель: `/` на Unix, `\` на Windows. Никакого `if (windows) ...` руками.

### Разбор пути

```cpp
fs::path p = "/Users/alice/docs/report.pdf";

p.parent_path();    // "/Users/alice/docs"
p.filename();       // "report.pdf"
p.stem();           // "report" (filename без extension)
p.extension();      // ".pdf"
p.root_path();      // "/"
p.root_directory(); // "/"
```

Удобно для:
- Найти родительский каталог.
- Узнать тип файла по расширению.
- Заменить расширение: `p.replace_extension(".bak")`.

Тип `fs::path` имеет операторы сравнения, hash — можно класть в `std::unordered_map<fs::path, T>`.

## Файловые операции

```cpp
fs::create_directory(path);        // создать одну папку
fs::create_directories(path);      // и все промежуточные (как mkdir -p)
fs::remove(path);                  // удалить файл или ПУСТУЮ папку
fs::remove_all(path);              // удалить папку со всем содержимым (rm -rf!)
fs::copy_file(from, to);
fs::rename(from, to);              // также для перемещения

fs::exists(path);
fs::is_regular_file(path);
fs::is_directory(path);
fs::is_symlink(path);
fs::is_empty(path);

fs::file_size(path);               // в байтах
fs::last_write_time(path);         // время модификации
```

Никаких POSIX-вызовов, без `#ifdef`. Работает одинаково на Linux/macOS/Windows.

### Текущая папка

```cpp
fs::path cwd = fs::current_path();
fs::current_path("/tmp");   // изменить cwd
```

Эквивалент `getcwd` / `chdir` в POSIX.

### Symlinks и absolute paths

```cpp
fs::path abs = fs::absolute(p);             // относительный → абсолютный
fs::path canonical = fs::canonical(p);     // абсолютный без `..` и симлинков
fs::create_symlink("/tmp", "/home/link");
```

## Итерация по каталогу

Два класса:

**`fs::directory_iterator`** — один уровень (как `ls`):

```cpp
for (const auto& entry : fs::directory_iterator("/tmp")) {
    std::cout << entry.path() << "\n";
}
```

**`fs::recursive_directory_iterator`** — все вложенные (как `find . -type f`):

```cpp
for (const auto& entry : fs::recursive_directory_iterator("/tmp")) {
    if (entry.is_regular_file()) {
        std::cout << entry.path()
                  << " " << entry.file_size() << " bytes\n";
    }
}
```

`entry` — это `fs::directory_entry`. Имеет `path()`, `is_regular_file()`, `is_directory()`, `file_size()`, и так далее. **Cached** — данные читаются один раз при обходе, не stat'ом на каждый запрос.

Это **гигантское** упрощение vs POSIX. Чтобы пройти дерево с opendir/readdir вручную, нужно стек, рекурсию, обработку errno. С `recursive_directory_iterator` — 3 строки.

## Демо

`utils/filesystem_demo.cpp`:

```cpp
namespace fs = std::filesystem;

int main() {
    fs::path base = "/tmp/fs_demo";
    fs::remove_all(base);
    fs::create_directories(base / "subdir1" / "deeper");

    std::ofstream(base / "a.txt") << "hello a";
    std::ofstream(base / "subdir1" / "b.txt") << "hello b";
    std::ofstream(base / "subdir1" / "deeper" / "c.txt") << "hello c";

    for (const auto& entry : fs::recursive_directory_iterator(base)) {
        if (entry.is_regular_file()) {
            std::cout << entry.path().string()
                      << "  (" << entry.file_size() << " bytes)\n";
        }
    }

    fs::path file = base / "a.txt";
    std::cout << "exists: " << fs::exists(file) << "\n";
    std::cout << "size: " << fs::file_size(file) << "\n";

    fs::copy_file(file, base / "a_copy.txt");
    fs::rename(base / "a_copy.txt", base / "a_renamed.txt");

    fs::path p = "/Users/alice/docs/report.pdf";
    std::cout << p.parent_path() << "\n";   // /Users/alice/docs
    std::cout << p.filename() << "\n";       // report.pdf
    std::cout << p.stem() << "\n";           // report
    std::cout << p.extension() << "\n";      // .pdf

    fs::remove_all(base);
}
```

Запуск:

```
=== Все файлы в "/tmp/fs_demo" ===
/tmp/fs_demo/a.txt  (7 bytes)
/tmp/fs_demo/subdir1/
/tmp/fs_demo/subdir1/deeper/
/tmp/fs_demo/subdir1/deeper/c.txt  (7 bytes)
/tmp/fs_demo/subdir1/b.txt  (7 bytes)

exists: 1
is_regular_file: 1
file_size: 7

=== Разбор "/Users/alice/docs/report.pdf" ===
parent_path: "/Users/alice/docs"
filename:    "report.pdf"
stem:        "report"
extension:   ".pdf"
```

50 строк кода — а покрывает «создать дерево, найти все файлы, разобрать пути». На POSIX это было бы 200 строк со стеками и errno.

## Где в наших проектах

В наших главах можно было заменить:

### demo-shell

Команда `ls` — глава 31 (`utils/mycat.cpp`/`mywc.cpp`):

```cpp
// Старый, POSIX:
DIR* dir = opendir(path);
struct dirent* entry;
while ((entry = readdir(dir)) != nullptr) {
    // entry->d_name — имя файла
    // нужен ещё stat() для типа
}
closedir(dir);

// Новый, std::filesystem:
for (const auto& e : fs::directory_iterator(path)) {
    std::cout << e.path().filename().string() << "\n";
}
```

5 строк вместо 10, плюс работает на Windows.

### demo-db

В `database.cpp`:

```cpp
::mkdir(dir.c_str(), 0755);   // POSIX
fs::create_directories(dir);  // C++17, кросс-платформенно
```

`fs::create_directories` — что-то типа `mkdir -p`, создаёт все промежуточные. POSIX `mkdir` ругается, если родительского нет.

### demo-chat

В `history.h` — open файла. Стандартный `std::ofstream` справляется, но `fs::file_size(path)` для проверки лимита — удобно.

### Обход проектной директории

В RPG — для загрузки всех save-файлов из `data/saves/`:

```cpp
std::vector<std::string> list_saves() {
    std::vector<std::string> result;
    for (const auto& e : fs::directory_iterator("data/saves")) {
        if (e.is_regular_file() && e.path().extension() == ".sav") {
            result.push_back(e.path().stem().string());
        }
    }
    return result;
}
```

Раньше пришлось бы `opendir` + `readdir` + проверять расширение строки руками. Сейчас — 5 строк.

## Ошибки

По умолчанию операции **бросают** `fs::filesystem_error` при ошибке. Можно через `std::error_code` (без throw):

```cpp
std::error_code ec;
fs::create_directory("/no/permission/here", ec);
if (ec) {
    std::cerr << "ошибка: " << ec.message() << "\n";
}
```

Подходит когда «попытаться, и если не вышло — продолжить». Например, при создании папки с возможностью «уже существует».

## Производительность

`std::filesystem` обычно **немного медленнее** ручного POSIX:
- Каждая операция оборачивается C++-классом.
- Path хранит строку (alloc).
- Iterator кэширует stat — выгодно, если используете несколько раз; накладно, если нет.

Для большинства приложений разница неощутима. На очень частых файловых операциях (миллион stat'ов) — может быть заметно.

## Windows nuances

Кросс-платформа не идеальна. На Windows есть особенности:

- **Пути в UTF-16** (wchar_t). `fs::path` использует `wstring` на Windows, `string` на POSIX. Для печати в `cout` нужно конвертировать.
- **Backslash** — Windows исторически. `fs::path` нормализует.
- **Привилегии** — `create_directory` в `C:\Windows` потребует admin.
- **Регистр имён** — Windows case-insensitive. `Foo.txt == foo.txt`.

`fs::path::string()` возвращает narrow string. На Windows с не-ASCII именами может потеряться информация. Безопаснее `.u8string()` или `.wstring()`.

## Fallback под C++11

Если у вас компилятор без C++17, есть несколько fallback:

1. **Boost.Filesystem** — почти то же API, работает на C++03+. `#include <boost/filesystem.hpp>`.
2. **TS version** — `<experimental/filesystem>`. Работает на GCC 5.3+ / Clang 3.9+.
3. **POSIX/WinAPI** вручную. Что мы и делали.

На современных компиляторах **`<filesystem>`** уже доступен — без Boost.

## Главные правила главы

1. **`fs::path` для путей** — кросс-платформенно, parser встроен.
2. **Оператор `/`** для конкатенации, не строки.
3. **`fs::create_directories`** делает «mkdir -p».
4. **`fs::recursive_directory_iterator`** для обхода дерева — 3 строки.
5. **`fs::remove_all` это `rm -rf`** — будьте осторожны!
6. **error_code для non-throwing** API. Бросать по умолчанию.
7. **Windows: backslash, wchar_t, регистр** — не идеальная переносимость.
8. **Современный default** для нового кода.

## Маленькое упражнение

1. Соберите и запустите `./build/filesystem_demo`. Изучите вывод.

2. Перепишите `Database::Database` в demo-db: вместо `::mkdir(dir.c_str(), 0755)` используйте `fs::create_directories(dir)`.

3. Напишите `ls`-utility через `fs::directory_iterator` за 10 строк.

4. Реализуйте `du` — рекурсивный обход и сумма `file_size`. На вашем home directory какая суммарная?

5. (Сложнее) Реализуйте функцию `list_saves()` (см. выше) в RPG. Добавьте команду `saves` в игровой цикл RPG для просмотра списка сохранений.

6. (Сложнее) Замените open/read/write в `History` (demo-chat, глава 46) на `std::ifstream`/`std::ofstream` + `fs::file_size`.

7. (Сложнее) Сделайте watch-mode: периодически проверять `last_write_time` файла, если изменился — перезагрузить.

8. Прочитайте `fs::space(path)` — свободное место на диске. Когда полезно?

## Что дальше

Глава 50 — **`std::string_view`, parallel algorithms**. Лёгкая обёртка для строк без копий, и `std::execution::par` для параллельных алгоритмов C++17.

Глава 51 — финал: куда расти после книги. C++20 concepts, ranges, coroutines, modules — обзорно.

Финал близок.
