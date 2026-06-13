# Глава 24. Сохранение и загрузка игры

Текущее состояние игры — это **снимок** мира и игрока в момент времени. Чтобы можно было выключить программу и продолжить позже — нужно этот снимок куда-то сохранить и потом восстановить. В этой главе сделаем систему сохранений: формат файла, версионирование, контрольную сумму. Добавим команды `save <slot>` и `load <slot>`.

Тема очень практическая. Любая нетривиальная программа рано или поздно сталкивается с **сериализацией**: сохранением состояния. Игры, базы данных, мессенджеры, графические редакторы — все хранят что-то на диске.

## Что сохранять

Состояние нашей игры состоит из:

- **Игрок**: имя, текущее hp, max hp, инвентарь (список предметов).
- **Текущая локация**: id.
- **Мир**: остаточные предметы по локациям (часть взяли — этих предметов больше нет).

Что **не** сохранять:
- Имена локаций, описания, выходы — это статика из `world.txt`.
- Список команд, обработчики — это код.

То есть сохраняется **разница** между «свежим миром из `world.txt`» и «текущим состоянием». Это уменьшает размер файла и облегчает миграции (если поменяем мир — старые сейвы продолжат работать в том, что не задели).

## Текстовый или бинарный формат

Два подхода.

**Бинарный**:
- Записываем байты значений напрямую (через `out.write(&value, sizeof(value))`).
- Компактно, быстро.
- Нечитаемо без декодера.
- Зависит от endianness, размеров типов.

**Текстовый**:
- Каждое значение — строка.
- Больше места, медленнее.
- Читается глазами.
- Легко версионировать и отлаживать.

Для нашей RPG — **текстовый**. Размер сейва — килобайты, скорость неважна. Зато можно открыть в редакторе и посмотреть, что не так. В мини-СУБД (Часть IV) будет бинарный — там скорость и размер критичны.

## Формат сейва

```
SAVE 1
name Герой
hp 20 20
loc mountain
inv weapon ржавый_меч 5 8
wloc cave weapon боевой_топор 7 12
wloc cave armor стальной_щит 6 6
wloc mountain consumable вода 1 3
wloc forest armor кожаный_нагрудник 8 4
wloc village consumable зелье_лечения 1 15
wloc village consumable хлеб 1 5
CHK 5e54e64c
```

Структура:

- **`SAVE <version>`** — первая строка, версия формата.
- **`name <player_name>`** — имя игрока (`_` вместо пробелов).
- **`hp <current> <max>`** — здоровье.
- **`loc <location_id>`** — где игрок.
- **`inv <type> <name> <weight> <extra>`** — предмет в инвентаре. Несколько строк.
- **`wloc <loc_id> <type> <name> <weight> <extra>`** — предмет, оставшийся в локации.
- **`CHK <8_hex_digits>`** — контрольная сумма для проверки целостности.

Похоже на формат мира из главы 23, но с другими ключами. Один и тот же стиль.

## Зачем версия

Программа развивается. Завтра вы добавите экипировку — у `Player` появится поле `equipped_weapon`. Старые сейвы не знают о нём.

**Без версии**: чтение крашится на отсутствии новых полей. Игрок теряет прогресс.

**С версией**: программа смотрит, какая версия в файле, и применяет соответствующую логику парсинга. Например:
- v1 — без экипировки.
- v2 — с экипировкой; для v1-сейвов считать «не экипирован».

В нашем коде:

```cpp
namespace save {
    constexpr int CURRENT_VERSION = 1;
    // ...
}
```

При загрузке проверяем:

```cpp
if (ver != CURRENT_VERSION) {
    throw GameError("несовместимая версия сейва: " + std::to_string(ver));
}
```

Это **строгая** реакция. Альтернатива — **миграции**: при v < CURRENT_VERSION вызывать функцию `migrate_v1_to_v2`, потом загружать как v2. Так делают серьёзные приложения. У нас пока строгое отклонение.

## Зачем контрольная сумма

