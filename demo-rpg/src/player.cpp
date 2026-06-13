#include "player.h"
#include "errors.h"

#include <utility>

namespace rpg {

Player::Player(std::string name, int max_hp)
    : name_(std::move(name)),
      max_hp_(max_hp),
      hp_(max_hp) {
    // Валидация. Если что-то не так — конструктор бросает, объект не создаётся.
    if (name_.empty()) {
        throw ValidationError("имя игрока не может быть пустым");
    }
    if (max_hp <= 0) {
        throw ValidationError("max_hp должен быть положительным");
    }
}

const std::string& Player::name() const { return name_; }
int Player::hp() const { return hp_; }
int Player::max_hp() const { return max_hp_; }
bool Player::alive() const { return hp_ > 0; }

void Player::take_damage(int amount) {
    if (amount < 0) amount = 0;
    hp_ -= amount;
    if (hp_ < 0) hp_ = 0;
}

void Player::heal(int amount) {
    if (amount < 0) amount = 0;
    hp_ += amount;
    if (hp_ > max_hp_) hp_ = max_hp_;
}

void Player::set_hp(int hp) {
    if (hp < 0) hp = 0;
    if (hp > max_hp_) hp = max_hp_;
    hp_ = hp;
}

Inventory& Player::inventory() { return inventory_; }
const Inventory& Player::inventory() const { return inventory_; }

}  // namespace rpg
