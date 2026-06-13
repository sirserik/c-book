# Глава 29. Парсер командной строки

Наш текущий shell разбирает строку через простой `tokenize` — split по пробелам с `|` как отдельный токен. Это работает для `ls -1 | wc`, но **не работает** для:

- `echo "hello world"` — пробел внутри строки должен быть частью одного аргумента.
- `echo hello > out.txt` — `>` это редирект, а не аргумент.
- `cat file.txt | grep "foo bar"` — кавычки и пайпы вместе.
- `ls 2> /dev/null` — отдельный редирект для stderr.

В этой главе сделаем настоящий парсер: с двойными кавычками, одинарными кавычками, escape через `\`, и редиректами `>`/`>>`/`<`/`2>`/`2>>`/`2>&1`. После этого shell сильно ближе к bash.

Парсинг **командной строки** — классическая задача. Идея та же, что в парсере мира (глава 23), но больше тонкостей.

## Что должен парсер

Возьмём строку:

```
grep "hello world" file.txt > result.txt 2>&1
```

Что должен выдать парсер:

- Команда: `grep`.
- Аргументы: `["grep", "hello world", "file.txt"]`.
- Редиректы:
  - `>` → файл `result.txt`.
  - `2>&1` → stderr туда же, куда stdout.

Структура:

```cpp
struct Redirect {
    enum Type { Input, Output, Append, ErrOutput, ErrAppend, ErrToOut };
    Type type;
    std::string target;
};

struct Command {
    std::vector<std::string> argv;
    std::vector<Redirect> redirects;
};

struct Pipeline {
    std::vector<Command> commands;
};
```

Pipeline — список Command. Command — argv + редиректы. Redirect — тип + файл.

Тип `ErrToOut` соответствует `2>&1` (нет файла — переключение fd).

## Этапы парсинга

Парсинг делится на два этапа:

1. **Tokenization** — превращение строки в список **токенов**: слова, кавычечные строки, спец-символы (`|`, `>`, `<`, `>>`, `2>`, ...).

2. **Parsing** — превращение списка токенов в дерево (Pipeline → Commands → argv/redirects).

Этапы независимы. Tokenizer не знает про команды и редиректы — он знает только про синтаксис строк, кавычек, спец-символов. Parser работает уже над токенами, знает про структуру команды.

Это разделение упрощает каждую часть. В больших парсерах (компиляторы) tokenizer и parser ещё более отделены, иногда tokenizer — генерируемая программа (lex/flex). У нас всё руками.

## Tokenizer

Главные требования:

1. **Пробелы и табы** разделяют слова, если **не внутри кавычек**.
2. **Двойные кавычки `"..."`** обрамляют строку. Внутри пробелы — часть слова. Внутри `\"`, `\\`, `\$`, `\\` — escape.
3. **Одинарные кавычки `'...'`** обрамляют **буквально**: никакого escape внутри.
4. **`\`** вне кавычек — escape следующего символа.
5. **`|`**, **`<`**, **`>`**, **`>>`** — отдельные токены.
6. **`2>`**, **`2>>`**, **`2>&1`** — отдельные токены, но **только в начале слова** (чтобы `file2.txt` не парсилось как `file` + `2>` + `.txt`).

Реализация — конечный автомат: для каждого символа смотрим, в каком состоянии (внутри кавычек или нет) и решаем, что делать.

```cpp
std::vector<std::string> tokenize(const std::string& line) {
    std::vector<std::string> tokens;
    std::string current;
    bool in_dq = false;   // в двойных кавычках
    bool in_sq = false;   // в одинарных

    auto flush = [&]() {
        if (!current.empty()) {
            tokens.push_back(std::move(current));
            current.clear();
        }
    };

    for (std::size_t i = 0; i < line.size(); ++i) {
        char c = line[i];

        if (in_sq) {
            if (c == '\'') in_sq = false;
            else current += c;
            continue;
        }

        if (in_dq) {
            if (c == '"') {
                in_dq = false;
            } else if (c == '\\' && i + 1 < line.size() &&
                       (line[i + 1] == '"' || line[i + 1] == '\\' ||
                        line[i + 1] == '$' || line[i + 1] == '`')) {
                current += line[++i];
            } else {
                current += c;
            }
            continue;
        }

        // Вне кавычек.
        if (c == '\'') { in_sq = true; continue; }
        if (c == '"')  { in_dq = true; continue; }

        if (c == '\\' && i + 1 < line.size()) {
            current += line[++i];
            continue;
        }

        if (c == ' ' || c == '\t') {
            flush();
            continue;
        }

        // 2>, 2>>, 2>&1 — только в начале слова.
        if (current.empty() && c == '2' && i + 1 < line.size() && line[i + 1] == '>') {
            if (i + 3 < line.size() &&
                line[i + 2] == '&' && line[i + 3] == '1') {
                tokens.push_back("2>&1");
                i += 3;
                continue;
            }
            if (i + 2 < line.size() && line[i + 2] == '>') {
                tokens.push_back("2>>");
                i += 2;
                continue;
            }
            tokens.push_back("2>");
            i += 1;
            continue;
        }

        // |, <, >, >>.
        std::string spec;
        std::size_t consumed = match_special(line, i, spec);
        if (consumed > 0) {
            flush();
            tokens.push_back(spec);
            i += consumed - 1;
            continue;
        }

        current += c;
    }

    if (in_sq || in_dq) {
        throw std::runtime_error("незакрытая кавычка");
    }

    flush();
    return tokens;
}
```

Разбор по блокам:

**Внутри одинарных кавычек** (`in_sq`) — только `'` закрывает, всё остальное буквально. `\'` внутри одинарных — нельзя. Если очень нужно — закрыть `'`, дописать `\'`, снова открыть `'`: `'don'\''t'` = `don't`. Bash-стиль.

**Внутри двойных кавычек** (`in_dq`) — `"` закрывает, `\` экранирует следующий **только** для `"`, `\`, `$`, `` ` ``. Другие escape-последовательности (`\n`, `\t`) внутри `"..."` **не** интерпретируются — будут литеральные `\n`. Это особенность bash; стандартная.

