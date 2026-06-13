#ifndef RPG_COMMANDS_H
#define RPG_COMMANDS_H

#include <functional>
#include <string>
#include <unordered_map>

namespace rpg {

// Реестр команд. Имя команды → обработчик (лямбда, функтор, что угодно).
// Алиасы: можно сказать `add_alias("q", "quit")`, и `q` будет работать так же.
class CommandRegistry {
public:
    using Handler = std::function<void(const std::string&)>;

    // Добавить обработчик команды. Если с таким именем уже был, заменяет.
    void add(const std::string& name, Handler handler);

    // Псевдоним: `alias` будет означать то же, что `target`.
    void add_alias(const std::string& alias, const std::string& target);

    // Выполнить команду. Возвращает true, если команда найдена.
    bool dispatch(const std::string& verb, const std::string& rest) const;

    // Список всех имён команд (без алиасов) — для help-меню.
    std::vector<std::string> names() const;

private:
    std::unordered_map<std::string, Handler> handlers_;
    std::unordered_map<std::string, std::string> aliases_;
};

}  // namespace rpg

#endif
