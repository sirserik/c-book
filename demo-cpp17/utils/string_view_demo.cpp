// std::string_view — невладеющая «обёртка» над уже существующими символами:
//   указатель + длина, без копии и без аллокации.
// Идеально для парсеров: режем входную строку на токены, не создавая
// новых std::string на каждый кусок.

#include <iostream>
#include <string>
#include <string_view>
#include <vector>

// Токенизатор: разбивает строку по пробелам и возвращает СРЕЗЫ исходной
// строки. Ни одной новой std::string — только пары (offset, length),
// спрятанные внутри string_view.
std::vector<std::string_view> tokenize(std::string_view line) {
    std::vector<std::string_view> tokens;
    std::size_t i = 0;
    while (i < line.size()) {
        // пропускаем пробелы
        while (i < line.size() && line[i] == ' ') ++i;
        if (i >= line.size()) break;
        std::size_t start = i;
        while (i < line.size() && line[i] != ' ') ++i;
        tokens.push_back(line.substr(start, i - start));  // substr на view = ещё один view, не копия
    }
    return tokens;
}

// Принимаем string_view, а не const std::string& — функцию можно вызвать
// и от std::string, и от литерала, и от куска большого буфера, без копий.
bool starts_with(std::string_view s, std::string_view prefix) {
    return s.size() >= prefix.size() &&
           s.substr(0, prefix.size()) == prefix;
}

int main() {
    std::string command = "  move   north   quickly  ";

    std::cout << "=== tokenize без копий ===\n";
    for (std::string_view tok : tokenize(command)) {
        std::cout << "[" << tok << "] (len=" << tok.size() << ")\n";
    }

    std::cout << "\n=== starts_with ===\n";
    std::cout << std::boolalpha;
    std::cout << "\"" << "move north" << "\".starts_with(\"move\") = "
              << starts_with("move north", "move") << "\n";
    std::cout << "\"" << "go east" << "\".starts_with(\"move\") = "
              << starts_with("go east", "move") << "\n";

    // Ловушка: string_view НЕ владеет данными. Срок жизни символов должен
    // пережить view. Вернуть view на локальную строку = dangling.
    std::cout << "\n=== срез не аллоцирует ===\n";
    std::string_view sv = command;
    std::string_view trimmed = sv.substr(2, 4);   // "move"
    std::cout << "исходная строка не тронута, срез = [" << trimmed << "]\n";

    return 0;
}
