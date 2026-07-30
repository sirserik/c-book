# Глава 17. Наследование и виртуальные функции

В этой главе — главная тема ООП в C++: **наследование** и **полиморфизм**. На примере нашего проекта построим иерархию предметов: базовый `Item`, и три потомка — `Weapon`, `Armor`, `Consumable`. Локация и инвентарь смогут хранить указатели на базу `Item*` — а внутри будут лежать конкретные оружия, брони, расходники. Один и тот же `item->describe()` будет работать по-разному в зависимости от реального типа объекта. Это и есть полиморфизм.

К концу главы вы поймёте: что такое `virtual`, зачем `override`, почему виртуальный деструктор обязателен, что такое vtable, в чём проблема «slicing». Тема концептуально тяжёлая, но если разберётесь — большая часть страшности C++ останется позади.

## Зачем наследование

Предположим, нам в игре нужны три типа предметов: оружие (даёт урон), броня (даёт защиту), расходник (даёт эффект при использовании). У всех общее:
У любого предмета есть имя, вес и возможность показать себя в списке инвентаря.

У каждого свои особенности:
А дальше начинаются различия: у оружия есть урон, у брони — защита, у расходника — то, что он делает при использовании.

Можно было бы написать три отдельных класса без связи. Локация бы тогда хранила три коллекции:

```cpp
class Location {
    std::vector<Weapon> weapons_;
    std::vector<Armor>  armors_;
    std::vector<Consumable> consumables_;
};
```

Дальше команда `take <name>` искала бы предмет в трёх местах. Команда `look` показывала бы три цикла. Если завтра появится «волшебный артефакт» — четвёртая коллекция, четвёртый цикл. И повторение кода: имена, вес, описание — у всех одинаковые.

Решение — **общая база**. Создаём абстрактный класс `Item`, от него наследуем три конкретных. Локация хранит один список **указателей на базу**:

```cpp
class Location {
    std::vector<Item*> items_;
};
```

Когда нужно «отобразить» предмет, мы вызываем `item->describe()` — компилятор сам выберет нужную реализацию в зависимости от реального типа. Это и есть полиморфизм.

## Базовый синтаксис наследования

```cpp
class Item {
public:
    Item(std::string name, int weight);
    
    const std::string& name() const;
    int weight() const;

protected:
    std::string name_;
    int weight_;
};

class Weapon : public Item {
public:
    Weapon(std::string name, int weight, int damage);
    
    int damage() const;

private:
    int damage_;
};
```

`class Weapon : public Item` — **`Weapon` наследует от `Item`**. Это значит:

1. У `Weapon` есть **все поля и методы** `Item` плюс свои.
2. `Weapon` — это «тоже `Item`» в каком-то смысле: можно положить `Weapon*` туда, где ждут `Item*`.

Объявление `Weapon`:

```cpp
Weapon sword("ржавый меч", 5, 8);

std::cout << sword.name();      // OK, унаследовано от Item
std::cout << sword.weight();    // OK, унаследовано
std::cout << sword.damage();    // OK, собственное
```

### public, protected, private — два смысла

В заголовке `class Weapon : public Item` слово `public` определяет **тип наследования**:

- `public Item` — публичное наследование. `Weapon` — это `Item` снаружи. Самый частый вид.
- `protected Item` — наследование «для своих». Внешний код не знает о связи.
- `private Item` — наследование как «реализация через». `Weapon` использует `Item` внутри, но снаружи не виден.

В нашей книге будем использовать только **`public`** — самый частый и осмысленный.

Внутри класса слова `public`/`protected`/`private` определяют **доступ к членам**. С `protected` мы пока не встречались. Это «доступно классу и его потомкам, но не снаружи»:

```cpp
class Item {
protected:
    std::string name_;
    // доступен снаружи: только через name()
    // доступен потомкам: напрямую как this->name_ или просто name_
};

class Weapon : public Item {
public:
    std::string describe() const {
        return name_ + " (оружие)";   // OK: protected доступно потомкам
    }
};
```

Если бы `name_` был `private` — Weapon не смог бы к нему обращаться напрямую. Пришлось бы через `name()` (геттер).

