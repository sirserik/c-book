# Глава 30. Builtins и история

К этому моменту наш shell умеет запускать любую внешнюю программу: пайпы, редиректы, кавычки, корректная обработка сигналов. Но что произойдёт, если вы наберёте `cd /tmp`? Запустится `/bin/cd` (или вернётся «не найден»). И даже если бы `/bin/cd` существовал — он бы поменял **свою** папку, не нашего shell. После завершения ребёнка shell остаётся в той же папке. Эффекта ноль.

То же с `export PATH=...`: ребёнок ставит переменную и умирает, родителю по умолчанию ничего не передастся.

Эти команды должны **выполняться в самом shell**, не в ребёнке. Они называются **builtins** — встроенные команды. В этой главе мы их добавим: `cd`, `pwd`, `export`, `unset`, `echo`, `history`. Плюс сохранение истории в `~/.myshell_history`.

## Зачем builtins

Часть команд **не может** работать через fork+exec, потому что они должны **менять состояние самого shell**:

- **`cd`** — менять рабочую папку shell. Ребёнок не может изменить cwd родителя.
- **`export`/`unset`** — менять переменные окружения shell. Ребёнок только меняет свои.
- **`exit`** — завершать сам shell.
- **`alias`** — добавлять алиасы для будущих команд в этом же shell.
- **`source`/`.`** — выполнять скрипт в текущем процессе.

Другие команды **могут** быть и встроенными, и внешними. У bash есть встроенные `echo`, `pwd`, `kill` — для скорости (не нужен `fork+exec`). Внешние `/bin/echo` и `/bin/pwd` тоже существуют. Builtin побеждает, если команда есть в обеих формах.

Третий вариант — **builtin для удобства**:
- **`history`** — показать сохранённые команды (которые знает только shell).
- **`jobs`/`fg`/`bg`** — управление фоновыми задачами.
- **`help`** — справка.

## API: try_builtin

В нашем коде:

```cpp
bool try_builtin(const std::vector<std::string>& argv, int& out_code);
```

Логика:
1. Если `argv[0]` — известная встроенная команда, выполнить её, положить код в `out_code`, вернуть `true`.
2. Иначе вернуть `false` — пусть вызывающий запускает через exec.

В `main` сначала пробуем builtin, потом — внешнюю:

```cpp
if (p.commands.size() == 1 && p.commands[0].redirects.empty()) {
    int code = 0;
    if (shell::try_builtin(first, code)) {
        if (code != 0) std::cerr << "[exit " << code << "]\n";
        continue;
    }
}

int rc = shell::run_pipeline(p);
```

**Условие**: builtin работает только если **одна команда** и **без редиректов**. Иначе делаем fork (как обычно).

### Почему так

Если в пайплайне (`echo hi | cd /tmp`) — `cd` в ребёнке. Эффекта на shell нет.

С редиректами (`cd /tmp > log.txt`) — теоретически можно: выполнить `cd` напрямую в shell, открыть файл и записать «что-то». Но bash тоже не делает builtins с редиректами в shell — он форкается. Это спорное место, мы упрощаем.

## Builtins реализации

### cd

```cpp
int builtin_cd(const std::vector<std::string>& argv) {
    std::string target;
    if (argv.size() < 2) {
        const char* home = std::getenv("HOME");
        if (!home) {
            std::cerr << "cd: HOME не задана\n";
            return 1;
        }
        target = home;
    } else {
        target = argv[1];
    }
    if (chdir(target.c_str()) < 0) {
        std::cerr << "cd: " << target << ": " << std::strerror(errno) << "\n";
        return 1;
    }
    return 0;
}
```

Без аргумента — в домашнюю папку (`$HOME`). С аргументом — туда. `chdir(path)` — системный вызов, меняет cwd текущего процесса. Возвращает 0 при успехе, -1 при ошибке.

`std::getenv("HOME")` — функция из `<cstdlib>`, читает переменную окружения. Возвращает указатель или `nullptr`.

В bash `cd -` означает «предыдущая папка», `cd ~user` — домашняя другого пользователя, и так далее. Мы — минимум.

### pwd

```cpp
int builtin_pwd() {
    char buf[4096];
    if (!getcwd(buf, sizeof(buf))) {
        std::cerr << "pwd: " << std::strerror(errno) << "\n";
        return 1;
    }
    std::cout << buf << "\n";
    return 0;
}
```

`getcwd(buf, size)` — системный вызов, кладёт в `buf` текущую папку. Возвращает указатель на `buf` при успехе, `nullptr` при ошибке (например, буфер маленький — `errno == ERANGE`).

`PATH_MAX` на Linux обычно 4096 байт. 4 KiB достаточно для разумных глубин.

### export

```cpp
int builtin_export(const std::vector<std::string>& argv) {
    if (argv.size() < 2) {
        std::cerr << "export: укажите KEY=VALUE\n";
        return 1;
    }
    for (std::size_t i = 1; i < argv.size(); ++i) {
        const std::string& kv = argv[i];
        auto eq = kv.find('=');
        if (eq == std::string::npos) {
            std::cerr << "export: ожидался формат KEY=VALUE: " << kv << "\n";
            return 1;
        }
        std::string key = kv.substr(0, eq);
        std::string val = kv.substr(eq + 1);
        if (setenv(key.c_str(), val.c_str(), 1) < 0) {
            std::cerr << "export: " << std::strerror(errno) << "\n";
            return 1;
        }
    }
    return 0;
}
```

