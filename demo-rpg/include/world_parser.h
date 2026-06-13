#ifndef RPG_WORLD_PARSER_H
#define RPG_WORLD_PARSER_H

#include "world.h"

#include <iosfwd>
#include <string>

namespace rpg {

// Парсер мира из текстового формата. Бросает WorldError при ошибках.
//
// Формат (line-oriented):
//   # комментарий
//   location <id>            — начало новой локации
//   name <rest of line>      — название
//   desc <rest of line>      — описание
//   exit <dir> <target_id>   — выход
//   item <type> <name> <w> <extra>  — предмет
//
//   type: weapon | armor | consumable
//   extra: damage | defense | heal_amount
//   В именах предметов подчёркивание → пробел.

// Из потока (для тестирования).
World parse_world(std::istream& in);

// Из файла. Удобная обёртка.
World load_world(const std::string& path);

}  // namespace rpg

#endif
