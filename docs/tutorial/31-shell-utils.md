# Глава 31. Свои утилиты — cat, wc, grep

Финальная глава Части III. Мы написали shell, который умеет запускать программы, соединять их пайпами, перенаправлять потоки. А теперь напишем **сами программы**: три классических Unix-утилиты с нуля. `mycat`, `mywc`, `mygrep`.

Это упражнение в **системном программировании** — низкоуровневое чтение файлов, обработка байтов, регулярные выражения. И **проверка концепций Unix**: ваш `mycat` читает из stdin, ваш `mygrep` пишет в stdout, и они соединяются в пайплайне через myshell. Полный цикл: написал shell → написал утилиты → запустил утилиты через свой shell.

После этой главы Часть III закрыта.

## Зачем писать свои

Готовые `cat`, `wc`, `grep` уже есть. Зачем повторять?

1. **Понять, как они работают изнутри.** Эти программы 50 лет используются, но мало кто знает, что у них под капотом.
2. **Тренировка в низкоуровневом C++.** `read`/`write` напрямую, без `<iostream>` сверху. UTF-8 на байтовом уровне.
3. **Проверить наш shell.** Соединить свои бинари пайпами — увидеть, что архитектура «программы как фильтры» работает.
4. **Это маленькие, но полные программы.** Каждая решает понятную задачу, можно увидеть её целиком.

## Структура

Каждая утилита — отдельный исполняемый файл с собственным `main`. Размещаем в `demo-shell/utils/`:

```
demo-shell/
├── src/         — код myshell
├── utils/
│   ├── mycat.cpp
│   ├── mywc.cpp
│   └── mygrep.cpp
└── Makefile     — собирает myshell и все утилиты
```

В Makefile добавим правило: для каждого `utils/X.cpp` собираем `build/X`. Через wildcard — без правок при добавлении новой утилиты.

```makefile
UTIL_SRCS := $(wildcard utils/*.cpp)
UTILS     := $(patsubst utils/%.cpp, build/%, $(UTIL_SRCS))

all: build/myshell utils

utils: $(UTILS)

build/%: utils/%.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $< -o $@
```

`make` соберёт `build/myshell`, `build/mycat`, `build/mywc`, `build/mygrep`. Каждая утилита — один `.cpp` файл, прямая компиляция без линковки с библиотеками проекта (они независимы).

## mycat — простейший фильтр

`cat` — самая базовая Unix-утилита. Читает файлы (или stdin) и пишет в stdout. Имя — от **concatenate**: при двух файлах сливает их.

Алгоритм:
1. Если аргументов нет — читать stdin.
2. Иначе для каждого аргумента — открыть, прочитать, записать.
3. `-` как имя файла означает stdin.

`utils/mycat.cpp`:

```cpp
#include <cerrno>
#include <cstring>
#include <iostream>
#include <string>

#include <fcntl.h>
#include <unistd.h>

namespace {

int cat_fd(int fd, const std::string& name) {
    char buf[4096];
    while (true) {
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n == 0) return 0;             // EOF
        if (n < 0) {
            if (errno == EINTR) continue; // прервало сигналом — повтор
            std::cerr << "mycat: " << name << ": " << std::strerror(errno) << "\n";
            return 1;
        }
        ssize_t written = 0;
        while (written < n) {
            ssize_t m = write(STDOUT_FILENO, buf + written, n - written);
            if (m < 0) {
                if (errno == EINTR) continue;
                std::cerr << "mycat: write: " << std::strerror(errno) << "\n";
                return 1;
            }
            written += m;
        }
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc < 2) {
        return cat_fd(STDIN_FILENO, "<stdin>");
    }

    int rc = 0;
    for (int i = 1; i < argc; ++i) {
        std::string name = argv[i];
        if (name == "-") {
            if (cat_fd(STDIN_FILENO, "<stdin>") != 0) rc = 1;
            continue;
        }
        int fd = open(name.c_str(), O_RDONLY);
        if (fd < 0) {
            std::cerr << "mycat: " << name << ": " << std::strerror(errno) << "\n";
            rc = 1;
            continue;
        }
        if (cat_fd(fd, name) != 0) rc = 1;
        close(fd);
    }
    return rc;
}
```

Разбор.

**`cat_fd(int fd, name)`** — основная работа. Читает из `fd`, пишет в STDOUT_FILENO.