`setenv(key, val, overwrite)` — установить переменную окружения. Третий аргумент: если 0, не перезаписывать существующую; если 1, перезаписать. Мы перезаписываем.

В bash `export PATH=$PATH:/new/path` использует **подстановку** `$PATH`. Мы её не реализовали (см. главу 29). Так что у нас `export PATH=$PATH:/x` даст буквальную строку `$PATH:/x`. Чтобы это работало — реализовать подстановку в tokenizer.

`unsetenv` — обратная операция:

```cpp
int builtin_unset(const std::vector<std::string>& argv) {
    for (std::size_t i = 1; i < argv.size(); ++i) {
        unsetenv(argv[i].c_str());
    }
    return 0;
}
```

### echo

В bash `echo` — и builtin, и внешний (`/bin/echo`). Builtin быстрее. Реализуем:

```cpp
int builtin_echo(const std::vector<std::string>& argv) {
    bool newline = true;
    std::size_t start = 1;
    if (argv.size() > 1 && argv[1] == "-n") {
        newline = false;
        start = 2;
    }
    for (std::size_t i = start; i < argv.size(); ++i) {
        if (i > start) std::cout << " ";
        std::cout << argv[i];
    }
    if (newline) std::cout << "\n";
    return 0;
}
```

Поддерживаем `-n` (без перевода строки на конце). Не поддерживаем `-e` (interpret escapes) — у нас уже tokenizer обрабатывает escape.

### exit

`exit` мы обрабатываем **до** try_builtin — это особый случай, заканчивает цикл shell:

```cpp
if (!first.empty() && (first[0] == "exit" || first[0] == "quit")) {
    break;
}
```

Можно было бы сделать builtin, но он бы вызвал `std::exit(N)` — это пропускает деструкторы и `atexit`. Лучше явное `break` из цикла.

## История команд

Простая модель: список строк в памяти. Сохраняем в файл при выходе, загружаем при старте.

### В памяти

```cpp
namespace {
    std::vector<std::string> g_history;
}

void history_add(const std::string& line) {
    if (line.empty()) return;
    if (!g_history.empty() && g_history.back() == line) return;
    g_history.push_back(line);
}
```

`if (g_history.back() == line)` — не сохраняем дубликаты подряд. Полезно, если кто-то много раз нажимает Enter на одной команде.

### Загрузка/сохранение

```cpp
void history_load(const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) return;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty()) g_history.push_back(line);
    }
}

void history_save(const std::string& path) {
    std::ofstream out(path);
    if (!out.is_open()) return;
    for (const auto& line : g_history) {
        out << line << "\n";
    }
}
```

Простой текстовый формат: одна команда на строку. Если файл не открылся (нет прав, нет папки) — молча игнорируем. Это **не критическая** функция.

### Builtin history

```cpp
int builtin_history() {
    std::size_t i = 1;
    for (const auto& line : g_history) {
        std::cout << "  " << i << "  " << line << "\n";
        ++i;
    }
    return 0;
}
```

Печатает с номерами.

### В main

```cpp
namespace {
    std::string history_path() {
        const char* home = std::getenv("HOME");
        if (!home) return "./.myshell_history";
        return std::string(home) + "/.myshell_history";
    }
}

int main() {
    shell::install_shell_signal_handlers();
    shell::history_load(history_path());

    while (...) {
        // ...
        shell::history_add(line);
        // ...
    }
    
    shell::history_save(history_path());
    return 0;
}
```

Загружаем при старте, добавляем после каждой команды, сохраняем при выходе.

### Альтернативный подход

Большие shell'ы (bash, zsh) пишут историю **сразу** после команды, не на выходе. Это надёжнее: если shell упадёт — история уцелеет.

Реализация — открыть файл в append-режиме, дописать строку, закрыть. На каждую команду.

В нашем коде проще: при выходе. Если shell умирает аварийно — потеря.

## Навигация стрелками — кратко

Если запустить `bash`, стрелка вверх возвращает предыдущую команду. У нас — нет. Стрелка вверх в `std::getline` даст коды `^[[A` (escape-последовательность).

Чтобы реализовать как bash, нужно:

1. **Поставить терминал в raw mode** через `tcsetattr(STDIN, TCSANOW, ...)` с отключённым `ICANON` и `ECHO`.
2. **Читать по одному символу** через `read(STDIN, &c, 1)`.
3. **Распознавать escape-последовательности**: `\033[A` — стрелка вверх, `\033[B` — вниз, `\033[C` — вправо, `\033[D` — влево.
4. **Управлять курсором** через эти же escape-последовательности (`\033[K` — стереть остаток строки, `\033[<n>D` — сдвинуть курсор влево на n).
5. **Восстановить терминал** при выходе.