Файлы повреждаются. Прервалась запись (отключили питание), повредился диск, кто-то открыл в редакторе и неудачно правил. Загружать **повреждённый** сейв опасно — можно получить непредсказуемое состояние, краш, или хуже — испорченную игровую логику.

**Контрольная сумма** (checksum) — функция, дающая на основе содержимого короткое значение. Если содержимое изменилось — значение тоже. На загрузке сравниваем сохранённую сумму с пересчитанной по содержимому. Не сходится — отказ.

Простые алгоритмы:
- **Сумма байтов** — очень простая, плохо ловит «компенсирующие» изменения.
- **CRC32** — стандартное, точное обнаружение.
- **Хеш-функции** (MD5, SHA-1) — криптографические, но избыточны для нашей задачи.

У нас — простой 32-битный «накатывающий хеш»:

```cpp
std::uint32_t checksum(const std::string& text) {
    std::uint32_t sum = 0;
    for (char ch : text) {
        sum = sum * 31u + static_cast<unsigned char>(ch);
    }
    return sum;
}
```

Это **не криптография**. Злонамеренный пользователь подделает её за минуту. Защита только от **случайных** повреждений. Для нашей цели — достаточно.

`31` — стандартный множитель в хешах строк (используется в Java `String.hashCode`). `unsigned char` — чтобы избежать UB при отрицательных значениях `char` (см. главу 8).

## API менеджера сохранений

`include/save_manager.h`:

```cpp
namespace rpg {
namespace save {

constexpr int CURRENT_VERSION = 1;

void write(const std::string& path,
           const Player& player,
           const World& world,
           const std::string& current_location_id);

void read(const std::string& path,
          Player& player,
          World& world,
          std::string& current_location_id);

}  // namespace save
}  // namespace rpg
```

Две функции: `write` и `read`. Оба бросают `GameError` при проблемах.

Заметьте: `save` — это **внутреннее пространство имён** внутри `rpg`. Так не «загрязняем» основной namespace. Использование: `rpg::save::write(...)`.

## Реализация write

```cpp
void write(const std::string& path,
           const Player& player,
           const World& world,
           const std::string& current_location_id) {
    std::ostringstream body;
    body << "SAVE " << CURRENT_VERSION << "\n";
    body << "name " << escape(player.name()) << "\n";
    body << "hp " << player.hp() << " " << player.max_hp() << "\n";
    body << "loc " << current_location_id << "\n";

    for (const auto& item : player.inventory().items()) {
        body << "inv " << item_line(*item) << "\n";
    }
    for (const auto& kv : world.locations()) {
        const Location& loc = kv.second;
        for (const auto& item : loc.items().items()) {
            body << "wloc " << loc.id() << " " << item_line(*item) << "\n";
        }
    }

    std::string content = body.str();
    std::uint32_t sum = checksum(content);

    std::ofstream out(path);
    if (!out.is_open()) {
        throw GameError("не открыт для записи: " + path);
    }
    out << content << "CHK " << to_hex(sum) << "\n";
}
```

Алгоритм:

1. **Собираем тело в `stringstream`**, чтобы посчитать checksum.
2. Каждое поле — одна строка.
3. Инвентарь игрока через цикл.
4. Все локации мира через `world.locations()` (`World` теперь возвращает свой `unordered_map`).
5. **Считаем checksum от тела**.
6. **Записываем в файл**: тело + строка `CHK <hex>`.

Зачем сначала в `stringstream`, а потом сразу в файл? Чтобы:
1. Посчитать checksum от того же текста, что попадёт в файл.
2. Если запись частично прошла и оборвалась — можно повторить.

### Сериализация предмета

```cpp
std::string item_line(const Item& it) {
    std::ostringstream ss;
    if (auto w = dynamic_cast<const Weapon*>(&it)) {
        ss << "weapon " << escape(w->name()) << " " << w->weight() << " " << w->damage();
    } else if (auto a = dynamic_cast<const Armor*>(&it)) {
        ss << "armor " << escape(a->name()) << " " << a->weight() << " " << a->defense();
    } else if (auto c = dynamic_cast<const Consumable*>(&it)) {
        ss << "consumable " << escape(c->name()) << " " << c->weight() << " " << c->heal_amount();
    } else {
        throw GameError("неизвестный тип предмета при сохранении");
    }
    return ss.str();
}
```