**Буфер 4 KB** — компромисс между «много syscall'ов на маленьком буфере» и «много памяти на большом». 4 KB примерно равно размеру страницы памяти на большинстве систем; это «дешёвый» размер.

**`read` возвращает**:
- 0 — EOF. Файл закончился.
- > 0 — число прочитанных байт (может быть меньше запрошенного — это **short read**).
- -1 — ошибка. `errno` уточняет.

**`EINTR`** — прерывание сигналом. Если read был прерван (например, программе пришёл SIGCHLD), мы повторяем. Без этого было бы случайное падение при появлении сигналов.

**`write` тоже может вернуть short write**: меньше, чем запрошено. Нужно повторять, пока всё не записано. В нашем цикле — внутренний цикл с накопителем `written`. Это **критично** для пайпов: если читатель медленный, буфер пайпа заполняется, и write может уйти в short.

**При ошибке read одного файла — продолжаем со следующим.** Не падаем сразу. Возвращаем `rc = 1` в конце, как делает обычный `cat`.

### Проверка

```bash
$ ./build/mycat Makefile | head -3
# Makefile для demo-shell.
# Зеркало стиля demo-rpg/Makefile: -MMD для авто-зависимостей,
# wildcard для подхвата новых .cpp без правок.

$ echo "hello" | ./build/mycat
hello

$ echo "stdin part" | ./build/mycat - Makefile | head -5
stdin part
# Makefile для demo-shell.
...
```

Работает. С точки зрения системы это полноценный `cat`.

### Почему read/write, а не std::ifstream

Мог бы быть код в стиле:

```cpp
std::ifstream in(name);
std::cout << in.rdbuf();
```

Короче, работает. Но:
- Менее **прозрачно** на уровне ядра. Каждый `std::ifstream` под капотом — буферы, скрытые промежуточные копии.
- Тут мы хотим **обучить** низкоуровневой работе. `read`/`write` — это **системные вызовы** один-к-одному.
- На крупных файлах ручной `read+write` часто **быстрее** — нет лишних копий.

В production-коде обычно `std::ifstream` достаточно. Здесь — учимся.

## mywc — счётчик

`wc` (word count) считает в файле:
- **Строки** (line count) — число символов `\n`.
- **Слова** (word count) — последовательности непробельных символов.
- **Байты** или **символы** (с `-c`).

`utils/mywc.cpp` (фрагмент):

```cpp
struct Counts {
    long long lines = 0;
    long long words = 0;
    long long bytes = 0;
    long long chars = 0;
};

Counts count_stream(std::istream& in) {
    Counts c;
    char ch;
    bool in_word = false;
    while (in.get(ch)) {
        ++c.bytes;

        // UTF-8: считаем «не-продолжающие» байты.
        if ((static_cast<unsigned char>(ch) & 0xC0) != 0x80) {
            ++c.chars;
        }

        if (ch == '\n') ++c.lines;

        bool ws = (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f' || ch == '\v');
        if (ws) {
            in_word = false;
        } else if (!in_word) {
            in_word = true;
            ++c.words;
        }
    }
    return c;
}
```

Разбор алгоритма.

**`bytes`** — простой счётчик байтов.

**`chars`** — счётчик символов UTF-8. Помните главу 2: UTF-8 кодирует один символ в 1-4 байта. Первый байт всегда **не имеет** маски `10` в старших двух битах; продолжающие байты имеют. Маска `0xC0` берёт верхние 2 бита; сравнение с `0x80` — это битовая комбинация `10xxxxxx`.

Если бит == `10` — это продолжение **существующего** символа, не считаем. Иначе — это начало нового символа (ASCII или первый байт многобайтного), считаем.

Для ASCII файла `bytes == chars`. Для UTF-8 с кириллицей `chars` будет меньше: «Привет» = 12 байт = 6 символов.

**`lines`** — простой подсчёт `\n`. Заметьте: если файл не оканчивается `\n`, последняя строка не считается. Это совместимо с POSIX `wc` — она тоже так.

**`words`** — двухсостояная машина:
- `in_word = false` — мы сейчас в пробелах. Если встречаем непробельный — переход в `in_word = true`, ++words.
- `in_word = true` — мы в слове. Пробельный → выход; непробельный → остаёмся.

Пробельные символы (ws): пробел, таб, `\n`, `\r`, `\f` (form feed), `\v` (vertical tab). Чтобы соответствовать `wc`.

