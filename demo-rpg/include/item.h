#ifndef RPG_ITEM_H
#define RPG_ITEM_H

#include <string>

namespace rpg {

// Базовый класс всех предметов. Абстрактный — нельзя создать просто Item.
// Конкретные виды: Weapon, Armor, Consumable.
class Item {
public:
    Item(std::string name, int weight);
    virtual ~Item() = default;

    // Запрещаем копирование — предметы будут владеться через unique_ptr (гл. 18).
    Item(const Item&) = delete;
    Item& operator=(const Item&) = delete;
    // Перемещения тоже отключим — пока не нужны.
    Item(Item&&) = delete;
    Item& operator=(Item&&) = delete;

    const std::string& name() const;
    int weight() const;

    // Виртуальные методы — переопределяются в потомках.
    virtual std::string kind() const = 0;     // pure virtual: тип «weapon», «armor», ...
    virtual std::string describe() const;     // подробное описание

protected:
    std::string name_;
    int weight_;
};

// Оружие: добавляет урон при атаке.
class Weapon final : public Item {
public:
    Weapon(std::string name, int weight, int damage);

    int damage() const;
    std::string kind() const override;
    std::string describe() const override;

private:
    int damage_;
};

// Броня: добавляет защиту.
class Armor final : public Item {
public:
    Armor(std::string name, int weight, int defense);

    int defense() const;
    std::string kind() const override;
    std::string describe() const override;

private:
    int defense_;
};

// Расходник: при использовании даёт эффект (например, лечит).
class Consumable final : public Item {
public:
    Consumable(std::string name, int weight, int heal_amount);

    int heal_amount() const;
    std::string kind() const override;
    std::string describe() const override;

private:
    int heal_amount_;
};

}  // namespace rpg

#endif