Через `dynamic_cast` определяем реальный тип и пишем нужное «extra» поле (урон для оружия, защиту для брони, лечение для расходника).

Этот код **дублирует** логику парсера из главы 23 — там по типу мы создавали потомков, здесь по типу пишем. Это типичная неприятность сериализации: чтение и запись должны быть **синхронны**.

Альтернативно можно было бы добавить виртуальный метод `Item::serialize(std::ostream&)`, и `Weapon`/`Armor`/`Consumable` его переопределили бы. Это чище — логика типа лежит в самом классе. Но требует трогать `Item.h`. Для учебного — `dynamic_cast` проще.

### Запись в файл

```cpp
std::ofstream out(path);
if (!out.is_open()) {
    throw GameError("не открыт для записи: " + path);
}
```

Если файл не открылся — бросаем. Чаще всего причина — нет папки `data/saves/`.

### Эскейпинг

`escape(name)` заменяет пробелы на `_`. На загрузке `unescape` обратно. Если бы оставить пробелы, поток `>>` разделил бы имя на части и парсинг сломался.

Альтернатива — заключать имена в кавычки и парсить их специально. Делать сложнее, у нас обходимся подчёркиваниями.

## Реализация read

```cpp
void read(const std::string& path,
          Player& player,
          World& world,
          std::string& current_location_id) {
    std::ifstream in(path);
    if (!in.is_open()) throw GameError("сейв не открыт: " + path);

    std::ostringstream buf;
    buf << in.rdbuf();
    std::string text = buf.str();
    // ...
}
```

Сначала читаем **весь файл в строку**. Зачем? Чтобы проверить checksum: нужен текст без строки CHK.

```cpp
auto chk_pos = text.rfind("\nCHK ");
if (chk_pos == std::string::npos) {
    throw GameError("в сейве нет CHK — файл повреждён");
}
std::string body = text.substr(0, chk_pos + 1);
std::string chk_line = text.substr(chk_pos + 1);

std::istringstream chk_ss(chk_line);
std::string chk_word, chk_hex;
chk_ss >> chk_word >> chk_hex;

std::uint32_t expected = static_cast<std::uint32_t>(std::stoul(chk_hex, nullptr, 16));
std::uint32_t actual = checksum(body);
if (expected != actual) {
    throw GameError("чек-сумма не сходится — сейв повреждён");
}
```

`rfind("\nCHK ")` — ищем строку CHK с конца. `text.substr(0, chk_pos + 1)` — всё до CHK (включая `\n`). `text.substr(chk_pos + 1)` — сама строка CHK.

Парсим hex через `std::stoul(s, nullptr, 16)`. `16` — основание системы.

Сравниваем суммы. Не сходятся — бросаем.

### Двухпроходный парсер

Главная тонкость загрузки: **не применять состояние сразу**. Если посередине файла ошибка, у нас окажется наполовину загруженная игра — нелогичное состояние.

Решение — **двухпроходно**: сначала всё парсим во **временные структуры**, проверяем валидность, **потом** атомарно применяем.

```cpp
std::string new_name = player.name();
int new_hp = 0, new_max = 1;
std::string new_loc;
std::vector<std::unique_ptr<Item>> player_items;
std::unordered_map<std::string, std::vector<std::unique_ptr<Item>>> location_items;

while (std::getline(lines, line)) {
    // парсим в new_name / new_hp / player_items / location_items
}

// Валидация
for (const auto& kv : location_items) {
    if (!world.find(kv.first)) {
        throw GameError("сейв ссылается на неизвестную локацию: " + kv.first);
    }
}

// Применение — теперь точно ничего не упадёт
player = Player(new_name, new_max);
player.set_hp(new_hp);
for (auto& it : player_items) {
    player.inventory().add(std::move(it));
}
for (const auto& kv : world.locations()) {
    Location* mloc = world.find(kv.first);
    if (mloc) mloc->items().clear();
}
for (auto& kv : location_items) {
    Location* mloc = world.find(kv.first);
    for (auto& it : kv.second) {
        mloc->items().add(std::move(it));
    }
}
current_location_id = new_loc;
```

