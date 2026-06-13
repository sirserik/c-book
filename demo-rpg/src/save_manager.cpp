#include "save_manager.h"
#include "errors.h"
#include "item.h"
#include "util.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace rpg {
namespace save {

namespace {

// Простая хеш-сумма (как в std::string hash в libstdc++).
// Не криптографическая — защита только от случайных правок.
std::uint32_t checksum(const std::string& text) {
    std::uint32_t sum = 0;
    for (char ch : text) {
        sum = sum * 31u + static_cast<unsigned char>(ch);
    }
    return sum;
}

std::string to_hex(std::uint32_t v) {
    std::ostringstream ss;
    ss << std::hex << std::setfill('0') << std::setw(8) << v;
    return ss.str();
}

std::string escape(const std::string& s) {
    std::string r = s;
    std::replace(r.begin(), r.end(), ' ', '_');
    return r;
}

std::string unescape(const std::string& s) {
    std::string r = s;
    std::replace(r.begin(), r.end(), '_', ' ');
    return r;
}

// Один предмет в виде "<type> <name> <weight> <extra>".
std::string item_line(const Item& it) {
    std::ostringstream ss;
    if (auto w = dynamic_cast<const Weapon*>(&it)) {
        ss << "weapon " << escape(w->name()) << " " << w->weight() << " " << w->damage();
    } else if (auto a = dynamic_cast<const Armor*>(&it)) {
        ss << "armor " << escape(a->name()) << " " << a->weight() << " " << a->defense();
    } else if (auto c = dynamic_cast<const Consumable*>(&it)) {
        ss << "consumable " << escape(c->name()) << " " << c->weight() << " " << c->heal_amount();
    } else {
        throw GameError("неизвестный тип предмета при сохранении");
    }
    return ss.str();
}

std::unique_ptr<Item> parse_item(std::istringstream& ss) {
    std::string type, name;
    int weight = 0, extra = 0;
    if (!(ss >> type >> name >> weight >> extra)) {
        throw GameError("плохая строка предмета в сейве");
    }
    std::string pretty = unescape(name);
    if (type == "weapon")     return make_unique<Weapon>(pretty, weight, extra);
    if (type == "armor")      return make_unique<Armor>(pretty, weight, extra);
    if (type == "consumable") return make_unique<Consumable>(pretty, weight, extra);
    throw GameError("неизвестный тип предмета: " + type);
}

}  // namespace

void write(const std::string& path,
           const Player& player,
           const World& world,
           const std::string& current_location_id) {
    std::ostringstream body;
    body << "SAVE " << CURRENT_VERSION << "\n";
    body << "name " << escape(player.name()) << "\n";
    body << "hp " << player.hp() << " " << player.max_hp() << "\n";
    body << "loc " << current_location_id << "\n";

    for (const auto& item : player.inventory().items()) {
        body << "inv " << item_line(*item) << "\n";
    }
    for (const auto& kv : world.locations()) {
        const Location& loc = kv.second;
        for (const auto& item : loc.items().items()) {
            body << "wloc " << loc.id() << " " << item_line(*item) << "\n";
        }
    }

    std::string content = body.str();
    std::uint32_t sum = checksum(content);

    std::ofstream out(path);
    if (!out.is_open()) {
        throw GameError("не открыт для записи: " + path);
    }
    out << content << "CHK " << to_hex(sum) << "\n";
}

void read(const std::string& path,
          Player& player,
          World& world,
          std::string& current_location_id) {
    std::ifstream in(path);
    if (!in.is_open()) {
        throw GameError("сейв не открыт: " + path);
    }

    std::ostringstream buf;
    buf << in.rdbuf();
    std::string text = buf.str();

    auto chk_pos = text.rfind("\nCHK ");
    if (chk_pos == std::string::npos) {
        throw GameError("в сейве нет строки CHK — файл повреждён");
    }
    std::string body = text.substr(0, chk_pos + 1);  // с финальным \n
    std::string chk_line = text.substr(chk_pos + 1);
    std::istringstream chk_ss(chk_line);
    std::string chk_word, chk_hex;
    chk_ss >> chk_word >> chk_hex;
    if (chk_word != "CHK" || chk_hex.size() != 8) {
        throw GameError("неправильная строка CHK");
    }

    std::uint32_t expected = static_cast<std::uint32_t>(std::stoul(chk_hex, nullptr, 16));
    std::uint32_t actual = checksum(body);
    if (expected != actual) {
        throw GameError("чек-сумма не сходится — сейв повреждён");
    }

    std::istringstream lines(body);
    std::string line;
    if (!std::getline(lines, line)) {
        throw GameError("пустой сейв");
    }
    {
        std::istringstream ss(line);
        std::string w; int ver = 0;
        ss >> w >> ver;
        if (w != "SAVE") throw GameError("не сейв-файл");
        if (ver != CURRENT_VERSION) {
            throw GameError("несовместимая версия сейва: " + std::to_string(ver));
        }
    }

    // Парсим во временные структуры. Применяем только если всё прошло.
    std::string new_name = player.name();
    int new_hp = 0, new_max = 1;
    std::string new_loc;
    std::vector<std::unique_ptr<Item>> player_items;
    std::unordered_map<std::string, std::vector<std::unique_ptr<Item>>> location_items;

    while (std::getline(lines, line)) {
        if (line.empty()) continue;
        std::istringstream ss(line);
        std::string key;
        ss >> key;

        if (key == "name") {
            std::string n; ss >> n;
            new_name = unescape(n);
        } else if (key == "hp") {
            ss >> new_hp >> new_max;
        } else if (key == "loc") {
            ss >> new_loc;
        } else if (key == "inv") {
            player_items.push_back(parse_item(ss));
        } else if (key == "wloc") {
            std::string lid; ss >> lid;
            location_items[lid].push_back(parse_item(ss));
        } else {
            throw GameError("неизвестный ключ в сейве: " + key);
        }
    }

    // Проверяем, что все упомянутые локации существуют.
    for (const auto& kv : location_items) {
        if (!world.find(kv.first)) {
            throw GameError("сейв ссылается на неизвестную локацию: " + kv.first);
        }
    }
    if (!world.find(new_loc)) {
        throw GameError("стартовая локация сейва не найдена: " + new_loc);
    }

    // Применяем — теперь точно ничего не упадёт.
    player = Player(new_name, new_max);
    player.set_hp(new_hp);
    for (auto& it : player_items) {
        player.inventory().add(std::move(it));
    }
    for (const auto& kv : world.locations()) {
        Location* mloc = world.find(kv.first);
        if (mloc) mloc->items().clear();
    }
    for (auto& kv : location_items) {
        Location* mloc = world.find(kv.first);
        for (auto& it : kv.second) {
            mloc->items().add(std::move(it));
        }
    }
    current_location_id = new_loc;
}

}  // namespace save
}  // namespace rpg