**Когда что использовать**:
- `private` — для внутренних деталей класса, потомкам не нужно лезть.
- `protected` — для общих полей/методов базы, к которым потомки хотят прямой доступ.
- `public` — то, что доступно снаружи.

В нашем `Item` поля `name_` и `weight_` — `protected`, чтобы потомки могли их печатать в своём `describe`.

## Конструктор базы

При создании `Weapon` сначала вызывается конструктор `Item`, потом — собственный конструктор `Weapon`. Вы обязаны указать, как звать конструктор базы:

```cpp
Weapon::Weapon(std::string name, int weight, int damage)
    : Item(std::move(name), weight),    // вызов конструктора базы
      damage_(damage) {
}
```

В инициализационном списке **первой** идёт инициализация базы — синтаксис как для поля, но с именем класса. Перед собственными полями. Без этого:

```cpp
Weapon::Weapon(std::string name, int weight, int damage)
    : damage_(damage) {                 // забыли про Item
}
```

— компилятор попытается вызвать `Item()` (default-конструктор). Раз у `Item` его нет (мы написали свой с параметрами) — будет ошибка компиляции.

## Виртуальные функции

Теперь центральная вещь. Допустим, у `Item` есть метод `describe()`, который возвращает описание:

```cpp
class Item {
public:
    std::string describe() const {
        return name_ + " (предмет)";
    }
};
```

И у `Weapon` мы хотим, чтобы `describe()` показывал и урон:

```cpp
class Weapon : public Item {
public:
    std::string describe() const {
        return name_ + " (оружие, урон " + std::to_string(damage_) + ")";
    }
};
```

Что произойдёт?

```cpp
Weapon sword("меч", 5, 10);
std::cout << sword.describe();   // "меч (оружие, урон 10)"  ✓

Item& item = sword;
std::cout << item.describe();    // "меч (предмет)"  ✗
```

Через ссылку базы вызывается метод **базы**, а не потомка. Это **static dispatch** — компилятор смотрит на тип переменной (`Item&`), не на реальный объект (`Weapon`).

Это не то, что мы хотим. Хотим: «какой бы тип реально ни был, вызови подходящий метод». Для этого — **`virtual`**:

```cpp
class Item {
public:
    virtual std::string describe() const {
        return name_ + " (предмет)";
    }
};

class Weapon : public Item {
public:
    std::string describe() const override {
        return name_ + " (оружие)";
    }
};
```

Ключевое слово `virtual` в базе говорит: «этот метод можно переопределить, и при вызове через ссылку/указатель базы выбирай реализацию по реальному типу».

Теперь:

```cpp
Item& item = sword;
std::cout << item.describe();   // "меч (оружие)"  ✓ dynamic dispatch
```

### override — обязательная привычка

Слово `override` в потомке — **не магия, а проверка**. Оно говорит компилятору: «эта функция переопределяет виртуальную функцию базы». Если что-то не так — ошибка компиляции.

Без `override` опечатки молча создают новую функцию:

```cpp
class Item {
public:
    virtual std::string describe() const { ... }
};

class Weapon : public Item {
public:
    std::string describle() const { ... }   // ОПЕЧАТКА: describle вместо describe
};
```

Без `override` компилятор подумает: «нормально, метод `describle` в `Weapon`, рядом с унаследованным `describe`». Тестирование укажет, что переопределение не сработало. Пол часа поисков, недоумение.

С `override`:

```cpp
std::string describle() const override { ... }
// error: 'describle' marked 'override' but does not override
```

Компилятор сразу указывает на проблему.

**Правило**: **ВСЕГДА** ставьте `override` на переопределяющие методы. Это бесплатный страховочный полис.

### final — запрет дальнейшего переопределения

Слово `final` запрещает потомкам переопределять метод:

```cpp
class Item {
public:
    virtual std::string describe() const { ... }
};

class Weapon : public Item {
public:
    std::string describe() const override final { ... }
};

class MagicSword : public Weapon {
public:
    std::string describe() const override { ... }   // ОШИБКА: переопределение final
};
```