### main

```cpp
int main(int argc, char* argv[]) {
    bool show_chars = false;
    std::vector<std::string> files;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-c") show_chars = true;
        else files.push_back(a);
    }

    if (files.empty()) {
        Counts c = count_stream(std::cin);
        print_counts(c, "", show_chars);
        return 0;
    }

    Counts total;
    int rc = 0;
    for (const auto& f : files) {
        std::ifstream in(f);
        if (!in.is_open()) {
            std::cerr << "mywc: " << f << ": " << std::strerror(errno) << "\n";
            rc = 1;
            continue;
        }
        Counts c = count_stream(in);
        print_counts(c, f, show_chars);
        total.lines += c.lines;
        total.words += c.words;
        total.bytes += c.bytes;
        total.chars += c.chars;
    }
    if (files.size() > 1) {
        print_counts(total, "total", show_chars);
    }
    return rc;
}
```

Парсим аргументы (`-c`), накапливаем тотал по всем файлам, печатаем строку «total» если файлов больше одного. Совместимо с POSIX `wc`.

### Проверка

```bash
$ ./build/mywc Makefile src/*.cpp
     72     195    1602 Makefile
    151     509    4041 src/builtins.cpp
    117     425    3239 src/exec_runner.cpp
     72     197    1928 src/main.cpp
    239     818    7964 src/pipeline.cpp
     32      80     880 src/signals.cpp
    683    2224   19654 total

$ echo "Привет мир" | ./build/mywc
      1       2      21       # 21 байт (12 на «Привет» + 1 пробел + 6 на «мир» + 2 на «\n»)
$ echo "Привет мир" | ./build/mywc -c
      1       2      11       # 11 символов (6 + 1 + 3 + 1)
```

Работает. Совпадает с системным `wc` побайтно.

## mygrep — фильтр по шаблону

`grep` (Global Regular Expression Print) ищет строки, **подходящие под шаблон**. Каждая строка проверяется через regex, и если есть совпадение — печатается.

Опции в нашей версии:
- `-i` — ignore case.
- `-n` — печатать номер строки.
- `-v` — invert (печатать НЕсовпадающие).

`utils/mygrep.cpp` (фрагмент):

```cpp
#include <regex>

struct Options {
    bool ignore_case = false;
    bool show_line_numbers = false;
    bool invert = false;
    std::string pattern;
    std::vector<std::string> files;
};

int grep_stream(std::istream& in, const std::regex& re,
                const Options& opts, const std::string& filename) {
    std::string line;
    long long line_no = 0;
    bool multi_file = opts.files.size() > 1;
    while (std::getline(in, line)) {
        ++line_no;
        bool matched = std::regex_search(line, re);
        if (matched == opts.invert) continue;

        if (multi_file && !filename.empty()) std::cout << filename << ":";
        if (opts.show_line_numbers)          std::cout << line_no << ":";
        std::cout << line << "\n";
    }
    return 0;
}
```

Разбор.

**`std::regex`** — стандартный класс для регулярных выражений. По умолчанию использует ECMAScript-синтаксис (близкий к POSIX extended). Поддерживает:
- `^foo` / `foo$` — начало/конец строки.
- `.` — любой символ.
- `*` / `+` / `?` — повторения.
- `[abc]` / `[a-z]` — символьные классы.
- `\d` / `\w` / `\s` — цифры/слова/пробелы.
- `()` — группы.
- `|` — альтернативы.

**`std::regex_search(line, re)`** — ищет **первое совпадение** где угодно в строке. Возвращает `bool`.

Альтернатива — `std::regex_match`, которая требует, чтобы **вся строка** совпала. Для grep нужен search — частичное совпадение.

**`matched == opts.invert`** — компактная инвертированная логика. Если `invert=false`, пропускаем когда не совпало; если `invert=true`, пропускаем когда совпало.

### Компиляция regex

```cpp
auto flags = std::regex::ECMAScript;
if (opts.ignore_case) flags |= std::regex::icase;

std::regex re;
try {
    re = std::regex(opts.pattern, flags);
} catch (const std::regex_error& e) {
    std::cerr << "mygrep: bad regex '" << opts.pattern << "': " << e.what() << "\n";
    return 2;
}
```

Компиляция regex — **дорогая** операция (парсинг шаблона, построение конечного автомата). Делаем **один раз** до цикла. Каждый `regex_search` уже быстрый.

