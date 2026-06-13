#include "world.h"
#include "item.h"
#include "util.h"

#include <utility>

namespace rpg {

World::World() = default;

void World::add(Location location) {
    std::string id = location.id();
    locations_.emplace(std::move(id), std::move(location));
}

// Возвращаем неконстантный указатель, чтобы можно было модифицировать
// (например, вынимать предметы при `take`).
Location* World::find(const std::string& id) {
    auto it = locations_.find(id);
    if (it == locations_.end()) return nullptr;
    return &it->second;
}

const Location* World::find(const std::string& id) const {
    auto it = locations_.find(id);
    if (it == locations_.end()) return nullptr;
    return &it->second;
}

std::size_t World::size() const {
    return locations_.size();
}

const std::unordered_map<std::string, Location>& World::locations() const {
    return locations_;
}

World make_demo_world() {
    World w;

    Location glade("glade", "Лесная поляна",
                   "Залитая солнцем поляна. На западе видна тропа к деревне, "
                   "на востоке — тёмный лес.");
    glade.add_exit("west", "village");
    glade.add_exit("east", "forest");
    glade.items().add(make_unique<Weapon>("ржавый меч", 5, 8));

    Location village("village", "Деревня",
                     "Маленькая деревня из десятка домов.");
    village.add_exit("east", "glade");
    village.items().add(make_unique<Consumable>("зелье лечения", 1, 15));

    Location forest("forest", "Тёмный лес",
                    "Ветви скрипят над головой. Здесь сумрачно и тихо.");
    forest.add_exit("west", "glade");
    forest.items().add(make_unique<Armor>("кожаный нагрудник", 8, 4));

    w.add(std::move(glade));
    w.add(std::move(village));
    w.add(std::move(forest));

    return w;
}

}  // namespace rpg
