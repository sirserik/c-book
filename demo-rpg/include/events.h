#ifndef RPG_EVENTS_H
#define RPG_EVENTS_H

#include <string>

namespace rpg {

// События — простые struct'ы с данными. Если в будущем понадобятся методы
// или иерархия, заведём настоящие классы.

struct ItemPickedEvent {
    std::string player_name;
    std::string item_name;
};

struct PlayerMovedEvent {
    std::string player_name;
    std::string from_id;
    std::string to_id;
};

}  // namespace rpg

#endif