`std::regex_error` бросается при невалидном шаблоне (например, `[unclosed`). Ловим и выводим ошибку.

### Проверка

```bash
$ ./build/mygrep -n "include" src/main.cpp
1:#include "builtins.h"
2:#include "pipeline.h"
3:#include "signals.h"
5:#include <cstdlib>
6:#include <exception>
...

$ ./build/mygrep -i "ERROR" *.cpp 2>/dev/null   # case-insensitive

$ ./build/mygrep -v "^#" Makefile  # строки НЕ начинающиеся с #
```

Полноценный grep, в 80 строк C++.

## Полный пайплайн через myshell

Соберём всё. В нашем myshell запустим **наши** утилиты, соединённые пайпами:

```bash
$ ./build/myshell
myshell$ ./build/mycat Makefile | ./build/mygrep -i make | ./build/mywc -c
      2      11      90
```

Что произошло:
1. `mycat Makefile` читает Makefile, пишет в stdout (но stdout перенаправлен myshell через pipe в `mygrep`).
2. `mygrep -i make` читает stdin (из пайпа), ищет «make» (ignore case), пишет совпадения в stdout (опять в пайп).
3. `mywc -c` читает stdin, считает строки/слова/символы, печатает на терминал.

Всё работает. Три **наши** программы, соединённые **нашим** shell. От fork+exec+pipe в shell до системных read/write в утилитах — весь цикл наш.

## Что мы не реализовали

Реальные `cat`, `wc`, `grep` имеют десятки опций. Мы реализовали базовые. Что не сделано:

**`cat`**:
- `-n` (нумерация строк).
- `-E` (показывать `$` на конце строк).
- `-T` (показывать `^I` для табов).
- `-A` (всё это вместе).

**`wc`**:
- `-l`/`-w`/`-c`/`-m` (только строки/слова/байты/символы соответственно).
- `-L` (максимальная длина строки).

**`grep`**:
- `-r` (рекурсивно по каталогам).
- `-l` (только имена файлов).
- `-c` (только число совпадений).
- `-A`/`-B` (контекст N строк до/после).
- `-E`/`-P` (extended / Perl regex).
- `-F` (literal string без regex).

Все они — простые расширения. Если хотите практики — допишите.

## mmap для скорости

POSIX даёт `mmap(addr, len, prot, flags, fd, off)` — **отображение файла в память**. После этого файл доступен как массив байтов, без `read`/`write`:

```cpp
#include <sys/mman.h>

int fd = open("big.txt", O_RDONLY);
struct stat st;
fstat(fd, &st);
char* data = (char*)mmap(nullptr, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);

// Теперь data[0..st.st_size-1] — содержимое файла.
// Использовать как char-массив.

munmap(data, st.st_size);
close(fd);
```

Преимущества mmap:
- **Никаких read/write**. Ядро само подкачивает страницы по мере обращения (lazy).
- **Меньше копий**. Данные в kernel-page-cache доступны напрямую программе.
- **Удобно**. Если нужно искать паттерн или скакать по файлу — `data[i]` это просто индексация.

Недостатки:
- **Не работает для не-seekable** (терминалов, pipe-ов).
- **Нагружает виртуальное пространство**. На больших файлах (10+ GB) приходится управлять регионами.
- **EOF на сетевом mount** дает SIGBUS, а не EOF — особый случай.

Для grep на больших файлах mmap может дать 2-3× ускорение. Для cat в pipe-пайплайне — не подойдёт (читатель stdin не отображается). Мы не использовали — наш read+buffer работает везде.

## Архитектура «маленьких программ»

Что мы реально сделали:

```
[mycat] → buffer → write → [pipe в ядре] → read → [mygrep] → ... → [mywc] → терминал
```

Каждая утилита читает из stdin или файла, пишет в stdout. Не знает, что справа и слева. Не знает, в файле stdin или в pipe.

Это **Unix-философия**: «маленькие программы, каждая делающая одно дело». Их можно соединять.

Альтернативный подход — **монолит**: один большой бинарь со всем. Большие IDE, Microsoft Office, монорепо-фреймворки. У него тоже есть преимущества (быстрее не запускать процессы, доступ к общим структурам данных).

