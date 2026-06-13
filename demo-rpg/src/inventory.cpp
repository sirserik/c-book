#include "inventory.h"

#include <algorithm>

namespace rpg {

void Inventory::add(std::unique_ptr<Item> item) {
    if (item) {
        items_.push_back(std::move(item));
    }
}

std::unique_ptr<Item> Inventory::take(const std::string& name) {
    auto it = std::find_if(items_.begin(), items_.end(),
                           [&name](const std::unique_ptr<Item>& p) {
                               return p && p->name() == name;
                           });
    if (it == items_.end()) {
        return nullptr;
    }
    std::unique_ptr<Item> result = std::move(*it);
    items_.erase(it);
    return result;
}

const Item* Inventory::find(const std::string& name) const {
    auto it = std::find_if(items_.begin(), items_.end(),
                           [&name](const std::unique_ptr<Item>& p) {
                               return p && p->name() == name;
                           });
    if (it == items_.end()) return nullptr;
    return it->get();
}

bool Inventory::empty() const {
    return items_.empty();
}

void Inventory::clear() {
    items_.clear();
}

const std::vector<std::unique_ptr<Item>>& Inventory::items() const {
    return items_;
}

int Inventory::total_weight() const {
    int sum = 0;
    for (const auto& p : items_) {
        if (p) sum += p->weight();
    }
    return sum;
}

}  // namespace rpg
