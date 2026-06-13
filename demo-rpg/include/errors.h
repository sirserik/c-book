#ifndef RPG_ERRORS_H
#define RPG_ERRORS_H

#include <stdexcept>
#include <string>

namespace rpg {

// Базовый класс ошибок игры. Все наши исключения наследуются от него
// (и от std::runtime_error, чтобы можно было ловить как std::exception).
class GameError : public std::runtime_error {
public:
    explicit GameError(const std::string& msg)
        : std::runtime_error(msg) {}
};

// Ошибка валидации входных параметров.
class ValidationError : public GameError {
public:
    explicit ValidationError(const std::string& msg)
        : GameError("ValidationError: " + msg) {}
};

// Ошибка состояния мира (например, локация не найдена).
class WorldError : public GameError {
public:
    explicit WorldError(const std::string& msg)
        : GameError("WorldError: " + msg) {}
};

}  // namespace rpg

#endif