Это **большая** работа. Готовое решение — **GNU readline** (или **libedit**). Подключаешь, и она делает всё: история, автодополнение, многострочный ввод, поиск Ctrl+R, и так далее.

В нашем учебном — без readline. Стрелочки не работают, но при наборе команды есть Ctrl+U (стереть всю строку — стандартный shortcut терминала). Для учебной цели достаточно.

## Job control — кратко

Что делает реальный shell для фоновых задач (`cmd &`):

1. **`setpgid`** в ребёнке — поместить его в **свою** группу процессов.
2. **`tcsetpgrp`** не вызывать (фоновая задача не владеет терминалом).
3. **Не делать `waitpid`** сразу — ребёнок идёт в фон.
4. Когда команда `jobs` — печатать список фоновых.
5. `fg N` — `tcsetpgrp(terminal, pgid)` + `waitpid`.
6. **`SIGCHLD`** — асинхронно обновлять статус (живой/умер).

Это серьёзная инфраструктура. У нас оставим как упражнение. Если интересно — посмотрите исходники mksh или dash.

## Что пользователь видит

```bash
$ ./build/myshell
=== myshell ===
Builtins: cd, pwd, export, unset, history, echo, exit.

myshell$ pwd
/Users/serik/Desktop/cpp-from-scratch/demo-shell

myshell$ cd /tmp
myshell$ pwd
/private/tmp

myshell$ export FOO=bar
myshell$ env | grep FOO
FOO=bar

myshell$ cd
myshell$ pwd
/Users/serik

myshell$ history
  1  pwd
  2  cd /tmp
  3  pwd
  4  export FOO=bar
  5  env | grep FOO
  6  cd
  7  pwd
  8  history

myshell$ exit
$ cat ~/.myshell_history
pwd
cd /tmp
pwd
export FOO=bar
env | grep FOO
cd
pwd
history
exit
```

Builtins работают. История сохранилась в `~/.myshell_history`. На следующем запуске она загрузится.

Заметьте: после `cd /tmp` команда `pwd` показывает `/private/tmp`. Это симлинк macOS (`/tmp` — это `/private/tmp`). На Linux была бы просто `/tmp`. `chdir` следует симлинкам.

## Builtins vs внешние — производительность

Запустить `echo hi` через builtin — ноль системных вызовов, кроме `write` для вывода. Через exec — fork (тысячи микросекунд), exec (загрузка `/bin/echo`, инициализация runtime), wait. Разница в **сотни раз**.

Поэтому популярные команды (`echo`, `printf`, `test`, `[`, `pwd`) **всегда** делают builtins в любом shell.

Поэтому же скрипты с тысячами `[ "$x" = "y" ]` работают быстро в bash — это builtin, не внешний `/usr/bin/[` (хотя последний существует ради совместимости).

## Главные правила главы

1. **Команды, меняющие состояние shell** (`cd`, `export`, `unset`), обязаны быть builtins.
2. **Try-builtin до fork+exec** — порядок важен.
3. **Builtins в пайплайне** не должны быть «настоящими» (мы упрощаем — пайплайны через exec).
4. **История в текстовом файле** — простой формат, легко расширять.
5. **Дедупликация подряд идущих** — не сохраняем одинаковые команды.
6. **Сохраняйте сразу** после команды для устойчивости к крэшу (наш код сохраняет на выходе — компромисс).
7. **Стрелки вверх/вниз** — отдельная инфраструктура с raw termios. Готовое решение — readline.
8. **Job control (`&`/jobs/fg/bg)** — отдельная подсистема с группами процессов.

## Маленькое упражнение

1. Соберите. Попробуйте `cd /tmp && pwd && ls`. У нас `&&` не работает — три команды на отдельных строках.

2. Добавьте builtin `alias`: `alias ll="ls -l"`. Парсер должен распознать `ll` как алиас и заменить на `ls -l` перед исполнением. Подсказка — глобальный `std::unordered_map<std::string, std::string>`.

3. Добавьте `cd -` — переход в предыдущую папку. Подсказка — хранить предыдущую папку в глобальной переменной.

4. Реализуйте подстановку переменных в tokenizer (см. глава 29, упражнение 4). После этого `export PATH=$PATH:/new` будет работать.

5. Сделайте сохранение истории **сразу** после каждой команды (append-режим), не на выходе. Сравните производительность для большого количества команд.

6. Добавьте `history -c` — очистить историю.

7. (Сложнее) Подключите libreadline. Установите `brew install readline` или `apt install libreadline-dev`. Замените `std::getline` на `readline("myshell$ ")`. Получите стрелочки, Ctrl+R, автодополнение по командам в `$PATH`.

8. (Сложнее) Реализуйте `&` фон. Минимально: после команды с `&` — не вызывать `waitpid`, но сохранить pid; команда `jobs` печатает список; при выходе из shell завершить всех. Job control полный — целая глава отдельной книги.

## Что дальше

Глава 31 — **свои утилиты cat, wc, grep**. Напишем три классические Unix-утилиты с нуля на C++. Это будет упражнение в **чтении файлов** на низком уровне, **обработке текста**, и **передаче через стандартные потоки**. После этого Часть III закроется, и мы перейдём к Части IV — мини-СУБД.
