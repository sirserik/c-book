#include "location.h"

#include <utility>

namespace rpg {

Location::Location(std::string id, std::string name, std::string description)
    : id_(std::move(id)),
      name_(std::move(name)),
      description_(std::move(description)) {
}

const std::string& Location::id() const { return id_; }
const std::string& Location::name() const { return name_; }
const std::string& Location::description() const { return description_; }

void Location::add_exit(const std::string& direction, const std::string& target_id) {
    exits_[direction] = target_id;
}

std::string Location::exit(const std::string& direction) const {
    auto it = exits_.find(direction);
    if (it == exits_.end()) return {};
    return it->second;
}

const std::unordered_map<std::string, std::string>& Location::exits() const {
    return exits_;
}

Inventory& Location::items() { return items_; }
const Inventory& Location::items() const { return items_; }

}  // namespace rpg
