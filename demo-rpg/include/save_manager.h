#ifndef RPG_SAVE_MANAGER_H
#define RPG_SAVE_MANAGER_H

#include "player.h"
#include "world.h"

#include <string>

namespace rpg {

// Менеджер сохранений. Сериализует тройку (player, world, current_loc) в
// текстовый файл и обратно. Формат версионируется, есть контрольная сумма.
namespace save {

// Текущая версия формата. Старые сейвы загружаются с предупреждением.
constexpr int CURRENT_VERSION = 1;

// Записать сейв. Бросает GameError при ошибках.
void write(const std::string& path,
           const Player& player,
           const World& world,
           const std::string& current_location_id);

// Прочитать сейв. Заполняет player/world/current_location_id.
// `world` ДОЛЖЕН быть свежезагруженным из data/world.txt — функция
// доочищает локации и наполняет их по сейву.
void read(const std::string& path,
          Player& player,
          World& world,
          std::string& current_location_id);

}  // namespace save

}  // namespace rpg

#endif
