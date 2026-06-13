// std::optional<T> — «значение или нет». Альтернатива {value, bool} парам,
// nullable указателям, special-value-конвенциям (-1 = «не найдено»).

#include <iostream>
#include <optional>
#include <string>
#include <unordered_map>

// Старый стиль: специальное значение
int find_age_oldstyle(const std::string& name) {
    if (name == "Alice") return 30;
    return -1;  // «не найден» — но что если возраст реально -1?
}

// C++17: optional явно «есть/нет»
std::optional<int> find_age(const std::string& name) {
    static const std::unordered_map<std::string, int> ages = {
        {"Alice", 30}, {"Bob", 25}
    };
    auto it = ages.find(name);
    if (it == ages.end()) return std::nullopt;
    return it->second;
}

int main() {
    // 1. Базовое использование.
    auto a = find_age("Alice");
    if (a.has_value()) {
        std::cout << "Alice: " << *a << "\n";
    }
    // Сокращение: optional в bool-контексте.
    if (auto b = find_age("Charlie")) {
        std::cout << "Charlie: " << *b << "\n";
    } else {
        std::cout << "Charlie не найден\n";
    }

    // 2. value_or — дефолт.
    int c_age = find_age("Charlie").value_or(0);
    std::cout << "Charlie age (or 0): " << c_age << "\n";

    // 3. Опционально-возвращаемая ошибка vs исключение.
    try {
        std::cout << find_age("ghost").value() << "\n";  // бросит bad_optional_access
    } catch (const std::bad_optional_access&) {
        std::cout << "bad_optional_access поймали\n";
    }

    // 4. Использование с move.
    std::optional<std::string> name = "Hello";
    std::string taken = std::move(*name);
    std::cout << "taken: " << taken << "\n";
    // *name теперь moved-from, но optional всё ещё has_value.
    name.reset();
    std::cout << "after reset, has_value=" << name.has_value() << "\n";

    return 0;
}
