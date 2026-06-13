#ifndef RPG_UTIL_H
#define RPG_UTIL_H

#include <memory>
#include <utility>

namespace rpg {

// std::make_unique появился в C++14. У нас база C++11 — пишем шим сами.
// Использование: auto p = rpg::make_unique<Weapon>("меч", 5, 10);
template <typename T, typename... Args>
std::unique_ptr<T> make_unique(Args&&... args) {
    return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}

}  // namespace rpg

#endif