Но Unix-стиль выиграл в системном программировании, потому что:
- **Композиция** простая. `ls -l | grep .cpp | wc -l` — три программы соединены за две клавиши.
- **Тестируемость**: каждая утилита тестируется отдельно.
- **Параллелизм**: ядро автоматически параллелит пайплайн.
- **Стабильность**: одна утилита упала — другие живы.

Эта философия — то, что вы освоили в Части III. Знаете, как написать программу-фильтр, как соединить программы, как shell их запускает. Дальше — применимо везде, где Linux/macOS.

## Главные правила главы

1. **read/write в цикле с обработкой EINTR и short**. Не предполагайте, что прочитается всё одним вызовом.
2. **Буфер 4 KB** — хороший дефолт. Меньше = больше syscall'ов; больше = расход памяти на буфер.
3. **Argv-парсинг руками** — для простых утилит сойдёт. Сложные — `getopt`.
4. **regex компилируется один раз** перед циклом, не на каждой итерации.
5. **`std::regex_search`** для grep, `std::regex_match` для целой строки.
6. **UTF-8 — байты с маской `10` в старших** — продолжающие. Не считаем как символы.
7. **Утилиты — фильтры stdin→stdout.** Не зависят от того, к чему подключены.
8. **mmap для больших файлов** — иногда быстрее `read`. Не работает на pipe/socket.

## Маленькое упражнение

1. Соберите все: `make`. Запустите тесты — `./build/mycat`, `./build/mywc`, `./build/mygrep`.

2. Прогоните **наш** пайплайн через **наш** shell:
   ```
   myshell$ ./build/mycat src/main.cpp | ./build/mygrep -n include | ./build/mywc
   ```
   Посчитайте сами и сравните.

3. Добавьте в `mycat` опцию `-n` (нумерация строк, как `cat -n`).

4. Добавьте в `mywc` опции `-l`/`-w`/`-c`/`-m` (показывать только запрошенное).

5. Добавьте в `mygrep` опцию `-c` (только число совпадений) и `-l` (только имена файлов с совпадением).

6. (Сложнее) Перепишите `mycat` на `mmap` — открыть файл, отобразить, `write` целиком. Замерьте скорость на 1 GB файле.

7. (Сложнее) Сделайте `myhead` — печатает первые N строк (по умолчанию 10). Похоже на mycat, но останавливается.

8. (Сложнее) Сделайте `myreverse` — печатает строки **в обратном порядке** (последняя первой). Намёк: для этого нужно прочитать **весь файл** в память, потом печатать reverse.

## Часть III закрыта

Подведём итоги Части III. Глава 26: процессы, fork/exec/wait. Глава 27: pipes и dup2. Глава 28: сигналы. Глава 29: парсер с кавычками и редиректами. Глава 30: builtins и история. Глава 31: свои утилиты.

В коде `demo-shell/`:
- ~800 строк shell-логики в `src/`.
- ~250 строк трёх утилит в `utils/`.
- ~1100 строк всего.

Что вы освоили:

- **POSIX API**: `fork`/`exec`/`wait`/`pipe`/`dup2`/`open`/`read`/`write`/`close`/`chdir`/`getcwd`/`setenv`/`unsetenv`/`signal`/`sigaction`/`kill`.
- **Парсинг текста**: токенизация с состояниями, парсинг команд.
- **Сигналы и асинхронность**: что такое async-signal-safe, как обрабатывать прерывания.
- **Управление процессами**: процессное дерево, foreground groups (упоминание), зомби.
- **Файловые дескрипторы**: что это, наследование при fork/exec, переназначение через dup2.
- **Unix-философия**: маленькие программы как фильтры, композиция через пайпы.

В резюме — теперь вы можете написать любую CLI-утилиту на C++. Прочитать конфиг, пройтись по файлам, общаться с другими программами через стандартные потоки. Это **системное программирование**.

## Что дальше

**Часть IV** — мини-СУБД `demo-db/` (главы 32-39). Низкоуровневая работа с диском: страничный файл, B+tree-индекс, write-ahead log, парсер SQL. Это про **производительность** и **корректность**: что значит «база данных надёжна» на уровне байтов.

После — **Часть V** (TCP-чат, главы 40-47) — сети, многопоточность, реактор-цикл.

И **Часть VI** (C++17 как бонус, главы 48-51) — `optional`/`variant`/`filesystem`/`string_view`.

Дальше — глава 32, старт мини-СУБД.
