// Юнит-тесты основных классов: Player, Inventory, Location, World.

#include "test_framework.h"

#include "errors.h"
#include "inventory.h"
#include "item.h"
#include "location.h"
#include "player.h"
#include "save_manager.h"
#include "util.h"
#include "world.h"
#include "world_parser.h"

#include <fstream>
#include <sstream>

using namespace rpg;

void test_player(test::Stats& s) {
    Player p("Alice", 50);
    CHECK_EQ(s, p.name(), std::string("Alice"));
    CHECK_EQ(s, p.hp(), 50);
    CHECK_EQ(s, p.max_hp(), 50);
    CHECK(s, p.alive());

    p.take_damage(20);
    CHECK_EQ(s, p.hp(), 30);
    p.heal(100);                  // не выше max
    CHECK_EQ(s, p.hp(), 50);
    p.take_damage(1000);           // не ниже 0
    CHECK_EQ(s, p.hp(), 0);
    CHECK(s, !p.alive());
}

void test_player_validation(test::Stats& s) {
    bool empty_caught = false;
    try { Player p("", 10); }
    catch (const ValidationError&) { empty_caught = true; }
    CHECK(s, empty_caught);

    bool neg_caught = false;
    try { Player p("ok", -1); }
    catch (const ValidationError&) { neg_caught = true; }
    CHECK(s, neg_caught);
}

void test_inventory(test::Stats& s) {
    Inventory inv;
    CHECK(s, inv.empty());

    inv.add(make_unique<Weapon>("меч", 5, 8));
    inv.add(make_unique<Armor>("шлем", 3, 4));
    CHECK_EQ(s, inv.items().size(), static_cast<std::size_t>(2));
    CHECK_EQ(s, inv.total_weight(), 8);

    auto taken = inv.take("меч");
    CHECK(s, static_cast<bool>(taken));
    CHECK_EQ(s, taken->name(), std::string("меч"));
    CHECK_EQ(s, inv.items().size(), static_cast<std::size_t>(1));

    auto miss = inv.take("щит");
    CHECK(s, !miss);

    inv.clear();
    CHECK(s, inv.empty());
}

void test_location(test::Stats& s) {
    Location loc("test", "Тест", "Описание");
    CHECK_EQ(s, loc.id(), std::string("test"));
    CHECK_EQ(s, loc.exit("nowhere"), std::string());

    loc.add_exit("north", "neighbor");
    CHECK_EQ(s, loc.exit("north"), std::string("neighbor"));
    CHECK_EQ(s, loc.exits().size(), static_cast<std::size_t>(1));
}

void test_world(test::Stats& s) {
    World w;
    Location a("a", "A", "");
    Location b("b", "B", "");
    w.add(std::move(a));
    w.add(std::move(b));
    CHECK_EQ(s, w.size(), static_cast<std::size_t>(2));
    CHECK(s, w.find("a") != nullptr);
    CHECK(s, w.find("missing") == nullptr);
}

void test_world_parser(test::Stats& s) {
    std::string source =
        "location start\n"
        "name Старт\n"
        "desc Тут начало.\n"
        "exit east finish\n"
        "item weapon кинжал 2 4\n"
        "\n"
        "location finish\n"
        "name Финиш\n"
        "desc Конец.\n";
    std::istringstream in(source);
    World w = parse_world(in);
    CHECK_EQ(s, w.size(), static_cast<std::size_t>(2));

    const Location* start = w.find("start");
    CHECK(s, start != nullptr);
    CHECK_EQ(s, start->name(), std::string("Старт"));
    CHECK_EQ(s, start->exit("east"), std::string("finish"));
    CHECK_EQ(s, start->items().items().size(), static_cast<std::size_t>(1));
}

void test_world_parser_errors(test::Stats& s) {
    bool caught = false;
    try {
        std::istringstream in("name без_локации\n");
        parse_world(in);
    } catch (const WorldError&) {
        caught = true;
    }
    CHECK(s, caught);
}

void test_save_load_roundtrip(test::Stats& s) {
    World w;
    Location loc("home", "Дом", "Тут уютно.");
    w.add(std::move(loc));

    Player p("Hero", 100);
    p.take_damage(30);
    p.inventory().add(make_unique<Consumable>("яблоко", 1, 5));

    std::string path = "/tmp/rpg_test_save.sav";
    save::write(path, p, w, "home");

    Player p2("Other", 50);
    World w2;
    Location loc2("home", "Дом", "Тут уютно.");
    w2.add(std::move(loc2));
    std::string cur;
    save::read(path, p2, w2, cur);

    CHECK_EQ(s, p2.name(), std::string("Hero"));
    CHECK_EQ(s, p2.hp(), 70);
    CHECK_EQ(s, p2.max_hp(), 100);
    CHECK_EQ(s, cur, std::string("home"));
    CHECK_EQ(s, p2.inventory().items().size(), static_cast<std::size_t>(1));
    CHECK_EQ(s, p2.inventory().items()[0]->name(), std::string("яблоко"));
}

int main() {
    test::Stats s;
    test_player(s);
    test_player_validation(s);
    test_inventory(s);
    test_location(s);
    test_world(s);
    test_world_parser(s);
    test_world_parser_errors(s);
    test_save_load_roundtrip(s);
    return s.report("unit-tests");
}