**Вне кавычек**:
- `\` экранирует следующий любой символ.
- Пробел/таб разделяют.
- Спец-символы становятся отдельными токенами.

Условие `current.empty()` для `2>` — критично. Без него `file2.txt` парсилось бы как `file` + `2>` + `.txt`. С условием — `file2.txt` остаётся одним словом, потому что когда мы видим `2`, `current` уже содержит `file`.

`match_special(line, i, spec)` распознаёт `|`, `<`, `>`, `>>`:

```cpp
std::size_t match_special(const std::string& s, std::size_t i, std::string& out) {
    char c = s[i];
    if (c == '|') { out = "|"; return 1; }
    if (c == '<') { out = "<"; return 1; }
    if (c == '>') {
        if (i + 1 < s.size() && s[i + 1] == '>') { out = ">>"; return 2; }
        out = ">"; return 1;
    }
    return 0;
}
```

Возвращает **количество поглощённых символов** (1 или 2). В вызывающей функции делаем `i += consumed - 1` (тоже + 1 в for-loop), и идём дальше.

### Незакрытые кавычки

В конце:

```cpp
if (in_sq || in_dq) {
    throw std::runtime_error("незакрытая кавычка");
}
```

Если строка кончилась, а кавычка не закрыта — синтаксическая ошибка. Bash в таком случае печатает дополнительный prompt `>` и ждёт продолжения. Мы — просто отказываем.

### Тестирование tokenize

```cpp
tokenize("echo hello")              // → {"echo", "hello"}
tokenize("echo \"hello world\"")    // → {"echo", "hello world"}
tokenize("ls | wc")                  // → {"ls", "|", "wc"}
tokenize("cmd > file.txt")           // → {"cmd", ">", "file.txt"}
tokenize("cmd 2>&1")                 // → {"cmd", "2>&1"}
tokenize("ls 2> err.txt")            // → {"ls", "2>", "err.txt"}
tokenize("echo a\\ b")               // → {"echo", "a b"}      (escape пробела)
tokenize("echo 'one \"two\"'")       // → {"echo", "one \"two\""}
```

## Parser

Теперь токены → Pipeline.

```cpp
Pipeline parse_pipeline(const std::string& line) {
    Pipeline p;
    auto tokens = tokenize(line);
    if (tokens.empty()) return p;

    auto seg_start = tokens.begin();
    for (auto it = tokens.begin(); it != tokens.end(); ++it) {
        if (*it == "|") {
            if (seg_start == it) {
                throw std::runtime_error("пустая команда перед '|'");
            }
            p.commands.push_back(parse_command(seg_start, it));
            seg_start = it + 1;
        }
    }
    if (seg_start != tokens.end()) {
        p.commands.push_back(parse_command(seg_start, tokens.end()));
    } else {
        throw std::runtime_error("пустая команда после '|'");
    }

    for (const auto& cmd : p.commands) {
        if (cmd.argv.empty()) {
            throw std::runtime_error("команда без названия");
        }
    }
    return p;
}
```

Идём по токенам, разбиваем на сегменты по `|`. Каждый сегмент идёт в `parse_command`.

```cpp
Command parse_command(std::vector<std::string>::iterator begin,
                      std::vector<std::string>::iterator end) {
    Command cmd;
    for (auto it = begin; it != end; ++it) {
        if (is_redirect_token(*it)) {
            Redirect r;
            r.type = redirect_type_for(*it);
            if (r.type == Redirect::ErrToOut) {
                // 2>&1 — без аргумента
            } else {
                if (it + 1 == end || is_redirect_token(*(it + 1)) || *(it + 1) == "|") {
                    throw std::runtime_error("редирект '" + *it + "' без файла");
                }
                ++it;
                r.target = *it;
            }
            cmd.redirects.push_back(std::move(r));
        } else {
            cmd.argv.push_back(*it);
        }
    }
    return cmd;
}
```

Идём по токенам:
- Если редирект — следующий токен это файл (кроме `2>&1`). Сохраняем.
- Иначе — слово, добавляем в `argv`.

Проверки: после редиректа должен быть файл, не другой редирект и не `|`.

## Применение редиректов

После `fork`, перед `exec`, в ребёнке вызываем `apply_redirects`:

```cpp
void apply_redirects(const std::vector<Redirect>& redirects) {
    for (const auto& r : redirects) {
        switch (r.type) {
            case Redirect::Input: {
                int fd = open(r.target.c_str(), O_RDONLY);
                if (fd < 0) {
                    std::cerr << r.target << ": " << std::strerror(errno) << "\n";
                    _exit(1);
                }
                dup2(fd, STDIN_FILENO);
                close(fd);
                break;
            }
            case Redirect::Output: {
                int fd = open(r.target.c_str(),
                              O_WRONLY | O_CREAT | O_TRUNC, 0644);
                // ... аналогично, dup2 в STDOUT_FILENO
            }
            case Redirect::Append: {
                int fd = open(r.target.c_str(),
                              O_WRONLY | O_CREAT | O_APPEND, 0644);
                // ... dup2 в STDOUT_FILENO
            }
            case Redirect::ErrOutput: {
                // ... open + dup2 в STDERR_FILENO
            }
            case Redirect::ErrAppend: {
                // O_APPEND + dup2 в STDERR_FILENO
            }
            case Redirect::ErrToOut: {
                dup2(STDOUT_FILENO, STDERR_FILENO);
                break;
            }
        }
    }
}
```

Каждый редирект:
1. Открыть файл с правильными флагами (`O_RDONLY` для чтения, `O_WRONLY | O_CREAT | O_TRUNC` для перезаписи, `O_WRONLY | O_CREAT | O_APPEND` для дописывания).
2. `dup2` дескриптор на нужный stdin/stdout/stderr.
3. Закрыть исходный fd.

**Третий аргумент `open`** (`0644`) — режим доступа для нового файла. `644` в восьмеричной = `rw-r--r--` (владелец читает/пишет, остальные читают). Учили в главе 5.

`ErrToOut` (`2>&1`) — просто `dup2(STDOUT_FILENO, STDERR_FILENO)`. После этого stderr идёт туда же, куда stdout.

### Порядок важен

```cpp
cmd > out.txt 2>&1
```

Это:
1. Открыть `out.txt`, `dup2(fd, 1)` — stdout идёт в файл.
2. `dup2(1, 2)` — stderr тоже идёт куда stdout (то есть в файл).

**Итог**: обе потока в `out.txt`.

```cpp
cmd 2>&1 > out.txt
```

Это:
1. `dup2(1, 2)` — stderr туда же, куда сейчас stdout (терминал!).
2. `open out.txt`, `dup2(fd, 1)` — stdout идёт в файл.

**Итог**: stderr остаётся на терминале (потому что в момент `2>&1` он направлен туда), stdout в файл.

Это **классический подвох bash**. Запомните: `> file 2>&1` — оба в файл. `2>&1 > file` — нет.

Наш парсер сохраняет порядок редиректов в `vector`, и `apply_redirects` применяет их по очереди. То же поведение, что в bash.

## Редиректы и пайпы

Что если в одной команде и пайп, и редирект?

```bash
$ ls > file.txt | wc -l
```

Тут `ls` имеет два направления вывода: пайп в `wc` и файл. Что побеждает?

По стандарту bash — **последний** редирект побеждает. Для `ls > file.txt`:
1. Сначала ls's stdout идёт в pipe (наш код dup2 от pipe в STDOUT).
2. Потом `apply_redirects` открывает `file.txt` и dup2 в STDOUT.

Итог: stdout `ls` идёт в `file.txt`. Pipe в `wc` остаётся пустым (read end ждёт EOF). `wc -l` напечатает `0`.

В нашем коде так и происходит, потому что мы вызываем `apply_redirects` **после** dup2 пайпов:

```cpp
if (pid == 0) {
    restore_default_signal_handlers();

    if (i > 0) dup2(pipes[i - 1][0], STDIN_FILENO);
    if (i < n - 1) dup2(pipes[i][1], STDOUT_FILENO);
    close_all_pipes(pipes);

    apply_redirects(p.commands[i].redirects);   // ← после пайп-dup2

    auto argv = to_c_argv(p.commands[i].argv);
    execvp(argv[0], argv.data());
    _exit(127);
}
```

## Чего парсер пока не умеет

В книге это упомяну, но реализовывать не буду — оставлю как упражнения.

### Последовательность команд: `;`, `&&`, `||`

```bash
$ cmd1 ; cmd2
$ cmd1 && cmd2     # cmd2 если cmd1 успешно
$ cmd1 || cmd2     # cmd2 если cmd1 ошибка
```

Это **отдельный уровень** парсинга. Pipeline становится «вершиной» в дереве:

```
Sequence
├── Pipeline (cmd1 | cmd2)
├── &&
├── Pipeline (cmd3)
├── ||
└── Pipeline (cmd4)
```

Для реализации — отдельный enum типов соединителей, отдельная функция `parse_sequence`. Не сложно, просто ещё уровень.

### Подстановка переменных: `$HOME`

```bash
$ echo $HOME
/Users/serik
$ echo "${HOME}/file"
/Users/serik/file
```

Tokenizer должен распознать `$VAR` (или `${VAR}`) и **подставить значение** из окружения. `getenv("HOME")` — функция для этого.

Особенность: **только внутри двойных кавычек и без кавычек**. Внутри одинарных — буквально.

```bash
$ echo "$HOME"     # /Users/serik
$ echo '$HOME'     # $HOME
```

Подстановка делается после tokenize или прямо во время него — обе схемы работают.

### Globbing: `*`, `?`, `[abc]`

```bash
$ ls *.cpp
foo.cpp bar.cpp
```

Это **wildcard expansion**. После tokenize, перед exec — каждое слово с `*`/`?`/`[]` развёртывается в список файлов. Стандартная функция — `glob()` из `<glob.h>`.

В нашем shell — не сделаем. Если нужно: возьмите glob() и разверните слова с шаблонами.

### `< /dev/null`, heredoc `<<EOF`

```bash
$ cmd < /dev/null
$ cmd << EOF
hello
EOF
```

`< file` уже работает у нас. `<<EOF` — heredoc — это особая фишка: содержимое до `EOF` передаётся как stdin. Парсер должен обрабатывать **многострочный ввод**, что усложняет REPL.

Не реализуем; для простых нужд `echo X | cmd` или временный файл достаточно.

### `&` фон

```bash
$ sleep 10 &
[1] 1234
$ jobs
[1]  Running    sleep 10
```

`&` означает «запусти и не жди». Shell сразу возвращается к prompt. Чтобы это сделать:
- В `run_pipeline` ребёнок не ждётся через `waitpid` сразу.
- Shell ведёт список запущенных в фоне процессов.
- `SIGCHLD` обрабатывается асинхронно, чтобы обнаружить умерших.
- Команда `jobs` показывает список, `fg N` — вернуть на передний план.

Это **job control** — целая отдельная подсистема. У нас выходит за рамки. Глава 30 коснёт лишь его минимума.

## Запуск

```bash
$ make
$ ./build/myshell

