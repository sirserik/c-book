#include "game.h"
#include "errors.h"
#include "item.h"
#include "save_manager.h"
#include "world_parser.h"

#include <iostream>
#include <sstream>
#include <string>

namespace rpg {

namespace {

// Утилита: вытащить аргумент команды (всё после первого слова).
std::string trim_left(const std::string& s) {
    auto pos = s.find_first_not_of(" \t");
    if (pos == std::string::npos) return {};
    return s.substr(pos);
}

}  // namespace

Game::Game()
    : running_(true),
      world_(load_world("data/world.txt")),
      player_("Герой", 20),
      current_location_id_("glade") {
    register_commands();
}

void Game::register_commands() {
    // Каждая команда — лямбда с захватом `this`. Игровое состояние под рукой,
    // CommandRegistry ничего о Game не знает.
    commands_.add("help",       [this](const std::string&)        { cmd_help(); });
    commands_.add("look",       [this](const std::string&)        { cmd_look(); });
    commands_.add("status",     [this](const std::string&)        { cmd_status(); });
    commands_.add("inventory",  [this](const std::string&)        { cmd_inventory(); });
    commands_.add("go",         [this](const std::string& rest)   {
        if (rest.empty()) std::cout << "Куда идти?\n";
        else cmd_go(rest);
    });
    commands_.add("take",       [this](const std::string& rest)   {
        if (rest.empty()) std::cout << "Что взять?\n";
        else cmd_take(rest);
    });
    commands_.add("drop",       [this](const std::string& rest)   {
        if (rest.empty()) std::cout << "Что бросить?\n";
        else cmd_drop(rest);
    });
    commands_.add("use",        [this](const std::string& rest)   {
        if (rest.empty()) std::cout << "Что использовать?\n";
        else cmd_use(rest);
    });
    commands_.add("save",       [this](const std::string& rest)   {
        if (rest.empty()) std::cout << "Имя слота? (например: save slot1)\n";
        else cmd_save(rest);
    });
    commands_.add("load",       [this](const std::string& rest)   {
        if (rest.empty()) std::cout << "Имя слота?\n";
        else cmd_load(rest);
    });
    commands_.add("quit",       [this](const std::string&)        { running_ = false; });

    // Алиасы для краткости.
    commands_.add_alias("l",   "look");
    commands_.add_alias("i",   "inventory");
    commands_.add_alias("inv", "inventory");
    commands_.add_alias("stat","status");
    commands_.add_alias("get", "take");
    commands_.add_alias("q",   "quit");
    commands_.add_alias("exit","quit");
}

int Game::run() {
    std::cout << "=== RPG-демо ===\n";
    std::cout << "Привет, " << player_.name() << "! Введите 'help' для справки.\n\n";
    cmd_look();

    while (running_) {
        if (!step()) break;
    }

    std::cout << "До свидания.\n";
    return 0;
}

bool Game::step() {
    std::cout << "\n> ";
    std::string line;
    if (!std::getline(std::cin, line)) {
        std::cout << "\n";
        return false;
    }
    if (line.empty()) return true;

    std::istringstream ss(line);
    std::string verb;
    ss >> verb;

    std::string rest;
    std::getline(ss, rest);
    rest = trim_left(rest);

    if (!commands_.dispatch(verb, rest)) {
        std::cout << "Неизвестная команда: '" << verb << "'. Введите 'help'.\n";
    }
    return running_;
}

void Game::cmd_help() const {
    std::cout << "Команды:\n";
    for (const auto& name : commands_.names()) {
        std::cout << "  " << name << "\n";
    }
}

void Game::cmd_look() const {
    const Location* loc = world_.find(current_location_id_);
    if (!loc) {
        std::cout << "Где-то в пустоте.\n";
        return;
    }
    std::cout << "== " << loc->name() << " ==\n";
    std::cout << loc->description() << "\n";

    if (!loc->exits().empty()) {
        std::cout << "Выходы:";
        for (const auto& e : loc->exits()) std::cout << " " << e.first;
        std::cout << "\n";
    }
    if (!loc->items().empty()) {
        std::cout << "На земле:\n";
        for (const auto& item : loc->items().items()) {
            std::cout << "  - " << item->describe() << "\n";
        }
    }
}

void Game::cmd_go(const std::string& direction) {
    const Location* loc = world_.find(current_location_id_);
    if (!loc) return;

    std::string target = loc->exit(direction);
    if (target.empty()) {
        std::cout << "Туда не пройти.\n";
        return;
    }
    current_location_id_ = target;
    cmd_look();
}

void Game::cmd_status() const {
    std::cout << player_.name() << ": HP " << player_.hp()
              << "/" << player_.max_hp();
    if (!player_.alive()) std::cout << " [мёртв]";
    std::cout << "\n";
}

void Game::cmd_inventory() const {
    const Inventory& inv = player_.inventory();
    if (inv.empty()) {
        std::cout << "Инвентарь пуст.\n";
        return;
    }
    std::cout << "Инвентарь (общий вес " << inv.total_weight() << "):\n";
    for (const auto& item : inv.items()) {
        std::cout << "  - " << item->describe() << "\n";
    }
}

void Game::cmd_take(const std::string& item_name) {
    Location* loc = world_.find(current_location_id_);
    if (!loc) return;

    std::unique_ptr<Item> picked = loc->items().take(item_name);
    if (!picked) {
        std::cout << "Здесь нет '" << item_name << "'.\n";
        return;
    }
    std::cout << "Вы подняли: " << picked->name() << "\n";
    player_.inventory().add(std::move(picked));
}

void Game::cmd_drop(const std::string& item_name) {
    std::unique_ptr<Item> dropped = player_.inventory().take(item_name);
    if (!dropped) {
        std::cout << "У вас нет '" << item_name << "'.\n";
        return;
    }
    Location* loc = world_.find(current_location_id_);
    if (!loc) {
        std::cout << "Вы выбросили " << dropped->name() << " в пустоту.\n";
        return;
    }
    std::cout << "Вы бросили: " << dropped->name() << "\n";
    loc->items().add(std::move(dropped));
}

void Game::cmd_save(const std::string& slot) {
    std::string path = "data/saves/" + slot + ".sav";
    try {
        save::write(path, player_, world_, current_location_id_);
        std::cout << "Сохранено в " << path << "\n";
    } catch (const GameError& e) {
        std::cout << "Ошибка сохранения: " << e.what() << "\n";
    }
}

void Game::cmd_load(const std::string& slot) {
    std::string path = "data/saves/" + slot + ".sav";
    try {
        // Сначала свежий мир из world.txt, потом наложим сейв.
        World fresh = load_world("data/world.txt");
        save::read(path, player_, fresh, current_location_id_);
        world_ = std::move(fresh);
        std::cout << "Загружено из " << path << "\n";
        cmd_look();
    } catch (const GameError& e) {
        std::cout << "Ошибка загрузки: " << e.what() << "\n";
    }
}

void Game::cmd_use(const std::string& item_name) {
    const Item* item = player_.inventory().find(item_name);
    if (!item) {
        std::cout << "У вас нет '" << item_name << "'.\n";
        return;
    }
    if (const Consumable* c = dynamic_cast<const Consumable*>(item)) {
        int heal = c->heal_amount();
        std::cout << "Вы выпили " << c->name() << " (+" << heal << " HP).\n";
        player_.heal(heal);
        player_.inventory().take(item_name);
        cmd_status();
    } else {
        std::cout << "Это нельзя «использовать».\n";
    }
}

}  // namespace rpg