`final` можно применить и к классу — запретит наследование вообще:

```cpp
class Weapon final : public Item {
    // от Weapon нельзя наследоваться
};
```

В нашем `Item.h` я как раз помечу `Weapon`, `Armor`, `Consumable` как `final` — на текущем этапе нам не нужно их расширять, и `final` помогает компилятору оптимизировать (статически разрешить виртуальные вызовы).

## Чисто виртуальные функции и абстрактные классы

В `Item` есть метод `kind()` — возвращает тип («оружие», «броня», ...). У базового `Item` непонятно, что вернуть — это зависит от потомка. Делаем метод **чисто виртуальным**:

```cpp
class Item {
public:
    virtual std::string kind() const = 0;   // = 0 значит «нет реализации»
};
```

`= 0` в конце — это синтаксис «pure virtual». Реализации нет; потомки **обязаны** её предоставить.

Класс, у которого есть хоть одна чисто виртуальная функция — **абстрактный**. Объекты абстрактного класса нельзя создать:

```cpp
Item item("...", 5);   // ОШИБКА: cannot allocate object of abstract type
Item* p = new Item(...);   // тоже ошибка
```

Можно только указатели и ссылки:

```cpp
Item* p = new Weapon(...);   // OK
Item& r = some_weapon;        // OK
```

Это удобно: запрещает «безсмысленные» объекты базы.

Конкретные потомки должны реализовать все чисто виртуальные методы:

```cpp
class Weapon : public Item {
public:
    std::string kind() const override { return "оружие"; }   // обязательно
};
```

Если забыть — `Weapon` останется абстрактным, его тоже нельзя будет создать. Компилятор быстро выявит.

## Виртуальный деструктор — обязателен!

Главный подвох наследования. Покажу на ошибке.

```cpp
class Item {
public:
    Item(std::string n) : name_(n) {}
    ~Item() { std::cout << "Item dtor\n"; }    // НЕ virtual!
protected:
    std::string name_;
};

class Weapon : public Item {
public:
    Weapon(std::string n, int* big_array) 
        : Item(n), buffer_(big_array) {}
    ~Weapon() { 
        std::cout << "Weapon dtor\n"; 
        delete[] buffer_;
    }
private:
    int* buffer_;
};

int main() {
    Item* p = new Weapon("...", new int[1000]);
    delete p;   // ВНИМАНИЕ
}
```

Что произойдёт при `delete p`? `p` имеет тип `Item*`. Компилятор смотрит на тип указателя, видит — деструктор `Item`. Вызывает `~Item()`. **`~Weapon()` НЕ вызывается!**

Результат: `Weapon::buffer_` не освобождён, утечка памяти 4 KB. И это ещё «милый» случай — если Weapon владел файлом или мьютексом, последствия серьёзнее.

Это **undefined behavior** в строгом смысле: стандарт C++ говорит, что `delete` через указатель базы без виртуального деструктора — UB. На практике обычно «работает» с утечкой; но в общем случае может быть что угодно.

**Решение**: деструктор базы должен быть **`virtual`**:

```cpp
class Item {
public:
    virtual ~Item() = default;     // вот так
};
```

Тогда при `delete p` компилятор смотрит в **vtable** реального объекта, находит правильный деструктор (`~Weapon`), вызывает его. Внутри `~Weapon` потом автоматически вызывается `~Item` базы. Цепочка корректная, утечки нет.

`= default` — потому что для `Item` деструктор тривиальный (только освободить `std::string name_`, что и так само). `virtual ~Item() = default;` — идиома для «у меня виртуальный деструктор, но особой логики нет».

**Правило**: **если класс задумывается как база для наследования, его деструктор обязан быть `virtual`**. Без исключений.

Если класс **не** задумывается как база — деструктор пусть будет невиртуальным или вовсе не объявлен. Виртуальность стоит лишние байты (vtable pointer внутри объекта).

## vtable — как это работает

Под капотом виртуальные вызовы устроены так.

Каждый класс с хотя бы одной виртуальной функцией имеет **vtable** (virtual table) — массив указателей на методы. Где-то в `.rodata`-секции бинарника лежит:

