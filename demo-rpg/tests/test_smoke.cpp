// Простейший smoke-тест: убеждаемся, что Game::run() компилируется и
// сразу выходит при EOF на stdin. Полноценные тесты появятся в главе 25.

#include "game.h"
#include <iostream>
#include <sstream>

int main() {
    // Подменяем stdin на пустой поток, чтобы getline сразу вернул false.
    std::istringstream empty_input("");
    std::cin.rdbuf(empty_input.rdbuf());

    rpg::Game game;
    int rc = game.run();

    if (rc != 0) {
        std::cerr << "FAIL: run() вернул " << rc << ", ожидался 0\n";
        return 1;
    }

    std::cout << "OK: smoke test passed\n";
    return 0;
}
