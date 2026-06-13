#ifndef RPG_INVENTORY_H
#define RPG_INVENTORY_H

#include "item.h"

#include <memory>
#include <string>
#include <vector>

namespace rpg {

// Инвентарь — владелец предметов. Хранит unique_ptr, поэтому только
// один инвентарь владеет каждым предметом. Передача между инвентарями —
// через move (`give` / `take`).
class Inventory {
public:
    Inventory() = default;

    // Положить предмет.
    void add(std::unique_ptr<Item> item);

    // Вынуть предмет по имени. Возвращает nullptr, если такого нет.
    std::unique_ptr<Item> take(const std::string& name);

    // Найти предмет по имени, не вынимая. Возвращает указатель на наблюдение.
    const Item* find(const std::string& name) const;

    // Пуст ли инвентарь.
    bool empty() const;

    // Очистить весь инвентарь (для load).
    void clear();

    // Доступ ко всем для отображения.
    const std::vector<std::unique_ptr<Item>>& items() const;

    // Суммарный вес — для будущих ограничений «нельзя поднять, тяжело».
    int total_weight() const;

private:
    std::vector<std::unique_ptr<Item>> items_;
};

}  // namespace rpg

#endif
