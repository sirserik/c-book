#ifndef SHELL_BUILTINS_H
#define SHELL_BUILTINS_H

#include <string>
#include <vector>

namespace shell {

// Попытаться выполнить встроенную команду. Возвращает true, если команда
// встроенная (вне зависимости от успеха/неуспеха выполнения; код в `out_code`).
// Возвращает false, если команда не встроенная — её надо запускать через exec.
bool try_builtin(const std::vector<std::string>& argv, int& out_code);

// Раскрыть алиас в первом слове строки. Возвращает строку без изменений,
// если алиас не найден. Раскрывается один уровень — рекурсию не поддерживаем.
std::string expand_alias(const std::string& line);

// История.
void history_add(const std::string& line);
void history_load(const std::string& path);
void history_save(const std::string& path);
const std::vector<std::string>& history();

}  // namespace shell

#endif