myshell$ echo "hello world"
hello world

myshell$ echo hello > /tmp/out.txt
myshell$ cat /tmp/out.txt
hello

myshell$ ls Makefile > /tmp/out.txt
myshell$ cat /tmp/out.txt
Makefile

myshell$ echo 'single quotes test'
single quotes test

myshell$ ls nonexistent 2> /tmp/err.txt
[exit 1]
myshell$ cat /tmp/err.txt
ls: nonexistent: No such file or directory

myshell$ echo "with pipe" | tr a-z A-Z
WITH PIPE

myshell$ ls 2>&1 | grep .cpp
exec_runner.cpp
main.cpp
pipeline.cpp
signals.cpp

myshell$ exit
```

Всё работает.

## Архитектура парсера в целом

Если посмотреть на нашу систему сверху:

```
Строка пользователя
       ↓
   tokenize  ← состояние: вне/внутри кавычек, escape
       ↓
   список токенов
       ↓
parse_pipeline ← разбивает по `|`, валидирует
       ↓
parse_command  ← собирает argv + redirects
       ↓
   Pipeline (commands + redirects)
       ↓
   run_pipeline ← системные вызовы fork/dup2/exec
       ↓
   результат
```

Это **классическая трёхступенчатая схема** компилятора (в миниатюре):
1. **Lexer** (наш tokenize) — текст → токены.
2. **Parser** (наш parse_pipeline) — токены → AST.
3. **Interpreter / Code generator** (наш run_pipeline) — AST → действие.

Те же три фазы в gcc, в python (CPython), в ваших любимых интерпретаторах.

## Главные правила главы

1. **Разделяйте lexer и parser** — упрощает каждую часть.
2. **Tokenizer = state machine.** Состояния: внутри кавычек, escape.
3. **Спец-символы определяются позицией.** `2>` только в начале слова.
4. **Парсер строит AST**, не выполняет действия.
5. **Применение редиректов — после пайп-dup2** в ребёнке.
6. **Порядок редиректов важен!** `> file 2>&1` ≠ `2>&1 > file`.
7. **Хорошие сообщения об ошибках** парсера полезны пользователям.
8. **Не реализуйте всё bash-фичи разом.** Сделайте core, остальное — по запросу.

## Маленькое упражнение

1. Соберите shell. Попробуйте:
   - `echo "Hello, World"` (с запятой и пробелом).
   - `echo it\'s` (escape апострофа).
   - `ls > /tmp/a.txt && cat /tmp/a.txt` (sequencing — а, нет, у нас не работает! проверьте, и какая ошибка).

