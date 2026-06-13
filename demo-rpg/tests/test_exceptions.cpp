// Демонстрация исключений и иерархии std::exception.

#include "errors.h"
#include "player.h"

#include <exception>
#include <iostream>

int main() {
    // 1. Валидное создание — никаких исключений.
    try {
        rpg::Player ok("Hero", 100);
        std::cout << "OK: создан " << ok.name() << "\n";
    } catch (...) {
        std::cerr << "FAIL: валидный конструктор бросил\n";
        return 1;
    }

    // 2. Пустое имя — ValidationError.
    try {
        rpg::Player bad("", 100);
        std::cerr << "FAIL: должно было бросить\n";
        return 1;
    } catch (const rpg::ValidationError& e) {
        std::cout << "OK: поймали ValidationError: " << e.what() << "\n";
    }

    // 3. Отрицательный HP — тоже ValidationError.
    try {
        rpg::Player bad("Hero", -5);
    } catch (const rpg::ValidationError& e) {
        std::cout << "OK: поймали ValidationError: " << e.what() << "\n";
    }

    // 4. Ловля более общего типа — GameError ловит ValidationError.
    try {
        rpg::Player bad("", 50);
    } catch (const rpg::GameError& e) {
        std::cout << "OK: GameError ловит производный: " << e.what() << "\n";
    }

    // 5. Ловля совсем общего — std::exception.
    try {
        rpg::Player bad("", 50);
    } catch (const std::exception& e) {
        std::cout << "OK: std::exception тоже ловит: " << e.what() << "\n";
    }

    return 0;
}
