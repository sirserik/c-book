// if constexpr — ветвление на этапе компиляции. Заменяет SFINAE и tag dispatch.

#include <iostream>
#include <string>
#include <type_traits>
#include <vector>

// Шаблонная функция, разный код для разных типов.
template <typename T>
void process(const T& v) {
    if constexpr (std::is_integral_v<T>) {
        std::cout << "integer: " << v << " (×2 = " << v * 2 << ")\n";
    } else if constexpr (std::is_floating_point_v<T>) {
        std::cout << "float: " << v << " (sqrt = " << std::sqrt(v) << ")\n";
    } else if constexpr (std::is_same_v<T, std::string>) {
        std::cout << "string: '" << v << "' (size " << v.size() << ")\n";
    } else {
        std::cout << "other type\n";
    }
}

// До C++17 — три перегрузки или SFINAE. С if constexpr — один шаблон.

#include <cmath>

int main() {
    process(42);
    process(3.14);
    process(std::string("hello"));
    process(std::vector<int>{1, 2, 3});
    return 0;
}