```
vtable_Item:
    &Item::describe        // или &Weapon::describe, ...
    &Item::kind            // или &Weapon::kind, ...
    &Item::~Item           // или &Weapon::~Weapon, ...
    ...
```

Каждый объект класса с виртуальными функциями имеет **скрытый указатель** на свою vtable (обычно как первое поле). Размер объекта увеличивается на 8 байт.

```
Item* p → объект Weapon в памяти:
    [vtable ptr: → vtable_Weapon]   <- скрытое поле
    [name_:  "меч"]                  <- из Item
    [weight_: 5]                     <- из Item
    [damage_: 10]                    <- из Weapon
```

Когда вы пишете `p->describe()`:

1. Компилятор знает, что `describe` — виртуальный.
2. Генерирует код: «возьми vtable из объекта по адресу `p`, найди в ней `describe`, вызови».
3. Поскольку у `Weapon` своя vtable, в ней `describe` указывает на `Weapon::describe`.

Это **дополнительный косвенный вызов** — чуть медленнее, чем прямой. Обычно — единицы наносекунд, не критично. Но если виртуальный метод вызывается миллионы раз за секунду — может стать узким местом. Тогда смотрят: правда ли нужен полиморфизм здесь?

## Слайсинг — главная ловушка

```cpp
Weapon sword("меч", 5, 10);

Item item = sword;        // !!! 
std::cout << item.describe();
```

Это копирование `sword` в **новый объект** `item` типа `Item`. При копировании поля `Weapon` (`damage_`) **отсекаются** — у `Item` их нет. Это **slicing** (срезание).

Объект `item` — настоящий `Item`, не `Weapon`. Даже если методы виртуальные, `item.describe()` вызовет `Item::describe()`.

Чтобы избежать slicing:

Отсюда правило без исключений: объект-потомок никогда не присваивают в переменную базового типа — только в указатель или ссылку. А если нужно хранить разнотипные объекты в одном контейнере, берут `std::vector<std::unique_ptr<Item>>`, о котором пойдёт речь в главе 18.

В нашем `Item` я сделал ещё страховку:

```cpp
class Item {
public:
    Item(const Item&) = delete;
    Item& operator=(const Item&) = delete;
    Item(Item&&) = delete;
    Item& operator=(Item&&) = delete;
};
```

Запрет копирования и перемещения. Если кто-то попробует `Item item = sword;` — ошибка компиляции. Безопасно: объект `Item` живёт ровно один раз, доступ к нему только через указатели/ссылки.

В реальной жизни иногда копирование нужно. Тогда определяют **виртуальный `clone()`-метод**:

```cpp
class Item {
public:
    virtual std::unique_ptr<Item> clone() const = 0;
};

class Weapon : public Item {
public:
    std::unique_ptr<Item> clone() const override {
        return std::unique_ptr<Item>(new Weapon(*this));
    }
};
```

`item->clone()` создаёт копию реального типа. Это идиома **prototype**. У нас в книге она появится позже — для копирования предметов в инвентаре.

## dynamic_cast — приведение базы к потомку

Иногда у вас на руках `Item*`, и нужно «если это оружие — узнать урон». В C++ для этого есть `dynamic_cast`:

```cpp
Item* p = ...;
if (Weapon* w = dynamic_cast<Weapon*>(p)) {
    int dmg = w->damage();
    ...
}
```

`dynamic_cast<Weapon*>(p)`:
Если объект действительно `Weapon` или его потомок, приведение возвращает указатель на него; если нет — пустой указатель. Поэтому результат обязательно проверяют, и проверка эта дешевле, чем кажется: она сводится к сравнению служебной информации о типе.

То же для ссылок (но при «не тот тип» бросает `std::bad_cast`, ловите через try/catch):

```cpp
try {
    Weapon& w = dynamic_cast<Weapon&>(some_item);
    ...
} catch (const std::bad_cast&) {
    std::cout << "не оружие\n";
}
```

