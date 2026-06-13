#ifndef RPG_LOCATION_H
#define RPG_LOCATION_H

#include "inventory.h"

#include <string>
#include <unordered_map>

namespace rpg {

class Location {
public:
    Location(std::string id, std::string name, std::string description);

    const std::string& id() const;
    const std::string& name() const;
    const std::string& description() const;

    void add_exit(const std::string& direction, const std::string& target_id);
    std::string exit(const std::string& direction) const;
    const std::unordered_map<std::string, std::string>& exits() const;

    // Предметы лежат в инвентаре локации. Тот же класс, разный смысл.
    Inventory& items();
    const Inventory& items() const;

private:
    std::string id_;
    std::string name_;
    std::string description_;
    std::unordered_map<std::string, std::string> exits_;
    Inventory items_;
};

}  // namespace rpg

#endif