Это **strong exception guarantee** (глава 22): если что-то бросит до «применения» — состояние программы не изменилось. Если применение пошло — гарантированно завершится (нет операций, которые могут бросить на этом этапе; перемещения `unique_ptr` это `noexcept`).

### Replace player

```cpp
player = Player(new_name, new_max);
player.set_hp(new_hp);
```

Move-assignment `player_ = ...`. Поскольку `Player` move-assignable (rule of zero — все поля движимы), это работает. Старый `Player` уничтожается, новый занимает его место.

После пересоздания `player` — пустой инвентарь. Далее добавляем сохранённые предметы.

## Команды в Game

```cpp
void Game::cmd_save(const std::string& slot) {
    std::string path = "data/saves/" + slot + ".sav";
    try {
        save::write(path, player_, world_, current_location_id_);
        std::cout << "Сохранено в " << path << "\n";
    } catch (const GameError& e) {
        std::cout << "Ошибка сохранения: " << e.what() << "\n";
    }
}

void Game::cmd_load(const std::string& slot) {
    std::string path = "data/saves/" + slot + ".sav";
    try {
        World fresh = load_world("data/world.txt");
        save::read(path, player_, fresh, current_location_id_);
        world_ = std::move(fresh);
        std::cout << "Загружено из " << path << "\n";
        cmd_look();
    } catch (const GameError& e) {
        std::cout << "Ошибка загрузки: " << e.what() << "\n";
    }
}
```

`cmd_save` собирает путь, вызывает `write`. Исключения ловит и показывает по-человечески.

`cmd_load` сначала загружает **свежий мир** из `world.txt` (статика — имена локаций, описания, выходы), потом накладывает сейв на этот мир (динамика — игрок, оставшиеся предметы). Если всё ок — заменяет `world_` на `fresh`.

Регистрация:

```cpp
commands_.add("save", [this](const std::string& rest) {
    if (rest.empty()) std::cout << "Имя слота?\n";
    else cmd_save(rest);
});
commands_.add("load", [this](const std::string& rest) {
    if (rest.empty()) std::cout << "Имя слота?\n";
    else cmd_load(rest);
});
```

## Тест

```bash
$ mkdir -p data/saves
$ make
$ echo -e "take ржавый меч\ngo south\nsave slot1\nq" | ./build/rpg
...
> Вы подняли: ржавый меч
== Горная тропка ==
...
> Сохранено в data/saves/slot1.sav

$ cat data/saves/slot1.sav
SAVE 1
name Герой
hp 20 20
loc mountain
inv weapon ржавый_меч 5 8
wloc cave weapon боевой_топор 7 12
wloc cave armor стальной_щит 6 6
wloc mountain consumable вода 1 3
wloc forest armor кожаный_нагрудник 8 4
wloc village consumable зелье_лечения 1 15
wloc village consumable хлеб 1 5
CHK 5e54e64c

$ echo -e "load slot1\nstatus\ninv\nq" | ./build/rpg
== Лесная поляна ==
...
> Загружено из data/saves/slot1.sav
== Горная тропка ==
> Герой: HP 20/20
> Инвентарь (общий вес 5):
  - ржавый меч (оружие, урон 8, вес 5)
```

Сохранение/загрузка работают.

Заметьте: после загрузки **меч в инвентаре** (мы его взяли до сейва), на **поляне его нет** (мы его подняли), мы **на горной тропке** (мы туда переместились). Состояние восстановлено в точности.

## Поломка checksum

```bash
$ sed -i '' 's/CHK 5e54e64c/CHK 00000000/' data/saves/slot1.sav
$ echo "load slot1" | ./build/rpg
> Ошибка загрузки: GameError: чек-сумма не сходится — сейв повреждён
```

Изменили чек-сумму руками — `load` отказался. Это и есть страховка.

Удалите `CHK` строку совсем:
```bash
$ sed -i '' '/^CHK /d' data/saves/slot1.sav
$ echo "load slot1" | ./build/rpg
> Ошибка загрузки: GameError: в сейве нет CHK — файл повреждён
```

