// Structured bindings — декомпозиция кортежей/пар/struct'ов в локальные имена.
// До C++17 — get<0>(t), get<1>(t), и так далее.

#include <iostream>
#include <map>
#include <string>
#include <tuple>

std::tuple<int, std::string, double> get_record() {
    return {42, "Alice", 5.7};
}

int main() {
    // 1. Из tuple
    auto [id, name, height] = get_record();
    std::cout << id << " " << name << " " << height << "\n";

    // 2. Из pair (например, map::find или map::insert)
    std::map<std::string, int> ages = {{"Alice", 30}, {"Bob", 25}};
    auto [it, inserted] = ages.insert({"Carol", 40});
    std::cout << "inserted=" << inserted << ", key=" << it->first << "\n";

    // 3. Из struct
    struct Point { int x; int y; };
    Point p{10, 20};
    auto& [x, y] = p;
    x = 100;  // меняет p.x!
    std::cout << "p = (" << p.x << "," << p.y << ")\n";

    // 4. В range-for с map.
    for (const auto& [key, value] : ages) {
        std::cout << key << " = " << value << "\n";
    }

    return 0;
}
