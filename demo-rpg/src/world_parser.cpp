#include "world_parser.h"
#include "errors.h"
#include "item.h"
#include "util.h"

#include <algorithm>
#include <fstream>
#include <istream>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace rpg {

namespace {

// Срезать ведущие/конечные пробелы.
std::string trim(const std::string& s) {
    auto b = s.find_first_not_of(" \t\r");
    if (b == std::string::npos) return {};
    auto e = s.find_last_not_of(" \t\r");
    return s.substr(b, e - b + 1);
}

// Заменить '_' → ' ' (для имён с пробелами).
std::string unescape_name(const std::string& s) {
    std::string r = s;
    std::replace(r.begin(), r.end(), '_', ' ');
    return r;
}

// Промежуточное состояние локации, которое собираем по мере чтения.
// Превращается в Location в конце блока.
struct LocationDraft {
    std::string id;
    std::string name;
    std::string desc;
    std::vector<std::pair<std::string, std::string>> exits;
    std::vector<std::unique_ptr<Item>> items;
};

// Собрать LocationDraft в готовую Location.
Location finalize(LocationDraft& draft) {
    Location loc(std::move(draft.id), std::move(draft.name), std::move(draft.desc));
    for (auto& e : draft.exits) {
        loc.add_exit(e.first, e.second);
    }
    for (auto& item : draft.items) {
        loc.items().add(std::move(item));
    }
    return loc;
}

// Парсинг строки-предмета: item <type> <name> <weight> <extra>
std::unique_ptr<Item> parse_item_line(std::istringstream& ss, int line_no) {
    std::string type, name;
    int weight = 0, extra = 0;
    if (!(ss >> type >> name >> weight >> extra)) {
        throw WorldError("строка " + std::to_string(line_no)
                         + ": неполная строка item");
    }
    std::string pretty = unescape_name(name);

    if (type == "weapon")     return make_unique<Weapon>(pretty, weight, extra);
    if (type == "armor")      return make_unique<Armor>(pretty, weight, extra);
    if (type == "consumable") return make_unique<Consumable>(pretty, weight, extra);

    throw WorldError("строка " + std::to_string(line_no)
                     + ": неизвестный тип предмета '" + type + "'");
}

}  // namespace

World parse_world(std::istream& in) {
    World world;
    LocationDraft draft;
    bool have_draft = false;

    auto commit_draft = [&]() {
        if (have_draft) {
            world.add(finalize(draft));
            have_draft = false;
            draft = LocationDraft{};
        }
    };

    std::string line;
    int line_no = 0;
    while (std::getline(in, line)) {
        ++line_no;
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;

        std::istringstream ss(line);
        std::string keyword;
        ss >> keyword;

        if (keyword == "location") {
            commit_draft();   // сохраняем предыдущий блок, если был
            ss >> draft.id;
            if (draft.id.empty()) {
                throw WorldError("строка " + std::to_string(line_no)
                                 + ": location без id");
            }
            have_draft = true;
            continue;
        }

        if (!have_draft) {
            throw WorldError("строка " + std::to_string(line_no)
                             + ": '" + keyword + "' до 'location'");
        }

        if (keyword == "name") {
            std::string rest;
            std::getline(ss, rest);
            draft.name = trim(rest);
        } else if (keyword == "desc") {
            std::string rest;
            std::getline(ss, rest);
            draft.desc = trim(rest);
        } else if (keyword == "exit") {
            std::string dir, target;
            ss >> dir >> target;
            if (dir.empty() || target.empty()) {
                throw WorldError("строка " + std::to_string(line_no)
                                 + ": exit требует <direction> <target_id>");
            }
            draft.exits.emplace_back(std::move(dir), std::move(target));
        } else if (keyword == "item") {
            draft.items.push_back(parse_item_line(ss, line_no));
        } else {
            throw WorldError("строка " + std::to_string(line_no)
                             + ": неизвестный ключ '" + keyword + "'");
        }
    }

    commit_draft();   // последний блок
    return world;
}

World load_world(const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) {
        throw WorldError("не открылся файл мира: " + path);
    }
    return parse_world(in);
}

}  // namespace rpg