Опять отказ. Файл без чек-суммы — повреждён.

## Что осталось

В реальной игре сделали бы ещё:

- **Метаданные сейва**: дата создания, имя игры, скриншот.
- **Несколько слотов** с автоматическими именами (slot1, slot2, ..., autosave).
- **Quicksave / quickload** на горячие клавиши.
- **Журнал изменений** (autosave каждые N минут).
- **Облачная синхронизация**.

Это всё надстройка над тем, что мы сделали. Базовая работа — `write` / `read` с версией и checksum — закрыта.

## Бинарный формат — кратко

Для контраста: тот же сейв в бинарном виде мог выглядеть так:

```cpp
out.write(reinterpret_cast<const char*>(&version), sizeof(version));   // 4 байта
out.write(reinterpret_cast<const char*>(&name_len), sizeof(name_len));
out.write(name.data(), name_len);
out.write(reinterpret_cast<const char*>(&hp), sizeof(hp));
out.write(reinterpret_cast<const char*>(&max_hp), sizeof(max_hp));
// ...
```

Преимущества:
- Меньше места: `int` это 4 байта, не 1-10 символов как в тексте.
- Быстрее запись и чтение.
- Не нужен парсинг — читаем по байтам в нужные позиции.

Недостатки:
- Нечитаемо без декодера.
- Зависит от endianness (на разных архитектурах байты в `int` могут лежать по-разному).
- Сложнее версионировать (добавил поле — старый файл неправильно интерпретируется).
- Сложнее отлаживать.

В Части IV (мини-СУБД) мы будем плотно работать с бинарными форматами — там скорость и размер критичны. Для игры — текст лучше.

## Главные правила главы

1. **Сохранение и парсер дополняют друг друга** — формат должен уметь сериализовать что парсер понимает.
2. **Версия в первой строке** — для обратной совместимости.
3. **Checksum для целостности** — простой хеш достаточен.
4. **Двухпроходный загрузчик** — сначала всё в локальные переменные, потом применяем (strong guarantee).
5. **Текст для отладки, бинарь для производительности.**
6. **Сохраняйте только разницу со статикой** — не дублируйте world.txt в сейве.
7. **Эскейпинг для значений с пробелами** — `_` → ` ` или кавычки.
8. **Метаданные** (дата, имя) полезны на больших проектах.

## Маленькое упражнение

1. Запустите игру, поиграйте, сохраните в `slot1`. Откройте `data/saves/slot1.sav` в редакторе. Изучите формат.

2. Сделайте `slot2`, `slot3`. Все работают независимо.

3. Сломайте чек-сумму руками — проверьте, что `load` отказывает.

4. Добавьте поддержку **`saves`** — команда выводящая список существующих сейвов в `data/saves/`. Подсказка: `std::filesystem` в C++17 / для C++11 — `opendir` из `<dirent.h>` или просто пробег по предсказуемым именам.

5. Добавьте в сейв **дату создания** (число секунд от эпохи). Используйте `std::time(nullptr)`. На загрузке игнорируйте, но при списке сейвов выводите.

6. Добавьте версию 2: в неё дополнительное поле «опыт игрока» (поле в Player ещё не добавлено — добавьте). Сделайте код, который при v=1 ставит опыт в 0, при v=2 читает.

7. (Сложнее) Замените простую сумму на CRC32. Готовая реализация — `<zlib.h>` или таблица CRC32 руками. Сравните, как ловит специально подобранные повреждения.

8. (Сложнее) Перепишите сейв в бинарном формате. Сравните размер с текстовым.

## Что дальше

Глава 25 — **финал RPG**. Закрепляем всё, что построили. Модульные тесты с GoogleTest (или свой простой test framework). Сборка на macOS/Linux/Windows. Итоги. Что осталось как упражнения для читателя. Это закроет Часть II.

После — Часть III (mini-shell), Часть IV (мини-СУБД), Часть V (TCP-чат), Часть VI (C++17 бонусы). Тут разворачивается остальная половина книги.