`dynamic_cast` использует **RTTI** (runtime type information) — компилятор записывает в vtable имя класса, и `dynamic_cast` это проверяет. Требует, чтобы у базы был **хотя бы один виртуальный метод** (иначе RTTI не работает; обычно работает, потому что виртуальный деструктор уже есть).

`dynamic_cast` — относительно медленная операция (несколько указательных переходов). Не злоупотребляйте — если в каждой второй функции нужен `dynamic_cast`, скорее всего, дизайн классов плохой. Подумайте: можно ли вместо `if (Weapon*)` сделать ещё один виртуальный метод?

В нашей игре `dynamic_cast` пригодится в команде «надеть» — отличить `Armor` от `Weapon` чтобы понять, в какой слот вставлять.

## Множественное наследование — кратко

В C++ можно наследоваться **сразу от нескольких классов**:

```cpp
class Swimmer { virtual void swim() = 0; };
class Flyer { virtual void fly() = 0; };

class Duck : public Swimmer, public Flyer {
    void swim() override { ... }
    void fly() override { ... }
};
```

Это **множественное наследование** (multiple inheritance). Считается «острым» — много граблей (diamond problem, неоднозначность имён). Java и C# его не разрешают вовсе; вместо этого — наследование от одного класса + реализация многих интерфейсов.

В C++ совет: **избегайте множественного наследования**, кроме одного случая:

**Множественное наследование от интерфейсов** — классов с одними чисто виртуальными функциями, без полей. Это безопасно и похоже на интерфейсы Java/C#.

```cpp
class Printable {
public:
    virtual ~Printable() = default;
    virtual std::string to_string() const = 0;
};

class Comparable {
public:
    virtual ~Comparable() = default;
    virtual int compare(const Comparable& other) const = 0;
};

class Item : public Printable, public Comparable {
    ...
};
```

В нашем коде множественного наследования не будет.

## Item-иерархия в проекте

Соберём всё. `include/item.h`:

```cpp
#ifndef RPG_ITEM_H
#define RPG_ITEM_H

#include <string>

namespace rpg {

class Item {
public:
    Item(std::string name, int weight);
    virtual ~Item() = default;

    Item(const Item&) = delete;
    Item& operator=(const Item&) = delete;
    Item(Item&&) = delete;
    Item& operator=(Item&&) = delete;

    const std::string& name() const;
    int weight() const;

    virtual std::string kind() const = 0;
    virtual std::string describe() const;

protected:
    std::string name_;
    int weight_;
};

class Weapon final : public Item {
public:
    Weapon(std::string name, int weight, int damage);

    int damage() const;
    std::string kind() const override;
    std::string describe() const override;

private:
    int damage_;
};

class Armor final : public Item {
public:
    Armor(std::string name, int weight, int defense);

    int defense() const;
    std::string kind() const override;
    std::string describe() const override;

private:
    int defense_;
};

class Consumable final : public Item {
public:
    Consumable(std::string name, int weight, int heal_amount);

    int heal_amount() const;
    std::string kind() const override;
    std::string describe() const override;

private:
    int heal_amount_;
};

}  // namespace rpg

#endif
```

Разберём ключевые моменты:

**База `Item`**:
Виртуальный деструктор обязателен, иначе удаление через указатель на базу разрушит только базовую часть. А запрет копирования через `= delete` защищает от срезки — того самого случая, когда потомок присваивается в переменную базы и теряет свою часть.
- Поля `protected`, чтобы потомки могли их печатать.
- `kind()` чисто виртуальный — `Item` абстрактный, его нельзя создать.
- `describe()` обычный виртуальный — с дефолтной реализацией.

**Потомки**:
- `final` после имени класса — нельзя дальше наследоваться.
- `override` на всех переопределяемых методах.

`src/item.cpp` (фрагменты):

```cpp
// Реализация базы
Item::Item(std::string name, int weight)
    : name_(std::move(name)), weight_(weight) {
}

const std::string& Item::name() const { return name_; }
int Item::weight() const { return weight_; }

std::string Item::describe() const {
    std::ostringstream ss;
    ss << name_ << " (" << kind() << ", вес " << weight_ << ")";
    return ss.str();
}
```

