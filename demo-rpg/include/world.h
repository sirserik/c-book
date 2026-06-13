#ifndef RPG_WORLD_H
#define RPG_WORLD_H

#include "location.h"

#include <string>
#include <unordered_map>

namespace rpg {

class World {
public:
    World();

    void add(Location location);

    // Две перегрузки find — для модификации и для чтения.
    Location* find(const std::string& id);
    const Location* find(const std::string& id) const;

    std::size_t size() const;

    // Доступ ко всем локациям для save (по const-ссылке).
    const std::unordered_map<std::string, Location>& locations() const;

private:
    std::unordered_map<std::string, Location> locations_;
};

World make_demo_world();

}  // namespace rpg

#endif
