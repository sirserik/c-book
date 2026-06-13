#include "errors.h"
#include "game.h"

#include <exception>
#include <iostream>

int main() {
    try {
        rpg::Game game;
        return game.run();
    } catch (const rpg::GameError& e) {
        std::cerr << "Игра не запустилась: " << e.what() << "\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Неожиданная ошибка: " << e.what() << "\n";
        return 2;
    } catch (...) {
        std::cerr << "Неизвестная ошибка\n";
        return 3;
    }
}