В `Item::describe()` мы вызываем `kind()` — виртуальный! Хотя сами мы в базе, во время выполнения через vtable будет вызван `kind()` реального объекта. Это нормально — внутри методов базы виртуальные вызовы тоже работают полиморфно.

```cpp
// Реализация Weapon
Weapon::Weapon(std::string name, int weight, int damage)
    : Item(std::move(name), weight), damage_(damage) {
}

int Weapon::damage() const { return damage_; }

std::string Weapon::kind() const { return "оружие"; }

std::string Weapon::describe() const {
    std::ostringstream ss;
    ss << name_ << " (оружие, урон " << damage_ << ", вес " << weight_ << ")";
    return ss.str();
}
```

`Weapon` явно переопределяет `describe()` для добавления урона. Если бы не переопределили — работала бы `Item::describe()`, использующая `kind()` = `"оружие"`. Тоже бы вышло осмысленно, но без урона.

## Тестируем полиморфизм

`tests/test_items.cpp`:

```cpp
#include "item.h"
#include <iostream>
#include <vector>

int main() {
    using namespace rpg;

    std::vector<Item*> items;
    items.push_back(new Weapon("ржавый меч", 5, 8));
    items.push_back(new Armor("кожаный нагрудник", 8, 4));
    items.push_back(new Consumable("зелье лечения", 1, 15));

    // Полиморфный обход
    for (Item* it : items) {
        std::cout << "- " << it->describe() << "\n";
    }

    // dynamic_cast
    for (Item* it : items) {
        if (Weapon* w = dynamic_cast<Weapon*>(it)) {
            std::cout << w->name() << " наносит " << w->damage() << "\n";
        }
    }

    for (Item* it : items) {
        delete it;
    }

    return 0;
}
```

Запуск:

```
- ржавый меч (оружие, урон 8, вес 5)
- кожаный нагрудник (броня, защита 4, вес 8)
- зелье лечения (расходник, лечит 15, вес 1)
ржавый меч наносит 8 урона
```

Видим: один и тот же `it->describe()` даёт разный вывод для разных реальных типов. Один и тот же `dynamic_cast<Weapon*>` срабатывает только на `Weapon`. Это полиморфизм в работе.

В цикле `delete it` мы освобождаем память. Без виртуального деструктора в `Item` это вызывало бы UB и утечку `damage_`/`defense_`/`heal_amount_`. С виртуальным — корректно вызывается `~Weapon`/`~Armor`/`~Consumable`, потом `~Item`, всё чисто.

В главе 18 заменим `Item*` и `new`/`delete` на `std::unique_ptr<Item>` — будет ещё короче и безопаснее.

## Несколько тонкостей

### Виртуальный вызов в конструкторе/деструкторе

В конструкторе базы виртуальный механизм **не работает**:

```cpp
class Item {
public:
    Item() { 
        std::cout << "creating " << kind();   // !!! вызовет Item::kind, не Weapon::kind
    }
    virtual std::string kind() const = 0;
};
```

На момент конструктора `Item` объект ещё не «стал» `Weapon`. Vtable указывает на vtable `Item`. Для чисто виртуального метода (`= 0`) это вообще приведёт к **UB** или к ошибке линковки.

В деструкторе то же — vtable уже «откатилась» к базе.

**Правило**: не вызывайте виртуальные методы из конструктора/деструктора базы.

Если нужно похожее поведение — отдельный метод `init()`, который вызывается после конструктора.

### Вызов метода базы из потомка

Иногда из переопределённого метода хочется вызвать базовую реализацию:

```cpp
class Weapon : public Item {
public:
    std::string describe() const override {
        return Item::describe() + ", урон " + std::to_string(damage_);
    }
};
```

`Item::describe()` — явный квалифицированный вызов. Обходит виртуальный механизм, вызывает именно версию `Item`.

Удобно, когда хочется «дополнить» базовое поведение, не дублируя его. У нас в книге пока не пользуемся, но идиома известная.

### protected и инвариант класса

Делать поля `protected` — иногда соблазнительно, но риск.

