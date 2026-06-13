// Демонстрация полиморфизма: храним указатели на базу Item, вызываем
// виртуальные методы — диспатчируются по реальному типу.

#include "item.h"

#include <iostream>
#include <vector>

int main() {
    using namespace rpg;

    // Создаём три разных предмета на куче. Указатели на базу.
    std::vector<Item*> items;
    items.push_back(new Weapon("ржавый меч", 5, 8));
    items.push_back(new Armor("кожаный нагрудник", 8, 4));
    items.push_back(new Consumable("зелье лечения", 1, 15));

    // Полиморфный обход: компилятор не знает, что лежит за Item*,
    // но через vtable вызовется правильный describe().
    for (Item* it : items) {
        std::cout << "- " << it->describe() << "\n";
    }

    // dynamic_cast — безопасное приведение базы к потомку.
    // Возвращает nullptr, если объект не того типа.
    for (Item* it : items) {
        if (Weapon* w = dynamic_cast<Weapon*>(it)) {
            std::cout << w->name() << " наносит " << w->damage() << " урона\n";
        }
    }

    // Не забываем убрать. В главе 18 заменим на unique_ptr.
    for (Item* it : items) {
        delete it;
    }

    return 0;
}
