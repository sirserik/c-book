#ifndef RPG_GAME_H
#define RPG_GAME_H

#include "commands.h"
#include "player.h"
#include "world.h"

#include <string>

namespace rpg {

class Game {
public:
    Game();
    int run();

private:
    bool step();
    void register_commands();

    // Реализации команд: каждая получает «остаток строки» (всё после verb).
    void cmd_help() const;
    void cmd_look() const;
    void cmd_go(const std::string& direction);
    void cmd_status() const;
    void cmd_inventory() const;
    void cmd_take(const std::string& item_name);
    void cmd_drop(const std::string& item_name);
    void cmd_use(const std::string& item_name);
    void cmd_save(const std::string& slot);
    void cmd_load(const std::string& slot);

    bool running_;
    World world_;
    Player player_;
    std::string current_location_id_;
    CommandRegistry commands_;
};

}  // namespace rpg

#endif
