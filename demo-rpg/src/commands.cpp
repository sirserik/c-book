#include "commands.h"

#include <algorithm>
#include <utility>
#include <vector>

namespace rpg {

void CommandRegistry::add(const std::string& name, Handler handler) {
    handlers_[name] = std::move(handler);
}

void CommandRegistry::add_alias(const std::string& alias, const std::string& target) {
    aliases_[alias] = target;
}

bool CommandRegistry::dispatch(const std::string& verb, const std::string& rest) const {
    // Сначала смотрим алиасы.
    std::string resolved = verb;
    auto alias_it = aliases_.find(verb);
    if (alias_it != aliases_.end()) {
        resolved = alias_it->second;
    }

    auto it = handlers_.find(resolved);
    if (it == handlers_.end()) {
        return false;
    }
    it->second(rest);
    return true;
}

std::vector<std::string> CommandRegistry::names() const {
    std::vector<std::string> result;
    result.reserve(handlers_.size());
    for (const auto& kv : handlers_) {
        result.push_back(kv.first);
    }
    std::sort(result.begin(), result.end());
    return result;
}

}  // namespace rpg
