// std::variant<T1, T2, ...> — типобезопасный union. Заменяет:
//   - tagged union (enum + union в C++03 стиле),
//   - наследование, если просто нужно «один из конкретных типов».

#include <iostream>
#include <string>
#include <variant>
#include <vector>

// Пример из RPG: предмет — оружие, броня или расходник.
struct Weapon  { int damage; };
struct Armor   { int defense; };
struct Potion  { int heal_amount; };

using Item = std::variant<Weapon, Armor, Potion>;

// Old style: иерархия с virtual. У нас в RPG так и сделано.
// New style с variant: значения вместо указателей.

void describe(const Item& it) {
    std::visit([](const auto& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, Weapon>) {
            std::cout << "Weapon, dmg=" << v.damage << "\n";
        } else if constexpr (std::is_same_v<T, Armor>) {
            std::cout << "Armor, def=" << v.defense << "\n";
        } else if constexpr (std::is_same_v<T, Potion>) {
            std::cout << "Potion, heal=" << v.heal_amount << "\n";
        }
    }, it);
}

// Альтернатива через std::holds_alternative + std::get:
void describe2(const Item& it) {
    if (std::holds_alternative<Weapon>(it)) {
        std::cout << "weapon dmg " << std::get<Weapon>(it).damage << "\n";
    } else if (std::holds_alternative<Armor>(it)) {
        std::cout << "armor def " << std::get<Armor>(it).defense << "\n";
    } else {
        std::cout << "potion heal " << std::get<Potion>(it).heal_amount << "\n";
    }
}

int main() {
    std::vector<Item> inventory = {
        Weapon{8},
        Armor{4},
        Potion{15},
    };

    std::cout << "=== std::visit + if constexpr ===\n";
    for (const auto& it : inventory) describe(it);

    std::cout << "\n=== holds_alternative + get ===\n";
    for (const auto& it : inventory) describe2(it);

    // std::variant хранит **значение** (на стеке), а не указатель на куче,
    // как при наследовании через unique_ptr<Item>. На горячем пути быстрее.

    return 0;
}