2. Перенаправьте только stderr на файл: `python3 -c "import sys; sys.stderr.write('error\n'); sys.stdout.write('out\n')" 2> /tmp/err.txt`. Проверьте, что `out` в stdout, а `error` в `/tmp/err.txt`.

3. Сравните `cmd > /tmp/x 2>&1` и `cmd 2>&1 > /tmp/x`. Создайте программу `errout` (на C++), которая печатает в stdout И stderr. Запустите обе версии. Посмотрите, что получилось в файле и на экране.

4. (Сложнее) Реализуйте подстановку переменных `$VAR`. Добавьте в tokenizer развёртывание перед добавлением в `current`. Внутри одинарных кавычек — не разворачивать.

5. (Сложнее) Реализуйте `;` (последовательность). Парсер должен возвращать `std::vector<Pipeline>`. main выполняет их по очереди, **независимо от кодов возврата**.

6. (Сложнее) Реализуйте `&&` и `||`. Парсер строит дерево или плоский список с типами связок. Выполнение учитывает код возврата предыдущей команды.

7. (Очень сложно) Реализуйте `&` (фон). Нужен SIGCHLD-обработчик, список фоновых задач, команда `jobs`.

## Что дальше

Глава 30 — **builtin-команды и история**. Команды `cd`, `export`, `pwd` нельзя реализовать через `fork+exec` (они должны менять состояние shell — текущую папку или окружение). Их делают «внутри» shell — встроенными. Заодно сделаем хранение истории команд (стрелка вверх не сработает без сырого termios, но запись в `~/.myshell_history` — можно).

Глава 31 — свои утилиты `cat`, `wc`, `grep` написанные с нуля. Поймём, как утилиты Unix устроены изнутри. После этого Часть III закроется.
