#include "item.h"

#include <sstream>
#include <utility>

namespace rpg {

// ===== Item =====

Item::Item(std::string name, int weight)
    : name_(std::move(name)), weight_(weight) {
}

const std::string& Item::name() const { return name_; }
int Item::weight() const { return weight_; }

std::string Item::describe() const {
    std::ostringstream ss;
    ss << name_ << " (" << kind() << ", вес " << weight_ << ")";
    return ss.str();
}

// ===== Weapon =====

Weapon::Weapon(std::string name, int weight, int damage)
    : Item(std::move(name), weight), damage_(damage) {
}

int Weapon::damage() const { return damage_; }

std::string Weapon::kind() const { return "оружие"; }

std::string Weapon::describe() const {
    std::ostringstream ss;
    ss << name_ << " (оружие, урон " << damage_ << ", вес " << weight_ << ")";
    return ss.str();
}

// ===== Armor =====

Armor::Armor(std::string name, int weight, int defense)
    : Item(std::move(name), weight), defense_(defense) {
}

int Armor::defense() const { return defense_; }

std::string Armor::kind() const { return "броня"; }

std::string Armor::describe() const {
    std::ostringstream ss;
    ss << name_ << " (броня, защита " << defense_ << ", вес " << weight_ << ")";
    return ss.str();
}

// ===== Consumable =====

Consumable::Consumable(std::string name, int weight, int heal_amount)
    : Item(std::move(name), weight), heal_amount_(heal_amount) {
}

int Consumable::heal_amount() const { return heal_amount_; }

std::string Consumable::kind() const { return "расходник"; }

std::string Consumable::describe() const {
    std::ostringstream ss;
    ss << name_ << " (расходник, лечит " << heal_amount_ << ", вес " << weight_ << ")";
    return ss.str();
}

}  // namespace rpg