Если поле `private`, доступ только через методы. Можете в методе валидировать (например, `take_damage` не пускает hp в отрицательные). Потомки тоже идут через метод.

Если поле `protected`, потомки могут менять напрямую. Инварианты класса могут нарушиться.

**Рекомендация**: лучше держать поля `private` и давать `protected` getters/setters, если потомкам нужно. Но это в идеале; в нашем `Item` поля `protected` для удобства (потомки печатают их в `describe`).

## Альтернативы наследованию

Наследование не единственный способ переиспользования. Современный C++ часто советует **композицию вместо наследования**:

```cpp
// Вместо наследования
class Magic : public Weapon { ... };

// Композиция
class Weapon {
    MagicEffect effect_;   // оружие имеет эффект
};
```

«Оружие имеет эффект» — композиция (has-a). «Магическое оружие — это оружие» — наследование (is-a). Если is-a сомнительно, лучше композиция.

Наследование добавляет **связанность**: потомок зависит от базы. Менять базу больно — все потомки могут сломаться. Композиция связана слабее: меняете внутреннее устройство, наружу не видно.

В нашей игре `Item → Weapon/Armor/Consumable` — настоящий is-a. «Меч — это предмет». Полностью оправдано.

Но не делайте наследование «потому что в учебнике написано». Если можно композицией — обычно лучше композицией.

## Главные правила главы

1. **Виртуальный деструктор** для любого класса, который задумывается как база. **Обязательно.**
2. **`override`** на каждом переопределении. Бесплатная защита от опечаток.
3. **`final`** на классах/методах, которые не будут расширять — даёт компилятору оптимизировать.
4. **Никогда не присваивайте объект потомка в переменную базы по значению** — slicing.
5. **Запрещайте копирование** для классов в иерархии — `= delete`. Доступ только через указатели/ссылки.
6. **`dynamic_cast`** для безопасного приведения — но злоупотребление — признак плохого дизайна.
7. **Виртуальные методы не работают в конструкторе/деструкторе базы** — не зовите их там.
8. **Композиция часто лучше наследования** — наследуйте только когда is-a реально и иерархия плоская.

## Маленькое упражнение

1. Добавьте новый тип предмета — `KeyItem` (квестовый предмет, нельзя выбросить). Чисто наследник `Item` с переопределённым `kind()` = `"квестовый"` и описанием.

2. Создайте `Item* it = new Weapon(...)` без виртуального деструктора в `Item` (нарочно уберите `virtual ~Item()`). Запустите под ASan (`make CXXFLAGS=...` с `-fsanitize=address`). ASan должен указать утечку.

3. Попробуйте `Item item("test", 5);` — должна быть ошибка компиляции (абстрактный класс). Прочтите сообщение.

4. Уберите `override` у `Weapon::describe()` и переименуйте его в `describle()`. Что произойдёт при сборке? Что произойдёт при попытке вызвать `weapon.describe()`?

5. Добавьте в `Item` виртуальный метод `int value() const` — «цена предмета». В `Weapon` — `damage_ * 10`, в `Armor` — `defense_ * 15`, в `Consumable` — `heal_amount_ * 5`. Используйте в тесте.

6. (Сложнее) Добавьте `virtual std::unique_ptr<Item> clone() const` для копирования предметов. Реализуйте в потомках. Какие проблемы это создаёт?

7. (Сложнее) Перепишите `Item` так, чтобы `Weapon`/`Armor`/`Consumable` не наследовались, а использовали композицию: `Item` хранит `enum Type` и `union` для специфических данных. Сравните, какой подход проще для добавления нового типа.

## Что дальше

Глава 18 — **умные указатели**: `std::unique_ptr`, `std::shared_ptr`, `std::weak_ptr`. Применим их к нашим Item: уберём ручной `new`/`delete`, заменим на `unique_ptr<Item>`. Добавим **Inventory** — игрок сможет таскать предметы. Локации тоже смогут содержать предметы — командой `take` будем их подбирать.

Это завершит «инфраструктуру предметов». После — бои, NPC, парсер мира из файла, сейвы. К 25-й главе соберётся всё.
