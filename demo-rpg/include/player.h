#ifndef RPG_PLAYER_H
#define RPG_PLAYER_H

#include "inventory.h"

#include <string>

namespace rpg {

class Player {
public:
    Player(std::string name, int max_hp);

    const std::string& name() const;
    int hp() const;
    int max_hp() const;
    bool alive() const;

    void take_damage(int amount);
    void heal(int amount);

    // Прямая установка hp (для load). Зажимается в [0, max_hp].
    void set_hp(int hp);

    // Доступ к инвентарю в двух вариантах: const и нет.
    Inventory& inventory();
    const Inventory& inventory() const;

private:
    std::string name_;
    int max_hp_;
    int hp_;
    Inventory inventory_;
};

}  // namespace rpg

#endif
